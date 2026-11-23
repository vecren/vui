export module vui.controls:scroll_types;
import vui;
using namespace rstd::prelude;
export namespace vui {
enum class ScrollWheelMode { Direct, SingleAxis };
enum class ScrollMovement { Clamped, Elastic };
enum class ScrollSnapAlignment { None, Start, Center, End };
struct ScrollSnapRequest {
    ViewportMetrics metrics;
    Point proposed, velocity;
    ScrollSnapAlignment alignment{};
};
struct ScrollSnapOptions {
    friend auto operator==(const ScrollSnapOptions &, const ScrollSnapOptions &) -> bool = default;
    ScrollSnapAlignment alignment{};
    Point interval;
    Serial duration_ns{180'000'000};
    Option<Point> (*target)(const ScrollSnapRequest &){};
    auto enabled() const -> bool {
        return target || interval != Point{} || alignment != ScrollSnapAlignment::None;
    }
    auto valid() const -> bool;
};
// A custom curve samples a monotone offset and velocity from its release time.
using ScrollFlingCurve = ScalarMotion (*)(ScalarMotion initial, Scalar rate, Serial elapsed_ns);
enum class ScrollNestedPhase { PreScroll, PostScroll, PreFling, PostFling };
// Scroll deltas and fling velocities are expressed in the connection node's axes.
struct ScrollNestedOffer {
    UiId source;
    Point consumed, available;
};
struct ScrollMotionOptions {
    friend auto operator==(const ScrollMotionOptions &, const ScrollMotionOptions &) -> bool = default;
    bool enabled{}, inertia{true};
    ScrollMovement movement{ScrollMovement::Clamped};
    Scalar threshold{8}, deceleration_rate{.135}, elasticity{.1}, rubber_scale{1};
    ScrollFlingCurve fling{};
    ScrollSnapOptions snap;
    auto valid() const -> bool;
};
struct ScrollStyle {
    friend auto operator==(const ScrollStyle &, const ScrollStyle &) -> bool = default;
    ScrollAxes axes;
    Scalar line_step{40};
    ScrollWheelMode wheel_mode{ScrollWheelMode::Direct};
    Point wheel_direction{1,1};
    ScrollMotionOptions motion;
    bool user_scroll_enabled{true}, reverse_semantics{};
    auto valid() const -> bool { return finite(line_step) && line_step>0 && (wheel_mode==ScrollWheelMode::Direct || wheel_mode==ScrollWheelMode::SingleAxis) &&
        (wheel_direction.x==1 || wheel_direction.x==-1) && (wheel_direction.y==1 || wheel_direction.y==-1) && motion.valid(); }
};
struct UserScroll { Point offset, maximum; };
struct ScrollStatus { bool dragging{}, moving{};Point velocity;Option<UiError> error; };
} // namespace vui
