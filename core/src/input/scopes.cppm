export module vui:input.scopes;
import :document.interface;
import :input.protocol;
using namespace rstd::prelude;

export namespace vui {
inline auto input_descendant(const InputSnapshot &snapshot, UiId root, UiId id) -> bool {
    auto cursor = Some(id);
    for (unsigned depth{}; cursor.is_some() && depth < 256; ++depth) {
        if (*cursor == root)
            return snapshot.get(root) != nullptr;
        auto node = snapshot.get(*cursor);
        if (!node)
            return false;
        cursor = node->parent;
    }
    return false;
}
inline auto input_current_mount(UiDocument &document, const InputSnapshot &snapshot, UiId target) -> bool {
    auto node = snapshot.get(target);
    auto mount = document.mount_state(target);
    return node && mount.is_ok() && mount->mounted && mount->generation == node->mount_generation;
}
inline auto input_eligible(const InputSnapshot &snapshot, UiId id, bool pointer,
                           bool require_focus = true) -> bool {
    auto cursor = Some(id);
    for (unsigned depth{}; cursor.is_some() && depth < 256; ++depth) {
        auto node = snapshot.get(*cursor);
        if (!node || !node->enabled || !node->properties.visible ||
            (pointer &&
             (!node->hit_test_visible || node->properties.transform.inverse().is_none())))
            return false;
        if (cursor == snapshot.root())
            return pointer || !require_focus || snapshot.get(id)->focusable;
        cursor = node->parent;
    }
    return false;
}
enum class FocusBoundary { Unrestricted, Contained };
enum class PointerBoundary { Unrestricted, BlockOutside };
enum class ScopeAccess { Scoped, Global };
enum class ScopeStatus { Pending, Active, Closed, Rejected };
enum class FocusRequestStatus { Applied, Rejected, Cancelled, Pending };
struct ScopeRestore;
using ScopeId = Identity<struct ScopeTag>;
using FocusRequestId = Identity<struct FocusRequestTag>;
struct InputScopeOptions {
    FocusBoundary focus{FocusBoundary::Contained};
    PointerBoundary pointer{PointerBoundary::Unrestricted};
    Option<ScopeId> parent;
    auto operator==(const InputScopeOptions &) const -> bool = default;
};
class InputScopes;
class FocusBookmark {
    Option<UiId> target_;
    Serial mount_{};
    friend class InputScopes;
};
struct ScopeDiagnostics {
    Serial omitted_scope_events{}, omitted_focus_events{}, evicted_focus_results{};
};
struct ScopeEvent {
    ScopeId id;
    ScopeStatus status;
    Serial revision{};
    Option<InputError> error;
};
struct FocusRequestEvent {
    FocusRequestId id;
    FocusRequestStatus status;
    Serial revision{};
    Option<InputError> error;
};
class InputScopeRegistration {
    struct State {
        ScopeId id;
        UiId root;
        InputScopeOptions options;
        BindingGuard guard;
        ScopeStatus status{ScopeStatus::Pending};
        Serial mount{};
        bool closing{}, fallback{}, options_dirty{};
        Option<FocusBookmark> restore;
    };
    rstd::rc::Rc<State> state_;
    Binding lifetime_;
    friend class InputScopes;
    InputScopeRegistration(rstd::rc::Rc<State> state, Binding lifetime)
        : state_(rstd::move(state)), lifetime_(rstd::move(lifetime)) {}

