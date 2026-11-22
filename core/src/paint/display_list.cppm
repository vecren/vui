module;
#include <rstd/enum.hpp>
export module vui:paint.display_list;
import :text.types;
import :text.images;
import :image.types;
import :image.nine_patch;
import :content.source;

using namespace rstd::prelude;

export namespace vui {
enum class DrawColorSource { Fixed, Current };
inline auto valid(DrawColorSource source) -> bool {
    return source == DrawColorSource::Fixed || source == DrawColorSource::Current;
}
class DrawOp {
    RSTD_ENUM(DrawOp, (Save), (Restore), (Transform, (vui::Transform value;)),
              (Clip, (vui::Shape value;)),
              (Shape, (vui::Shape shape; Color color; Scalar border_width; DrawColorSource color_source;)),
              (BackdropBlur, (vui::Shape mask; Scalar sigma;)),
              (Text, (TextLayout layout; Point origin; Color color; TextEffects effects{}; TextColors colors{};)),
              (Image, (ImageData data; Rect destination; Rect source; ImageSampling sampling;
                       Color tint; ImageEdges edges; bool clamp_source;
                       ImageBlend blend{ImageBlend::SourceOver}; ImageRadialFill radial{};
                       DrawColorSource color_source{DrawColorSource::Fixed};)),
              (External, (ContentSource source; Rect destination; ImageFit fit;
                          ImageSampling sampling; Color placeholder;
                          ImageBlend blend{ImageBlend::SourceOver};)),
              (Children, (Transform parent_coordinates{};)))
};

enum class RecordingError {
    Unbalanced,
    InvalidValue,
    ChildrenScope,
    DuplicateChildren,
    Closed,
    Limit
};

class DisplayListRecorder;
class DisplayList {
    struct Data {
        Vec<DrawOp> operations;
        bool children{}, frame_queries{};
        Option<Rect> bounds;
        Data(Vec<DrawOp> ops, bool has_children, bool has_frame_queries, Option<Rect> extent)
            : operations(rstd::move(ops)), children(has_children), frame_queries(has_frame_queries), bounds(extent) {}
    };
    rstd::sync::Arc<Data> data_;
    DisplayList(Vec<DrawOp> ops, bool children, bool frame_queries, Option<Rect> bounds = None())
        : data_(rstd::sync::Arc<Data>::make(rstd::move(ops), children, frame_queries, bounds)) {}
    friend class DisplayListRecorder;

  public:
    DisplayList() : DisplayList(Vec<DrawOp>{}, false, false) {}
    DisplayList(const DisplayList &other) : data_(other.data_.clone()) {}
    auto operator=(const DisplayList &other) -> DisplayList & {
        if (this != &other)
            data_ = other.data_.clone();
        return *this;
    }
    DisplayList(DisplayList &&) = default;
    auto operator=(DisplayList &&) -> DisplayList & = default;
    auto operations() const -> slice<DrawOp> { return data_->operations.as_slice(); }
    auto has_children() const -> bool { return data_->children; }
    auto has_frame_queries() const -> bool { return data_->frame_queries; }
    auto bounds() const -> Option<Rect> { return data_->bounds; }
    auto same_content(const DisplayList &other) const -> bool {
        return rstd::sync::Arc<Data>::ptr_eq(data_, other.data_);
    }
};

class Canvas {
    friend class DisplayListRecorder;
    Canvas() = default;
    Canvas(Canvas &&) = default;
    auto operator=(Canvas &&) -> Canvas & = default;
    ~Canvas() = default;

    Vec<DrawOp> operations_;
    usize depth_{};
    bool children_{}, frame_queries_{}, base_modified_{}, closed_{};
    Option<RecordingError> error_;
    Transform coordinates_;
    Vec<Transform> coordinates_stack_;
    Option<Rect> bounds_;
    void include(Rect rect) {
        Point points[] = {coordinates_.map({rect.x, rect.y}),
                          coordinates_.map({rect.x + rect.width, rect.y}),
                          coordinates_.map({rect.x, rect.y + rect.height}),
                          coordinates_.map({rect.x + rect.width, rect.y + rect.height})};
        auto left = points[0].x, right = left, top = points[0].y, bottom = top;
        for (auto point : points) {
            if (!finite(point.x) || !finite(point.y)) {
                reject(RecordingError::InvalidValue);
                return;
            }
            left = rstd::cmp::min(left, point.x);
            right = rstd::cmp::max(right, point.x);
            top = rstd::cmp::min(top, point.y);
            bottom = rstd::cmp::max(bottom, point.y);
        }
        if (bounds_.is_some()) {
            left = rstd::cmp::min(left, bounds_->x);
            top = rstd::cmp::min(top, bounds_->y);
            right = rstd::cmp::max(right, bounds_->x + bounds_->width);
            bottom = rstd::cmp::max(bottom, bounds_->y + bounds_->height);
        }
        Rect bounds{left, top, right - left, bottom - top};
        if (!bounds.valid()) {
            reject(RecordingError::InvalidValue);
            return;
        }
        bounds_ = Some(bounds);
    }

