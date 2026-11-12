export module vui.examples:style;
import vui;

export namespace examples {
using namespace vui;
using namespace rstd::prelude;

auto block(Scalar width, Scalar height, Color color, Scalar radius = 0) -> NodeStyle {
    return NodeStyleBuilder{}.width(width).height(height).background(color).radius(radius).build();
}
} // namespace examples
