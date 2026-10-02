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
        Data(Vec<DrawOp> ops, bool has_children)
            : operations(rstd::move(ops)), children(has_children) {}
    };
    rstd::sync::Arc<Data> data_;
    DisplayList(Vec<DrawOp> ops, bool children)
        : data_(rstd::sync::Arc<Data>::make(rstd::move(ops), children)) {}
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
    auto same_content(const DisplayList &other) const -> bool {
        return rstd::sync::Arc<Data>::ptr_eq(data_, other.data_);
    }
};

class DisplayListRecorder {
    Vec<DrawOp> operations_;
    usize depth_{};
    bool children_{}, base_modified_{}, closed_{};
    Option<RecordingError> error_;

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
        operations_.push(DrawOp::Rect(value, color, radius));
    }
    void text(TextLayout layout, Point origin, Color color) {
        if (!active())
            return;
        if (!finite(origin.x) || !finite(origin.y) || !color.valid()) {
            reject(RecordingError::InvalidValue);
            return;
        }
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
        return Ok(DisplayList(rstd::move(operations_), children_));
    }
};
} // namespace vui