    void reject(RecordingError error) {
        if (error_.is_none())
            error_ = Some(error);
    }
    auto active() -> bool {
        if (closed_)
            reject(RecordingError::Closed);
        return error_.is_none();
    }
    void draw_shape(Shape shape, Color color, Scalar width, DrawColorSource color_source,
                    bool border) {
        if (!active())
            return;
        if (!shape.valid() || !color.valid() || !valid(color_source) ||
            !finite(width) || width < 0) {
            reject(RecordingError::InvalidValue);
            return;
        }
        if (shape.empty() || (border && width == 0))
            return;
        include(shape.bounds);
        operations_.push(DrawOp::Shape(shape.normalized(), color, width, color_source));
    }

  public:
    auto local_transform() const -> Transform { return coordinates_; }
    void save() {
        if (!active())
            return;
        if (depth_ == usize(256)) {
            reject(RecordingError::Limit);
            return;
        }
        ++depth_;
        coordinates_stack_.push(Transform(coordinates_));
        operations_.push(DrawOp::Save());
    }
    void restore() {
        if (!active())
            return;
        if (depth_ == usize()) {
            reject(RecordingError::Unbalanced);
            return;
        }
        --depth_;
        coordinates_ = coordinates_stack_.pop().unwrap();
        operations_.push(DrawOp::Restore());
    }
    void transform(Transform value) {
        if (!active())
            return;
        auto coordinates = coordinates_ * value;
        if (!value.valid() || !coordinates.valid()) {
            reject(RecordingError::InvalidValue);
            return;
        }
        if (depth_ == usize())
            base_modified_ = true;
        coordinates_ = coordinates;
        operations_.push(DrawOp::Transform(value));
    }
    void clip(Shape value) {
        if (!active())
            return;
        if (!value.valid()) {
            reject(RecordingError::InvalidValue);
            return;
        }
        if (depth_ == usize())
            base_modified_ = true;
        operations_.push(DrawOp::Clip(value.normalized()));
    }
    void fill(Shape shape, Color color, DrawColorSource color_source = DrawColorSource::Fixed) {
        draw_shape(shape, color, 0, color_source, false);
    }
    void border(Shape shape, Scalar width, Color color,
                DrawColorSource color_source = DrawColorSource::Fixed) {
        draw_shape(shape, color, width, color_source, true);
    }
    void rect(Rect bounds, Color color, Scalar radius = 0,
              DrawColorSource color_source = DrawColorSource::Fixed) {
        fill(Shape::rounded_rect(bounds, radius), color, color_source);
    }
    void backdrop_blur(Shape mask, Scalar sigma) {
        if (!active())
            return;
        if (!mask.valid() || !finite(sigma) || sigma < 0) {
            reject(RecordingError::InvalidValue);
            return;
        }
        if (sigma == 0 || mask.empty())
            return;
        include(mask.bounds);
        operations_.push(DrawOp::BackdropBlur(mask.normalized(), sigma));
    }
    void text(TextLayout layout, Point origin, Color color, TextEffects effects = {}, TextColors colors = {}, TextDecorations decorations = {}, TextInlineImages inline_images = {}) {
        if (!active())
            return;
        if (!finite(origin.x) || !finite(origin.y) || !color.valid() || !effects.valid() || !colors.matches(layout.text())) {
            reject(RecordingError::InvalidValue);
            return;
        }
        auto decoration_rects = decorations.rects(layout, origin);
        if (decoration_rects.is_err()) { reject(RecordingError::InvalidValue); return; }
        auto geometry = rstd::move(decoration_rects).unwrap();
        auto image_placements = inline_images.placements(layout, origin);
        if (image_placements.is_err()) { reject(RecordingError::InvalidValue); return; }
        auto placed_images = rstd::move(image_placements).unwrap();
        for (const auto &decoration : geometry) if (!decoration.foreground) rect(decoration.bounds, decoration.color);
        auto ink = layout.ink_bounds();
        auto border = effects.outline_width > 0 ? effects.outline_width + 1 : 0;
        Rect bounds{ink.x + origin.x - border, ink.y + origin.y - border,
                    ink.width + border * 2, ink.height + border * 2};
        include(bounds);
        if (effects.shadow_color.a > 0)
            include({bounds.x + effects.shadow_offset.x, bounds.y + effects.shadow_offset.y,
                     bounds.width, bounds.height});
        operations_.push(DrawOp::Text(rstd::move(layout), origin, color, effects, rstd::move(colors)));
        for (const auto &placed : placed_images) {
            const auto &source = inline_images.images()[usize(placed.image)];
            image(ImageData(source.image), placed.destination, source.source, source.sampling, source.tint, ImageEdges::All, true, source.blend);
        }
        for (const auto &decoration : geometry) if (decoration.foreground) rect(decoration.bounds, decoration.color);
    }
    void image(ImageData data, Rect destination, Rect source,
               ImageSampling sampling = ImageSampling::Linear, Color tint = {1, 1, 1, 1},
               ImageEdges edges = ImageEdges::All, bool clamp_source = false,
               ImageBlend blend = ImageBlend::SourceOver, ImageRadialFill radial = {},
               DrawColorSource color_source = DrawColorSource::Fixed) {
        if (!active())
            return;
        if (!radial.valid() || !valid(blend) || !valid(color_source) ||
            static_cast<unsigned>(edges) > 15 || !tint.valid() || !destination.valid() ||
            !data.valid_source(source) ||
            (sampling != ImageSampling::Nearest && sampling != ImageSampling::Linear)) {
            reject(RecordingError::InvalidValue);
            return;
        }
        if (radial.amount == 0) return;
        include(destination);
        frame_queries_ = true;
        operations_.push(DrawOp::Image(rstd::move(data), destination, source, sampling, tint, edges,
                                       clamp_source, blend, radial, color_source));
    }
    void nine_patch(const ImageRegion &region, const NinePatchSpec &spec, Rect destination,
                    Color tint = {1, 1, 1, 1}, ImageSampling sampling = ImageSampling::Linear,
                    ImageBlend blend = ImageBlend::SourceOver,
                    DrawColorSource color_source = DrawColorSource::Fixed) {
        if (!active())
            return;
        if (!valid(blend) || !valid(color_source)) {
            reject(RecordingError::InvalidValue);
            return;
        }
        auto pieces = compute_nine_patch(region, spec, destination);
        if (pieces.is_err()) {
            reject(RecordingError::InvalidValue);
            return;
        }
        for (const auto &piece : pieces.unwrap())
            image(ImageData(region.image), piece.destination, piece.source, sampling, tint,
                  piece.edges, true, blend, {}, color_source);
    }
    void external(ContentSource source, Rect destination, ImageFit fit = ImageFit::Contain,
                  ImageSampling sampling = ImageSampling::Linear,
                  Color placeholder = {0.15, 0.15, 0.15, 1},
                  ImageBlend blend = ImageBlend::SourceOver) {
        if (!active())
            return;
        if (!source.valid() || !destination.valid() || !placeholder.valid() || !valid(blend) ||
            (fit != ImageFit::Fill && fit != ImageFit::Contain && fit != ImageFit::Cover) ||
            (sampling != ImageSampling::Nearest && sampling != ImageSampling::Linear)) {
            reject(RecordingError::InvalidValue);
            return;
        }
        include(destination);
        frame_queries_ = true;
        operations_.push(
            DrawOp::External(rstd::move(source), destination, fit, sampling, placeholder, blend));
    }
};

class DisplayListRecorder : public Canvas {
  public:
    void children() {
        if (!active())
            return;
        if (children_) {
            reject(RecordingError::DuplicateChildren);
            return;
        }
        if (depth_ != usize() || base_modified_) {
            reject(RecordingError::ChildrenScope);
            return;
        }
        children_ = true;
        operations_.push(DrawOp::Children());
    }
    void children(Transform parent_coordinates) {
        if (!active()) return;
        if (children_) { reject(RecordingError::DuplicateChildren); return; }
        if (!parent_coordinates.valid()) { reject(RecordingError::InvalidValue); return; }
        children_ = true;
        operations_.push(DrawOp::Children(parent_coordinates));
    }
    void suppress_children() {
        if (active()) children_ = true;
    }
    auto has_children() const -> bool { return children_; }
    auto finish() && -> Result<DisplayList, RecordingError> {
        if (!active())
            return Err(*error_);
        closed_ = true;
        if (depth_ != usize())
            return Err(RecordingError::Unbalanced);
        return Ok(DisplayList(rstd::move(operations_), children_, frame_queries_, bounds_));
    }
};
} // namespace vui
