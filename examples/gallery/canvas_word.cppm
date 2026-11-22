export module vui.examples.gallery:canvas_word;
import vui;
import vectext;
import rstd;

namespace examples::gallery::detail {
using namespace rstd::prelude;
using namespace rstd::literals;
using vui::Point;
using vui::Scalar;

namespace {
auto interpolate(Point a, Point b, Scalar t) -> Point {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

auto separation_squared(Point a, Point b) -> Scalar {
    const auto x = a.x - b.x, y = a.y - b.y;
    return x * x + y * y;
}

void flatten(Vec<Point> &contour, Point start, Point a, Point b, Point end, unsigned depth = 0) {
    constexpr Scalar tolerance = .20;
    if (depth == 8 ||
        (separation_squared(a, interpolate(start, end, 1.0 / 3)) <= tolerance * tolerance &&
         separation_squared(b, interpolate(start, end, 2.0 / 3)) <= tolerance * tolerance)) {
        contour.push(Point(end));
        return;
    }
    const auto left = interpolate(start, a, .5), middle = interpolate(a, b, .5), right = interpolate(b, end, .5);
    const auto left_control = interpolate(left, middle, .5), right_control = interpolate(middle, right, .5);
    const auto split = interpolate(left_control, right_control, .5);
    flatten(contour, start, left, left_control, split, depth + 1);
    flatten(contour, split, right_control, right, end, depth + 1);
}

auto area(const Vec<Point> &contour) -> Scalar {
    Scalar cross{};
    for (usize i{}; i < contour.len(); ++i) {
        const auto &a = contour[i], &b = contour[(i + usize(1)) % contour.len()];
        cross += a.x * b.y - a.y * b.x;
    }
    return __builtin_fabs(cross);
}

auto flatten_glyph(const vectext::GlyphOutline &glyph, vectext::Point position) -> Vec<Vec<Point>> {
    using Verb = vectext::GlyphOutlineVerb;
    auto placed = [position](vectext::Point point) -> Point {
        return {position.x + point.x, position.y + point.y};
    };
    Vec<Vec<Point>> contours;
    Vec<Point> contour;
    Point current;
    for (const auto &command : glyph.commands) {
        const auto end = placed(command.end);
        switch (command.verb) {
        case Verb::Move:
            contour.clear();
            contour.push(Point(end));
            break;
        case Verb::Line:
            contour.push(Point(end));
            break;
        case Verb::Quadratic: {
            const auto control = placed(command.control1);
            flatten(contour, current, interpolate(current, control, 2.0 / 3),
                    interpolate(end, control, 2.0 / 3), end);
            break;
        }
        case Verb::Cubic:
            flatten(contour, current, placed(command.control1), placed(command.control2), end);
            break;
        case Verb::Close:
            if (contour.len() > usize(1) && contour[usize(0)] == contour[contour.len() - usize(1)])
                (void)contour.pop();
            if (contour.len() >= usize(3)) contours.push(rstd::exchange(contour, Vec<Point>{}));
            break;
        }
        current = end;
    }
    rstd::slice_::sort_unstable_by(contours.as_mut_slice().as_mut_ref(), [](const auto &a, const auto &b) {
        return area(a) > area(b);
    });
    return contours;
}
} // namespace

auto canvas_word_outline() -> Vec<Vec<Point>> {
    vectext::SystemTextService text;
    auto layout = text.layout({.text = "canvas"_str, .family = "sans-serif"_str, .size = 128,
                               .wrap = vectext::TextWrap::None, .bold = true});
    if (layout.is_err()) return {};
    Vec<Vec<Point>> word;
    for (const auto &glyph : layout->glyphs()) {
        if (!glyph.drawable || glyph.missing) return {};
        auto path = layout->fonts()[usize(glyph.font)].outline(glyph.glyph);
        if (path.is_err() || path->commands.is_empty()) return {};
        auto contours = flatten_glyph(*path, glyph.position);
        for (auto &contour : contours) word.push(rstd::move(contour));
    }
    if (word.is_empty()) return word;

    auto minimum = word[usize(0)][usize(0)], maximum = minimum;
    for (const auto &contour : word) {
        for (const auto point : contour) {
            minimum.x = rstd::min(minimum.x, point.x);
            minimum.y = rstd::min(minimum.y, point.y);
            maximum.x = rstd::cmp::max(maximum.x, point.x);
            maximum.y = rstd::cmp::max(maximum.y, point.y);
        }
    }
    const auto width = maximum.x - minimum.x, height = maximum.y - minimum.y;
    if (width <= 0 || height <= 0) return {};
    const auto center = interpolate(minimum, maximum, .5);
    const auto scale = rstd::min(2.36 / width, 1.55 / height);
    for (auto &contour : word)
        for (auto &point : contour)
            point = {(point.x - center.x) * scale, (point.y - center.y) * scale};
    return word;
}
} // namespace examples::gallery::detail
