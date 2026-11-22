export module vui.host:frames;
import vui.render;
using namespace rstd::prelude;

export namespace vui::host {
enum class FrameEventType {
    Submitted,
    PresentQueued,
    PresentRejected,
    GpuComplete,
    Skipped,
    Failed,
    Cancelled,
    CaptureReady,
    CaptureCancelled
};
enum class PresentationFeedback { Unsupported };
enum class FrameRequestError { Busy, Closing, Faulted, Limit };
struct CaptureSelection {
    Serial surface_generation;
    Serial min_ui_revision;
};
struct RenderSnapshot {
    unsigned validation_errors{};
    Option<gpu::DrawStats> draws;
    Option<gpu::FrameTimings> timings;
    gpu::TextCacheStats text;
    gpu::ImageCacheStats images;
    Option<gpu::PresentMode> present_mode;
    VkExtent2D extent{};
};
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
    Serial capture{};
    Option<gpu::Pixels> pixels;
    Option<UiFrame> view;
    auto completed_view() const -> Option<UiFrame> {
        if (type != FrameEventType::GpuComplete || !view || present == Some(gpu::PresentStatus::OutOfDate)) return None();
        return Some(UiFrame(*view));
    }
};
struct FrameLoopStatus {
    Option<Serial> active, pending;
    bool submitted{}, rebuilding{}, suspended{}, closing{}, closed{}, backpressure{};
    Option<gpu::Error> fault;
    auto clone() const -> FrameLoopStatus {
        return {active, pending, submitted, rebuilding, suspended, closing, closed, backpressure, fault.clone()};
    }
};
struct FramePacket {
    FramePlan plan;
    Option<InputTimings> inputs;
    bool frozen{};
    Option<UiFrame> view;
};
} // namespace vui::host

namespace vui::host::detail {
inline auto plan_frame_packet(UiSession &session, bool culling = true) -> Result<FramePacket, FrameError> {
    auto frame = session.plan_ui_frame(culling ? gpu::frame_clip_culling : nullptr);
    if (frame.is_err()) return Err(frame.unwrap_err());
    return Ok(FramePacket{frame->plan(), session.last_successful_input_timings(), false,
                          Some(rstd::move(*frame))});
}

inline void finish_pending_request(Option<Serial> &pending, Vec<FrameEvent> &events, FrameEventType outcome) {
    if (pending.is_none()) return;
    events.push({*pending, outcome});
    pending = None();
}

inline auto is_terminal_frame_event(FrameEventType type) -> bool {
    return type == FrameEventType::GpuComplete || type == FrameEventType::Skipped ||
           type == FrameEventType::Cancelled || type == FrameEventType::Failed;
}
} // namespace vui::host::detail

export namespace vui::host {
template <class Backend = gpu::Renderer> class FrameExecutor {
    struct Active {
        Serial request;
        FrameIdentity frame;
        Serial submission{};
        Option<InputTimings> inputs;
        Serial earlier_unselected_input_batches{};
        Option<UiFrame> view;
        Option<gpu::PresentStatus> present;
    };
    Backend &renderer_;
    bool windowed_;
    gpu::DrawOptions options_;
    Option<Active> active_;
    Option<Serial> pending_;
    Vec<FrameEvent> events_;
    Serial frame_period_ns_{16'666'667};
    Serial frame_interval_ns_{};
    Option<Serial> last_submission_ns_;
    gpu::PresentMode presentation_{gpu::PresentMode::Fifo};
    Serial next_request_{}, generation_{}, submitted_input_sequence_{};
    VkExtent2D extent_{};
    bool rebuild_{}, closing_{}, closed_{}, waiting_{}, externally_suspended_{};
    Option<gpu::Error> fault_;

    static auto waiting(const gpu::Error &error) -> bool {
        return (error.stage == gpu::Stage::Acquire || error.stage == gpu::Stage::Timeout ||
                error.stage == gpu::Stage::Pending) &&
               (error.result == VK_TIMEOUT || error.result == VK_NOT_READY);
    }
    auto backpressure() const -> bool { return events_.len() >= usize(56); }
    auto suspended() const -> bool { return windowed_ && (externally_suspended_ || !extent_.width || !extent_.height); }
    auto interval_remaining(Serial now) const -> Serial {
        if (!frame_interval_ns_ || last_submission_ns_.is_none()) return 0;
        auto elapsed = now >= *last_submission_ns_ ? now - *last_submission_ns_ : 0;
        return elapsed < frame_interval_ns_ ? frame_interval_ns_ - elapsed : 0;
    }
    void notify(FrameEventType type, Option<gpu::Error> error = None(),
                Option<gpu::PresentStatus> present = None()) {
        FrameEvent event{active_->request,    type,    Some(active_->frame),
                         active_->submission, present, rstd::move(error)};
        if (active_->view) event.view = Some(UiFrame(*active_->view));
        if (type == FrameEventType::GpuComplete) event.present = active_->present;
        if (active_->inputs.is_some()) {
            event.inputs = Some(InputTimings(*active_->inputs));
            event.observed = Some(rstd::time::Instant::now());
            event.earlier_unselected_input_batches = active_->earlier_unselected_input_batches;
        }
        events_.push(rstd::move(event));
    }
    auto fail(gpu::Error error) -> Result<empty, gpu::Error> {
        bool first = fault_.is_none();
        fault_ = Some(gpu::Error(error));
        if (active_.is_some()) {
            if (first)
                notify(FrameEventType::Failed, Some(gpu::Error(error)));
            if (!active_->submission || error.result == VK_ERROR_DEVICE_LOST)
                active_ = None();
        }
        detail::finish_pending_request(pending_, events_, FrameEventType::Cancelled);
        return Err(error);
    }

    auto retry_or_fail(gpu::Error error) -> Result<empty, gpu::Error> {
        if (!waiting(error))
            return fail(error);
        waiting_ = true;
        return Ok(empty{});
    }
    auto submit_active(auto &clock) -> Result<empty, gpu::Error> {
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
        last_submission_ns_ = Some(clock().now_ns);
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
            active_->present = Some(result);
            notify(result == gpu::PresentStatus::OutOfDate ? FrameEventType::PresentRejected
                                                           : FrameEventType::PresentQueued,
                   None(), Some(result));
            rebuild_ |= result != gpu::PresentStatus::Queued;
        }
        return Ok(empty{});
    }

