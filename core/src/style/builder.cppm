export module vui:style.builder;
import :node.types;
import :layout;
using namespace rstd::prelude;

export namespace vui {
class LayoutStyleBuilder {
    LayoutStyle value_;

  public:
    LayoutStyleBuilder() = default;
    explicit LayoutStyleBuilder(LayoutStyle value) : value_(rstd::move(value)) {}

    auto width(Option<Scalar> value) & -> LayoutStyleBuilder & {
        value_.width = rstd::move(value);
        return *this;
    }
    auto width(Option<Scalar> value) && -> LayoutStyleBuilder {
        width(rstd::move(value));
        return rstd::move(*this);
    }
    auto width(Scalar value) & -> LayoutStyleBuilder & {
        width(Some(value));
        return *this;
    }
    auto width(Scalar value) && -> LayoutStyleBuilder {
        width(rstd::move(value));
        return rstd::move(*this);
    }
    auto height(Option<Scalar> value) & -> LayoutStyleBuilder & {
        value_.height = rstd::move(value);
        return *this;
    }
    auto height(Option<Scalar> value) && -> LayoutStyleBuilder {
        height(rstd::move(value));
        return rstd::move(*this);
    }
    auto height(Scalar value) & -> LayoutStyleBuilder & {
        height(Some(value));
        return *this;
    }
    auto height(Scalar value) && -> LayoutStyleBuilder {
        height(rstd::move(value));
        return rstd::move(*this);
    }
    auto gap(Scalar value) & -> LayoutStyleBuilder & {
        value_.gap = rstd::move(value);
        return *this;
    }
    auto gap(Scalar value) && -> LayoutStyleBuilder {
        gap(rstd::move(value));
        return rstd::move(*this);
    }
    auto padding(Scalar value) & -> LayoutStyleBuilder & {
        value_.padding = rstd::move(value);
        return *this;
    }
    auto padding(Scalar value) && -> LayoutStyleBuilder {
        padding(rstd::move(value));
        return rstd::move(*this);
    }
    auto min_width(Scalar value) & -> LayoutStyleBuilder & {
        value_.min_width = rstd::move(value);
        return *this;
    }
    auto min_width(Scalar value) && -> LayoutStyleBuilder {
        min_width(rstd::move(value));
        return rstd::move(*this);
    }
    auto min_height(Scalar value) & -> LayoutStyleBuilder & {
        value_.min_height = rstd::move(value);
        return *this;
    }
    auto min_height(Scalar value) && -> LayoutStyleBuilder {
        min_height(rstd::move(value));
        return rstd::move(*this);
    }
    auto max_width(Option<Scalar> value) & -> LayoutStyleBuilder & {
        value_.max_width = rstd::move(value);
        return *this;
    }
    auto max_width(Option<Scalar> value) && -> LayoutStyleBuilder {
        max_width(rstd::move(value));
        return rstd::move(*this);
    }
    auto max_width(Scalar value) & -> LayoutStyleBuilder & {
        max_width(Some(value));
        return *this;
    }
    auto max_width(Scalar value) && -> LayoutStyleBuilder {
        max_width(rstd::move(value));
        return rstd::move(*this);
    }
    auto max_height(Option<Scalar> value) & -> LayoutStyleBuilder & {
        value_.max_height = rstd::move(value);
        return *this;
    }
    auto max_height(Option<Scalar> value) && -> LayoutStyleBuilder {
        max_height(rstd::move(value));
        return rstd::move(*this);
    }
    auto max_height(Scalar value) & -> LayoutStyleBuilder & {
        max_height(Some(value));
        return *this;
    }
    auto max_height(Scalar value) && -> LayoutStyleBuilder {
        max_height(rstd::move(value));
        return rstd::move(*this);
    }
    auto horizontal(Alignment value) & -> LayoutStyleBuilder & {
        value_.horizontal = rstd::move(value);
        return *this;
    }
    auto horizontal(Alignment value) && -> LayoutStyleBuilder {
        horizontal(rstd::move(value));
        return rstd::move(*this);
    }
    auto vertical(Alignment value) & -> LayoutStyleBuilder & {
        value_.vertical = rstd::move(value);
        return *this;
    }
    auto vertical(Alignment value) && -> LayoutStyleBuilder {
        vertical(rstd::move(value));
        return rstd::move(*this);
    }
    auto build() const & -> LayoutStyle { return value_; }
    auto build() && -> LayoutStyle { return rstd::move(value_); }
};

class NodeStyleBuilder {
    NodeStyle value_;

