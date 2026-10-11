module;
#include <rstd/enum.hpp>
export module vui:display_list;
import :text;
import :image;
import :content;

using namespace rstd::prelude;

export namespace vui {
class DrawOp {
    RSTD_ENUM(DrawOp, (Save), (Restore), (Transform, (vui::Transform value;)),
              (Clip, (vui::Clip value;)), (Rect, (vui::Rect rect; Color color; Scalar radius;)),
              (Text, (TextLayout layout; Point origin; Color color;)),
              (Image, (ImageData data; Rect destination; Rect source; ImageSampling sampling;)),
              (External, (ContentSource source; Rect destination; ImageFit fit;
                          ImageSampling sampling; Color placeholder;)),
              (Children))
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
        bool children{};
        Option<Rect> bounds;
        Data(Vec<DrawOp> ops, bool has_children, Option<Rect> extent)
            : operations(rstd::move(ops)), children(has_children), bounds(extent) {}
    };
    rstd::sync::Arc<Data> data_;
    DisplayList(Vec<DrawOp> ops, bool children, Option<Rect> bounds = None())
        : data_(rstd::sync::Arc<Data>::make(rstd::move(ops), children, bounds)) {}
    friend class DisplayListRecorder;

  public:
    DisplayList() : DisplayList(Vec<DrawOp>{}, false) {}
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
    auto bounds() const -> Option<Rect> { return data_->bounds; }
    auto same_content(const DisplayList &other) const -> bool {
        return rstd::sync::Arc<Data>::ptr_eq(data_, other.data_);
    }
};

class DisplayListRecorder {
    Vec<DrawOp> operations_;
    usize depth_{};
    bool children_{}, base_modified_{}, closed_{};
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

  public:
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
        if (!value.valid()) {
            reject(RecordingError::InvalidValue);
            return;
        }
        if (depth_ == usize())
            base_modified_ = true;
        coordinates_ = coordinates_ * value;
        operations_.push(DrawOp::Transform(value));
    }
    void clip(Clip value) {
        if (!active())
            return;
        if (!value.valid()) {
            reject(RecordingError::InvalidValue);
            return;
        }
        if (depth_ == usize())
            base_modified_ = true;
        operations_.push(DrawOp::Clip(value));
    }
    void rect(Rect value, Color color, Scalar radius = 0) {
        if (!active())
            return;
        if (!value.valid() || !color.valid() || !finite(radius) || radius < 0) {
            reject(RecordingError::InvalidValue);
            return;
        }
        include(value);
        operations_.push(DrawOp::Rect(value, color, radius));
    }
    void text(TextLayout layout, Point origin, Color color) {
        if (!active())
            return;
        if (!finite(origin.x) || !finite(origin.y) || !color.valid()) {
            reject(RecordingError::InvalidValue);
            return;
        }
        auto ink = layout.ink_bounds();
        include({ink.x + origin.x, ink.y + origin.y, ink.width, ink.height});
        operations_.push(DrawOp::Text(rstd::move(layout), origin, color));
    }
    void image(ImageData data, Rect destination, Rect source,
               ImageSampling sampling = ImageSampling::Linear) {
        if (!active())
            return;
        if (!destination.valid() || !source.valid() || source.x < 0 || source.y < 0 ||
            source.x + source.width > data.width() || source.y + source.height > data.height() ||
            (sampling != ImageSampling::Nearest && sampling != ImageSampling::Linear)) {
            reject(RecordingError::InvalidValue);
            return;
        }
        include(destination);
        operations_.push(DrawOp::Image(rstd::move(data), destination, source, sampling));
    }
    void external(ContentSource source, Rect destination, ImageFit fit = ImageFit::Contain,
                  ImageSampling sampling = ImageSampling::Linear,
                  Color placeholder = {0.15, 0.15, 0.15, 1}) {
        if (!active())
            return;
        if (!source.valid() || !destination.valid() || !placeholder.valid() ||
            (fit != ImageFit::Fill && fit != ImageFit::Contain && fit != ImageFit::Cover) ||
            (sampling != ImageSampling::Nearest && sampling != ImageSampling::Linear)) {
            reject(RecordingError::InvalidValue);
            return;
        }
        include(destination);
        operations_.push(
            DrawOp::External(rstd::move(source), destination, fit, sampling, placeholder));
    }
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
    auto finish() && -> Result<DisplayList, RecordingError> {
        if (!active())
            return Err(*error_);
        closed_ = true;
        if (depth_ != usize())
            return Err(RecordingError::Unbalanced);
        return Ok(DisplayList(rstd::move(operations_), children_, bounds_));
    }
};
} // namespace vui