  public:
    // The loop exclusively schedules renderer work. Both owners must outlive it.
    FrameExecutor(Backend &renderer, bool windowed = true,
              gpu::DrawOptions options = {})
        : renderer_(renderer), windowed_(windowed), options_(options),
          extent_(renderer.pixel_extent()) {
        events_.reserve(usize(64));
    }
    FrameExecutor(const FrameExecutor &) = delete;
    auto operator=(const FrameExecutor &) -> FrameExecutor & = delete;
    auto request(Option<Serial> identity = {}) -> Result<Serial, FrameRequestError> {
        if (closing_ || closed_)
            return Err(FrameRequestError::Closing);
        if (fault_.is_some())
            return Err(FrameRequestError::Faulted);
        if (backpressure())
            return Err(FrameRequestError::Busy);
        if (next_request_ == ~Serial{})
            return Err(FrameRequestError::Limit);
        if (identity.is_some() && *identity <= next_request_)
            return Err(FrameRequestError::Limit);
        detail::finish_pending_request(pending_, events_, FrameEventType::Skipped);
        next_request_ = identity.unwrap_or(next_request_ + 1);
        pending_ = Some(next_request_);
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
    void set_suspended(bool value) { if (!closing_ && !closed_) externally_suspended_ = value; }
    void close() {
        if (closing_ || closed_)
            return;
        closing_ = true;
        detail::finish_pending_request(pending_, events_, FrameEventType::Cancelled);
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
                fault_.clone()};
    }
    void set_frame_period(Serial nanoseconds) {
        if (nanoseconds)
            frame_period_ns_ = nanoseconds;
    }
    // Uses the same monotonic clock as pump and wait_milliseconds; zero removes the limit.
    void set_frame_interval(Serial nanoseconds) { frame_interval_ns_ = nanoseconds; }
    auto production_wait(Serial now) const -> Index {
        return Index((interval_remaining(now) + 999'999) / 1'000'000);
    }
    auto requested_present_mode() const -> gpu::PresentMode { return presentation_; }
    auto set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error> {
        if (!windowed_ || closing_ || closed_ || unsigned(mode) > unsigned(gpu::PresentMode::FifoRelaxed))
            return Err(gpu::Error{gpu::Stage::State});
        if (presentation_ != mode) { presentation_ = mode; rebuild_ = true; }
        return Ok(empty{});
    }
    auto detach_surface(Serial timeout_nanoseconds = 1'000'000'000) -> Result<empty, gpu::Error> {
        if (!windowed_ || closing_ || closed_)
            return Err(gpu::Error{gpu::Stage::State});
        externally_suspended_ = true;
        auto detached = renderer_.detach_surface(timeout_nanoseconds);
        if (detached.is_err())
            return detached;
        if (active_.is_some()) {
            notify(FrameEventType::Cancelled);
            active_ = None();
        }
        detail::finish_pending_request(pending_, events_, FrameEventType::Cancelled);
        extent_ = {};
        rebuild_ = false;
        return Ok(empty{});
    }
    auto attach_surface(gpu::SurfaceHost &host, Serial generation) -> Result<empty, gpu::Error> {
        if (!windowed_ || closing_ || closed_ || !generation)
            return Err(gpu::Error{gpu::Stage::State});
        auto attached = renderer_.attach_surface(host);
        if (attached.is_err())
            return attached;
        extent_ = renderer_.pixel_extent();
        generation_ = generation;
        rebuild_ = false;
        externally_suspended_ = false;
        return Ok(empty{});
    }
    auto wait_milliseconds(bool animating = false, FrameTiming timing = {},
                           Option<Serial> deadline = {}) const -> Index {
        auto animation_wait = [&]() -> Index {
            if (!animating && deadline.is_none())
                return 1000;
            auto target = deadline;
            if (animating && timing.expected_present_ns.is_some() && *timing.expected_present_ns > timing.now_ns &&
                (target.is_none() || *timing.expected_present_ns < *target))
                target = timing.expected_present_ns;
            auto duration = target.is_some()
                                ? (*target > timing.now_ns ? *target - timing.now_ns : 0)
                                : frame_period_ns_;
            duration = rstd::cmp::max(duration, interval_remaining(timing.now_ns));
            duration = rstd::min(Serial(1'000'000'000), duration);
            return static_cast<Index>(rstd::min(Serial(1000), (duration + 999'999) / 1'000'000));
        };
        if (closed_ || (fault_.is_some() && !closing_) || backpressure())
            return animation_wait();
        auto interval_wait = [&] {
            auto left = rstd::min(Serial(1'000'000'000), interval_remaining(timing.now_ns));
            return static_cast<Index>((left + 999'999) / 1'000'000);
        };
        if ((active_.is_some() && (active_->submission || !suspended())) || closing_ || (rebuild_ && !suspended()))
            return active_.is_some() && !active_->submission && !waiting_ && !closing_ && !rebuild_
                ? interval_wait() : 1;
        if (pending_.is_some() && !suspended())
            return waiting_ ? 1 : interval_wait();
        return animation_wait();
    }
    auto pump_with_clock(auto clock, auto produce) -> Result<empty, gpu::Error> {
        return pump_with_clock(clock, produce, [] {});
    }
    // Live clocks are sampled again after submission so preparation time cannot shorten the limit.
    auto pump_with_clock(auto clock, auto produce, auto synchronize) -> Result<empty, gpu::Error> {
        auto timing = clock();
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
        if (active_.is_some() && closing_) {
            auto stopped = renderer_.shutdown(0);
            if (stopped.is_err())
                return retry_or_fail(stopped.unwrap_err());
            notify(FrameEventType::Cancelled);
            active_ = None();
            closed_ = true;
            return Ok(empty{});
        }
        if (active_.is_some() && (pending_.is_some() || rebuild_)) {
            auto discarded = renderer_.discard_prepared(0);
            if (discarded.is_err()) {
                auto error = discarded.unwrap_err();
                return retry_or_fail(error);
            }
            if (pending_.is_some())
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
            auto resized = renderer_.resize(extent_, presentation_, 0);
            if (resized.is_err()) {
                auto error = resized.unwrap_err();
                return retry_or_fail(error);
            }
            rebuild_ = false;
        }
        if (interval_remaining(timing.now_ns)) return Ok(empty{});
        if (active_.is_some()) {
            synchronize();
            if (closing_ || rebuild_ || suspended()) return Ok(empty{});
            return submit_active(clock);
        }
        if (pending_.is_none())
            return Ok(empty{});
        auto id = *pending_;
        auto plan = produce();
        if (plan.is_err()) {
            if (plan.unwrap_err() == FrameError::Busy) {
                waiting_ = true;
                return Ok(empty{});
            }
            events_.push({id, FrameEventType::Failed, {}, 0, {}, {}, Some(plan.unwrap_err())});
            pending_ = None();
            return Ok(empty{});
        }
        auto packet = rstd::move(plan).unwrap();
        auto value = rstd::move(packet.plan);
        auto identity = value.identity();
        auto inputs = rstd::move(packet.inputs);
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
                          packet.frozen ? FrameEventType::Skipped : FrameEventType::Failed,
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
            events_.push(
                {id, FrameEventType::Failed, Some(identity), 0, {}, Some(gpu::Error(error))});
            pending_ = None();
            if (error.result == VK_ERROR_DEVICE_LOST)
                return fail(error);
            return Ok(empty{});
        }
        active_ = Some(Active{id, identity, 0, rstd::move(inputs), 0, rstd::move(packet.view)});
        pending_ = None();
        synchronize();
        if (closing_ || rebuild_ || suspended()) return Ok(empty{});
        return submit_active(clock);
    }
};
class FrameLoop : public FrameExecutor<> {
    UiSession &session_;
    bool culling_;
  public:
    FrameLoop(UiSession &session, gpu::Renderer &renderer, bool windowed = true,
              gpu::DrawOptions options = {})
        : FrameExecutor<>(renderer, windowed, options), session_(session), culling_(options.culling) {}
    auto pump(FrameTiming timing = {}) -> Result<empty, gpu::Error> {
        return pump_with_clock([timing] { return timing; });
    }
    auto pump_with_clock(auto clock) -> Result<empty, gpu::Error> {
        return FrameExecutor<>::pump_with_clock(clock, [&] { return detail::plan_frame_packet(session_, culling_); });
    }
};
} // namespace vui::host
