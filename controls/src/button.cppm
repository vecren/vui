export module vui.controls:button;
import vui;
import :types;
import :button_tint;
using namespace rstd::prelude;
export namespace vui {
struct ButtonStyle {
    Color normal{0.06, 0.2, 0.4, 1};
    Color pressed{0.03, 0.1, 0.2, 1};
    Color focused{0.12, 0.35, 0.65, 1};
    Scalar radius{4};
    friend auto operator==(const ButtonStyle &, const ButtonStyle &) -> bool = default;
    auto valid() const -> bool {
        return normal.valid() && pressed.valid() && focused.valid() && finite(radius) &&
               radius >= 0;
    }
};

class ButtonState {
    ButtonStyle style_;
    ButtonOptions options_;
    Color original_background_;
    Scalar original_radius_{};
    Option<rstd::rc::Rc<ButtonTintState>> tint_;
    Signal<void(EventContext &, const ClickEvent &)> clicked_;
    Signal<void(UiDocument &, UiId, const ButtonStatus &)> status_changed_;
    Option<PointerKey> pointer_;
    Option<Key> key_;
    bool inside_{}, focused_{}, hovered_{};

    void paint(EventContext &context, UiId id) {
        if (options_.appearance == ButtonAppearance::Custom)
            return;
        auto color = status().pressed ? style_.pressed : focused_ ? style_.focused : style_.normal;
        context.document().set_background(id, color, style_.radius);
    }
    static void consume(EventContext &context) {
        context.prevent_default();
        context.stop_propagation();
    }

