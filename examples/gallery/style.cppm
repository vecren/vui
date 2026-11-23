export module vui.examples.gallery:style;
import :glass;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace cx = vui::compose;

inline const auto canvas = Color::from_srgba8(10, 10, 10).unwrap();
inline constexpr Color navigation_surface{0, 0, 0, .24f};
inline const auto panel_surface = Color::from_srgba8(38, 38, 38).unwrap();
inline const auto field_surface = Color::from_srgba8(29, 29, 29).unwrap();
inline constexpr auto control_surface = white(.036f);
inline constexpr auto selected_surface = white(.062f);
inline constexpr auto pressed_surface = white(.085f);
inline constexpr auto edge = white(.020f);
inline const auto foreground = Color::from_srgba8(246, 246, 246).unwrap();
inline const auto muted = Color::from_srgba8(161, 161, 161).unwrap();
constexpr Scalar panel_radius = 16;
constexpr Scalar panel_padding = 14;

auto fill() -> NodeStyleBuilder {
    return NodeStyleBuilder{}.horizontal(Alignment::Stretch).vertical(Alignment::Stretch);
}

auto panel() -> NodeStyleBuilder {
    return NodeStyleBuilder{}.horizontal(Alignment::Stretch).padding(panel_padding).radius(panel_radius);
}

auto label_style() -> NodeStyleBuilder {
    return NodeStyleBuilder{}.vertical(Alignment::Center).hit_test_visible(false);
}

auto text_format(Scalar size, bool bold = false, bool digits = false) -> TextFormat {
    return TextFormatBuilder{}.family(digits ? "monospace"_str : "Noto Sans"_str)
        .size(size).wrap(TextWrap::None).bold(bold).build();
}

auto label(ref<str> text, Scalar size = 14, Color color = foreground, bool bold = false) -> cx::Element {
    return cx::with_semantics(cx::text(text).format(text_format(size, bold)).color(color).style(label_style().build()),
        {.role = SemanticsRole::Text, .label = String::make(text)});
}

auto button_label(ref<str> text, Color color = foreground) -> cx::Element {
    return label(text, 14, color, true).style(label_style().horizontal(Alignment::Center).build());
}

auto button_style(bool active = false) -> ButtonStyle {
    return ButtonStyleBuilder{}.normal(active ? selected_surface : control_surface).pressed(pressed_surface)
        .focused(active ? selected_surface : control_surface).radius(6).build();
}

struct ControlFinish {
    using State = ControlFinish;
    bool highlight{}, focused{};

    auto create() const -> State { return {highlight}; }
    void update(State &state, cx::BehaviorContext &) const { state.highlight = highlight; }

    void handle(EventContext &context, const RoutedEvent &event) {
        if (event.current != event.target) return;
        if (event.input.type != EventType::FocusGained && event.input.type != EventType::FocusLost) return;
        focused = event.input.type == EventType::FocusGained;
        context.document().invalidate_paint(event.current).unwrap();
    }

    auto draw(ModifierDrawContext &context) -> Result<empty, UiError> {
        auto &paint = context.paint();
        auto &canvas = paint.canvas();
        const auto size = paint.size();
        const auto shape = Shape::rounded_rect({0, 0, size.width, size.height}, paint.style().radius);
        if (highlight) glass_sheen(canvas, shape);
        auto drawn = context.draw_content();
        if (drawn.is_err()) return drawn;
        if (focused) canvas.border(shape, 1, white(.68f));
        else if (highlight) canvas.border(shape, 1, white(.012f));
        return Ok(empty{});
    }
};

auto control_finish(cx::Element control, bool highlight = true) -> cx::Element {
    return rstd::move(control).behavior(cx::Key{"control-finish"_str}, ControlFinish{highlight});
}

struct SliderTrackPaint {
    UiSession &session;

    auto paint(PaintContext &context) -> Result<empty, UiError> {
        auto state = Slider::status(session.document(), context.id());
        if (state.is_err()) return Err(state.unwrap_err());
        auto &canvas = context.canvas();
        const auto size = context.size();
        const auto radius = rstd::min(Scalar(10), rstd::min(size.width, size.height) / 2);
        if (radius <= 0) return Ok(empty{});
        const auto x = radius + (size.width - 2 * radius) * state->ratio;
        const auto y = size.height / 2;
        const auto thickness = rstd::min(Scalar(6), radius);
        canvas.fill(Shape::capsule({0, y - thickness / 2, size.width, thickness}), selected_surface);
        canvas.fill(Shape::capsule({0, y - thickness / 2, x, thickness}), muted);
        if (session.input().focused() == Some(context.id())) {
            const auto inset = radius * .88;
            canvas.fill(Shape::ellipse({x - inset, y - inset, 2 * inset, 2 * inset}), panel_surface);
            canvas.border(Shape::ellipse({x - radius, y - radius, 2 * radius, 2 * radius}), radius - inset,
                foreground);
        }
        const auto grip = radius * .7;
        canvas.fill(Shape::ellipse({x - grip, y - grip, 2 * grip, 2 * grip}), foreground);
        return Ok(empty{});
    }
};

auto divider() -> cx::Element {
    return cx::native(LayoutKind::Leaf).style(NodeStyleBuilder{}.horizontal(Alignment::Stretch)
        .height(1).background(edge).hit_test_visible(false).build());
}

auto touch_scroll() -> ScrollStyle {
    return ScrollStyleBuilder{}.motion(ScrollMotionOptionsBuilder{}.enabled(true).build()).build();
}

auto append_label(UiDocument &ui, UiId parent, ref<str> value, NodeStyle style,
                  TextFormat format, Color color) -> Result<UiId, UiError> {
    auto added = ui.append<Text>(parent, style);
    if (added.is_err()) return Err(added.unwrap_err());
    auto id = added.unwrap();
    auto text = ui.set_text(id, value);
    if (text.is_err()) return Err(text.unwrap_err());
    auto formatted = ui.set_text_format(id, format);
    if (formatted.is_err()) return Err(formatted.unwrap_err());
    auto colored = ui.set_text_color(id, color);
    if (colored.is_err()) return Err(colored.unwrap_err());
    return Ok(id);
}

} // namespace examples::gallery::detail
