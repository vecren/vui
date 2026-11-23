module vui.controls;
import :button;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto ButtonTintState::desired() const -> Color {
    auto state = button_visual_state(button_status_, source_.enabled, style_.retain_press_outside);
    auto value = state == ButtonVisualState::Disabled ? style_.disabled
        : state == ButtonVisualState::Pressed ? style_.pressed
        : state == ButtonVisualState::Focused ? style_.focused
        : state == ButtonVisualState::Hovered ? style_.hovered : style_.normal;
    return base_.modulate(value);
}

auto ButtonTintState::value_at(Serial time) const -> Color {
    if (!transitioning_ || style_.duration_ns == 0) return to_;
    auto elapsed = time >= started_ ? time - started_ : 0;
    auto t = elapsed >= style_.duration_ns ? Scalar(1) : Scalar(elapsed) / Scalar(style_.duration_ns);
    return AnimationValue::Tint(from_).interpolate(AnimationValue::Tint(to_), t).unwrap().as_Tint().value;
}

auto ButtonTintState::restore() -> Result<empty, UiError> {
    transitioning_ = false;
    active_ = false;
    if (lease_.active()) {
        auto result = ui_.release_presentation(lease_, LeaseEndPolicy::RestoreBase);
        if (result.is_err()) return result;
    } else if (dirty_ && ui_.valid(target_)) {
        auto result = ui_.set_content_tint(target_, base_);
        if (result.is_err()) return result;
    }
    dirty_ = false;
    current_ = from_ = to_ = base_;
    return Ok(empty{});
}

void ButtonTintState::fail(UiError error) {
    error_ = Some(error);
    transitioning_ = false;
    active_ = false;
}

auto ButtonTintState::acquire() -> Result<empty, UiError> {
    if (lease_.active()) return Ok(empty{});
    PropertyKey key{target_, PresentationProperty::ContentTint};
    auto value = ui_.take_over(slice<PropertyKey>::from_raw_parts(&key, usize(1)), TakeoverPolicy::RejectOccupied);
    if (value.is_err()) return Err(value.unwrap_err());
    lease_ = rstd::move(value).unwrap();
    return Ok(empty{});
}

auto ButtonTintState::refresh(bool force) -> Result<empty, UiError> {
    auto source = session_.input().input_availability(button_);
    auto graphic = session_.input().input_availability(target_);
    bool remounted = source.mount_generation != source_.mount_generation || graphic.mount_generation != graphic_.mount_generation;
    source_ = source;
    graphic_ = graphic;
    if (error_.is_some() && !force) return Err(*error_);
    if (!ui_.valid(button_) || !ui_.valid(target_)) {
        fail(UiError::InvalidId); return Err(UiError::InvalidId);
    }
    bool available = source.mounted && source.visible && source.window_focused && graphic.mounted && graphic.visible;
    if (remounted || !available) {
        auto result = restore();
        if (result.is_err()) { fail(result.unwrap_err()); return result; }
    } else if (active_ && !lease_.active()) {
        fail(UiError::PropertyOwned); return Err(UiError::PropertyOwned);
    }
    if (!available) return Ok(empty{});
    auto acquired = acquire();
    if (acquired.is_err()) { fail(acquired.unwrap_err()); return acquired; }
    auto target = desired();
    if (!active_) {
        current_ = from_ = to_ = target;
        transitioning_ = false;
        PresentationBatch batch; batch.content_tint(target_, target);
        auto written = ui_.write_presentation(lease_, batch);
        if (written.is_err()) { fail(written.unwrap_err()); return written; }
        dirty_ = true;
        active_ = true;
    } else if (force || to_ != target) {
        from_ = current_ = value_at(now());
        to_ = target;
        started_ = now();
        transitioning_ = from_ != to_ && style_.duration_ns != 0;
    }
    return Ok(empty{});
}

ButtonTintState::ButtonTintState(UiSession &session, UiId button, UiId target, ButtonTintStyle style, ButtonStatus status)
    : session_(session), ui_(session.document()), lifetime_(ui_.lifetime()), button_(button), target_(target),
      style_(style), button_status_(status) {
    base_ = current_ = from_ = to_ = ui_.resolved_style(target).unwrap().content_tint;
    source_ = session.input().input_availability(button);
    graphic_ = session.input().input_availability(target);
}

