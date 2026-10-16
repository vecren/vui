export module vui.controls:popup;
import vui;
import :types;
using namespace rstd::prelude;

export namespace vui {
enum class PopupCloseReason { Escape, OutsidePress };
enum class EscapePolicy { Ignore, RequestClose };
enum class OutsidePressPolicy { Ignore, RequestClose };
enum class RestoreFocus { None, Previous };
struct PopupOptions {
    bool modal{true}, owns_content{};
    Option<UiId> initial_focus;
    Option<ScopeId> parent_scope;
    RestoreFocus restore_focus{RestoreFocus::Previous};
    EscapePolicy escape{EscapePolicy::Ignore};
    OutsidePressPolicy outside_press{OutsidePressPolicy::Ignore};
    Color backdrop{0, 0, 0, 0};
};
class Popup {
    struct State {
        UiSession &session;
        UiId root, content;
        PopupOptions options;
        Binding lifetime;
        Binding key_bubble, key_target, outside, global_outside_capture, global_outside_target;
        InputScopeRegistration scope;
        FocusBookmark bookmark;
        Signal<void(PopupCloseReason)> requested;
        bool opened{};
        State(UiSession &s, UiId r, UiId c, PopupOptions o)
            : session(s), root(r), content(c), options(o),
              lifetime(s.document().bindings().make()) {}
        auto open() -> Result<empty, ControlError> {
            if (!lifetime.alive())
                return Err(ControlError{{}, Some(InputError::Unavailable)});
            if (opened &&
                (scope.status() == ScopeStatus::Pending || scope.status() == ScopeStatus::Active))
                return Ok(empty{});
            opened = false;
            bookmark = session.input().bookmark_focus();
            auto visible = session.document().set_visible(root, true);
            if (visible.is_err())
                return Err(ControlError{Some(visible.unwrap_err()), {}});
            auto registered = session.input().open_scope(
                root,
                {options.modal ? FocusBoundary::Contained : FocusBoundary::Unrestricted,
                 options.modal ? PointerBoundary::BlockOutside : PointerBoundary::Unrestricted,
                 options.parent_scope},
                lifetime.guard());
            if (registered.is_err()) {
                session.document().set_visible(root, false);
                return Err(ControlError{{}, Some(registered.unwrap_err())});
            }
            scope = rstd::move(registered).unwrap();
            if (options.initial_focus.is_some()) {
                auto focus = session.input().focus_when_ready(*options.initial_focus, scope.id(),
                                                              lifetime.guard());
                if (focus.is_err()) {
                    scope.close();
                    session.document().set_visible(root, false);
                    return Err(ControlError{{}, Some(focus.unwrap_err())});
                }
            }
            opened = true;
            return Ok(empty{});
        }
        auto restore() -> Option<FocusBookmark> {
            if (options.restore_focus != RestoreFocus::Previous)
                return None();
            auto focused = session.input().focused();
            if (!options.modal && focused.is_some()) {
                auto inside = session.document().is_descendant(root, *focused);
                if (inside.is_err() || !inside.unwrap())
                    return None();
            }
            return Some(bookmark);
        }
        void outside_press(EventContext &, const RoutedEvent &event) {
            if (!opened || options.outside_press != OutsidePressPolicy::RequestClose)
                return;
            auto inside = session.document().is_descendant(root, event.target);
            if (inside.is_ok() && !inside.unwrap())
                requested.emit(PopupCloseReason::OutsidePress).unwrap();
        }
        auto close() -> Result<empty, ControlError> {
            if (!lifetime.alive())
                return Err(ControlError{{}, Some(InputError::Unavailable)});
            if (!opened)
                return Ok(empty{});
            auto visible = session.document().set_visible(root, false);
            if (visible.is_err())
                return Err(ControlError{Some(visible.unwrap_err()), {}});
            scope.close(restore());
            opened = false;
            return Ok(empty{});
        }
        void key(EventContext &context, const RoutedEvent &event) {
            if (!opened || context.default_prevented() || options.escape != EscapePolicy::RequestClose ||
                event.input.key != Key::Escape || event.input.repeat)
                return;
            context.prevent_default();
            context.stop_propagation();
            requested.emit(PopupCloseReason::Escape).unwrap();
        }
        void press(EventContext &context, const RoutedEvent &event) {
            if (!opened || event.target != root)
                return;
            context.prevent_default();
            context.stop_propagation();
            if (options.outside_press == OutsidePressPolicy::RequestClose)
                requested.emit(PopupCloseReason::OutsidePress).unwrap();
        }
    };
    rstd::rc::Rc<State> state_;
    explicit Popup(rstd::rc::Rc<State> state) : state_(rstd::move(state)) {}
    auto change(bool open) -> Result<empty, ControlError> {
        if (!state_ || !state_.get()->lifetime.alive())
            return Err(ControlError{{}, Some(InputError::Unavailable)});
        auto &ui = state_.get()->session.document();
        if (ui.can_route())
            return open ? state_.get()->open() : state_.get()->close();
        auto weak = state_.downgrade();
        auto result = ui.defer(state_.get()->root, state_.get()->lifetime.guard(),
                               [weak, open](UiDocument &) -> Result<empty, UiError> {
                                   auto state = weak.upgrade();
                                   if (state.is_none())
                                       return Ok(empty{});
                                   auto changed =
                                       open ? state->get()->open() : state->get()->close();
                                   return changed.is_ok()
                                              ? Result<empty, UiError>(Ok(empty{}))
                                              : Result<empty, UiError>(Err(UiError::InvalidValue));
                               });
        return result.is_ok()
                   ? Result<empty, ControlError>(Ok(empty{}))
                   : Result<empty, ControlError>(Err(ControlError{Some(result.unwrap_err()), {}}));
    }
    void dispose() {
        if (!state_ || !state_.get()->lifetime.alive())
            return;
        auto &state = *state_.get();
        state.scope.close(state.opened ? state.restore() : None<FocusBookmark>());
        auto root = state.root, content = state.content;
        bool owns = state.options.owns_content;
        auto cleanup = [root, content, owns](UiDocument &ui) -> Result<empty, UiError> {
            auto descendant = ui.is_descendant(root, content);
            if (!owns && descendant.is_ok() && descendant.unwrap()) {
                auto detached = ui.detach(content);
                if (detached.is_err())
                    return detached;
            }
            if (owns && ui.valid(content)) {
                auto removed = ui.destroy(content);
                if (removed.is_err())
                    return removed;
            }
            return ui.valid(root) ? ui.destroy(root) : Ok(empty{});
        };
        auto &ui = state.session.document();
        if (ui.can_route())
            (void)cleanup(ui);
        else
            (void)ui.defer_cleanup(rstd::move(cleanup));
        state.lifetime.disconnect();
    }