  public:
    NodeStyleBuilder() = default;
    explicit NodeStyleBuilder(NodeStyle value) : value_(rstd::move(value)) {}

    auto layout(LayoutStyle value) & -> NodeStyleBuilder & {
        value_.layout = rstd::move(value);
        return *this;
    }
    auto layout(LayoutStyle value) && -> NodeStyleBuilder {
        layout(rstd::move(value));
        return rstd::move(*this);
    }
    auto width(Option<Scalar> value) & -> NodeStyleBuilder & {
        value_.layout.width = rstd::move(value);
        return *this;
    }
    auto width(Option<Scalar> value) && -> NodeStyleBuilder {
        width(rstd::move(value));
        return rstd::move(*this);
    }
    auto width(Scalar value) & -> NodeStyleBuilder & {
        width(Some(value));
        return *this;
    }
    auto width(Scalar value) && -> NodeStyleBuilder {
        width(rstd::move(value));
        return rstd::move(*this);
    }
    auto height(Option<Scalar> value) & -> NodeStyleBuilder & {
        value_.layout.height = rstd::move(value);
        return *this;
    }
    auto height(Option<Scalar> value) && -> NodeStyleBuilder {
        height(rstd::move(value));
        return rstd::move(*this);
    }
    auto height(Scalar value) & -> NodeStyleBuilder & {
        height(Some(value));
        return *this;
    }
    auto height(Scalar value) && -> NodeStyleBuilder {
        height(rstd::move(value));
        return rstd::move(*this);
    }
    auto gap(Scalar value) & -> NodeStyleBuilder & {
        value_.layout.gap = rstd::move(value);
        return *this;
    }
    auto gap(Scalar value) && -> NodeStyleBuilder {
        gap(rstd::move(value));
        return rstd::move(*this);
    }
    auto padding(Scalar value) & -> NodeStyleBuilder & {
        value_.layout.padding = rstd::move(value);
        return *this;
    }
    auto padding(Scalar value) && -> NodeStyleBuilder {
        padding(rstd::move(value));
        return rstd::move(*this);
    }
    auto min_width(Scalar value) & -> NodeStyleBuilder & {
        value_.layout.min_width = rstd::move(value);
        return *this;
    }
    auto min_width(Scalar value) && -> NodeStyleBuilder {
        min_width(rstd::move(value));
        return rstd::move(*this);
    }
    auto min_height(Scalar value) & -> NodeStyleBuilder & {
        value_.layout.min_height = rstd::move(value);
        return *this;
    }
    auto min_height(Scalar value) && -> NodeStyleBuilder {
        min_height(rstd::move(value));
        return rstd::move(*this);
    }
    auto max_width(Option<Scalar> value) & -> NodeStyleBuilder & {
        value_.layout.max_width = rstd::move(value);
        return *this;
    }
    auto max_width(Option<Scalar> value) && -> NodeStyleBuilder {
        max_width(rstd::move(value));
        return rstd::move(*this);
    }
    auto max_width(Scalar value) & -> NodeStyleBuilder & {
        max_width(Some(value));
        return *this;
    }
    auto max_width(Scalar value) && -> NodeStyleBuilder {
        max_width(rstd::move(value));
        return rstd::move(*this);
    }
    auto max_height(Option<Scalar> value) & -> NodeStyleBuilder & {
        value_.layout.max_height = rstd::move(value);
        return *this;
    }
    auto max_height(Option<Scalar> value) && -> NodeStyleBuilder {
        max_height(rstd::move(value));
        return rstd::move(*this);
    }
    auto max_height(Scalar value) & -> NodeStyleBuilder & {
        max_height(Some(value));
        return *this;
    }
    auto max_height(Scalar value) && -> NodeStyleBuilder {
        max_height(rstd::move(value));
        return rstd::move(*this);
    }
    auto horizontal(Alignment value) & -> NodeStyleBuilder & {
        value_.layout.horizontal = rstd::move(value);
        return *this;
    }
    auto horizontal(Alignment value) && -> NodeStyleBuilder {
        horizontal(rstd::move(value));
        return rstd::move(*this);
    }
    auto vertical(Alignment value) & -> NodeStyleBuilder & {
        value_.layout.vertical = rstd::move(value);
        return *this;
    }
    auto vertical(Alignment value) && -> NodeStyleBuilder {
        vertical(rstd::move(value));
        return rstd::move(*this);
    }
    auto background(Color value) & -> NodeStyleBuilder & {
        value_.background = rstd::move(value);
        return *this;
    }
    auto background(Color value) && -> NodeStyleBuilder {
        background(rstd::move(value));
        return rstd::move(*this);
    }
    auto radius(Scalar value) & -> NodeStyleBuilder & {
        value_.radius = rstd::move(value);
        return *this;
    }
    auto radius(Scalar value) && -> NodeStyleBuilder {
        radius(rstd::move(value));
        return rstd::move(*this);
    }
    auto transform(Transform value) & -> NodeStyleBuilder & {
        value_.transform = rstd::move(value);
        return *this;
    }
    auto transform(Transform value) && -> NodeStyleBuilder {
        transform(rstd::move(value));
        return rstd::move(*this);
    }
    auto clip(Clip value) & -> NodeStyleBuilder & {
        value_.clip = rstd::move(value);
        return *this;
    }
    auto clip(Clip value) && -> NodeStyleBuilder {
        clip(rstd::move(value));
        return rstd::move(*this);
    }
    auto opacity(Scalar value) & -> NodeStyleBuilder & {
        value_.opacity = rstd::move(value);
        return *this;
    }
    auto opacity(Scalar value) && -> NodeStyleBuilder {
        opacity(rstd::move(value));
        return rstd::move(*this);
    }
    auto visible(bool value) & -> NodeStyleBuilder & {
        value_.visible = rstd::move(value);
        return *this;
    }
    auto visible(bool value) && -> NodeStyleBuilder {
        visible(rstd::move(value));
        return rstd::move(*this);
    }
    auto hit_test_visible(bool value) & -> NodeStyleBuilder & {
        value_.hit_test_visible = rstd::move(value);
        return *this;
    }
    auto hit_test_visible(bool value) && -> NodeStyleBuilder {
        hit_test_visible(rstd::move(value));
        return rstd::move(*this);
    }
    auto enabled(bool value) & -> NodeStyleBuilder & {
        value_.enabled = rstd::move(value);
        return *this;
    }
    auto enabled(bool value) && -> NodeStyleBuilder {
        enabled(rstd::move(value));
        return rstd::move(*this);
    }
    auto focusable(bool value) & -> NodeStyleBuilder & {
        value_.focusable = rstd::move(value);
        return *this;
    }
    auto focusable(bool value) && -> NodeStyleBuilder {
        focusable(rstd::move(value));
        return rstd::move(*this);
    }
    auto hit_self(bool value) & -> NodeStyleBuilder & {
        value_.hit_self = rstd::move(value);
        return *this;
    }
    auto hit_self(bool value) && -> NodeStyleBuilder {
        hit_self(rstd::move(value));
        return rstd::move(*this);
    }
    auto text_input_area(Option<Rect> value) & -> NodeStyleBuilder & {
        value_.text_input_area = rstd::move(value);
        return *this;
    }
    auto text_input_area(Option<Rect> value) && -> NodeStyleBuilder {
        text_input_area(rstd::move(value));
        return rstd::move(*this);
    }
    auto content_tint(Color value) & -> NodeStyleBuilder & {
        value_.content_tint = rstd::move(value);
        return *this;
    }
    auto content_tint(Color value) && -> NodeStyleBuilder {
        content_tint(rstd::move(value));
        return rstd::move(*this);
    }
    auto paint_color(Color value) & -> NodeStyleBuilder & {
        value_.paint_color = rstd::move(value);
        return *this;
    }
    auto paint_color(Color value) && -> NodeStyleBuilder {
        paint_color(rstd::move(value));
        return rstd::move(*this);
    }
    auto build() const & -> NodeStyle { return value_; }
    auto build() && -> NodeStyle { return rstd::move(value_); }
};
} // namespace vui
