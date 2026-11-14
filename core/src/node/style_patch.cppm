export module vui:node.style_patch;
import :node.types;
using namespace rstd::prelude;

export namespace vui {
enum class NodeStyleField {
    Width, Height, Gap, Padding, MinWidth, MinHeight, MaxWidth, MaxHeight, Horizontal, Vertical, Background, Radius, Transform, Clip, Opacity, Visible, HitTestVisible, Enabled, Focusable, HitSelf, TextInputArea, ContentTint, PaintColor, Count
};
class NodeStylePatch {
    NodeStyle value_;
    uint32_t fields_{};
    bool valid_{true};
    static constexpr auto bit(NodeStyleField field) -> uint32_t { return uint32_t(1) << unsigned(field); }
  public:
    NodeStylePatch() = default;
    static auto all(NodeStyle value) -> NodeStylePatch {
        NodeStylePatch patch; patch.value_ = rstd::move(value);
        patch.fields_ = bit(NodeStyleField::Count) - 1; return patch;
    }
    static auto select(NodeStyle value, initializer_list<NodeStyleField> fields) -> NodeStylePatch {
        NodeStylePatch patch; patch.value_ = rstd::move(value);
        for (auto field : fields) {
            if (unsigned(field) >= unsigned(NodeStyleField::Count)) patch.valid_ = false;
            else patch.fields_ |= bit(field);
        }
        return patch;
    }
    static auto layout(LayoutStyle value) -> NodeStylePatch {
        NodeStylePatch patch; patch.value_.layout = rstd::move(value);
        patch.fields_ = bit(NodeStyleField::Background) - 1; return patch;
    }
    static auto difference(const NodeStyle &before, NodeStyle value) -> NodeStylePatch {
        NodeStylePatch patch;
        if (before.layout.width != value.layout.width) patch.fields_ |= bit(NodeStyleField::Width);
        if (before.layout.height != value.layout.height) patch.fields_ |= bit(NodeStyleField::Height);
        if (before.layout.gap != value.layout.gap) patch.fields_ |= bit(NodeStyleField::Gap);
        if (before.layout.padding != value.layout.padding) patch.fields_ |= bit(NodeStyleField::Padding);
        if (before.layout.min_width != value.layout.min_width) patch.fields_ |= bit(NodeStyleField::MinWidth);
        if (before.layout.min_height != value.layout.min_height) patch.fields_ |= bit(NodeStyleField::MinHeight);
        if (before.layout.max_width != value.layout.max_width) patch.fields_ |= bit(NodeStyleField::MaxWidth);
        if (before.layout.max_height != value.layout.max_height) patch.fields_ |= bit(NodeStyleField::MaxHeight);
        if (before.layout.horizontal != value.layout.horizontal) patch.fields_ |= bit(NodeStyleField::Horizontal);
        if (before.layout.vertical != value.layout.vertical) patch.fields_ |= bit(NodeStyleField::Vertical);
        if (before.background != value.background) patch.fields_ |= bit(NodeStyleField::Background);
        if (before.radius != value.radius) patch.fields_ |= bit(NodeStyleField::Radius);
        if (before.transform != value.transform) patch.fields_ |= bit(NodeStyleField::Transform);
        if (before.clip != value.clip) patch.fields_ |= bit(NodeStyleField::Clip);
        if (before.opacity != value.opacity) patch.fields_ |= bit(NodeStyleField::Opacity);
        if (before.visible != value.visible) patch.fields_ |= bit(NodeStyleField::Visible);
        if (before.hit_test_visible != value.hit_test_visible) patch.fields_ |= bit(NodeStyleField::HitTestVisible);
        if (before.enabled != value.enabled) patch.fields_ |= bit(NodeStyleField::Enabled);
        if (before.focusable != value.focusable) patch.fields_ |= bit(NodeStyleField::Focusable);
        if (before.hit_self != value.hit_self) patch.fields_ |= bit(NodeStyleField::HitSelf);
        if (before.text_input_area != value.text_input_area) patch.fields_ |= bit(NodeStyleField::TextInputArea);
        if (before.content_tint != value.content_tint) patch.fields_ |= bit(NodeStyleField::ContentTint);
        if (before.paint_color != value.paint_color) patch.fields_ |= bit(NodeStyleField::PaintColor);
        patch.value_ = rstd::move(value); return patch;
    }
    auto valid() const -> bool { return valid_; }
    auto empty() const -> bool { return !fields_; }
    auto contains(NodeStyleField field) const -> bool {
        return unsigned(field) < unsigned(NodeStyleField::Count) && (fields_ & bit(field));
    }
    auto apply(NodeStyle current) const -> NodeStyle {
        if (contains(NodeStyleField::Width)) current.layout.width = value_.layout.width;
        if (contains(NodeStyleField::Height)) current.layout.height = value_.layout.height;
        if (contains(NodeStyleField::Gap)) current.layout.gap = value_.layout.gap;
        if (contains(NodeStyleField::Padding)) current.layout.padding = value_.layout.padding;
        if (contains(NodeStyleField::MinWidth)) current.layout.min_width = value_.layout.min_width;
        if (contains(NodeStyleField::MinHeight)) current.layout.min_height = value_.layout.min_height;
        if (contains(NodeStyleField::MaxWidth)) current.layout.max_width = value_.layout.max_width;
        if (contains(NodeStyleField::MaxHeight)) current.layout.max_height = value_.layout.max_height;
        if (contains(NodeStyleField::Horizontal)) current.layout.horizontal = value_.layout.horizontal;
        if (contains(NodeStyleField::Vertical)) current.layout.vertical = value_.layout.vertical;
        if (contains(NodeStyleField::Background)) current.background = value_.background;
        if (contains(NodeStyleField::Radius)) current.radius = value_.radius;
        if (contains(NodeStyleField::Transform)) current.transform = value_.transform;
        if (contains(NodeStyleField::Clip)) current.clip = value_.clip;
        if (contains(NodeStyleField::Opacity)) current.opacity = value_.opacity;
        if (contains(NodeStyleField::Visible)) current.visible = value_.visible;
        if (contains(NodeStyleField::HitTestVisible)) current.hit_test_visible = value_.hit_test_visible;
        if (contains(NodeStyleField::Enabled)) current.enabled = value_.enabled;
        if (contains(NodeStyleField::Focusable)) current.focusable = value_.focusable;
        if (contains(NodeStyleField::HitSelf)) current.hit_self = value_.hit_self;
        if (contains(NodeStyleField::TextInputArea)) current.text_input_area = value_.text_input_area;
        if (contains(NodeStyleField::ContentTint)) current.content_tint = value_.content_tint;
        if (contains(NodeStyleField::PaintColor)) current.paint_color = value_.paint_color;
        return current;
    }
};
} // namespace vui
