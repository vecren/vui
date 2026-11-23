export module vui:runtime.session;
import :input.router;
import :motion.drivers;
import :scene.visual;
import :frame.types;
import :frame.content;
import :state.bindings;
import :runtime.update;
import :semantics;
using namespace rstd::prelude;

export namespace vui {
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
    bool state_pending{};
};
struct AdvanceTimings {
    Serial document{}, ui_revision{}, render_revision{};
    FrameTiming requested;
    CpuTiming advance, geometry, update, layout, paint, collect, input, build, apply;
    UpdateStats work;
    usize processed_events{};
    Option<SessionStage> error;
};
struct InputSample {
    usize index{};
    InputStamp stamp;
    Option<rstd::time::Instant> dispatch_started, dispatch_finished;
    bool dispatched{}, coalesced{};
    SampleTime sample_time;
    Option<Serial> age_at_receipt_ns;
    Serial history_count{}, history_lost{};
    InputWork work;
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
    MotionRuntime motion_{document_};
    FrameContentRuntime frame_content_{document_};
    EventRouter input_{document_};
    Semantics semantics_{document_, input_};
    UiStateBindings state_bindings_{document_};
    UiUpdates updates_{document_};
    Serial render_session_{new_session()};
    VisualBuilder visual_{render_session_};
    RenderEndpoint endpoint_{render_session_};
    Option<UiUpdate> update_;
    Option<SurfaceInfo> surface_;
    Vec<MutationResult> mutations_;
    FrameTiming last_timing_;
    Serial published_ui_{}, frame_id_{}, revealed_focus_{};
    bool advancing_{}, planning_{}, closing_{}, closed_{};
    AdvanceTimings *active_timings_{};
    Option<AdvanceTimings> last_timings_;
    InputTimings::Data *active_inputs_{};
    Option<InputTimings> last_inputs_, applied_inputs_;
    Serial input_sequence_{}, successful_input_sequence_{};
    usize input_cursor_{};
    auto begin_input_timings(slice<InputEvent> events) -> Option<InputTimings::Data>;
    void finish_input_timings(Option<InputTimings::Data> data, Option<SessionStage> error,
                              bool has_events);
    Option<FrameWaker> frame_waker_;
    struct ContentObservation {
        ContentSource source;
        Serial revision;
        Option<ContentSubscription> subscription;
    };
    Vec<ContentObservation> content_;
    auto observe_content() -> bool;

    template <class Snapshot>
    auto reveal_focus(const Snapshot &snapshot) -> Result<bool, SessionError> {
        if (revealed_focus_ == input_.focus_revision())
            return Ok(false);
        if (auto target = input_.focused(); target.is_some()) {
            auto revealed = document_.bring_into_view(snapshot, *target);
            if (revealed.is_err())
                return Err(SessionError{SessionStage::Update, Some(revealed.unwrap_err())});
        }
        revealed_focus_ = input_.focus_revision();
        return Ok(document_.revision() != snapshot.revision());
    }
    auto settle_input(Size size, const FrameTiming &timing, UpdateStats &stats)
        -> Result<empty, SessionError>;
    auto settle(Size size, const FrameTiming &timing, UpdateStats &stats) -> Result<empty, SessionError>;
    auto resume_subcomposition(UiError) -> Result<bool, SessionError>;

  public:
    UiSession() = default;
    ~UiSession();
    UiSession(const UiSession &) = delete;
    auto operator=(const UiSession &) -> UiSession & = delete;
    auto document() -> UiDocument & { return document_; }
    auto input() -> EventRouter & { return input_; }
    auto semantics() -> Semantics & { return semantics_; }
    auto motion() -> MotionRuntime & { return motion_; }
    auto frame_content() -> FrameContentRuntime & { return frame_content_; }
    auto state_bindings() -> UiStateBindings & { return state_bindings_; }
    auto updates() -> UiUpdates & { return updates_; }
    auto next_tick() const -> Option<Serial> { return frame_content_.active() ? None<Serial>() : motion_.next_tick(); }
    auto render_scene() const -> const RenderScene & { return endpoint_.scene(); }
    auto set_frame_waker(Option<FrameWaker> waker) -> Result<empty, SessionError>;
    auto content_pending() const -> bool;
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
                 DiagnosticsOptions options = {}) -> Result<AdvanceResult, SessionError>;

  private:
    auto advance_impl(slice<InputEvent> events, FrameTiming timing, SurfaceInfo surface)
        -> Result<AdvanceResult, SessionError>;

    auto publish_update() -> Result<empty, SessionError>;

  public:
    auto plan_frame(FrameClipPolicy clip_policy = {}) -> Result<FramePlan, FrameError>;
    auto plan_ui_frame(FrameClipPolicy clip_policy = {}) -> Result<UiFrame, FrameError>;
    // The host acknowledges a completed plan; GPU completion does not prove display presentation.
    auto complete_frame(UiFrame frame) -> Result<bool, FrameError>;
    auto close() -> Result<empty, SessionError>;
};
} // namespace vui
