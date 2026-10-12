export module vui.controls:button;
import vui;
import :types;
using namespace rstd::prelude;
export namespace vui {
struct ButtonStyle {
    Color normal{0.06, 0.2, 0.4, 1};
    Color pressed{0.03, 0.1, 0.2, 1};
    Color focused{0.12, 0.35, 0.65, 1};
    Scalar radius{4};
    auto valid() const -> bool {
        return normal.valid() && pressed.valid() && focused.valid() && finite(radius) &&
               radius >= 0;
    }
};
struct ButtonStatus {
    bool pressed{}, focused{}, hovered{};
    friend auto operator==(const ButtonStatus &, const ButtonStatus &) -> bool = default;
};

class ButtonState {
    ButtonStyle style_;
    ButtonOptions options_;
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
    explicit ButtonState(ButtonStyle style, ButtonOptions options = {})
        : style_(style), options_(options) {}
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
        return {key_.is_some() || (pointer_.is_some() && inside_), focused_, hovered_};
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
        if (previous != current)
            status_changed_.emit(context.document(), event.current, current).unwrap();
        if (activate)
            clicked_.emit(context, ClickEvent{event.current, input}).unwrap();
    }
};

class Button {
  public:
    static auto create(UiSession &session, NodeStyle style, ButtonStyle appearance = {},
                       ButtonOptions options = {}) -> Result<UiId, ControlError> {
        if (!appearance.valid())
            return Err(ControlError{Some(UiError::InvalidValue), {}});
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
        auto state = document.attach_state(id, ButtonState(appearance, options));
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
    static auto set_style(UiDocument &document, UiId id, ButtonStyle style)
        -> Result<empty, UiError> {
        if (auto state = document.state<ButtonState>(id))
            return state->set_style(document, id, style);
        return Err(UiError::InvalidId);
    }
};
} // namespace vui
