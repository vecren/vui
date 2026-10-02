export module vui:session;
import :events;
import :visual;
import :frame;
using namespace rstd::prelude;

export namespace vui {
struct FrameTiming {
    Serial now_ns{};
    Option<Serial> expected_present_ns;
};
enum class SessionStage {
    Closed,
    Reentrant,
    Timing,
    Surface,
    Update,
    Input,
    Publish,
    Apply,
    Limit,
    Backpressure,
    Animation
};
struct SessionError {
    SessionStage stage;
    Option<UiError> ui{};
    Option<InputError> input{};
    Option<PublishError> publish{};
    Option<ApplyError> apply{};
    usize processed_events{};
    Option<AnimationError> animation{};
};
struct AdvanceResult {
    Serial ui_revision{}, render_revision{};
    UpdateStats stats;
    bool frame_needed{};
    usize processed_events{};
    bool animations_active{};
};
struct AdvanceTimings {
    Serial document{}, ui_revision{}, render_revision{};
    FrameTiming requested;
    CpuTiming advance, update, layout, paint, collect, input, build, apply;
    UpdateStats work;
    usize processed_events{};
    Option<SessionStage> error;
};
struct InputSample {
    usize index{};
    InputStamp stamp;
    Option<rstd::time::Instant> dispatch_started, dispatch_finished;
    bool dispatched{};
};
struct InputBatchTiming {
    Serial document{}, sequence{}, successful_sequence{}, scene_session{}, ui_revision{},
        render_revision{};
    usize input_count{}, untraced{}, omitted{};
    Option<rstd::time::Instant> applied;
    rstd::time::Instant finished{};
    Option<SessionStage> error;
};
class InputTimings {
    struct Data {
        InputBatchTiming batch;
        Vec<InputSample> samples;
    };
    rstd::sync::Arc<Data> data_;
    explicit InputTimings(Data data) : data_(rstd::sync::Arc<Data>::make(rstd::move(data))) {}
    friend class UiSession;

  public:
    InputTimings(const InputTimings &other) : data_(other.data_.clone()) {}
    auto operator=(const InputTimings &other) -> InputTimings & {
        if (this != &other)
            data_ = other.data_.clone();
        return *this;
    }
    InputTimings(InputTimings &&) = default;
    auto operator=(InputTimings &&) -> InputTimings & = default;
    auto batch() const -> InputBatchTiming { return data_->batch; }
    auto samples() const -> slice<InputSample> { return data_->samples.as_slice(); }
};
class UiSession {
    UiDocument document_;
    EventRouter input_{document_};
    Serial render_session_{new_session()};
    VisualBuilder visual_{render_session_};
    RenderEndpoint endpoint_{render_session_};
    Option<UiUpdate> update_;
    Option<SurfaceInfo> surface_;
    Vec<MutationResult> mutations_;
    Serial last_time_{}, published_ui_{}, frame_id_{}, revealed_focus_{};
    bool advancing_{}, closing_{}, closed_{};
    AdvanceTimings *active_timings_{};
    Option<AdvanceTimings> last_timings_;
    InputTimings::Data *active_inputs_{};
    Option<InputTimings> last_inputs_, applied_inputs_;
    Serial input_sequence_{}, successful_input_sequence_{};
    usize input_cursor_{};
    auto begin_input_timings(slice<InputEvent> events) -> Option<InputTimings::Data> {
        if (input_sequence_ == ~Serial{} || successful_input_sequence_ == ~Serial{})
            return None();
        InputTimings::Data result;
        result.batch.input_count = events.len();
        for (usize i{}; i < events.len(); ++i) {
            if (events[i].stamp.is_none()) {
                ++result.batch.untraced;
                continue;
            }
            if (result.samples.len() >= usize(1024)) {
                ++result.batch.omitted;
                continue;
            }
            result.samples.push({i, *events[i].stamp, {}, {}, false});
        }
        if (result.samples.is_empty())
            return None();
        result.batch.sequence = ++input_sequence_;
        return Some(rstd::move(result));
    }
    void finish_input_timings(Option<InputTimings::Data> data, Option<SessionStage> error,
                              bool has_events) {
        active_inputs_ = nullptr;
        last_inputs_ = None();
        if (has_events)
            applied_inputs_ = None();
        if (data.is_none())
            return;
        auto value = data.unwrap();
        auto &batch = value.batch;
        batch.document = document_.session();
        batch.scene_session = render_session_;
        batch.ui_revision = document_.revision();
        batch.render_revision = endpoint_.scene().revision();
        batch.finished = rstd::time::Instant::now();
        batch.error = error;
        if (error.is_none())
            batch.successful_sequence = ++successful_input_sequence_;
        last_inputs_ = Some(InputTimings(rstd::move(value)));
        if (error.is_none())
            applied_inputs_ = Some(InputTimings(*last_inputs_));
    }
    Option<FrameWaker> frame_waker_;
    struct ContentObservation {
        ContentSource source;
        Serial revision;
        Option<ContentSubscription> subscription;
    };
    Vec<ContentObservation> content_;
    auto observe_content() -> bool {
        Vec<ContentObservation> next;
        bool changed{};
        for (const auto &visual : update_->visuals()) {
            for (const auto &op : visual.content.operations()) {
                if (!op.is_External())
                    continue;
                const auto &source = op.as_External().source;
                bool duplicate{};
                for (const auto &observed : next)
                    duplicate |= observed.source.id() == source.id();
                if (duplicate)
                    continue;
                bool unchanged{};
                Option<ContentSubscription> subscription;
                for (auto &observed : content_) {
                    if (observed.source.id() != source.id())
                        continue;
                    unchanged = observed.revision == source.revision();
                    subscription = rstd::move(observed.subscription);
                    break;
                }
                if (source.closed())
                    subscription = None();
                else if (subscription.is_none() && frame_waker_.is_some())
                    subscription = Some(source.subscribe(*frame_waker_).unwrap());
                changed |= !unchanged;
                next.push(ContentObservation{source, source.revision(), rstd::move(subscription)});
            }
        }
        changed |= next.len() != content_.len();
        content_ = rstd::move(next);
        return changed;
    }

