module vui.controls;
import :popup;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
namespace {
auto valid_popup_options(const PopupOptions &options) -> bool {
    return options.backdrop.valid() &&
        (options.restore_focus == RestoreFocus::None || options.restore_focus == RestoreFocus::Previous) &&
        (options.escape == EscapePolicy::Ignore || options.escape == EscapePolicy::RequestClose) &&
        (options.outside_press == OutsidePressPolicy::Ignore || options.outside_press == OutsidePressPolicy::RequestClose);
}
auto popup_scope_options(const PopupOptions &options) -> InputScopeOptions {
    return {options.modal ? FocusBoundary::Contained : FocusBoundary::Unrestricted,
            options.modal ? PointerBoundary::BlockOutside : PointerBoundary::Unrestricted, options.parent_scope};
}
auto popup_style(UiDocument &ui, UiId root, const PopupOptions &options) -> Result<empty, UiError> {
    NodeStyle style;
    style.background = options.backdrop; style.hit_self = options.modal;
    return ui.patch_style(root, NodeStylePatch::select(style, {NodeStyleField::Background, NodeStyleField::HitSelf}));
}
auto popup_focus(UiSession &session, UiId root, Option<UiId> target,
                 const InputScopeRegistration &scope, BindingGuard owner, Binding &lifetime)
    -> Result<empty, ControlError> {
    if (target.is_none()) return Ok(empty{});
    auto inside = session.document().is_descendant(root, *target);
    if (inside.is_err() || !inside.unwrap()) return Err(ControlError{Some(UiError::InvalidId), {}});
    lifetime = session.document().bindings().make([owner] { return owner.alive(); });
    auto requested = session.input().focus_when_ready(*target, scope.id(), lifetime.guard());
    if (requested.is_err()) return Err(ControlError{{}, Some(requested.unwrap_err())});
    return Ok(empty{});
}
}

auto Popup::change(bool open) -> Result<empty, ControlError> {
    if (!state_ || !state_.get()->lifetime.alive())
        return Err(ControlError{{}, Some(InputError::Unavailable)});
    auto &ui = state_.get()->session.document();
    if (ui.can_route()) return open ? state_.get()->open() : state_.get()->close();
    auto result = ui.defer(state_.get()->root, state_.get()->lifetime.guard(),
        [weak = state_.downgrade(), open](UiDocument &) -> Result<empty, UiError> {
            auto state = weak.upgrade();
            if (state.is_none()) return Ok(empty{});
            auto changed = open ? state->get()->open() : state->get()->close();
            return changed.is_ok() ? Result<empty, UiError>(Ok(empty{}))
                                   : Result<empty, UiError>(Err(UiError::InvalidValue));
        });
    return result.is_ok() ? Result<empty, ControlError>(Ok(empty{}))
                          : Result<empty, ControlError>(Err(ControlError{Some(result.unwrap_err()), {}}));
}

auto Popup::set_options(PopupOptions options) -> Result<empty, ControlError> {
    if (!state_ || !state_.get()->lifetime.alive())
        return Err(ControlError{{}, Some(InputError::Unavailable)});
    auto &ui = state_.get()->session.document();
    if (ui.can_route()) return state_.get()->configure(options);
    auto result = ui.defer(state_.get()->root, state_.get()->lifetime.guard(),
        [weak = state_.downgrade(), options](UiDocument &) -> Result<empty, UiError> {
            auto state = weak.upgrade();
            if (state.is_none()) return Ok(empty{});
            auto changed = state->get()->configure(options);
            return changed.is_ok() ? Result<empty, UiError>(Ok(empty{}))
                                   : Result<empty, UiError>(Err(UiError::InvalidValue));
        });
    return result.is_ok() ? Result<empty, ControlError>(Ok(empty{}))
                          : Result<empty, ControlError>(Err(ControlError{Some(result.unwrap_err()), {}}));
}

auto Popup::set_content(UiId content) -> Result<empty, ControlError> {
    if (!state_ || !state_.get()->lifetime.alive())
        return Err(ControlError{{}, Some(InputError::Unavailable)});
    auto &ui = state_.get()->session.document();
    if (ui.can_route()) return state_.get()->replace_content(content);
    auto result = ui.defer(state_.get()->root, state_.get()->lifetime.guard(),
        [weak = state_.downgrade(), content](UiDocument &) -> Result<empty, UiError> {
            auto state = weak.upgrade();
            if (state.is_none()) return Ok(empty{});
            auto changed = state->get()->replace_content(content);
            return changed.is_ok() ? Result<empty, UiError>(Ok(empty{}))
                                   : Result<empty, UiError>(Err(UiError::InvalidValue));
        });
    return result.is_ok() ? Result<empty, ControlError>(Ok(empty{}))
                          : Result<empty, ControlError>(Err(ControlError{Some(result.unwrap_err()), {}}));
}

