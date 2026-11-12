export module vui.controls:builders;
import vui;
import :types;
import :button;
import :scroll_types;
using namespace rstd::prelude;

export namespace vui {
class ButtonStyleBuilder {
    ButtonStyle value_;

  public:
    ButtonStyleBuilder() = default;
    explicit ButtonStyleBuilder(ButtonStyle value) : value_(rstd::move(value)) {}

    auto normal(Color value) & -> ButtonStyleBuilder & {
        value_.normal = rstd::move(value);
        return *this;
    }
    auto normal(Color value) && -> ButtonStyleBuilder {
        normal(rstd::move(value));
        return rstd::move(*this);
    }
    auto pressed(Color value) & -> ButtonStyleBuilder & {
        value_.pressed = rstd::move(value);
        return *this;
    }
    auto pressed(Color value) && -> ButtonStyleBuilder {
        pressed(rstd::move(value));
        return rstd::move(*this);
    }
    auto focused(Color value) & -> ButtonStyleBuilder & {
        value_.focused = rstd::move(value);
        return *this;
    }
    auto focused(Color value) && -> ButtonStyleBuilder {
        focused(rstd::move(value));
        return rstd::move(*this);
    }
    auto radius(Scalar value) & -> ButtonStyleBuilder & {
        value_.radius = rstd::move(value);
        return *this;
    }
    auto radius(Scalar value) && -> ButtonStyleBuilder {
        radius(rstd::move(value));
        return rstd::move(*this);
    }
    auto build() const & -> ButtonStyle { return value_; }
    auto build() && -> ButtonStyle { return rstd::move(value_); }
};

class ButtonOptionsBuilder {
    ButtonOptions value_;

  public:
    ButtonOptionsBuilder() = default;
    explicit ButtonOptionsBuilder(ButtonOptions value) : value_(rstd::move(value)) {}

    auto focus_on_press(bool value) & -> ButtonOptionsBuilder & {
        value_.focus_on_press = rstd::move(value);
        return *this;
    }
    auto focus_on_press(bool value) && -> ButtonOptionsBuilder {
        focus_on_press(rstd::move(value));
        return rstd::move(*this);
    }
    auto capture_on_press(bool value) & -> ButtonOptionsBuilder & {
        value_.capture_on_press = rstd::move(value);
        return *this;
    }
    auto capture_on_press(bool value) && -> ButtonOptionsBuilder {
        capture_on_press(rstd::move(value));
        return rstd::move(*this);
    }
    auto pointer_enabled(bool value) & -> ButtonOptionsBuilder & {
        value_.pointer_enabled = rstd::move(value);
        return *this;
    }
    auto pointer_enabled(bool value) && -> ButtonOptionsBuilder {
        pointer_enabled(rstd::move(value));
        return rstd::move(*this);
    }
    auto first_key(Key value) & -> ButtonOptionsBuilder & {
        value_.first_key = rstd::move(value);
        return *this;
    }
    auto first_key(Key value) && -> ButtonOptionsBuilder {
        first_key(rstd::move(value));
        return rstd::move(*this);
    }
    auto second_key(Key value) & -> ButtonOptionsBuilder & {
        value_.second_key = rstd::move(value);
        return *this;
    }
    auto second_key(Key value) && -> ButtonOptionsBuilder {
        second_key(rstd::move(value));
        return rstd::move(*this);
    }
    auto appearance(ButtonAppearance value) & -> ButtonOptionsBuilder & {
        value_.appearance = rstd::move(value);
        return *this;
    }
    auto appearance(ButtonAppearance value) && -> ButtonOptionsBuilder {
        appearance(rstd::move(value));
        return rstd::move(*this);
    }
    auto build() const & -> ButtonOptions { return value_; }
    auto build() && -> ButtonOptions { return rstd::move(value_); }
};

class ScrollMotionOptionsBuilder {
    ScrollMotionOptions value_;

  public:
    ScrollMotionOptionsBuilder() = default;
    explicit ScrollMotionOptionsBuilder(ScrollMotionOptions value) : value_(rstd::move(value)) {}