  public:
    auto configured(ButtonStyle style, ButtonOptions options, Color background, Scalar radius) const -> bool {
        return style_ == style && options_ == options && original_background_ == background && original_radius_ == radius;
    }
    void configure_prepared(ButtonStyle style, ButtonOptions options, Color background, Scalar radius) {
        style_ = style; options_ = options; original_background_ = background; original_radius_ = radius;
    }
    auto configured_style(NodeStyle style) const -> NodeStyle {
        if (options_.appearance == ButtonAppearance::Default) {
            auto current = status();
            style.background = current.pressed ? style_.pressed : current.focused ? style_.focused : style_.normal;
            style.radius = style_.radius;
        }
        style.focusable = true; return style;
    }
    explicit ButtonState(ButtonStyle style, ButtonOptions options = {})
        : ButtonState(style, options, Color{}, 0) {}
    ButtonState(ButtonStyle style, ButtonOptions options, Color background, Scalar radius)
        : style_(style), options_(options), original_background_(background), original_radius_(radius) {}
    auto set_tint(UiSession &session, UiId id, UiId target, ButtonTintStyle style)
        -> Result<empty, ControlError> {
        if (tint_.is_some() && tint_->get()->target() == target) {
            auto result = tint_->get()->configure(style);
            return result.is_ok() ? Result<empty, ControlError>(Ok(empty{}))
                                 : Result<empty, ControlError>(Err(ControlError{Some(result.unwrap_err()), {}}));
        }
        auto value = ButtonTintState::make(session, id, target, style, status());
        if (value.is_err()) return Err(value.unwrap_err());
        tint_ = Some(rstd::move(value).unwrap());
        return Ok(empty{});
    }
    auto set_tint_frame_interval(Serial value) -> Result<empty, UiError> {
        return tint_.is_some() ? tint_->get()->set_frame_interval(value)
                              : Result<empty, UiError>(Err(UiError::InvalidValue));
    }
    void clear_tint() { tint_ = None(); }
    auto tint_status() const -> Option<ButtonTintStatus> {
        return tint_.is_some() ? Some(tint_->get()->status()) : None<ButtonTintStatus>();
    }
    auto set_appearance(UiDocument &ui, UiId id, ButtonAppearance appearance) -> Result<empty, UiError> {
        if (appearance != ButtonAppearance::Default && appearance != ButtonAppearance::Custom)
            return Err(UiError::InvalidValue);
        if (appearance == options_.appearance) return Ok(empty{});
        auto color = appearance == ButtonAppearance::Custom ? original_background_
            : status().pressed ? style_.pressed : focused_ ? style_.focused : style_.normal;
        auto result = ui.set_background(id, color, appearance == ButtonAppearance::Custom ? original_radius_ : style_.radius);
        if (result.is_ok()) options_.appearance = appearance;
        return result;
    }
    template <class F>
    auto on_clicked(BindingRegistry &registry, F callback) -> Result<Binding, SignalError> {
        return clicked_.connect(registry, rstd::move(callback));
    }
    template <class T, class F>
    auto bind_clicked(BindingRegistry &registry, rstd::rc::Weak<T> receiver, F callback)
        -> Result<Binding, SignalError> {
        return clicked_.bind(registry, rstd::move(receiver), rstd::move(callback));
    }
    template <class F>
    auto on_status_changed(BindingRegistry &registry, F callback) -> Result<Binding, SignalError> {
        return status_changed_.connect(registry, rstd::move(callback));
    }
    template <class T, class F>
    auto bind_status_changed(BindingRegistry &registry, rstd::rc::Weak<T> receiver, F callback)
        -> Result<Binding, SignalError> {
        return status_changed_.bind(registry, rstd::move(receiver), rstd::move(callback));
    }
    auto status() const -> ButtonStatus {
        return {key_.is_some() || (pointer_.is_some() && inside_), focused_, hovered_,
                key_.is_some() || pointer_.is_some()};
    }
    auto set_style(UiDocument &document, UiId id, ButtonStyle style) -> Result<empty, UiError> {
        if (!style.valid())
            return Err(UiError::InvalidValue);
        if (options_.appearance == ButtonAppearance::Custom) {
            style_ = style;
            return Ok(empty{});
        }
        auto color = status().pressed ? style.pressed : focused_ ? style.focused : style.normal;
        auto result = document.set_background(id, color, style.radius);
        if (result.is_ok())
            style_ = style;
        return result;
    }
    void handle(EventContext &context, const RoutedEvent &event) {
        const auto &input = event.input;
        auto previous = status();
        bool activate{};
        PointerKey pointer{input.device, input.pointer};
        switch (input.type) {
        case EventType::PointerDown:
            if (!options_.pointer_enabled || (input.pointer_kind != PointerKind::Touch &&
                                              input.button != PointerButton::Primary))
                return;
            if (pointer_.is_none() && key_.is_none() &&
                (!options_.capture_on_press || context.capture_pointer().is_ok())) {
                pointer_ = Some(PointerKey(pointer));
                inside_ = context.hit_within();
                if (options_.focus_on_press)
                    context.focus();
            }
            consume(context);
            break;
        case EventType::PointerEnter:
        case EventType::PointerLeave:
            if (input.pointer_kind != PointerKind::Touch)
                hovered_ = input.type == EventType::PointerEnter;
            if (pointer_ == Some(PointerKey(pointer)))
                inside_ = context.hit_within();
            break;
        case EventType::PointerMove:
            if (pointer_ != Some(PointerKey(pointer)))
                return;
            inside_ = context.hit_within();
            break;
        case EventType::PointerUp:
            if ((input.pointer_kind != PointerKind::Touch &&
                 input.button != PointerButton::Primary) ||
                pointer_ != Some(PointerKey(pointer)))
                return;
            pointer_ = None();
            inside_ = false;
            context.release_pointer();
            if (!context.default_prevented() && context.hit_within())
                activate = true;
            consume(context);
            break;
        case EventType::PointerCancel:
        case EventType::CaptureLost:
            if (pointer_ != Some(PointerKey(pointer)))
                return;
            pointer_ = None();
            inside_ = false;
            break;
        case EventType::KeyDown:
            if (event.target != event.current)
                return;
            if (input.key == Key::Escape) {
                if (key_.is_none())
                    return;
                key_ = None();
            } else if (input.key == options_.first_key || input.key == options_.second_key) {
                if (!input.repeat && key_.is_none() && pointer_.is_none())
                    key_ = Some(Key(input.key));
            } else {
                return;
            }
            consume(context);
            break;
        case EventType::KeyUp:
            if (key_ != Some(Key(input.key)))
                return;
            key_ = None();
            if (!context.default_prevented())
                activate = true;
            consume(context);
            break;
        case EventType::FocusGained:
            focused_ = true;
            break;
        case EventType::FocusLost:
            focused_ = false;
            key_ = None();
            break;
        default:
            return;
        }
        paint(context, event.current);
        auto current = status();
        if (previous != current) {
            if (tint_.is_some()) tint_->get()->update(current);
            status_changed_.emit(context.document(), event.current, current).unwrap();
        }
        if (activate)
            clicked_.emit(context, ClickEvent{event.current, input}).unwrap();
    }
};

class Button {
  public:
    template <class F>
    static auto prepare_clicked(UiDocument &document, UiEditBatch &edit, UiNodeRef node, F callback)
        -> Result<Binding, ControlError> {
        auto state = edit.prepared_state<ButtonState>(node);
        if (!state) return Err(ControlError{Some(UiError::InvalidId), {}});
        auto connected = state->on_clicked(document.bindings(), rstd::move(callback));
        if (connected.is_err()) return Err(ControlError{Some(UiError::Limit), {}});
        return Ok(connected.unwrap());
    }
    static auto prepare(UiDocument &document, UiEditBatch &edit, Option<UiId> previous,
                        NodeStyle style, ButtonStyle appearance = {}, ButtonOptions options = {},
                        Option<NodeStyle> before = {})
        -> Result<UiNodeRef, ControlError> {
        if (!appearance.valid() || unsigned(options.appearance) > unsigned(ButtonAppearance::Custom))
            return Err(ControlError{Some(UiError::InvalidValue), {}});
        auto background = style.background; auto radius = style.radius;
        auto state = previous.is_some() ? document.state<ButtonState>(*previous) : nullptr;
        if (previous.is_some() && !state) return Err(ControlError{Some(UiError::InvalidId), {}});
        if (options.appearance == ButtonAppearance::Default) {
            auto status = state ? state->status() : ButtonStatus{};
            style.background = status.pressed ? appearance.pressed : status.focused ? appearance.focused : appearance.normal;
            style.radius = appearance.radius;
        }
        style.focusable = true;
        auto node = previous.is_some() ? edit.existing(*previous) : edit.create(LayoutKind::Stack, style);
        if (state && before.is_some()) edit.style(node, style, state->configured_style(*before));
        else edit.style(node, style);
        if (!state) edit.state(node, ButtonState(appearance, options, background, radius));
        else if (!state->configured(appearance, options, background, radius)) {
            struct Patch final : UiStatePatch {
                ButtonStyle style; ButtonOptions options; Color background; Scalar radius;
                Patch(ButtonStyle s, ButtonOptions o, Color b, Scalar r) : style(s), options(o), background(b), radius(r) {}
                auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<ButtonState>(); }
                void publish(UiNodeState &target) override {
                    static_cast<UiStoredState<ButtonState> &>(target).value.configure_prepared(style, options, background, radius);
                }
            };
            auto patch = Box<Patch>::make(appearance, options, background, radius);
            edit.patch(node, Box<UiStatePatch>::from_raw(rstd::move(patch).into_raw().template cast<UiStatePatch>()));
        }
        return Ok(node);
    }
    static auto create(UiSession &session, NodeStyle style, ButtonStyle appearance = {},
                       ButtonOptions options = {}) -> Result<UiId, ControlError> {
        if (!appearance.valid())
            return Err(ControlError{Some(UiError::InvalidValue), {}});
        auto original_background = style.background;
        auto original_radius = style.radius;
        if (options.appearance == ButtonAppearance::Default) {
            style.background = appearance.normal;
            style.radius = appearance.radius;
        }
        style.focusable = true;
        auto &document = session.document();
        auto created = document.create<Stack>(style);
        if (created.is_err())
            return Err(ControlError{Some(created.unwrap_err()), {}});
        auto id = created.unwrap();
        auto state = document.attach_state(id, ButtonState(appearance, options, original_background, original_radius));
        if (state.is_err()) {
            document.destroy(id);
            return Err(ControlError{Some(state.unwrap_err()), {}});
        }
        auto behavior = session.input().attach_behavior<ButtonState>(id);
        if (behavior.is_err()) {
            document.destroy(id);
            return Err(ControlError{{}, Some(behavior.unwrap_err())});
        }
        return Ok(id);
    }
    template <class F>
    static auto on_clicked(UiDocument &document, UiId id, F callback)
        -> Result<Binding, ControlError> {
        auto state = document.state<ButtonState>(id);
        if (!state)
            return Err(ControlError{Some(UiError::InvalidId), {}});
        auto result = state->on_clicked(document.bindings(), rstd::move(callback));
        if (result.is_err())
            return Err(ControlError{Some(UiError::Limit), {}});
        return Ok(rstd::move(result).unwrap());
    }
    template <class T, class F>
    static auto bind_clicked(UiDocument &document, UiId id, rstd::rc::Weak<T> receiver, F callback)
        -> Result<Binding, ControlError> {
        auto state = document.state<ButtonState>(id);
        if (!state)
            return Err(ControlError{Some(UiError::InvalidId), {}});
        auto result =
            state->bind_clicked(document.bindings(), rstd::move(receiver), rstd::move(callback));
        if (result.is_err())
            return Err(ControlError{Some(UiError::Limit), {}});
        return Ok(rstd::move(result).unwrap());
    }
    template <class F>
    static auto on_status_changed(UiDocument &document, UiId id, F callback)
        -> Result<Binding, ControlError> {
        auto state = document.state<ButtonState>(id);
        if (!state)
            return Err(ControlError{Some(UiError::InvalidId), {}});
        auto function = rstd::rc::make_rc<F>(rstd::move(callback));
        auto connected = state->on_status_changed(
            document.bindings(), [function](UiDocument &ui, UiId id, const ButtonStatus &status) {
                (*function.get())(ui, id, status);
            });
        if (connected.is_err())
            return Err(ControlError{Some(UiError::Limit), {}});
        auto binding = rstd::move(connected).unwrap();
        auto guard = binding.guard();
        auto initial =
            document.defer([id, guard, function](UiDocument &ui) -> Result<empty, UiError> {
                if (auto state = ui.state<ButtonState>(id); state && guard.alive()) {
                    auto status = state->status();
                    (*function.get())(ui, id, status);
                }
                return Ok(empty{});
            });
        if (initial.is_err())
            return Err(ControlError{Some(initial.unwrap_err()), {}});
        return Ok(rstd::move(binding));
    }
    template <class T, class F>
    static auto bind_status_changed(UiDocument &document, UiId id, rstd::rc::Weak<T> receiver,
                                    F callback) -> Result<Binding, ControlError> {
        auto state = document.state<ButtonState>(id);
        if (!state)
            return Err(ControlError{Some(UiError::InvalidId), {}});
        auto function = rstd::rc::make_rc<F>(rstd::move(callback));
        auto connected = state->bind_status_changed(
            document.bindings(), receiver,
            [function](T &receiver, UiDocument &ui, UiId id, const ButtonStatus &status) {
                (*function.get())(receiver, ui, id, status);
            });
        if (connected.is_err())
            return Err(ControlError{Some(UiError::Limit), {}});
        auto binding = rstd::move(connected).unwrap();
        auto guard = binding.guard();
        auto initial = document.defer([id, guard, receiver,
                                       function](UiDocument &ui) -> Result<empty, UiError> {
            auto value = receiver.upgrade();
            if (auto state = ui.state<ButtonState>(id); state && value.is_some() && guard.alive()) {
                auto status = state->status();
                (*function.get())(*value->get(), ui, id, status);
            }
            return Ok(empty{});
        });
        if (initial.is_err())
            return Err(ControlError{Some(initial.unwrap_err()), {}});
        return Ok(rstd::move(binding));
    }
    static auto status(UiDocument &document, UiId id) -> Option<ButtonStatus> {
        if (auto state = document.state<ButtonState>(id))
            return Some(state->status());
        return None();
    }
    static auto set_tint(UiSession &session, UiId id, UiId target, ButtonTintStyle style = {})
        -> Result<empty, ControlError> {
        auto state = session.document().state<ButtonState>(id);
        return state ? state->set_tint(session, id, target, style)
                     : Result<empty, ControlError>(Err(ControlError{Some(UiError::InvalidId), {}}));
    }
    static auto set_tint_frame_interval(UiDocument &ui, UiId id, Serial value)
        -> Result<empty, UiError> {
        auto state = ui.state<ButtonState>(id);
        return state ? state->set_tint_frame_interval(value)
                     : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    static auto clear_tint(UiDocument &ui, UiId id) -> Result<empty, UiError> {
        auto state = ui.state<ButtonState>(id);
        if (!state) return Err(UiError::InvalidId);
        state->clear_tint(); return Ok(empty{});
    }
    static auto tint_status(UiDocument &ui, UiId id) -> Option<ButtonTintStatus> {
        auto state = ui.state<ButtonState>(id);
        return state ? state->tint_status() : None<ButtonTintStatus>();
    }
    static auto set_appearance(UiDocument &ui, UiId id, ButtonAppearance appearance)
        -> Result<empty, UiError> {
        auto state = ui.state<ButtonState>(id);
        return state ? state->set_appearance(ui, id, appearance)
                     : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    static auto set_style(UiDocument &document, UiId id, ButtonStyle style)
        -> Result<empty, UiError> {
        if (auto state = document.state<ButtonState>(id))
            return state->set_style(document, id, style);
        return Err(UiError::InvalidId);
    }
};
} // namespace vui
