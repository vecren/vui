module vui.controls:button_tint;
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
    auto desired() const -> Color {
        auto state = button_visual_state(button_status_, source_.enabled, style_.retain_press_outside);
        auto value = state == ButtonVisualState::Disabled ? style_.disabled
            : state == ButtonVisualState::Pressed ? style_.pressed
            : state == ButtonVisualState::Focused ? style_.focused
            : state == ButtonVisualState::Hovered ? style_.hovered : style_.normal;
        return base_.modulate(value);
    }
    auto value_at(Serial time) const -> Color {
        if (!transitioning_ || style_.duration_ns == 0) return to_;
        auto elapsed = time >= started_ ? time - started_ : 0;
        auto t = elapsed >= style_.duration_ns ? Scalar(1) : Scalar(elapsed) / Scalar(style_.duration_ns);
        return AnimationValue::Tint(from_).interpolate(AnimationValue::Tint(to_), t).unwrap().as_Tint().value;
    }
    void wake() {
        if (motion_.binding.alive()) session_.motion().request(motion_.id);
    }
    auto restore() -> Result<empty, UiError> {
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
    void fail(UiError error) {
        error_ = Some(error);
        transitioning_ = false;
        active_ = false;
    }
    auto acquire() -> Result<empty, UiError> {
        if (lease_.active()) return Ok(empty{});
        PropertyKey key{target_, PresentationProperty::ContentTint};
        auto value = ui_.take_over(slice<PropertyKey>::from_raw_parts(&key, usize(1)), TakeoverPolicy::RejectOccupied);
        if (value.is_err()) return Err(value.unwrap_err());
        lease_ = rstd::move(value).unwrap();
        return Ok(empty{});
    }
    auto refresh(bool force = false) -> Result<empty, UiError> {
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
    void availability_changed() {
        refresh();
        wake();
    }

  public:
    ButtonTintState(UiSession &session, UiId button, UiId target, ButtonTintStyle style, ButtonStatus status)
        : session_(session), ui_(session.document()), lifetime_(ui_.lifetime()), button_(button), target_(target),
          style_(style), button_status_(status) {
        base_ = current_ = from_ = to_ = ui_.resolved_style(target).unwrap().content_tint;
        source_ = session.input().input_availability(button);
        graphic_ = session.input().input_availability(target);
    }
    ~ButtonTintState() {
        source_binding_.disconnect(); graphic_binding_.disconnect();
        if (lifetime_.alive()) restore();
        motion_.binding.disconnect();
    }
    static auto make(UiSession &session, UiId button, UiId target, ButtonTintStyle style, ButtonStatus status)
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
    auto target() const -> UiId { return target_; }
    auto set_frame_interval(Serial value) -> Result<empty, UiError> {
        if (!value) return Err(UiError::InvalidValue);
        style_.frame_interval_ns = value;
        wake(); return Ok(empty{});
    }
    auto status() const -> ButtonTintStatus { return {current_, transitioning_, error_}; }
    void update(ButtonStatus value) {
        button_status_ = value;
        refresh();
        wake();
    }
    auto configure(ButtonTintStyle style) -> Result<empty, UiError> {
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
    auto sample(MotionContext &context, FrameTiming) -> Result<MotionStatus, MotionError> {
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
};
} // namespace vui
