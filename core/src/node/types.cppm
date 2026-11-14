export module vui:node.types;
import :scene.composition;
import :layout.types;
using namespace rstd::prelude;
export namespace vui {
enum class LayoutKind { Leaf, Row, Column, Stack, Viewport, Text, Image, Custom, Scene, Item };
inline auto accepts_children(LayoutKind kind) -> bool {
    return kind == LayoutKind::Row || kind == LayoutKind::Column || kind == LayoutKind::Stack ||
           kind == LayoutKind::Viewport || kind == LayoutKind::Custom || kind == LayoutKind::Item;
}
struct Item {
    static constexpr LayoutKind layout = LayoutKind::Item;
};
struct SceneViewport {
    static constexpr LayoutKind layout = LayoutKind::Scene;
};
struct Viewport {
    static constexpr LayoutKind layout = LayoutKind::Viewport;
};
struct ScrollAxes {
    bool horizontal{}, vertical{true};
    friend auto operator==(const ScrollAxes &, const ScrollAxes &) -> bool = default;
};
enum class ViewportBoundary { Clamped, Unclamped };
struct ViewportMetrics {
    Size viewport, extent;
    Point offset, maximum;
    ViewportBoundary boundary{ViewportBoundary::Clamped};
    auto operator==(const ViewportMetrics &) const -> bool = default;
};
struct Image {
    static constexpr LayoutKind layout = LayoutKind::Image;
};
struct Text {
    static constexpr LayoutKind layout = LayoutKind::Text;
};
struct Rectangle {
    static constexpr LayoutKind layout = LayoutKind::Leaf;
};
struct Row {
    static constexpr LayoutKind layout = LayoutKind::Row;
};
struct Column {
    static constexpr LayoutKind layout = LayoutKind::Column;
};
struct CustomLayout {
    static constexpr LayoutKind layout = LayoutKind::Custom;
};
struct Stack {
    static constexpr LayoutKind layout = LayoutKind::Stack;
};
struct NodeStyle {
    LayoutStyle layout;
    Color background{0, 0, 0, 0};
    Scalar radius{};
    Transform transform;
    Clip clip;
    Scalar opacity{1};
    bool visible{true}, hit_test_visible{true}, enabled{true}, focusable{};
    bool hit_self{true};
    Option<Rect> text_input_area;
    Color content_tint{1, 1, 1, 1};
    Color paint_color{1, 1, 1, 1};
    friend auto operator==(const NodeStyle &, const NodeStyle &) -> bool = default;
    auto valid() const -> bool {
        return layout.valid() && background.valid() && content_tint.valid() && paint_color.valid() && finite(radius) && radius >= 0 &&
               RenderProperties{transform, {}, clip, opacity, visible}.valid() &&
               (text_input_area.is_none() || text_input_area->valid());
    }
};
} // namespace vui