    auto settle(Size size, Serial sample_time, UpdateStats &stats) -> Result<empty, SessionError> {
        for (int iteration = 0; iteration < 16; ++iteration) {
            if (mutations_.len() + document_.pending_mutations() > usize(4096))
                return Err(SessionError{SessionStage::Backpressure});
            auto mutations = document_.flush_mutations();
            if (mutations.is_err())
                return Err(SessionError{SessionStage::Update, Some(mutations.unwrap_err())});
            for (auto &result : mutations.unwrap())
                mutations_.push(rstd::move(result));
            auto sampled = document_.sample_animations(sample_time);
            if (sampled.is_err()) {
                SessionError error{SessionStage::Animation};
                error.animation = Some(sampled.unwrap_err());
                return Err(rstd::move(error));
            }
            auto callbacks = document_.dispatch_animation_callbacks();
            if (callbacks.is_err()) {
                SessionError error{SessionStage::Animation};
                error.animation = Some(callbacks.unwrap_err());
                return Err(rstd::move(error));
            }
            auto next = document_.update(size, {active_timings_ != nullptr});
            if (active_timings_) {
                const auto sample = document_.last_update_timings().unwrap();
                active_timings_->update.add(sample.update);
                active_timings_->layout.add(sample.layout);
                active_timings_->paint.add(sample.paint);
                active_timings_->collect.add(sample.collect);
                active_timings_->work.add(sample.work);
            }
            if (next.is_err())
                return Err(SessionError{SessionStage::Update, Some(next.unwrap_err())});
            update_ = Some(next.unwrap());
            stats.add(update_->stats());
            auto synced = input_.synchronize(*update_);
            if (synced.is_err())
                return Err(SessionError{SessionStage::Input, {}, Some(synced.unwrap_err())});
            if (document_.revision() == update_->revision() &&
                document_.pending_mutations() == usize() &&
                !document_.pending_animation_callbacks()) {
                if (revealed_focus_ != input_.focus_revision()) {
                    if (auto target = input_.focused(); target.is_some()) {
                        auto revealed = document_.bring_into_view(*update_, *target);
                        if (revealed.is_err())
                            return Err(
                                SessionError{SessionStage::Update, Some(revealed.unwrap_err())});
                    }
                    revealed_focus_ = input_.focus_revision();
                    if (document_.revision() != update_->revision())
                        continue;
                }
                return Ok(empty{});
            }
        }
        return Err(SessionError{SessionStage::Limit});
    }

