export module vui.examples:style;
import vui;

export namespace examples {
using namespace vui;
using namespace rstd::prelude;

auto block(Scalar width, Scalar height, Color color, Scalar radius = 0) -> NodeStyle {
    NodeStyle style;
    style.layout.width = Some(width);
    style.layout.height = Some(height);
    style.background = color;
    style.radius = radius;
    return style;
}
} // namespace examples
