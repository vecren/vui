module vui;
import :document.reveal;
using namespace rstd::prelude;

namespace vui {
DocumentRevealRegion::DocumentRevealRegion(Rect value) {
    points.push({value.x, value.y});
    points.push({value.x + value.width, value.y});
    points.push({value.x + value.width, value.y + value.height});
    points.push({value.x, value.y + value.height});
}

auto DocumentRevealRegion::bounds() const -> Result<Rect, UiError> {
    if (points.is_empty()) return Ok(Rect{});
    auto left = points[usize()].x, right = left, top = points[usize()].y, bottom = top;
    for (auto point : points) {
        left = rstd::cmp::min(left, point.x); right = rstd::cmp::max(right, point.x);
        top = rstd::cmp::min(top, point.y); bottom = rstd::cmp::max(bottom, point.y);
    }
    Rect result{left, top, right - left, bottom - top};
    return result.valid() ? Result<Rect, UiError>(Ok(result)) : Result<Rect, UiError>(Err(UiError::InvalidValue));
}

auto DocumentRevealRegion::map(Transform transform) -> Result<empty, UiError> {
    for (auto &point : points) {
        point = transform.map(point);
        if (!finite(point.x) || !finite(point.y)) return Err(UiError::InvalidValue);
    }
    return Ok(empty{});
}

auto DocumentRevealRegion::clip(Rect rect) -> Result<bool, UiError> {
    bool clipped{};
    for (unsigned edge = 0; edge < 4 && !points.is_empty(); ++edge) {
        auto distance = [&](Point point) -> Scalar {
            switch (edge) {
            case 0: return point.x - rect.x;
            case 1: return rect.x + rect.width - point.x;
            case 2: return point.y - rect.y;
            default: return rect.y + rect.height - point.y;
            }
        };
        Vec<Point> output;
        auto previous = points[points.len() - usize(1)];
        auto before = distance(previous);
        for (auto point : points) {
            auto after = distance(point);
            if ((before >= 0) != (after >= 0)) {
                auto t = before / (before - after);
                Point intersection{previous.x * (1 - t) + point.x * t,
                                   previous.y * (1 - t) + point.y * t};
                if (!finite(intersection.x) || !finite(intersection.y)) return Err(UiError::InvalidValue);
                output.push(rstd::move(intersection));
            }
            if (after >= 0) output.push(Point(point));
            else clipped = true;
            previous = point; before = after;
        }
        points = rstd::move(output);
    }
    return Ok(clipped);
}
}
