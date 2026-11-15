module vui;
import :frame.types;
using namespace rstd::prelude;

namespace vui {
auto FrameImage::content_position(Point surface, ContentBounds bounds) const -> Option<Point> {
    auto inverse = local_to_surface.inverse();
    return inverse ? placement.content_position(inverse->map(surface), bounds) : None<Point>();
}
auto FramePlan::image(RenderId node, Serial image_id) const -> Result<Option<FrameImage>, FrameError> {
    Option<FrameImage> result;
    for (const auto &pass : data_->passes) for (const auto &draw : pass.draws) if (draw.is_Image()) {
        const auto &image = draw.as_Image();
        if (image.node != node || image.data.id() != image_id) continue;
        if (result) return Err(FrameError::InvalidContent);
        result = Some(FrameImage{identity(), ImageData(image.data),
            {image.destination, image.source, image.edges}, image.transform, image.node_to_surface});
    }
    return Ok(rstd::move(result));
}
}
