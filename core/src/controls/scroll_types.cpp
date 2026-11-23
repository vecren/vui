module vui.controls;
import :scroll_types;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto ScrollSnapOptions::valid() const -> bool {
    return (alignment == ScrollSnapAlignment::None || alignment == ScrollSnapAlignment::Start ||
            alignment == ScrollSnapAlignment::Center || alignment == ScrollSnapAlignment::End) &&
           finite(interval.x) && interval.x >= 0 && finite(interval.y) && interval.y >= 0 &&
           (!enabled() || duration_ns != 0);
}

auto ScrollMotionOptions::valid() const -> bool {
    return (movement==ScrollMovement::Clamped || movement==ScrollMovement::Elastic) &&
        finite(threshold) && threshold>=0 && threshold<=1048576 && finite(deceleration_rate) && deceleration_rate>=0 && deceleration_rate<=1 &&
        finite(elasticity) && elasticity>=.001 && elasticity<=60 && finite(rubber_scale) && rubber_scale>=0 && rubber_scale<=10 &&
        snap.valid();
}
} // namespace vui