void Popup::dispose() {
    if (!state_ || !state_.get()->lifetime.alive()) return;
    auto &state = *state_.get();
    state.focus_lifetime.disconnect();
    if (state.opened) state.scope.close(state.restore());
    auto root = state.root, content = state.content;
    auto identity = state.identity;
    auto owns_root = state.owns_root, owns_content = state.options.owns_content;
    auto cleanup = [root, content, identity, owns_root, owns_content](UiDocument &ui) -> Result<empty, UiError> {
        if (!owns_root) {
            auto marker = ui.state<RootBinding>(root, NodeStateKey{rstd::any::TypeId::of<RootBinding>(), 0});
            return marker && marker->identity == identity ? ui.set_visible(root, false) : Ok(empty{});
        }
        auto descendant = ui.is_descendant(root, content);
        if (!owns_content && descendant.is_ok() && descendant.unwrap()) {
            auto detached = ui.detach(content);
            if (detached.is_err()) return detached;
        }
        if (owns_content && ui.valid(content)) {
            auto removed = ui.destroy(content);
            if (removed.is_err()) return removed;
        }
        return ui.valid(root) ? ui.destroy(root) : Ok(empty{});
    };
    auto &ui = state.session.document();
    if (ui.can_route()) (void)cleanup(ui);
    else (void)ui.defer_cleanup(rstd::move(cleanup));
    state.lifetime.disconnect();
}

auto Popup::operator=(Popup &&other) -> Popup & {
    if (this != &other) { dispose(); state_ = rstd::move(other.state_); }
    return *this;
}

auto Popup::State::connect_outside() -> Result<empty, ControlError> {
    auto &ui = session.document();
    auto parent = ui.parent_relation(root);
    if (parent.is_err()) return Err(ControlError{Some(parent.unwrap_err()), {}});
    auto relation = parent.unwrap();
    auto target = ui.root().unwrap_or(relation.is_some() ? UiId(relation->parent) : root);
    if (host == target && global_outside_capture.alive() && global_outside_target.alive()) return Ok(empty{});
    auto capture = session.input().bind_on(target, EventType::PointerDown, self,
        [](State &state, auto &context, const auto &event) { state.outside_press(context, event); },
        RoutePhase::Capture, ScopeAccess::Global);
    if (capture.is_err()) return Err(ControlError{{}, Some(capture.unwrap_err())});
    auto direct = session.input().bind_on(target, EventType::PointerDown, self,
        [](State &state, auto &context, const auto &event) { state.outside_press(context, event); },
        RoutePhase::Target, ScopeAccess::Global);
    if (direct.is_err()) return Err(ControlError{{}, Some(direct.unwrap_err())});
    global_outside_capture = capture.unwrap();
    global_outside_target = direct.unwrap();
    host = target;
    return Ok(empty{});
}

auto Popup::State::connect() -> Result<empty, ControlError> {
    auto bubble = session.input().bind_on(root, EventType::KeyDown, self,
        [](State &state, auto &context, const auto &event) { state.key(context, event); }, RoutePhase::Bubble);
    if (bubble.is_err()) return Err(ControlError{{}, Some(bubble.unwrap_err())});
    auto direct = session.input().bind_on(root, EventType::KeyDown, self,
        [](State &state, auto &context, const auto &event) { state.key(context, event); });
    if (direct.is_err()) return Err(ControlError{{}, Some(direct.unwrap_err())});
    auto outside = session.input().bind_on(root, EventType::PointerDown, self,
        [](State &state, auto &context, const auto &event) { state.press(context, event); });
    if (outside.is_err()) return Err(ControlError{{}, Some(outside.unwrap_err())});
    auto global = connect_outside();
    if (global.is_err()) return global;
    key_bubble = bubble.unwrap(); key_target = direct.unwrap(); this->outside = outside.unwrap();
    return Ok(empty{});
}