ButtonTintState::~ButtonTintState() {
    source_binding_.disconnect(); graphic_binding_.disconnect();
    if (lifetime_.alive()) restore();
    motion_.binding.disconnect();
}

auto ButtonTintState::make(UiSession &session, UiId button, UiId target, ButtonTintStyle style, ButtonStatus status)
    -> Result<rstd::rc::Rc<ButtonTintState>, ControlError> {
    if (!style.valid()) return Err(ControlError{Some(UiError::InvalidValue), {}});
    if (!session.document().valid(target)) return Err(ControlError{Some(UiError::InvalidId), {}});
    auto owner = rstd::rc::make_rc<ButtonTintState>(session, button, target, style, status);
    auto self = owner.downgrade();
    auto source = session.input().on_input_availability_changed(button, [self](const InputAvailability &) {
        auto owner = self.upgrade(); if (owner.is_some()) owner->get()->availability_changed();
    });
    if (source.is_err()) return Err(ControlError{{}, Some(source.unwrap_err())});
    owner.get()->source_binding_ = rstd::move(source).unwrap();
    if (target != button) {
        auto graphic = session.input().on_input_availability_changed(target, [self](const InputAvailability &) {
            auto owner = self.upgrade(); if (owner.is_some()) owner->get()->availability_changed();
        });
        if (graphic.is_err()) return Err(ControlError{{}, Some(graphic.unwrap_err())});
        owner.get()->graphic_binding_ = rstd::move(graphic).unwrap();
    }
    auto motion = session.motion().bind(self);
    if (motion.is_err()) return Err(ControlError{Some(UiError::Limit), {}});
    owner.get()->motion_ = rstd::move(motion).unwrap();
    auto ready = owner.get()->refresh();
    if (ready.is_err()) return Err(ControlError{Some(ready.unwrap_err()), {}});
    return Ok(rstd::move(owner));
}

auto ButtonTintState::set_frame_interval(Serial value) -> Result<empty, UiError> {
    if (!value) return Err(UiError::InvalidValue);
    style_.frame_interval_ns = value;
    wake(); return Ok(empty{});
}

void ButtonTintState::update(ButtonStatus value) {
    button_status_ = value;
    refresh();
    wake();
}

auto ButtonTintState::configure(ButtonTintStyle style) -> Result<empty, UiError> {
    if (!style.valid()) return Err(UiError::InvalidValue);
    current_ = from_ = to_ = value_at(now());
    transitioning_ = false;
    if (active_ && !lease_.active()) {
        auto result = restore();
        if (result.is_err()) { fail(result.unwrap_err()); return result; }
    }
    style_ = style;
    error_ = None();
    auto result = refresh(true);
    wake();
    return result;
}

auto ButtonTintState::sample(MotionContext &context, FrameTiming) -> Result<MotionStatus, MotionError> {
    auto ready = refresh();
    if (ready.is_err() || !active_) return Ok(MotionStatus::Idle);
    auto value = value_at(context.now_ns());
    PresentationBatch batch; batch.content_tint(target_, value);
    auto written = context.write(lease_, batch);
    if (written.is_err()) { fail(UiError::PropertyOwned); return Ok(MotionStatus::Idle); }
    dirty_ = true;
    current_ = value;
    auto elapsed = context.now_ns() >= started_ ? context.now_ns() - started_ : 0;
    if (elapsed >= style_.duration_ns) transitioning_ = false;
    if (!transitioning_) return Ok(MotionStatus::Idle);
    auto remaining = style_.duration_ns - elapsed;
    auto interval = remaining < style_.frame_interval_ns ? remaining : style_.frame_interval_ns;
    if (interval > ~Serial{} - context.now_ns()) { fail(UiError::Limit); return Ok(MotionStatus::Idle); }
    context.request_tick(context.now_ns() + interval).unwrap();
    return Ok(MotionStatus::Running);
}

void ButtonState::paint(EventContext &context, UiId id) {
    if (options_.appearance == ButtonAppearance::Custom)
        return;
    auto color = status().pressed ? style_.pressed : focused_ ? style_.focused : style_.normal;
    context.document().set_background(id, color, style_.radius);
}

