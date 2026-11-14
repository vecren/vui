export module vui.controls:scroll_types;
import vui;
using namespace rstd::prelude;
export namespace vui {
enum class ScrollWheelMode { Direct, SingleAxis };
enum class ScrollMovement { Clamped, Elastic };
struct ScrollMotionOptions {
    friend auto operator==(const ScrollMotionOptions &, const ScrollMotionOptions &) -> bool = default;
    bool enabled{}, inertia{true};
    ScrollMovement movement{ScrollMovement::Clamped};
    Scalar threshold{8}, deceleration_rate{.135}, elasticity{.1}, rubber_scale{1};
    auto valid() const -> bool {
        return (movement==ScrollMovement::Clamped || movement==ScrollMovement::Elastic) &&
            finite(threshold) && threshold>=0 && threshold<=1048576 && finite(deceleration_rate) && deceleration_rate>=0 && deceleration_rate<=1 &&
            finite(elasticity) && elasticity>=.001 && elasticity<=60 && finite(rubber_scale) && rubber_scale>=0 && rubber_scale<=10;
    }
};
struct ScrollStyle {
    friend auto operator==(const ScrollStyle &, const ScrollStyle &) -> bool = default;
    ScrollAxes axes;
    Scalar line_step{40};
    ScrollWheelMode wheel_mode{ScrollWheelMode::Direct};
    Point wheel_direction{1,1};
    ScrollMotionOptions motion;
    auto valid() const -> bool { return finite(line_step) && line_step>0 && (wheel_mode==ScrollWheelMode::Direct || wheel_mode==ScrollWheelMode::SingleAxis) &&
        (wheel_direction.x==1 || wheel_direction.x==-1) && (wheel_direction.y==1 || wheel_direction.y==-1) && motion.valid(); }
};
struct UserScroll { Point offset, maximum; };
struct ScrollStatus { bool dragging{}, moving{};Point velocity;Option<UiError> error; };
} // namespace vui
