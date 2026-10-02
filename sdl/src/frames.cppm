export module vui.sdl:frames;
import vui.render;
using namespace rstd::prelude;

export namespace vui::sdl {
enum class FrameEventType {
    Submitted,
    PresentQueued,
    PresentRejected,
    GpuComplete,
    Skipped,
    Failed,
    Cancelled
};
enum class PresentationFeedback { Unsupported };
enum class FrameRequestError { Busy, Closing, Faulted, Limit };
struct FrameEvent {
    Serial request{};
    FrameEventType type{};
    Option<FrameIdentity> frame;
    Serial submission{};
    Option<gpu::PresentStatus> present;
    Option<gpu::Error> error;
    Option<FrameError> plan_error;
    PresentationFeedback presentation{PresentationFeedback::Unsupported};
    Option<InputTimings> inputs;
    Option<rstd::time::Instant> observed;
    Serial earlier_unselected_input_batches{};
};
struct FrameLoopStatus {
    Option<Serial> active, pending;
    bool submitted{}, rebuilding{}, suspended{}, closing{}, closed{}, backpressure{};
    Option<gpu::Error> fault;
};
class FrameLoop {
    struct Active {
        Serial request;
        FrameIdentity frame;
        Serial submission{};
        Option<InputTimings> inputs;
        Serial earlier_unselected_input_batches{};
    };
    UiSession &session_;
    gpu::Renderer &renderer_;
    bool windowed_;
    gpu::DrawOptions options_;
    Option<Active> active_;
    Option<Serial> pending_;
    Vec<FrameEvent> events_;
    Serial next_request_{}, generation_{}, submitted_input_sequence_{};
    VkExtent2D extent_{};
    bool rebuild_{}, closing_{}, closed_{}, waiting_{};
    Option<gpu::Error> fault_;

    static auto waiting(const gpu::Error &error) -> bool {
        return (error.stage == gpu::Stage::Acquire || error.stage == gpu::Stage::Timeout) &&
               (error.result == VK_TIMEOUT || error.result == VK_NOT_READY);
    }
    auto backpressure() const -> bool { return events_.len() >= usize(56); }
    auto suspended() const -> bool { return windowed_ && (!extent_.width || !extent_.height); }
    void notify(FrameEventType type, Option<gpu::Error> error = None(),
                Option<gpu::PresentStatus> present = None()) {
        FrameEvent event{active_->request,    type,    Some(active_->frame),
                         active_->submission, present, error};
        if (active_->inputs.is_some()) {
            event.inputs = Some(InputTimings(*active_->inputs));
            event.observed = Some(rstd::time::Instant::now());
            event.earlier_unselected_input_batches = active_->earlier_unselected_input_batches;
        }
        events_.push(rstd::move(event));
    }
    auto fail(gpu::Error error) -> Result<empty, gpu::Error> {
        bool first = fault_.is_none();
        fault_ = Some(error);
        if (active_.is_some()) {
            if (first)
                notify(FrameEventType::Failed, Some(error));
            if (!active_->submission || error.result == VK_ERROR_DEVICE_LOST)
                active_ = None();
        }
        if (pending_.is_some()) {
            events_.push({*pending_, FrameEventType::Cancelled});
            pending_ = None();
        }
        return Err(error);
    }

    auto retry_or_fail(gpu::Error error) -> Result<empty, gpu::Error> {
        if (!waiting(error))
            return fail(error);
        waiting_ = true;
        return Ok(empty{});
    }
    auto submit_active() -> Result<empty, gpu::Error> {
        auto submitted = renderer_.submit();
        if (submitted.is_err()) {
            auto error = submitted.unwrap_err();
            if (windowed_ && error.stage == gpu::Stage::Acquire &&
                error.result == VK_ERROR_OUT_OF_DATE_KHR) {
                rebuild_ = true;
                return Ok(empty{});
            }
            return retry_or_fail(error);
        }
        active_->submission = submitted.unwrap();
        if (active_->inputs.is_some()) {
            auto sequence = active_->inputs->batch().successful_sequence;
            active_->earlier_unselected_input_batches = sequence - submitted_input_sequence_ - 1;
            submitted_input_sequence_ = sequence;
        }
        notify(FrameEventType::Submitted);
        if (windowed_) {
            auto presented = renderer_.present();
            if (presented.is_err())
                return fail(presented.unwrap_err());
            auto result = presented.unwrap().status;
            notify(result == gpu::PresentStatus::OutOfDate ? FrameEventType::PresentRejected
                                                           : FrameEventType::PresentQueued,
                   None(), Some(result));
            rebuild_ |= result != gpu::PresentStatus::Queued;
        }
        return Ok(empty{});
    }