auto ButtonState::configured_style(NodeStyle style) const -> NodeStyle {
    if (options_.appearance == ButtonAppearance::Default) {
        auto current = status();
        style.background = current.pressed ? style_.pressed : current.focused ? style_.focused : style_.normal;
        style.radius = style_.radius;
    }
    style.focusable = true; return style;
}

auto ButtonState::set_tint(UiSession &session, UiId id, UiId target, ButtonTintStyle style)
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

auto ButtonState::set_appearance(UiDocument &ui, UiId id, ButtonAppearance appearance) -> Result<empty, UiError> {
    if (appearance != ButtonAppearance::Default && appearance != ButtonAppearance::Custom)
        return Err(UiError::InvalidValue);
    if (appearance == options_.appearance) return Ok(empty{});
    auto color = appearance == ButtonAppearance::Custom ? original_background_
        : status().pressed ? style_.pressed : focused_ ? style_.focused : style_.normal;
    auto result = ui.set_background(id, color, appearance == ButtonAppearance::Custom ? original_radius_ : style_.radius);
    if (result.is_ok()) options_.appearance = appearance;
    return result;
}

auto ButtonState::set_style(UiDocument &document, UiId id, ButtonStyle style) -> Result<empty, UiError> {
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

void ButtonState::handle(EventContext &context, const RoutedEvent &event) {
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
    case EventType::SemanticAction:
        if (event.target != event.current || input.semantic_action != SemanticsAction::Activate ||
            context.default_prevented()) return;
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

auto Button::prepare(UiDocument &document, UiEditBatch &edit, Option<UiId> previous,
                    NodeStyle style, ButtonStyle appearance, ButtonOptions options,
                    Option<NodeStyle> before)
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
    if (state && before.is_some()) edit.style(node, NodeStylePatch::difference(state->configured_style(*before), style));
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

auto Button::default_semantics() -> SemanticsProperties {
    return {.role = SemanticsRole::Button,
            .actions = semantics_action_mask(SemanticsAction::Activate) | semantics_action_mask(SemanticsAction::Focus),
            .merge_descendants = true};
}

auto Button::create(UiSession &session, NodeStyle style, ButtonStyle appearance,
                   ButtonOptions options) -> Result<UiId, ControlError> {
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
    auto described = session.semantics().set_default(id, default_semantics());
    if (described.is_err()) {
        document.destroy(id);
        return Err(ControlError{Some(described.unwrap_err()), {}});
    }
    return Ok(id);
}

auto Button::status(UiDocument &document, UiId id) -> Option<ButtonStatus> {
    if (auto state = document.state<ButtonState>(id))
        return Some(state->status());
    return None();
}

auto Button::set_tint(UiSession &session, UiId id, UiId target, ButtonTintStyle style)
    -> Result<empty, ControlError> {
    auto state = session.document().state<ButtonState>(id);
    return state ? state->set_tint(session, id, target, style)
                 : Result<empty, ControlError>(Err(ControlError{Some(UiError::InvalidId), {}}));
}

auto Button::set_tint_frame_interval(UiDocument &ui, UiId id, Serial value)
    -> Result<empty, UiError> {
    auto state = ui.state<ButtonState>(id);
    return state ? state->set_tint_frame_interval(value)
                 : Result<empty, UiError>(Err(UiError::InvalidId));
}

auto Button::clear_tint(UiDocument &ui, UiId id) -> Result<empty, UiError> {
    auto state = ui.state<ButtonState>(id);
    if (!state) return Err(UiError::InvalidId);
    state->clear_tint(); return Ok(empty{});
}

auto Button::set_appearance(UiDocument &ui, UiId id, ButtonAppearance appearance)
    -> Result<empty, UiError> {
    auto state = ui.state<ButtonState>(id);
    return state ? state->set_appearance(ui, id, appearance)
                 : Result<empty, UiError>(Err(UiError::InvalidId));
}

auto Button::set_style(UiDocument &document, UiId id, ButtonStyle style)
    -> Result<empty, UiError> {
    if (auto state = document.state<ButtonState>(id))
        return state->set_style(document, id, style);
    return Err(UiError::InvalidId);
}
} // namespace vui
