export module vui.examples:style;
import vui;
import vui.compose;

export namespace examples {
using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;

auto block(Scalar width, Scalar height, Color color, Scalar radius = 0) -> NodeStyle {
    return NodeStyleBuilder{}.width(width).height(height).background(color).radius(radius).build();
}

namespace theme {
inline const auto canvas = Color::from_srgba8(242, 244, 245).unwrap();
inline const auto surface = Color::from_srgba8(255, 255, 255).unwrap();
inline const auto ink = Color::from_srgba8(35, 50, 58).unwrap();
inline const auto muted = Color::from_srgba8(102, 117, 124).unwrap();
inline const auto accent = Color::from_srgba8(20, 107, 105).unwrap();
inline const auto tint = Color::from_srgba8(226, 239, 236).unwrap();
inline const compose::CompositionLocal<Color> local_accent{accent};

auto label(ref<str> value, Scalar size = 14, Color color = ink, bool bold = false) -> compose::Element {
    return compose::text(value).format(TextFormatBuilder{}.family("Noto Sans"_str).size(size)
        .wrap(TextWrap::None).bold(bold).build()).color(color)
        .style(NodeStyleBuilder{}.hit_test_visible(false).build());
}
auto button_label(ref<str> value, Color color = ink, Scalar size = 14) -> compose::Element {
    return label(value, size, color).style(NodeStyleBuilder{}
        .horizontal(Alignment::Center).vertical(Alignment::Center).hit_test_visible(false).build());
}
auto button_style(bool primary = false) -> ButtonStyle {
    return ButtonStyleBuilder{}.normal(primary ? accent : tint)
        .pressed(primary ? ink : Color::from_srgba8(196, 221, 215).unwrap())
        .focused(primary ? Color::from_srgba8(37, 131, 127).unwrap()
                         : Color::from_srgba8(177, 210, 202).unwrap()).radius(8).build();
}
} // namespace theme
} // namespace examples