  public:
    UiSession() = default;
    UiSession(const UiSession &) = delete;
    auto operator=(const UiSession &) -> UiSession & = delete;
    auto document() -> UiDocument & { return document_; }
    auto input() -> EventRouter & { return input_; }
    auto render_scene() const -> const RenderScene & { return endpoint_.scene(); }
    auto set_frame_waker(Option<FrameWaker> waker) -> Result<empty, SessionError> {
        if (closed_ || closing_)
            return Err(SessionError{SessionStage::Closed});
        if (advancing_ || !document_.can_route())
            return Err(SessionError{SessionStage::Reentrant});
        if (waker.is_some() && !waker->valid())
            waker = None();
        frame_waker_ = rstd::move(waker);
        for (auto &observed : content_) {
            observed.subscription = None();
            if (frame_waker_.is_some() && frame_waker_->valid() && !observed.source.closed())
                observed.subscription = Some(observed.source.subscribe(*frame_waker_).unwrap());
        }
        if (frame_waker_.is_some() && content_pending())
            frame_waker_->request();
        return Ok(empty{});
    }
    auto content_pending() const -> bool {
        if (closed_ || closing_)
            return false;
        for (const auto &observed : content_)
            if (observed.source.revision() != observed.revision)
                return true;
        return false;
    }
    auto visual_update() const -> const UiUpdate * {
        return update_.is_some() ? &*update_ : nullptr;
    }
    auto render_id(UiId id) const -> Option<RenderId> { return visual_.render_id(id); }
    auto poll_mutations() -> Vec<MutationResult> {
        return rstd::exchange(mutations_, Vec<MutationResult>{});
    }
    auto last_advance_timings() const -> Option<AdvanceTimings> { return last_timings_; }
    auto last_input_timings() const -> Option<InputTimings> {
        return last_inputs_.is_some() ? Some(InputTimings(*last_inputs_)) : None<InputTimings>();
    }
    auto last_successful_input_timings() const -> Option<InputTimings> {
        return applied_inputs_.is_some() ? Some(InputTimings(*applied_inputs_))
                                         : None<InputTimings>();
    }
    auto advance(slice<InputEvent> events, FrameTiming timing, SurfaceInfo surface,
                 DiagnosticsOptions options = {}) -> Result<AdvanceResult, SessionError> {
        if (closed_ || closing_)
            return Err(SessionError{SessionStage::Closed});
        if (advancing_ || !document_.can_route())
            return Err(SessionError{SessionStage::Reentrant});
        AdvanceTimings sample;
        sample.requested = timing;
        active_timings_ = options.timings ? &sample : nullptr;
        CpuTimer timer(active_timings_ ? &sample.advance : nullptr);
        auto inputs = begin_input_timings(events);
        active_inputs_ = inputs.is_some() ? &*inputs : nullptr;
        input_cursor_ = usize();
        auto result = advance_impl(events, timing, surface);
        timer.pause();
        active_timings_ = nullptr;
        last_timings_ = None();
        if (options.timings) {
            sample.document = document_.session();
            sample.ui_revision = document_.revision();
            sample.render_revision = visual_.applied_revision();
        }
        if (result.is_err()) {
            auto error = result.unwrap_err();
            finish_input_timings(rstd::move(inputs), Some(error.stage), !events.is_empty());
            if (options.timings) {
                sample.error = Some(error.stage);
                sample.processed_events = error.processed_events;
                last_timings_ = Some(sample);
            }
            return Err(rstd::move(error));
        }
        finish_input_timings(rstd::move(inputs), None(), !events.is_empty());
        if (options.timings)
            last_timings_ = Some(sample);
        return result;
    }