  public:
    // The loop exclusively schedules renderer work. Both owners must outlive it.
    FrameLoop(UiSession &session, gpu::Renderer &renderer, bool windowed = true,
              gpu::DrawOptions options = {})
        : session_(session), renderer_(renderer), windowed_(windowed), options_(options),
          extent_(renderer.pixel_extent()) {
        events_.reserve(usize(64));
    }
    FrameLoop(const FrameLoop &) = delete;
    auto operator=(const FrameLoop &) -> FrameLoop & = delete;
    auto request() -> Result<Serial, FrameRequestError> {
        if (closing_ || closed_)
            return Err(FrameRequestError::Closing);
        if (fault_.is_some())
            return Err(FrameRequestError::Faulted);
        if (backpressure())
            return Err(FrameRequestError::Busy);
        if (next_request_ == ~Serial{})
            return Err(FrameRequestError::Limit);
        if (pending_.is_some())
            events_.push({*pending_, FrameEventType::Skipped});
        pending_ = Some(++next_request_);
        return Ok(next_request_);
    }
    void set_surface(VkExtent2D extent, Serial generation) {
        if (!windowed_ || closing_ || closed_)
            return;
        rebuild_ |= (generation_ && generation_ != generation) || extent_.width != extent.width ||
                    extent_.height != extent.height;
        extent_ = extent;
        generation_ = generation;
    }
    void close() {
        if (closing_ || closed_)
            return;
        closing_ = true;
        if (pending_.is_some()) {
            events_.push({*pending_, FrameEventType::Cancelled});
            pending_ = None();
        }
    }
    auto poll_events() -> Vec<FrameEvent> {
        Vec<FrameEvent> result = rstd::move(events_);
        events_ = Vec<FrameEvent>{};
        events_.reserve(usize(64));
        return result;
    }
    auto status() const -> FrameLoopStatus {
        return {active_.is_some() ? Some(Serial(active_->request)) : None<Serial>(),
                pending_,
                active_.is_some() && active_->submission != 0,
                rebuild_,
                suspended(),
                closing_,
                closed_,
                backpressure(),
                fault_};
    }
    auto wait_milliseconds(bool animating = false) const -> Index {
        if (closed_ || (fault_.is_some() && !closing_) || backpressure())
            return animating ? 16 : 1000;
        if (active_.is_some() || closing_ || (rebuild_ && !suspended()))
            return active_.is_some() && !active_->submission && !waiting_ ? 0 : 1;
        if (pending_.is_some() && !suspended())
            return waiting_ ? 1 : 0;
        return animating ? 16 : 1000;
    }
    auto pump() -> Result<empty, gpu::Error> {
        if (closed_)
            return Ok(empty{});
        if (fault_.is_some() && !closing_)
            return Err(*fault_);
        if (backpressure())
            return Ok(empty{});
        waiting_ = false;
        if (active_.is_some() && active_->submission) {
            auto completed = renderer_.poll();
            if (completed.is_err())
                return fail(completed.unwrap_err());
            if (completed.unwrap().is_some()) {
                notify(FrameEventType::GpuComplete);
                active_ = None();
            }
            return Ok(empty{});
        }
        if (active_.is_some() && (pending_.is_some() || rebuild_ || closing_)) {
            auto discarded = renderer_.discard_prepared(0);
            if (discarded.is_err()) {
                auto error = discarded.unwrap_err();
                return retry_or_fail(error);
            }
            if (closing_)
                notify(FrameEventType::Cancelled);
            else if (pending_.is_some())
                notify(FrameEventType::Skipped);
            else
                pending_ = Some(active_->request);
            active_ = None();
        }
        if (closing_) {
            auto stopped = renderer_.shutdown(0);
            if (stopped.is_err()) {
                auto error = stopped.unwrap_err();
                return retry_or_fail(error);
            }
            closed_ = true;
            return Ok(empty{});
        }
        if (suspended())
            return Ok(empty{});
        if (rebuild_) {
            auto resized = renderer_.resize(extent_, 0);
            if (resized.is_err()) {
                auto error = resized.unwrap_err();
                return retry_or_fail(error);
            }
            rebuild_ = false;
        }
        if (active_.is_some())
            return submit_active();
        if (pending_.is_none())
            return Ok(empty{});
        auto id = *pending_;
        auto plan = session_.plan_frame();
        if (plan.is_err()) {
            events_.push({id, FrameEventType::Failed, {}, 0, {}, {}, Some(plan.unwrap_err())});
            pending_ = None();
            return Ok(empty{});
        }
        auto value = plan.unwrap();
        auto identity = value.identity();
        auto inputs = session_.last_successful_input_timings();
        if (inputs.is_some()) {
            auto batch = inputs->batch();
            if (batch.successful_sequence <= submitted_input_sequence_ ||
                batch.scene_session != identity.scene_session ||
                batch.render_revision > identity.scene_revision ||
                batch.ui_revision > identity.ui_revision)
                inputs = None();
        }
        if (windowed_ && generation_ && identity.surface_generation != generation_) {
            events_.push({id,
                          FrameEventType::Failed,
                          Some(identity),
                          0,
                          {},
                          {},
                          Some(FrameError::InvalidSurface)});
            pending_ = None();
            return Ok(empty{});
        }
        auto prepared = renderer_.prepare(rstd::move(value), options_);
        if (prepared.is_err()) {
            auto error = prepared.unwrap_err();
            if (windowed_ && error.stage == gpu::Stage::Surface &&
                error.result == VK_ERROR_OUT_OF_DATE_KHR) {
                rebuild_ = true;
                return Ok(empty{});
            }
            if (waiting(error))
                return retry_or_fail(error);
            events_.push({id, FrameEventType::Failed, Some(identity), 0, {}, Some(error)});
            pending_ = None();
            if (error.result == VK_ERROR_DEVICE_LOST)
                return fail(error);
            return Ok(empty{});
        }
        active_ = Some(Active{id, identity, 0, rstd::move(inputs)});
        pending_ = None();
        return submit_active();
    }
};
} // namespace vui::sdl