  public:
    InputScopeRegistration() = default;
    InputScopeRegistration(const InputScopeRegistration &) = delete;
    InputScopeRegistration(InputScopeRegistration &&) = default;
    auto operator=(InputScopeRegistration &&) -> InputScopeRegistration & = default;
    auto id() const -> ScopeId { return state_ ? state_.get()->id : ScopeId{}; }
    auto status() const -> ScopeStatus {
        return state_ ? state_.get()->status : ScopeStatus::Closed;
    }
    void close(Option<FocusBookmark> restore = {}, bool fallback = false) {
        if (state_) {
            state_.get()->closing = true;
            state_.get()->restore = rstd::move(restore);
            state_.get()->fallback = fallback;
        }
        lifetime_.disconnect();
    }
};
struct ScopeRestore {
    FocusBookmark bookmark;
    bool fallback{};
};
class InputScopes {
    using InputScopeState = InputScopeRegistration::State;
    struct Request {
        FocusRequestId id;
        UiId target;
        Option<ScopeId> scope;
        BindingGuard guard;
        Serial mount{};
    };
    UiDocument &document_;
    Serial owner_{new_session()}, next_{1};
    Vec<rstd::rc::Rc<InputScopeState>> scopes_;
    Vec<Request> requests_;
    Vec<ScopeEvent> scope_events_;
    Vec<FocusRequestEvent> focus_events_;
    Vec<FocusRequestEvent> focus_results_;
    Option<UiId> focus_root_, pointer_root_;
    bool closed_{};
    ScopeDiagnostics diagnostics_;
    auto find(ScopeId id) -> InputScopeState * {
        for (auto &state : scopes_)
            if (state.get()->id == id)
                return state.get();
        return nullptr;
    }
    auto validate(UiId root, InputScopeOptions options, Option<ScopeId> current = {}, bool parent_changed = true) -> Option<InputError> {
        if ((options.focus != FocusBoundary::Unrestricted && options.focus != FocusBoundary::Contained) ||
            (options.pointer != PointerBoundary::Unrestricted && options.pointer != PointerBoundary::BlockOutside))
            return Some(InputError::InvalidEvent);
        if (!parent_changed) return None();
        auto cursor = options.parent;
        if (cursor.is_none()) return None();
        auto direct = find(*cursor);
        if (!direct || !direct->guard.alive() || direct->closing ||
            (direct->status != ScopeStatus::Pending && direct->status != ScopeStatus::Active))
            return Some(InputError::InvalidId);
        auto contained = document_.is_descendant(direct->root, root);
        if (contained.is_err() || !contained.unwrap()) return Some(InputError::InvalidId);
        for (unsigned depth{}; cursor.is_some() && depth < 64; ++depth) {
            if (cursor == current) return Some(InputError::InvalidId);
            auto parent = find(*cursor);
            if (!parent || !parent->guard.alive() || parent->closing ||
                (parent->status != ScopeStatus::Pending && parent->status != ScopeStatus::Active))
                return None();
            cursor = parent->options.parent;
        }
        return cursor.is_some() ? Some(InputError::InvalidId) : None<InputError>();
    }
    template <class T> static void record(Vec<T> &events, T event, Serial &omitted) {
        if (events.len() == usize(256)) {
            events.remove(usize());
            if (omitted != ~Serial{})
                ++omitted;
        }
        events.push(rstd::move(event));
    }
    void roots() {
        focus_root_ = None();
        pointer_root_ = None();
        for (auto &value : scopes_) {
            const auto &state = *value.get();
            if (state.status != ScopeStatus::Active)
                continue;
            if (state.options.focus == FocusBoundary::Contained)
                focus_root_ = Some(UiId(state.root));
            if (state.options.pointer == PointerBoundary::BlockOutside)
                pointer_root_ = Some(UiId(state.root));
        }
    }

