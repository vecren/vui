module vui;

namespace vui {
auto Shape::valid() const -> bool {
    return bounds.valid() && radii.valid() &&
           (kind == ShapeKind::Rectangle || kind == ShapeKind::RoundedRectangle ||
            kind == ShapeKind::Ellipse);
}

auto Shape::normalized() const -> Shape {
    if (kind != ShapeKind::RoundedRectangle)
        return {bounds, {}, kind};
    auto limit = rstd::cmp::min(bounds.width, bounds.height) / 2;
    CornerRadii corners{rstd::cmp::min(radii.top_left, limit),
                        rstd::cmp::min(radii.top_right, limit),
                        rstd::cmp::min(radii.bottom_right, limit),
                        rstd::cmp::min(radii.bottom_left, limit)};
    return corners == CornerRadii{} ? rectangle(bounds) : rounded_rect(bounds, corners);
}

auto Shape::inset(Scalar amount) const -> Shape {
    if (!valid() || !finite(amount) || amount < 0)
        return {};
    auto shape = normalized();
    if (amount == 0 || shape.empty())
        return shape;
    auto inset_x = rstd::cmp::min(amount, bounds.width / 2);
    auto inset_y = rstd::cmp::min(amount, bounds.height / 2);
    shape.bounds = {bounds.x + inset_x, bounds.y + inset_y,
                    amount >= bounds.width / 2 ? 0 : bounds.width - 2 * inset_x,
                    amount >= bounds.height / 2 ? 0 : bounds.height - 2 * inset_y};
    auto shrink = [amount](Scalar radius) { return rstd::cmp::max(Scalar(0), radius - amount); };
    shape.radii = {shrink(shape.radii.top_left), shrink(shape.radii.top_right),
                   shrink(shape.radii.bottom_right), shrink(shape.radii.bottom_left)};
    return shape.normalized();
}

auto Shape::contains(Point point) const -> bool {
    if (!valid() || !bounds.contains(point))
        return false;
    if (kind == ShapeKind::Rectangle)
        return true;
    auto x = point.x - bounds.x;
    auto y = point.y - bounds.y;
    if (kind == ShapeKind::Ellipse) {
        auto dx = 2 * (x / bounds.width) - 1;
        auto dy = 2 * (y / bounds.height) - 1;
        return dx * dx + dy * dy <= 1;
    }
    auto left = x < bounds.width / 2;
    auto top = y < bounds.height / 2;
    auto radius = top ? (left ? radii.top_left : radii.top_right)
                      : (left ? radii.bottom_left : radii.bottom_right);
    radius = rstd::cmp::min(radius, rstd::cmp::min(bounds.width, bounds.height) / 2);
    if (radius == 0)
        return true;
    auto side_x = left ? x : bounds.width - x;
    auto side_y = top ? y : bounds.height - y;
    auto dx = rstd::cmp::max(Scalar(0), 1 - side_x / radius);
    auto dy = rstd::cmp::max(Scalar(0), 1 - side_y / radius);
    return dx * dx + dy * dy <= 1;
}
} // namespace vui