  public:
    Popup() = default;
    Popup(const Popup &) = delete;
    Popup(Popup &&) = default;
    auto operator=(Popup &&other) -> Popup & {
        if (this != &other) {
            dispose();
            state_ = rstd::move(other.state_);
        }
        return *this;
    }
    ~Popup() { dispose(); }
    static auto create(UiSession &session, UiId parent, UiId content, PopupOptions options = {})
        -> Result<Popup, ControlError> {
        auto &ui = session.document();
        if (!options.backdrop.valid() || !ui.valid(parent) || !ui.valid(content))
            return Err(ControlError{Some(UiError::InvalidValue), {}});
        NodeStyle style;
        style.layout.horizontal = Alignment::Stretch;
        style.layout.vertical = Alignment::Stretch;
        style.background = options.backdrop;
        style.hit_self = options.modal;
        auto previous = ui.parent_relation(content).unwrap();
        auto created = ui.append<Stack>(parent, style);
        if (created.is_err())
            return Err(ControlError{Some(created.unwrap_err()), {}});
        auto root = created.unwrap();
        auto attached = ui.attach(root, content);
        if (attached.is_err()) {
            ui.destroy(root);
            return Err(ControlError{Some(attached.unwrap_err()), {}});
        }
        ui.set_visible(root, false).unwrap();
        auto state = rstd::rc::make_rc<State>(session, root, content, options);
        auto weak = state.downgrade();
        auto bubble = session.input().bind_on(
            root, EventType::KeyDown, weak,
            [](State &s, auto &context, const auto &event) { s.key(context, event); },
            RoutePhase::Bubble);
        auto target = session.input().bind_on(
            root, EventType::KeyDown, weak,
            [](State &s, auto &context, const auto &event) { s.key(context, event); });
        auto outside = session.input().bind_on(
            root, EventType::PointerDown, weak,
            [](State &s, auto &context, const auto &event) { s.press(context, event); });
        if (bubble.is_err() || target.is_err() || outside.is_err()) {
            ui.detach(content);
            if (previous.is_some())
                ui.attach(previous->parent, content);
            ui.destroy(root);
            return Err(ControlError{{}, Some(InputError::Unavailable)});
        }
        if (!options.modal && options.outside_press == OutsidePressPolicy::RequestClose) {
            auto capture = session.input().bind_on(
                parent, EventType::PointerDown, weak,
                [](State &s, auto &context, const auto &event) { s.outside_press(context, event); },
                RoutePhase::Capture, ScopeAccess::Global);
            auto target = session.input().bind_on(
                parent, EventType::PointerDown, weak,
                [](State &s, auto &context, const auto &event) { s.outside_press(context, event); },
                RoutePhase::Target, ScopeAccess::Global);
            if (capture.is_err() || target.is_err()) {
                ui.detach(content);
                if (previous.is_some())
                    ui.attach(previous->parent, content);
                ui.destroy(root);
                return Err(ControlError{{}, Some(InputError::Limit)});
            }
            state.get()->global_outside_capture = rstd::move(capture).unwrap();
            state.get()->global_outside_target = rstd::move(target).unwrap();
        }
        state.get()->key_bubble = rstd::move(bubble).unwrap();
        state.get()->key_target = rstd::move(target).unwrap();
        state.get()->outside = rstd::move(outside).unwrap();
        return Ok(Popup(rstd::move(state)));
    }
    auto root() const -> UiId { return state_ ? state_.get()->root : UiId{}; }
    auto scope_id() const -> ScopeId { return state_ ? state_.get()->scope.id() : ScopeId{}; }
    auto is_open() const -> bool {
        return state_ && state_.get()->opened &&
               (state_.get()->scope.status() == ScopeStatus::Active ||
                state_.get()->scope.status() == ScopeStatus::Pending);
    }
    auto open() -> Result<empty, ControlError> { return change(true); }
    auto close() -> Result<empty, ControlError> { return change(false); }
    template <class F> auto on_close_requested(F callback) -> Result<Binding, SignalError> {
        if (!state_ || !state_.get()->lifetime.alive())
            return Err(SignalError::Closed);
        return state_.get()->requested.connect(state_.get()->session.document().bindings(),
                                               rstd::move(callback));
    }
    template <class T, class F>
    auto bind_close_requested(rstd::rc::Weak<T> receiver, F callback)
        -> Result<Binding, SignalError> {
        if (!state_ || !state_.get()->lifetime.alive())
            return Err(SignalError::Closed);
        return state_.get()->requested.bind(state_.get()->session.document().bindings(),
                                            rstd::move(receiver), rstd::move(callback));
    }
};
} // namespace vui
