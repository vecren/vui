export module vui:image.nine_patch;
import :image.types;
using namespace rstd::prelude;

export namespace vui {
struct Insets {
    Scalar left{}, top{}, right{}, bottom{};
    friend auto operator==(Insets, Insets) -> bool = default;
    auto valid() const -> bool {
        return finite(left) && finite(top) && finite(right) && finite(bottom) && left >= 0 &&
               top >= 0 && right >= 0 && bottom >= 0;
    }
};
struct ImageRegion {
    ImageData image;
    Rect source;
    static auto whole(ImageData image) -> ImageRegion {
        Rect source{0, 0, Scalar(image.width()), Scalar(image.height())};
        return {rstd::move(image), source};
    }
    auto valid() const -> bool {
        return image.valid_source(source);
    }
};
struct NinePatchSpec {
    Insets source_insets;
    Option<Insets> destination_insets;
    bool draw_center{true};
};
struct NinePatchRegion {
    Rect source;
    Scalar density{1};
    auto valid() const -> bool {
        return source.valid() && finite(density) && density > 0;
    }
};
inline auto compute_nine_patch(NinePatchRegion region, const NinePatchSpec &spec,
                               Rect destination) -> Result<Vec<ImagePlacement>, ImageError> {
    auto border = spec.source_insets;
    auto target =
        spec.destination_insets.is_some()
            ? *spec.destination_insets
            : Insets{border.left / region.density, border.top / region.density,
                     border.right / region.density, border.bottom / region.density};
    if (!region.valid() || !destination.valid() || !border.valid() || !target.valid() ||
        border.left + border.right > region.source.width ||
        border.top + border.bottom > region.source.height || !finite(target.left + target.right) ||
        !finite(target.top + target.bottom))
        return Err(ImageError::InvalidValue);
    auto shrink = [](Scalar extent, Scalar &first, Scalar &last) {
        auto sum = first + last;
        if (sum > extent) {
            first *= extent / sum;
            last = extent - first;
        }
    };
    shrink(destination.width, target.left, target.right);
    shrink(destination.height, target.top, target.bottom);
    Scalar sx[]{region.source.x, region.source.x + border.left,
                region.source.x + region.source.width - border.right,
                region.source.x + region.source.width};
    Scalar sy[]{region.source.y, region.source.y + border.top,
                region.source.y + region.source.height - border.bottom,
                region.source.y + region.source.height};
    Scalar dx[]{destination.x, destination.x + target.left,
                destination.x + destination.width - target.right,
                destination.x + destination.width};
    Scalar dy[]{destination.y, destination.y + target.top,
                destination.y + destination.height - target.bottom,
                destination.y + destination.height};
    // Shrunk borders can cross by a few ulps after translation.
    dx[2] = (dx[1] > dx[2] ? dx[1] : dx[2]);
    dy[2] = (dy[1] > dy[2] ? dy[1] : dy[2]);
    Vec<ImagePlacement> pieces;
    pieces.reserve(usize(9));
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 3; ++x) {
            if (!spec.draw_center && x == 1 && y == 1)
                continue;
            Rect source{sx[x], sy[y], sx[x + 1] - sx[x], sy[y + 1] - sy[y]};
            Rect target{dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y]};
            if (!source.valid() || !target.valid())
                return Err(ImageError::InvalidValue);
            // Collapsed source intervals still sample a line or point across the destination.
            if (target.width && target.height)
                pieces.push(
                    {target, source,
                     static_cast<ImageEdges>(
                         (target.x == destination.x ? 1u : 0u) |
                         (target.y == destination.y ? 2u : 0u) |
                         (target.x + target.width == destination.x + destination.width ? 4u : 0u) |
                         (target.y + target.height == destination.y + destination.height ? 8u
                                                                                         : 0u))});
        }
    return Ok(rstd::move(pieces));
}
inline auto compute_nine_patch(const ImageRegion &region, const NinePatchSpec &spec,
                               Rect destination) -> Result<Vec<ImagePlacement>, ImageError> {
    if (!region.valid()) return Err(ImageError::InvalidValue);
    return compute_nine_patch(NinePatchRegion{region.source, region.image.density()}, spec,
                              destination);
}
} // namespace vui