  private:
    auto advance_impl(slice<InputEvent> events, FrameTiming timing, SurfaceInfo surface)
        -> Result<AdvanceResult, SessionError> {
        usize processed{};
        auto failure = [&](SessionError error) -> Result<AdvanceResult, SessionError> {
            error.processed_events = processed;
            return Err(rstd::move(error));
        };
        if (!surface.valid())
            return failure(SessionError{SessionStage::Surface});
        if (timing.now_ns < last_time_ ||
            (timing.expected_present_ns.is_some() && *timing.expected_present_ns < timing.now_ns))
            return failure(SessionError{SessionStage::Timing});
        advancing_ = true;
        struct Guard {
            bool &flag;
            ~Guard() { flag = false; }
        } guard{advancing_};
        last_time_ = timing.now_ns;
        UpdateStats stats;
        auto sample_time =
            timing.expected_present_ns.is_some() ? *timing.expected_present_ns : timing.now_ns;
        auto settled = settle(surface.logical_size, sample_time, stats);
        if (settled.is_err())
            return failure(settled.unwrap_err());
        for (const auto &event : events) {
            InputSample *sample{};
            if (active_inputs_ && input_cursor_ < active_inputs_->samples.len() &&
                active_inputs_->samples[input_cursor_].index == processed) {
                sample = &active_inputs_->samples[input_cursor_++];
                sample->dispatch_started = Some(rstd::time::Instant::now());
            }
            CpuTimer input_time(active_timings_ ? &active_timings_->input : nullptr);
            auto dispatched = input_.dispatch(event);
            input_time.pause();
            if (sample) {
                sample->dispatch_finished = Some(rstd::time::Instant::now());
                sample->dispatched = dispatched.is_ok();
            }
            ++processed;
            if (active_timings_)
                active_timings_->processed_events = processed;
            if (dispatched.is_err())
                return failure(
                    SessionError{SessionStage::Input, {}, Some(dispatched.unwrap_err())});
            settled = settle(surface.logical_size, sample_time, stats);
            if (settled.is_err())
                return failure(settled.unwrap_err());
        }
        bool changed = published_ui_ != update_->revision();
        bool surface_changed = surface_.is_none() || surface_->generation != surface.generation ||
                               surface_->logical_size != surface.logical_size ||
                               surface_->scale != surface.scale;
        if (changed) {
            auto transaction = visual_.build(*update_, {active_timings_ != nullptr});
            if (active_timings_)
                active_timings_->build.add(visual_.last_build_timings()->build);
            if (transaction.is_err())
                return failure(
                    SessionError{SessionStage::Publish, {}, {}, Some(transaction.unwrap_err())});
            auto pending = Some(transaction.unwrap());
            auto enqueued = endpoint_.try_enqueue(pending);
            if (enqueued != EnqueueResult::Accepted) {
                visual_.acknowledge(ApplyEvent::Cancelled(pending->header())).unwrap();
                return failure(SessionError{SessionStage::Backpressure});
            }
            endpoint_.apply_pending({active_timings_ != nullptr});
            if (active_timings_)
                active_timings_->apply.add(endpoint_.last_apply_timings()->apply);
            auto applied = endpoint_.poll_events();
            for (const auto &event : applied) {
                if (active_inputs_ && event.is_Applied())
                    active_inputs_->batch.applied = Some(rstd::time::Instant::now());
                auto acknowledged = visual_.acknowledge(event);
                if (acknowledged.is_err())
                    return failure(SessionError{
                        SessionStage::Publish, {}, {}, Some(acknowledged.unwrap_err())});
                if (event.is_Rejected())
                    return failure(SessionError{SessionStage::Apply,
                                                {},
                                                {},
                                                {},
                                                Some(ApplyError(event.as_Rejected().error))});
                if (event.is_Cancelled())
                    return failure(SessionError{SessionStage::Closed});
            }
            published_ui_ = update_->revision();
        }
        surface_ = Some(surface);
        bool content_changed = observe_content();
        return Ok(AdvanceResult{update_->revision(), visual_.applied_revision(), stats,
                                changed || surface_changed || content_changed, processed,
                                document_.animations_active()});
    }

  public:
    auto plan_frame() -> Result<FramePlan, FrameError> {
        if (closed_ || closing_ || surface_.is_none())
            return Err(FrameError::InvalidSurface);
        if (frame_id_ == ~Serial{})
            return Err(FrameError::Limit);
        FramePlanner planner;
        return planner.build(endpoint_.scene(), *surface_, ++frame_id_);
    }
    auto close() -> Result<empty, SessionError> {
        if (closed_)
            return Ok(empty{});
        if (advancing_ || !document_.can_route())
            return Err(SessionError{SessionStage::Reentrant});
        closing_ = true;
        auto animations = document_.close_animations();
        if (animations.is_err()) {
            SessionError error{SessionStage::Animation};
            error.animation = Some(animations.unwrap_err());
            return Err(rstd::move(error));
        }
        auto result = input_.close();
        if (result.is_err())
            return Err(SessionError{SessionStage::Input, {}, Some(result.unwrap_err())});
        for (auto &mutation : document_.cancel_mutations().unwrap())
            mutations_.push(rstd::move(mutation));
        endpoint_.close();
        content_.clear();
        frame_waker_ = None();
        closed_ = true;
        last_timings_ = None();
        last_inputs_ = None();
        applied_inputs_ = None();
        return Ok(empty{});
    }
};
} // namespace vui