    auto enabled(bool value) & -> ScrollMotionOptionsBuilder & {
        value_.enabled = rstd::move(value);
        return *this;
    }
    auto enabled(bool value) && -> ScrollMotionOptionsBuilder {
        enabled(rstd::move(value));
        return rstd::move(*this);
    }
    auto inertia(bool value) & -> ScrollMotionOptionsBuilder & {
        value_.inertia = rstd::move(value);
        return *this;
    }
    auto inertia(bool value) && -> ScrollMotionOptionsBuilder {
        inertia(rstd::move(value));
        return rstd::move(*this);
    }
    auto movement(ScrollMovement value) & -> ScrollMotionOptionsBuilder & {
        value_.movement = rstd::move(value);
        return *this;
    }
    auto movement(ScrollMovement value) && -> ScrollMotionOptionsBuilder {
        movement(rstd::move(value));
        return rstd::move(*this);
    }
    auto threshold(Scalar value) & -> ScrollMotionOptionsBuilder & {
        value_.threshold = rstd::move(value);
        return *this;
    }
    auto threshold(Scalar value) && -> ScrollMotionOptionsBuilder {
        threshold(rstd::move(value));
        return rstd::move(*this);
    }
    auto deceleration_rate(Scalar value) & -> ScrollMotionOptionsBuilder & {
        value_.deceleration_rate = rstd::move(value);
        return *this;
    }
    auto deceleration_rate(Scalar value) && -> ScrollMotionOptionsBuilder {
        deceleration_rate(rstd::move(value));
        return rstd::move(*this);
    }
    auto elasticity(Scalar value) & -> ScrollMotionOptionsBuilder & {
        value_.elasticity = rstd::move(value);
        return *this;
    }
    auto elasticity(Scalar value) && -> ScrollMotionOptionsBuilder {
        elasticity(rstd::move(value));
        return rstd::move(*this);
    }
    auto rubber_scale(Scalar value) & -> ScrollMotionOptionsBuilder & {
        value_.rubber_scale = rstd::move(value);
        return *this;
    }
    auto rubber_scale(Scalar value) && -> ScrollMotionOptionsBuilder {
        rubber_scale(rstd::move(value));
        return rstd::move(*this);
    }
    auto build() const & -> ScrollMotionOptions { return value_; }
    auto build() && -> ScrollMotionOptions { return rstd::move(value_); }
};

class ScrollStyleBuilder {
    ScrollStyle value_;

  public:
    ScrollStyleBuilder() = default;
    explicit ScrollStyleBuilder(ScrollStyle value) : value_(rstd::move(value)) {}

    auto axes(ScrollAxes value) & -> ScrollStyleBuilder & {
        value_.axes = rstd::move(value);
        return *this;
    }
    auto axes(ScrollAxes value) && -> ScrollStyleBuilder {
        axes(rstd::move(value));
        return rstd::move(*this);
    }
    auto line_step(Scalar value) & -> ScrollStyleBuilder & {
        value_.line_step = rstd::move(value);
        return *this;
    }
    auto line_step(Scalar value) && -> ScrollStyleBuilder {
        line_step(rstd::move(value));
        return rstd::move(*this);
    }
    auto wheel_mode(ScrollWheelMode value) & -> ScrollStyleBuilder & {
        value_.wheel_mode = rstd::move(value);
        return *this;
    }
    auto wheel_mode(ScrollWheelMode value) && -> ScrollStyleBuilder {
        wheel_mode(rstd::move(value));
        return rstd::move(*this);
    }
    auto wheel_direction(Point value) & -> ScrollStyleBuilder & {
        value_.wheel_direction = rstd::move(value);
        return *this;
    }
    auto wheel_direction(Point value) && -> ScrollStyleBuilder {
        wheel_direction(rstd::move(value));
        return rstd::move(*this);
    }
    auto motion(ScrollMotionOptions value) & -> ScrollStyleBuilder & {
        value_.motion = rstd::move(value);
        return *this;
    }
    auto motion(ScrollMotionOptions value) && -> ScrollStyleBuilder {
        motion(rstd::move(value));
        return rstd::move(*this);
    }
    auto build() const & -> ScrollStyle { return value_; }
    auto build() && -> ScrollStyle { return rstd::move(value_); }
};
} // namespace vui