auto Popup::initialize(UiSession &session, UiId root, UiId content, PopupOptions options, bool owned)
    -> Result<Popup, ControlError> {
    auto &ui = session.document();
    if (!ui.can_route()) return Err(ControlError{Some(UiError::Routing), {}});
    if (!valid_popup_options(options) || !ui.valid(root) || !ui.valid(content) || root == content || (!owned && options.owns_content))
        return Err(ControlError{Some(UiError::InvalidValue), {}});
    auto descendant = ui.is_descendant(root, content);
    if (descendant.is_err() || !descendant.unwrap()) return Err(ControlError{Some(UiError::InvalidId), {}});
    if (options.initial_focus.is_some()) {
        auto inside = ui.is_descendant(root, *options.initial_focus);
        if (inside.is_err() || !inside.unwrap()) return Err(ControlError{Some(UiError::InvalidId), {}});
    }
    NodeStateKey key{rstd::any::TypeId::of<RootBinding>(), 0};
    auto marker = ui.state<RootBinding>(root, key);
    if (marker) {
        auto previous = marker->receiver.upgrade();
        if (previous.is_some() && previous->get()->lifetime.alive()) return Err(ControlError{Some(UiError::PropertyOwned), {}});
    }
    auto state = rstd::rc::make_rc<State>(session, root, content, options, owned);
    state.get()->self = state.downgrade();
    auto connected = state.get()->connect();
    if (connected.is_err()) return Err(connected.unwrap_err());
    auto background = popup_style(ui, root, options);
    if (background.is_err()) return Err(ControlError{Some(background.unwrap_err()), {}});
    auto hidden = ui.set_visible(root, false);
    if (hidden.is_err()) return Err(ControlError{Some(hidden.unwrap_err()), {}});
    if (marker) { marker->receiver = state.downgrade(); marker->identity = state->identity; }
    else {
        auto attached = ui.attach_state(root, RootBinding{state.downgrade(), state->identity}, key);
        if (attached.is_err()) return Err(ControlError{Some(attached.unwrap_err()), {}});
    }
    return Ok(Popup(rstd::move(state)));
}

auto Popup::bind(UiSession &session, UiId root, UiId content, PopupOptions options)
    -> Result<Popup, ControlError> { return initialize(session, root, content, options, false); }

auto Popup::create(UiSession &session, UiId parent, UiId content, PopupOptions options)
    -> Result<Popup, ControlError> {
    auto &ui = session.document();
    if (!ui.can_route()) return Err(ControlError{Some(UiError::Routing), {}});
    if (!valid_popup_options(options) || !ui.valid(parent) || !ui.valid(content))
        return Err(ControlError{Some(UiError::InvalidValue), {}});
    auto previous = ui.parent_relation(content).unwrap();
    NodeStyle style;
    style.layout.horizontal = Alignment::Stretch; style.layout.vertical = Alignment::Stretch;
    style.background = options.backdrop; style.hit_self = options.modal; style.visible = false;
    auto created = ui.append<Stack>(parent, style);
    if (created.is_err()) return Err(ControlError{Some(created.unwrap_err()), {}});
    auto root = created.unwrap();
    auto attached = ui.attach(root, content);
    if (attached.is_err()) { ui.destroy(root); return Err(ControlError{Some(attached.unwrap_err()), {}}); }
    auto result = initialize(session, root, content, options, true);
    if (result.is_err()) {
        ui.detach(content);
        if (previous.is_some()) ui.attach(previous->parent, content);
        ui.destroy(root);
    }
    return result;
}

auto Popup::is_open() const -> bool {
    return state_ && state_.get()->opened &&
        (state_.get()->scope.status() == ScopeStatus::Active || state_.get()->scope.status() == ScopeStatus::Pending);
}

auto Popup::State::open() -> Result<empty, ControlError> {
    if (!lifetime.alive()) return Err(ControlError{{}, Some(InputError::Unavailable)});
    if (opened && (scope.status() == ScopeStatus::Pending || scope.status() == ScopeStatus::Active)) return Ok(empty{});
    auto global = connect_outside();
    if (global.is_err()) return global;
    if (scope.status() != ScopeStatus::Pending && scope.status() != ScopeStatus::Active)
        bookmark = session.input().bookmark_focus();
    opened = false;
    auto visible = session.document().set_visible(root, true);
    if (visible.is_err()) return Err(ControlError{Some(visible.unwrap_err()), {}});
    auto registered = session.input().open_scope(root, popup_scope_options(options), lifetime.guard());
    if (registered.is_err()) { session.document().set_visible(root, false); return Err(ControlError{{}, Some(registered.unwrap_err())}); }
    auto candidate = registered.unwrap();
    Binding focus;
    auto focused = popup_focus(session, root, options.initial_focus, candidate, lifetime.guard(), focus);
    if (focused.is_err()) { candidate.close(); session.document().set_visible(root, false); return focused; }
    focus_lifetime = rstd::move(focus);
    scope = rstd::move(candidate);
    opened = true;
    return Ok(empty{});
}

