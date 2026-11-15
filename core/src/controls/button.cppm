export module vui.controls:button;
import vui;
import :types;
using namespace rstd::prelude;
namespace vui {
class ButtonTintState {
    UiSession &session_;
    UiDocument &ui_;
    BindingGuard lifetime_;
    UiId button_, target_;
    ButtonTintStyle style_;
    ButtonStatus button_status_;
    InputAvailability source_, graphic_;
    MotionRegistration motion_;
    Binding source_binding_, graphic_binding_;
    PresentationLease lease_;
    Color base_, current_, from_, to_;
    Serial started_{};
    bool active_{}, transitioning_{}, dirty_{};
    Option<UiError> error_;

    auto now() const -> Serial {
        auto value = ui_.animation_sample_time();
        return value.is_some() ? *value : 0;
    }
    auto desired() const -> Color;
    auto value_at(Serial time) const -> Color;
    void wake() {
        if (motion_.binding.alive()) session_.motion().request(motion_.id);
    }
    auto restore() -> Result<empty, UiError>;
    void fail(UiError error);
    auto acquire() -> Result<empty, UiError>;
    auto refresh(bool force = false) -> Result<empty, UiError>;
    void availability_changed() {
        refresh();
        wake();
    }

  public:
    ButtonTintState(UiSession &session, UiId button, UiId target, ButtonTintStyle style, ButtonStatus status);
    ~ButtonTintState();
    static auto make(UiSession &session, UiId button, UiId target, ButtonTintStyle style, ButtonStatus status)
        -> Result<rstd::rc::Rc<ButtonTintState>, ControlError>;
    auto target() const -> UiId { return target_; }
    auto set_frame_interval(Serial value) -> Result<empty, UiError>;
    auto status() const -> ButtonTintStatus { return {current_, transitioning_, error_}; }
    void update(ButtonStatus value);
    auto configure(ButtonTintStyle style) -> Result<empty, UiError>;
    auto sample(MotionContext &context, FrameTiming) -> Result<MotionStatus, MotionError>;
};
} // namespace vui

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

    void paint(EventContext &context, UiId id);
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
    auto configured_style(NodeStyle style) const -> NodeStyle;
    explicit ButtonState(ButtonStyle style, ButtonOptions options = {})
        : ButtonState(style, options, Color{}, 0) {}
    ButtonState(ButtonStyle style, ButtonOptions options, Color background, Scalar radius)
        : style_(style), options_(options), original_background_(background), original_radius_(radius) {}
    auto set_tint(UiSession &session, UiId id, UiId target, ButtonTintStyle style)
        -> Result<empty, ControlError>;
    auto set_tint_frame_interval(Serial value) -> Result<empty, UiError> {
        return tint_.is_some() ? tint_->get()->set_frame_interval(value)
                              : Result<empty, UiError>(Err(UiError::InvalidValue));
    }
    void clear_tint() { tint_ = None(); }
    auto tint_status() const -> Option<ButtonTintStatus> {
        return tint_.is_some() ? Some(tint_->get()->status()) : None<ButtonTintStatus>();
    }
    auto set_appearance(UiDocument &ui, UiId id, ButtonAppearance appearance) -> Result<empty, UiError>;
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
    auto set_style(UiDocument &document, UiId id, ButtonStyle style) -> Result<empty, UiError>;
    void handle(EventContext &context, const RoutedEvent &event);
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
        -> Result<UiNodeRef, ControlError>;
    static auto create(UiSession &session, NodeStyle style, ButtonStyle appearance = {},
                       ButtonOptions options = {}) -> Result<UiId, ControlError>;
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
    static auto status(UiDocument &document, UiId id) -> Option<ButtonStatus>;
    static auto set_tint(UiSession &session, UiId id, UiId target, ButtonTintStyle style = {})
        -> Result<empty, ControlError>;
    static auto set_tint_frame_interval(UiDocument &ui, UiId id, Serial value)
        -> Result<empty, UiError>;
    static auto clear_tint(UiDocument &ui, UiId id) -> Result<empty, UiError>;
    static auto tint_status(UiDocument &ui, UiId id) -> Option<ButtonTintStatus> {
        auto state = ui.state<ButtonState>(id);
        return state ? state->tint_status() : None<ButtonTintStatus>();
    }
    static auto set_appearance(UiDocument &ui, UiId id, ButtonAppearance appearance)
        -> Result<empty, UiError>;
    static auto set_style(UiDocument &document, UiId id, ButtonStyle style)
        -> Result<empty, UiError>;
};
} // namespace vui
