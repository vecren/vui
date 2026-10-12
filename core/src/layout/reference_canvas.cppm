export module vui:layout.reference_canvas;
import :layout.protocol;
using namespace rstd::prelude;

export namespace vui {
enum class CanvasFit { Contain, Cover, Stretch };
struct ReferenceCanvas {
    Size reference_size;
    CanvasFit fit{CanvasFit::Contain};
    Point alignment{.5, .5};
    bool clip{true};
    Option<Scalar> max_scale;
    auto valid() const -> bool {
        return reference_size.valid() && reference_size.width > 0 && reference_size.height > 0 &&
               finite(alignment.x) && finite(alignment.y) && alignment.x >= 0 && alignment.x <= 1 &&
               alignment.y >= 0 && alignment.y <= 1 &&
               (fit == CanvasFit::Contain || fit == CanvasFit::Cover ||
                fit == CanvasFit::Stretch) &&
               (max_scale.is_none() || (finite(*max_scale) && *max_scale > 0));
    }
    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints)
        -> Result<Size, UiError> {
        if (!valid() || context.child_count() != usize(1))
            return Err(UiError::InvalidValue);
        auto measured = context.measure_child(
            usize(), {Some(reference_size.width), Some(reference_size.height)});
        if (measured.is_err())
            return Err(measured.unwrap_err());
        return Ok(Size{constraints.max_width.unwrap_or(reference_size.width),
                       constraints.max_height.unwrap_or(reference_size.height)});
    }
    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        if (!valid() || context.child_count() != usize(1))
            return Err(UiError::InvalidValue);
        auto desired = context.desired_child(usize());
        if (desired.is_err())
            return Err(desired.unwrap_err());
        auto local = desired.unwrap();
        auto sx = size.width / reference_size.width, sy = size.height / reference_size.height;
        if (fit != CanvasFit::Stretch) {
            auto scale = fit == CanvasFit::Contain ? rstd::min(sx, sy) : rstd::cmp::max(sx, sy);
            sx = sy = scale;
        }
        if (max_scale.is_some()) {
            sx = rstd::min(sx, *max_scale);
            sy = rstd::min(sy, *max_scale);
        }
        if (!size.width || !size.height)
            sx = sy = 0;
        Transform mapping{sx,
                          0,
                          0,
                          sy,
                          (size.width - local.width * sx) * alignment.x,
                          (size.height - local.height * sy) * alignment.y};
        auto clipped =
            context.set_clip(clip ? Clip{true, {0, 0, size.width, size.height}, 0} : Clip{});
        if (clipped.is_err())
            return clipped;
        return context.place_child(
            usize(), LayoutPlacement{{0, 0, size.width, size.height}, local, mapping});
    }
};
} // namespace vui