auto Popup::State::configure(PopupOptions value) -> Result<empty, ControlError> {
    if (!valid_popup_options(value) || value.owns_content != options.owns_content)
        return Err(ControlError{Some(UiError::InvalidValue), {}});
    if (value.initial_focus.is_some()) {
        auto inside = session.document().is_descendant(root, *value.initial_focus);
        if (inside.is_err() || !inside.unwrap()) return Err(ControlError{Some(UiError::InvalidId), {}});
    }
    auto global = connect_outside();
    if (global.is_err()) return global;
    bool active = opened && (scope.status() == ScopeStatus::Pending || scope.status() == ScopeStatus::Active);
    bool refocus = active && value.initial_focus != options.initial_focus;
    Binding focus;
    if (refocus) {
        auto requested = popup_focus(session, root, value.initial_focus, scope, lifetime.guard(), focus);
        if (requested.is_err()) return requested;
    }
    if (active) {
        auto changed = session.input().set_scope_options(scope, popup_scope_options(value));
        if (changed.is_err()) return Err(ControlError{{}, Some(changed.unwrap_err())});
    }
    auto background = popup_style(session.document(), root, value);
    if (background.is_err()) {
        if (active) session.input().set_scope_options(scope, popup_scope_options(options));
        return Err(ControlError{Some(background.unwrap_err()), {}});
    }
    options = value;
    if (!active && opened) { opened = false; focus_lifetime.disconnect(); }
    if (refocus) focus_lifetime = rstd::move(focus);
    return Ok(empty{});
}

auto Popup::State::replace_content(UiId value) -> Result<empty, ControlError> {
    if (owns_root) return Err(ControlError{Some(UiError::InvalidValue), {}});
    auto descendant = session.document().is_descendant(root, value);
    if (root == value || descendant.is_err() || !descendant.unwrap())
        return Err(ControlError{Some(UiError::InvalidId), {}});
    content = value;
    return Ok(empty{});
}

auto Popup::State::restore() -> Option<FocusBookmark> {
    if (options.restore_focus != RestoreFocus::Previous) return None();
    auto focused = session.input().focused();
    if (!options.modal && focused.is_some()) {
        auto inside = session.document().is_descendant(root, *focused);
        if (inside.is_err() || !inside.unwrap()) return None();
    }
    return Some(bookmark);
}

auto Popup::State::close() -> Result<empty, ControlError> {
    if (!lifetime.alive()) return Err(ControlError{{}, Some(InputError::Unavailable)});
    if (!opened) return Ok(empty{});
    auto &ui = session.document();
    if (ui.valid(root)) {
        auto hidden = ui.set_visible(root, false);
        if (hidden.is_err()) return Err(ControlError{Some(hidden.unwrap_err()), {}});
    }
    focus_lifetime.disconnect();
    scope.close(restore());
    opened = false;
    return Ok(empty{});
}

void Popup::State::outside_press(EventContext &, const RoutedEvent &event) {
    if (!opened || options.modal || options.outside_press != OutsidePressPolicy::RequestClose) return;
    auto inside = session.document().is_descendant(root, event.target);
    if (inside.is_ok() && !inside.unwrap()) requested.emit(PopupCloseReason::OutsidePress).unwrap();
}
void Popup::State::key(EventContext &context, const RoutedEvent &event) {
    if (!opened || context.default_prevented() || options.escape != EscapePolicy::RequestClose ||
        event.input.key != Key::Escape || event.input.repeat) return;
    context.prevent_default(); context.stop_propagation();
    requested.emit(PopupCloseReason::Escape).unwrap();
}
void Popup::State::press(EventContext &context, const RoutedEvent &event) {
    if (!opened || event.target != root) return;
    context.prevent_default(); context.stop_propagation();
    if (options.outside_press == OutsidePressPolicy::RequestClose) requested.emit(PopupCloseReason::OutsidePress).unwrap();
}
}
