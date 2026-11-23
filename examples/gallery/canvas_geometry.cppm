export module vui.examples.gallery:canvas_geometry;
import vui.compose;

namespace examples::gallery::detail {
using namespace vui;

inline constexpr Scalar canvas_turn = 6.283185307179586;

auto point_distance(Point start, Point end) -> Scalar {
    const auto x = end.x - start.x, y = end.y - start.y;
    return __builtin_sqrt(x * x + y * y);
}

void stroke_segment(Canvas &canvas, Point start, Point end, Scalar width, Color color) {
    const auto length = point_distance(start, end);
    if (length <= 0) return;
    const auto x = (end.x - start.x) / length, y = (end.y - start.y) / length;
    canvas.save();
    canvas.transform({x, y, -y, x, start.x, start.y});
    canvas.fill(Shape::capsule({-width / 2, -width / 2, length + width, width}), color);
    canvas.restore();
}

void stroke_curve(Canvas &canvas, Index segments, Scalar width, Color color, auto point) {
    auto previous = point(Scalar(0));
    for (Index i = 1; i <= segments; ++i) {
        const auto next = point(Scalar(i) / segments);
        stroke_segment(canvas, previous, next, width, color);
        previous = next;
    }
}
} // namespace examples::gallery::detail
