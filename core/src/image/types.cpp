module vui;
import :image.types;
using namespace rstd::prelude;

namespace vui {
auto ImageData::operator==(const ImageData &other) const -> bool {
    return id() == other.id() && metadata_.is_some() == other.metadata_.is_some() &&
        (!metadata_ || *metadata_ == *other.metadata_);
}
auto ImageData::concurrent_read() const -> bool {
    return !metadata_ || metadata_->concurrent_read();
}
auto ImagePlacement::content_position(Point local, ContentBounds bounds) const -> Option<Point> {
    const auto &d = destination;
    const auto &s = source;
    if (!finite(local.x) || !finite(local.y) || !d.valid() || d.width <= 0 || d.height <= 0 ||
        !s.valid() || s.width <= 0 || s.height <= 0 ||
        (bounds != ContentBounds::RequireInside && bounds != ContentBounds::AllowOutside) ||
        (bounds == ContentBounds::RequireInside &&
            (local.x < d.x || local.y < d.y || local.x >= d.x + d.width || local.y >= d.y + d.height)))
        return None();
    auto result = Point{s.x + (local.x - d.x) * s.width / d.width,
                        s.y + (local.y - d.y) * s.height / d.height};
    return finite(result.x) && finite(result.y) ? Some(result) : None<Point>();
}
}
