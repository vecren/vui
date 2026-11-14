export module vui:motion.sequence;
import :document;
using namespace rstd::prelude;

export namespace vui {
enum class AnimationSequenceFailure { Closed, Inactive, Reentrant, InvalidValue, Limit, Animation, Ui };
struct AnimationSequenceError {
    AnimationSequenceFailure kind;
    Option<AnimationError> animation;
    Option<UiError> ui;
};
template <class T> using AnimationSequenceResult = Result<T, AnimationSequenceError>;
enum class AnimationRunEndReason { Finished, Cancelled, Replaced, TargetDetached, Failed, ScopeClosed };
struct AnimationRunResult {
    AnimationRunEndReason reason;
    Option<AnimationSequenceError> error;
};
struct AnimationTrack {
    UiId target;
    AnimationProperty property;
    AnimationValue to;
    AnimationOptions options;
    Option<AnimationValue> from;
    static auto transform(UiId id, Transform to, AnimationOptions options = {}, Option<Transform> from = {}) -> AnimationTrack {
        return {id, AnimationProperty::Transform, AnimationValue::Matrix(to), options,
            from.is_some() ? Some(AnimationValue::Matrix(*from)) : None<AnimationValue>()};
    }
    static auto opacity(UiId id, Scalar to, AnimationOptions options = {}, Option<Scalar> from = {}) -> AnimationTrack {
        return {id, AnimationProperty::Opacity, AnimationValue::Number(to), options,
            from.is_some() ? Some(AnimationValue::Number(*from)) : None<AnimationValue>()};
    }
    static auto width(UiId id, Scalar to, AnimationOptions options = {}, Option<Scalar> from = {}) -> AnimationTrack {
        return {id, AnimationProperty::Width, AnimationValue::Number(to), options,
            from.is_some() ? Some(AnimationValue::Number(*from)) : None<AnimationValue>()};
    }
    static auto height(UiId id, Scalar to, AnimationOptions options = {}, Option<Scalar> from = {}) -> AnimationTrack {
        return {id, AnimationProperty::Height, AnimationValue::Number(to), options,
            from.is_some() ? Some(AnimationValue::Number(*from)) : None<AnimationValue>()};
    }
    static auto background(UiId id, Color to, AnimationOptions options = {}, Option<Color> from = {}) -> AnimationTrack {
        return {id, AnimationProperty::Background, AnimationValue::Tint(to), options,
            from.is_some() ? Some(AnimationValue::Tint(*from)) : None<AnimationValue>()};
    }
};
using AnimationGroup = Vec<AnimationTrack>;
namespace animation_detail { struct RunState; }
class AnimationGate {
    struct State {
        Serial id{new_session()};
        bool signaled{};
        Vec<rstd::rc::Weak<animation_detail::RunState>> waiters;
    };
    rstd::rc::Rc<State> state_{rstd::rc::make_rc<State>()};
    friend struct animation_detail::RunState;
  public:
    auto id() const -> Serial { return state_->id; }
    auto ready() const -> bool { return state_->signaled; }
    auto signal() const -> AnimationSequenceResult<bool>;
};
namespace animation_detail {
struct GroupFactory {
    Box<rstd::dyn<rstd::FnMut<AnimationSequenceResult<AnimationGroup>(UiDocument &)>>> invoke;
    template <class F> explicit GroupFactory(F factory)
        : invoke(Box<rstd::dyn<rstd::FnMut<AnimationSequenceResult<AnimationGroup>(UiDocument &)>>>::make(rstd::move(factory))) {}
};
struct Action {
    Box<rstd::dyn<rstd::FnMut<AnimationSequenceResult<empty>(UiDocument &)>>> invoke;
    template <class F> explicit Action(F action)
        : invoke(Box<rstd::dyn<rstd::FnMut<AnimationSequenceResult<empty>(UiDocument &)>>>::make(rstd::move(action))) {}
};
struct Completion {
    Box<rstd::dyn<rstd::FnMut<void(const AnimationRunResult &)>>> invoke;
    template <class F> explicit Completion(F callback)
        : invoke(Box<rstd::dyn<rstd::FnMut<void(const AnimationRunResult &)>>>::make(rstd::move(callback))) {}
};
struct Step {
    Option<Box<GroupFactory>> group;
    Option<Box<Action>> action;
    Option<AnimationGate> gate;
};
}
class AnimationSequence {
    Vec<animation_detail::Step> steps_;
    friend struct animation_detail::RunState;
    friend class AnimationScope;
  public:
    AnimationSequence() = default;
    AnimationSequence(AnimationSequence &&) = default;
    auto operator=(AnimationSequence &&) -> AnimationSequence & = default;
    void parallel(AnimationGroup group) {
        parallel([group = rstd::move(group)](UiDocument &) mutable -> AnimationSequenceResult<AnimationGroup> {
            return Ok(rstd::move(group));
        });
    }
    template <class F> void parallel(F factory) {
        steps_.push({Some(Box<animation_detail::GroupFactory>::make(rstd::move(factory))), {}, {}});
    }
    template <class F> void then(F action) {
        steps_.push({{}, Some(Box<animation_detail::Action>::make(rstd::move(action))), {}});
    }
    void wait(AnimationGate gate) { steps_.push({{}, {}, Some(rstd::move(gate))}); }
};
namespace animation_detail {
struct RunState {
    struct Track { AnimationId id; UiId target; AnimationProperty property; bool completed{}; };
    UiDocument *document;
    BindingGuard lifetime;
    Serial id{new_session()};
    AnimationSequence sequence;
    Option<Box<Completion>> completion;
    Vec<Track> tracks;
    usize next{}, remaining{};
    Option<AnimationGate> waiting;
    Option<AnimationRunResult> result;
    bool driving{};
    RunState(UiDocument &ui, AnimationSequence value, Box<Completion> callback)
        : document(&ui), lifetime(ui.lifetime()), sequence(rstd::move(value)), completion(Some(rstd::move(callback))) {}
    auto active() const -> bool { return result.is_none() && lifetime.alive(); }
    void finish(AnimationRunEndReason reason, Option<AnimationSequenceError> error = {}) {
        if (result.is_some()) return;
        result = Some(AnimationRunResult{reason, error});
        auto pending = rstd::exchange(tracks, Vec<Track>{});
        remaining = usize(); waiting = None(); sequence.steps_.clear();
        auto callback = rstd::move(completion);
        auto stopped = *result;
        auto cleanup = [pending = rstd::move(pending), callback = rstd::move(callback), stopped](UiDocument &ui) mutable -> Result<empty, UiError> {
            for (const auto &track : pending) if (!track.completed) ui.cancel_animation(track.id);
            if (callback.is_some()) (*callback)->invoke->operator()(stopped);
            return Ok(empty{});
        };
        if (!lifetime.alive()) return;
        if (document->can_animate()) cleanup(*document);
        else {
            auto queued = document->defer_cleanup(rstd::move(cleanup));
            if (queued.is_err()) result = Some(AnimationRunResult{AnimationRunEndReason::Failed,
                Some(AnimationSequenceError{AnimationSequenceFailure::Ui, {}, Some(queued.unwrap_err())})});
        }
    }
    auto write_from(const AnimationTrack &track) -> Result<empty, UiError> {
        if (track.from.is_none()) return Ok(empty{});
        const auto &value = *track.from;
        switch (track.property) {
        case AnimationProperty::Transform: return document->set_transform(track.target, value.as_Matrix().value);
        case AnimationProperty::Opacity: return document->set_opacity(track.target, value.as_Number().value);
        case AnimationProperty::Width:
        case AnimationProperty::Height: {
            auto style = document->base_style(track.target); if (style.is_err()) return Err(style.unwrap_err());
            auto layout = style.unwrap().layout;
            if (track.property == AnimationProperty::Width) layout.width = Some(Scalar(value.as_Number().value));
            else layout.height = Some(Scalar(value.as_Number().value));
            return document->set_layout(track.target, layout);
        }
        case AnimationProperty::Background: {
            auto style = document->base_style(track.target); if (style.is_err()) return Err(style.unwrap_err());
            return document->set_background(track.target, value.as_Tint().value, style.unwrap().radius);
        }
        }
        return Err(UiError::InvalidValue);
    }
    auto start(const AnimationTrack &track) -> Result<AnimationId, AnimationError> {
        switch (track.property) {
        case AnimationProperty::Transform: return document->animate_transform(track.target, track.to.as_Matrix().value, track.options);
        case AnimationProperty::Opacity: return document->animate_opacity(track.target, track.to.as_Number().value, track.options);
        case AnimationProperty::Width: return document->animate_width(track.target, track.to.as_Number().value, track.options);
        case AnimationProperty::Height: return document->animate_height(track.target, track.to.as_Number().value, track.options);
        case AnimationProperty::Background: return document->animate_background(track.target, track.to.as_Tint().value, track.options);
        }
        return Err(AnimationError::InvalidValue);
    }
    void ended(const AnimationEvent &event, rstd::rc::Weak<RunState> self) {
        if (!active()) return;
        for (auto &track : tracks) if (track.id == event.id && !track.completed) {
            track.completed = true;
            if (event.reason == AnimationEndReason::Finished) {
                auto mount = document->mount_state(event.target);
                if (mount.is_err() || !mount->mounted || mount->generation != event.mount_generation) {
                    finish(AnimationRunEndReason::TargetDetached); return;
                }
            }
            if (event.reason != AnimationEndReason::Finished) {
                auto reason = event.reason == AnimationEndReason::Replaced ? AnimationRunEndReason::Replaced
                    : event.reason == AnimationEndReason::TargetDetached ? AnimationRunEndReason::TargetDetached
                    : AnimationRunEndReason::Cancelled;
                finish(reason); return;
            }
            if (--remaining == usize()) { tracks.clear(); advance(rstd::move(self)); }
            return;
        }
    }
    void advance(rstd::rc::Weak<RunState> self) {
        if (result.is_some() || driving) return;
        if (!lifetime.alive()) { finish(AnimationRunEndReason::ScopeClosed); return; }
        driving = true;
        struct Guard { bool &value; ~Guard() { value = false; } } guard{driving};
        while (active() && remaining == usize()) {
            if (waiting.is_some()) {
                if (!waiting->ready()) return;
                waiting = None();
            }
            if (next >= sequence.steps_.len()) { finish(AnimationRunEndReason::Finished); return; }
            auto step = rstd::move(sequence.steps_[next++]);
            if (step.gate.is_some()) {
                if (!step.gate->ready()) {
                    auto &waiters = step.gate->state_.get()->waiters;
                    waiters.retain([](const auto &waiter) { auto run = waiter.upgrade(); return run.is_some() && run->get()->active(); });
                    if (waiters.len() >= usize(4096)) {
                        finish(AnimationRunEndReason::Failed, Some(AnimationSequenceError{AnimationSequenceFailure::Limit})); return;
                    }
                    waiting = Some(AnimationGate(*step.gate));
                    waiters.push(rstd::rc::Weak<RunState>(self)); return;
                }
            } else if (step.action.is_some()) {
                auto executed = (*step.action)->invoke->operator()(*document);
                if (!active()) return;
                if (executed.is_err()) { finish(AnimationRunEndReason::Failed, Some(executed.unwrap_err())); return; }
            } else if (step.group.is_some()) {
                auto made = (*step.group)->invoke->operator()(*document);
                if (!active()) return;
                if (made.is_err()) { finish(AnimationRunEndReason::Failed, Some(made.unwrap_err())); return; }
                auto group = made.unwrap();
                if (group.len() > usize(4096)) { finish(AnimationRunEndReason::Failed, Some(AnimationSequenceError{AnimationSequenceFailure::Limit})); return; }
                for (usize i{}; i < group.len(); ++i) {
                    const auto &track = group[i];
                    if (!track.options.valid() || !track.to.valid(track.property) || (track.from.is_some() && !track.from->valid(track.property))) {
                        finish(AnimationRunEndReason::Failed, Some(AnimationSequenceError{AnimationSequenceFailure::InvalidValue})); return;
                    }
                    auto mounted = document->mount_state(track.target);
                    if (mounted.is_err() || !mounted->mounted) {
                        finish(AnimationRunEndReason::TargetDetached); return;
                    }
                    for (usize j{}; j < i; ++j) if (group[j].target == track.target && group[j].property == track.property) {
                        finish(AnimationRunEndReason::Failed, Some(AnimationSequenceError{AnimationSequenceFailure::InvalidValue})); return;
                    }
                }
                tracks.reserve(group.len());
                for (const auto &track : group) {
                    auto initial = write_from(track);
                    if (initial.is_err()) { finish(AnimationRunEndReason::Failed, Some(AnimationSequenceError{AnimationSequenceFailure::Ui, {}, Some(initial.unwrap_err())})); return; }
                    auto started = start(track);
                    if (started.is_err()) { finish(AnimationRunEndReason::Failed, Some(AnimationSequenceError{AnimationSequenceFailure::Animation, Some(started.unwrap_err())})); return; }
                    auto id = started.unwrap();
                    tracks.push({id, track.target, track.property}); ++remaining;
                    auto registered = document->on_animation_end(id, [self](UiDocument &, const AnimationEvent &event) {
                        auto run = self.upgrade(); if (run.is_some()) run->get()->ended(event, rstd::rc::Weak<RunState>(self));
                    });
                    if (registered.is_err()) { finish(AnimationRunEndReason::Failed, Some(AnimationSequenceError{AnimationSequenceFailure::Animation, Some(registered.unwrap_err())})); return; }
                }
                if (remaining != usize()) document->bindings().wake();
            }
        }
    }
};
}
inline auto AnimationGate::signal() const -> AnimationSequenceResult<bool> {
    auto state = state_;
    if (state->signaled) return Ok(false);
    for (const auto &weak : state->waiters) {
        auto run = weak.upgrade();
        if (run.is_some() && run->get()->active() && run->get()->lifetime.alive() && !run->get()->document->can_animate())
            return Err(AnimationSequenceError{AnimationSequenceFailure::Reentrant});
    }
    state.get()->signaled = true;
    auto waiters = rstd::exchange(state.get()->waiters, Vec<rstd::rc::Weak<animation_detail::RunState>>{});
    for (auto &weak : waiters) {
        auto run = weak.upgrade(); if (run.is_some()) run->get()->advance(rstd::move(weak));
    }
    return Ok(true);
}
class AnimationRun {
    rstd::rc::Rc<animation_detail::RunState> state_;
    friend class AnimationScope;
    explicit AnimationRun(rstd::rc::Rc<animation_detail::RunState> state) : state_(rstd::move(state)) {}
  public:
    AnimationRun() = default;
    auto id() const -> Serial { return state_ ? state_->id : 0; }
    auto active() const -> bool { return state_ && state_->active(); }
    auto result() const -> Option<AnimationRunResult> {
        if (!state_) return None();
        if (state_->result.is_some()) return state_->result;
        return state_->lifetime.alive() ? None<AnimationRunResult>() : Some(AnimationRunResult{AnimationRunEndReason::ScopeClosed});
    }
    auto cancel() const -> AnimationSequenceResult<empty> {
        if (!state_ || !state_->active()) return Ok(empty{});
        if (state_->lifetime.alive() && !state_->document->can_animate()) return Err(AnimationSequenceError{AnimationSequenceFailure::Reentrant});
        auto state = state_.clone(); state.get()->finish(AnimationRunEndReason::Cancelled); return Ok(empty{});
    }
    auto retarget(UiId target, AnimationProperty property, AnimationValue value, Option<AnimationOptions> options = {}) const -> AnimationSequenceResult<empty> {
        if (!active() || !state_->lifetime.alive()) return Err(AnimationSequenceError{AnimationSequenceFailure::Closed});
        for (const auto &track : state_->tracks) if (!track.completed && track.target == target && track.property == property) {
            auto changed = options.is_some() ? state_->document->retarget_animation(track.id, rstd::move(value), *options)
                : state_->document->retarget_animation(track.id, rstd::move(value));
            if (changed.is_err()) return Err(AnimationSequenceError{AnimationSequenceFailure::Animation, Some(changed.unwrap_err())});
            return Ok(empty{});
        }
        return Err(AnimationSequenceError{AnimationSequenceFailure::InvalidValue});
    }
};
class AnimationScope {
    UiDocument *document_;
    BindingGuard lifetime_;
    Vec<rstd::rc::Rc<animation_detail::RunState>> runs_;
    bool activated_{}, closed_{};
    AnimationScope(UiDocument &document, bool active) : document_(&document), lifetime_(document.lifetime()), activated_(active) {}
  public:
    static auto create(UiDocument &document, bool activated = true) -> rstd::rc::Rc<AnimationScope> {
        return rstd::rc::Rc<AnimationScope>(new AnimationScope(document, activated));
    }
    ~AnimationScope() { close(); }
    auto activate() -> AnimationSequenceResult<empty> {
        if (closed_ || !lifetime_.alive()) return Err(AnimationSequenceError{AnimationSequenceFailure::Closed});
        if (!document_->can_animate()) return Err(AnimationSequenceError{AnimationSequenceFailure::Reentrant});
        activated_ = true; return Ok(empty{});
    }
    auto active() const -> bool {
        for (const auto &run : runs_) if (run->active()) return true;
        return false;
    }
    template <class F> auto play(AnimationSequence sequence, F on_end) -> AnimationSequenceResult<AnimationRun> {
        if (closed_ || !lifetime_.alive()) return Err(AnimationSequenceError{AnimationSequenceFailure::Closed});
        if (!activated_) return Err(AnimationSequenceError{AnimationSequenceFailure::Inactive});
        if (!document_->can_animate()) return Err(AnimationSequenceError{AnimationSequenceFailure::Reentrant});
        runs_.retain([](const auto &run) { return run->active(); });
        if (runs_.len() >= usize(4096) || sequence.steps_.len() > usize(4096)) return Err(AnimationSequenceError{AnimationSequenceFailure::Limit});
        auto run = rstd::rc::make_rc<animation_detail::RunState>(*document_, rstd::move(sequence), Box<animation_detail::Completion>::make(rstd::move(on_end)));
        runs_.push(run.clone()); run.get()->advance(run.downgrade());
        return Ok(AnimationRun(rstd::move(run)));
    }
    auto play(AnimationSequence sequence) -> AnimationSequenceResult<AnimationRun> {
        return play(rstd::move(sequence), [](const AnimationRunResult &) {});
    }
    auto cancel() -> AnimationSequenceResult<empty> {
        if (lifetime_.alive() && !document_->can_animate()) return Err(AnimationSequenceError{AnimationSequenceFailure::Reentrant});
        auto pending = rstd::exchange(runs_, Vec<rstd::rc::Rc<animation_detail::RunState>>{});
        for (auto &run : pending) run.get()->finish(AnimationRunEndReason::Cancelled);
        return Ok(empty{});
    }
    void close() {
        if (closed_) return;
        closed_ = true; activated_ = false;
        auto pending = rstd::exchange(runs_, Vec<rstd::rc::Rc<animation_detail::RunState>>{});
        for (auto &run : pending) run.get()->finish(AnimationRunEndReason::ScopeClosed);
    }
};
} // namespace vui