  public:
    explicit InputScopes(UiDocument &document) : document_(document) {}
    auto open(UiId root, InputScopeOptions options, BindingGuard receiver)
        -> Result<InputScopeRegistration, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (!document_.valid(root) || !receiver.alive())
            return Err(InputError::InvalidId);
        scopes_.retain([](const auto &state) {
            return state.get()->status == ScopeStatus::Pending ||
                   state.get()->status == ScopeStatus::Active;
        });
        if (scopes_.len() >= usize(64) || next_ == ~Serial{})
            return Err(InputError::Limit);
        if (auto error = validate(root, options); error.is_some()) return Err(*error);
        auto lifetime = document_.bindings().make([receiver] { return receiver.alive(); });
        auto state = rstd::rc::make_rc<InputScopeState>();
        state.get()->id = {owner_, static_cast<Index>(next_), next_++};
        state.get()->root = root;
        state.get()->options = options;
        state.get()->guard = lifetime.guard();
        auto mount = document_.mount_state(root).unwrap();
        state.get()->mount = mount.mounted ? mount.generation : 0;
        scopes_.push(state.clone());
        document_.bindings().wake();
        return Ok(InputScopeRegistration(rstd::move(state), rstd::move(lifetime)));
    }
    auto set_options(const InputScopeRegistration &registration, InputScopeOptions options)
        -> Result<empty, InputError> {
        if (closed_) return Err(InputError::Unavailable);
        auto state = find(registration.id());
        if (!state || state != registration.state_.get() || !state->guard.alive() || state->closing ||
            (state->status != ScopeStatus::Pending && state->status != ScopeStatus::Active))
            return Err(InputError::InvalidId);
        if (state->options == options) return Ok(empty{});
        if (auto error = validate(state->root, options, Some(ScopeId(state->id)), options.parent != state->options.parent); error.is_some()) return Err(*error);
        if (state->options != options) {
            state->options = options;
            state->options_dirty = true;
            document_.bindings().wake();
        }
        return Ok(empty{});
    }
    auto request(UiId target, Option<ScopeId> scope, BindingGuard guard)
        -> Result<FocusRequestId, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (!document_.valid(target) || !guard.alive() || (scope.is_some() && !find(*scope)))
            return Err(InputError::InvalidId);
        if (requests_.len() >= usize(256) || next_ == ~Serial{})
            return Err(InputError::Limit);
        FocusRequestId id{owner_, static_cast<Index>(next_), next_++};
        auto mount = document_.mount_state(target).unwrap();
        requests_.push({id, target, scope, guard, mount.mounted ? mount.generation : 0});
        document_.bindings().wake();
        return Ok(id);
    }
    auto bookmark(const InputSnapshot &snapshot, Option<UiId> target) const -> FocusBookmark {
        FocusBookmark result;
        if (target.is_some())
            if (auto node = snapshot.get(*target)) {
                result.target_ = target;
                result.mount_ = node->mount_generation;
            }
        return result;
    }
    auto pointer_root() const -> Option<UiId> { return pointer_root_; }
    auto focus_root() const -> Option<UiId> { return focus_root_; }
    auto allows(const InputSnapshot &snapshot, UiId target, bool pointer) const -> bool {
        auto root = pointer ? pointer_root_ : focus_root_;
        return root.is_none() || input_descendant(snapshot, *root, target);
    }
    auto pending() const -> bool {
        if (!requests_.is_empty())
            return true;
        for (const auto &value : scopes_)
            if (value.get()->status == ScopeStatus::Pending ||
                (value.get()->status == ScopeStatus::Active &&
                 (!value.get()->guard.alive() || value.get()->options_dirty)))
                return true;
        return false;
    }
    auto synchronize(const InputSnapshot &next) -> Option<ScopeRestore> {
        Vec<ScopeRestore> restores;
        InputScopeState *top{};
        for (auto &value : scopes_)
            if (value.get()->status == ScopeStatus::Active)
                top = value.get();
        for (usize pass{}; pass <= scopes_.len(); ++pass) {
            bool progressed{};
            for (auto &value : scopes_) {
                auto &state = *value.get();
                if (state.status != ScopeStatus::Active && state.status != ScopeStatus::Pending)
                    continue;
                auto node = next.get(state.root);
                auto parent = state.options.parent.is_some()
                                  ? find(*state.options.parent)
                                  : nullptr;
                bool root_valid = node && input_eligible(next, state.root, false, false) &&
                                  (!state.mount || state.mount == node->mount_generation);
                bool parent_pending = state.status == ScopeStatus::Pending && parent && parent->status == ScopeStatus::Pending &&
                                      parent->guard.alive() && !parent->closing;
                if (root_valid && state.guard.alive() && !state.closing && parent_pending) continue;
                // An active range can outlive the registration used to enter it.
                bool valid = root_valid && (state.status == ScopeStatus::Active || state.options.parent.is_none() ||
                    (parent && parent->status == ScopeStatus::Active && parent->guard.alive() && !parent->closing &&
                     input_descendant(next, parent->root, state.root)));
                if (!state.guard.alive() || state.closing || !valid) {
                    if (state.closing && (state.restore.is_some() || state.fallback))
                        restores.push(ScopeRestore{state.restore.is_some() ? *state.restore : FocusBookmark{}, state.fallback});
                    state.status = state.status == ScopeStatus::Pending && !valid && state.guard.alive() && !state.closing
                                       ? ScopeStatus::Rejected
                                       : ScopeStatus::Closed;
                    record(scope_events_,
                           ScopeEvent{state.id, state.status, next.revision(),
                                      !valid && !state.closing ? Some(InputError::InvalidId) : None<InputError>()},
                           diagnostics_.omitted_scope_events);
                    progressed = true;
                    continue;
                }
                if (state.status == ScopeStatus::Pending) {
                    state.mount = node->mount_generation;
                    state.status = ScopeStatus::Active;
                    record(scope_events_, ScopeEvent{state.id, state.status, next.revision(), {}},
                           diagnostics_.omitted_scope_events);
                    progressed = true;
                }
                state.options_dirty = false;
            }
            if (!progressed) break;
        }
        roots();
        if (top && top->status == ScopeStatus::Active) return None();
        for (usize i = restores.len(); i > usize(); --i)
            if (restore_target(next, restores[i - usize(1)]).is_some())
                return Some(ScopeRestore(restores[i - usize(1)]));
        return restores.is_empty() ? None<ScopeRestore>() : Some(ScopeRestore(restores[usize()]));
    }
    auto restore_target(const InputSnapshot &snapshot, const ScopeRestore &restore)
        -> Option<UiId> {
        const auto &bookmark = restore.bookmark;
        if (bookmark.target_.is_some()) {
            auto node = snapshot.get(*bookmark.target_);
            if (node && node->mount_generation == bookmark.mount_ &&
                input_eligible(snapshot, *bookmark.target_, false) &&
                allows(snapshot, *bookmark.target_, false))
                return bookmark.target_;
        }
        if (restore.fallback)
            for (const auto &node : snapshot.visuals())
                if (input_eligible(snapshot, node.id, false) && allows(snapshot, node.id, false))
                    return Some(UiId(node.id));
        return None();
    }
    template <class Apply>
    auto resolve_focus(const InputSnapshot &snapshot, bool available, Apply apply) -> void {
        auto requests = rstd::exchange(requests_, Vec<Request>{});
        for (const auto &request : requests) {
            auto scope = request.scope.is_some() ? find(*request.scope) : nullptr;
            auto node = snapshot.get(request.target);
            bool alive = request.guard.alive();
            bool valid = available && node &&
                         (!request.mount || node->mount_generation == request.mount) &&
                         input_eligible(snapshot, request.target, false) &&
                         allows(snapshot, request.target, false) &&
                         (request.scope.is_none() ||
                          (scope && scope->status == ScopeStatus::Active &&
                           input_descendant(snapshot, scope->root, request.target)));
            auto status = !alive  ? FocusRequestStatus::Cancelled
                          : valid ? FocusRequestStatus::Applied
                                  : FocusRequestStatus::Rejected;
            if (alive && valid)
                apply(Some(UiId(request.target)));
            FocusRequestEvent event{request.id, status, snapshot.revision(),
                                    alive && !valid ? Some(InputError::InvalidId)
                                                    : None<InputError>()};
            record(focus_events_, FocusRequestEvent(event), diagnostics_.omitted_focus_events);
            record(focus_results_, FocusRequestEvent(event), diagnostics_.evicted_focus_results);
        }
    }
    auto diagnostics() const -> ScopeDiagnostics { return diagnostics_; }
    auto focus_result(FocusRequestId id) const -> Option<FocusRequestEvent> {
        for (const auto &request : requests_)
            if (request.id == id)
                return Some(FocusRequestEvent{id, FocusRequestStatus::Pending, 0, {}});
        for (const auto &event : focus_results_)
            if (event.id == id)
                return Some(FocusRequestEvent(event));
        return None();
    }
    auto poll_events() -> Vec<ScopeEvent> {
        return rstd::exchange(scope_events_, Vec<ScopeEvent>{});
    }
    auto poll_focus_events() -> Vec<FocusRequestEvent> {
        return rstd::exchange(focus_events_, Vec<FocusRequestEvent>{});
    }
    void close() {
        closed_ = true;
        for (auto &value : scopes_) {
            value.get()->status = ScopeStatus::Closed;
            value.get()->guard.disconnect();
        }
        for (const auto &request : requests_) {
            FocusRequestEvent event{request.id, FocusRequestStatus::Cancelled, 0, {}};
            record(focus_events_, FocusRequestEvent(event), diagnostics_.omitted_focus_events);
            record(focus_results_, FocusRequestEvent(event), diagnostics_.evicted_focus_results);
        }
        requests_.clear();
        roots();
    }
};
} // namespace vui
