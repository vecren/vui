export module vui.examples.composition.view;
import vui.compose;
import vui.examples;
import vui.examples.composition.counter;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace composition_example {
using namespace vui;
namespace cx = vui::compose;

struct PhaseDecoration {
    state::Value<bool> active;
    auto measure(ModifierMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError> {
        return context.measure_content(constraints);
    }
    auto arrange(ModifierArrangeContext &context, Size size) -> Result<empty, UiError> {
        auto value = state::read(active);
        if (value.is_err()) return Err(UiError::LayoutFailed);
        return context.place_content({{0, 0, size.width, size.height}, size,
            {1, 0, 0, 1, value.unwrap().value() ? Scalar(4) : Scalar(), 0}});
    }
    auto draw(ModifierDrawContext &context) -> Result<empty, UiError> {
        auto content = context.draw_content();
        if (content.is_err()) return content;
        auto value = state::read(active);
        if (value.is_err()) return Err(UiError::PaintFailed);
        auto &paint = context.paint();
        auto size = paint.size();
        paint.canvas().rect({18, size.height - 8, size.width - 36, 3},
            value.unwrap().value() ? theme::accent : theme::tint, 1.5);
        return Ok(empty{});
    }
};
struct PhaseDecorationDescriptor {
    using State = PhaseDecoration;
    state::Value<bool> active;
    auto create() const -> State { return {active}; }
    void update(State &state, cx::BehaviorContext &) const { state.active = active; }
};

auto card(cx::Element value, cx::NodeRef target) -> cx::Element {
    return rstd::move(value).style(NodeStyleBuilder{}.width(234).height(158)
        .background(theme::surface).radius(12).build()).gap(9).padding(18).reference(rstd::move(target));
}

[[vui::compose]]
auto Screen() -> cx::ComposeResult<cx::Element> {
    auto phase_state = cx::state(false);
    if (phase_state.is_err()) return Err(phase_state.unwrap_err());
    auto phase = phase_state.unwrap();
    auto theme_state = cx::state(false);
    if (theme_state.is_err()) return Err(theme_state.unwrap_err());
    auto theme_choice = theme_state.unwrap();
    auto theme_value = cx::read(theme_choice.value());
    if (theme_value.is_err()) return Err(theme_value.unwrap_err());
    auto accent = theme_value.unwrap().value() ? Color::from_srgba8(117, 76, 157).unwrap() : theme::accent;
    auto dialog_state = cx::state(false);
    if (dialog_state.is_err()) return Err(dialog_state.unwrap_err());
    auto dialog_shown = dialog_state.unwrap();
    auto dialog_value = cx::read(dialog_shown.value());
    if (dialog_value.is_err()) return Err(dialog_value.unwrap_err());
    auto moving_saved = cx::remember<cx::MovableContent>([] { return MakeMovingBadge(); });
    if (moving_saved.is_err()) return Err(moving_saved.unwrap_err());
    auto moving = moving_saved.unwrap().get();
    if (moving.is_err()) return Err(moving.unwrap_err());
    auto move_state = cx::state(false);
    if (move_state.is_err()) return Err(move_state.unwrap_err());
    auto moved = move_state.unwrap();
    auto move_value = cx::read(moved.value());
    if (move_value.is_err()) return Err(move_value.unwrap_err());
    bool is_moved = move_value.unwrap().value();
    auto saved_order = cx::state(false);
    if (saved_order.is_err()) return Err(saved_order.unwrap_err());
    auto order = saved_order.unwrap();
    auto saved_shown = cx::state(true);
    if (saved_shown.is_err()) return Err(saved_shown.unwrap_err());
    auto shown = saved_shown.unwrap();
    auto reverse = cx::read(order.value());
    if (reverse.is_err()) return Err(reverse.unwrap_err());
    bool reversed = reverse.unwrap().value();
    auto visible = cx::read(shown.value());
    if (visible.is_err()) return Err(visible.unwrap_err());
    bool is_visible = visible.unwrap().value();

    Vec<cx::Element> repeated;
    for (int id = 1; id <= 2; ++id) {
        auto saved = cx::node(cx::Key{Serial(id)});
        if (saved.is_err()) return Err(saved.unwrap_err());
        auto target = saved.unwrap();
        auto item = Counter(id, target);
        if (item.is_err()) return Err(item.unwrap_err());
        repeated.push(cx::stack({card(item.unwrap(), target)})
            .behavior(cx::Key{90}, PhaseDecorationDescriptor{phase.value()}));
    }
    Vec<cx::Element> keyed;
    for (int position = 0; position < 3; ++position) {
        auto id = reversed ? 5 - position : 3 + position;
        if (id == 5 && !is_visible) continue;
        auto entered = cx::group(cx::Key{Serial(id)});
        if (entered.is_err()) return Err(entered.unwrap_err());
        auto group = entered.unwrap();
        auto saved = cx::node();
        if (saved.is_err()) return Err(saved.unwrap_err());
        auto target = saved.unwrap();
        auto item = Counter(id, target);
        if (item.is_err()) return Err(item.unwrap_err());
        keyed.push(card(item.unwrap().key(cx::Key{Serial(id)}), target));
    }
    auto reverse_button = cx::button(theme::button_label("Reverse"_str, theme::surface), [order] {
        return order.update([](bool current) { return !current; });
    }, theme::button_style(true)).style(NodeStyleBuilder{}.width(112).height(36).build());
    auto toggle_button = cx::button(theme::button_label(is_visible ? "Hide 05"_str : "Show 05"_str), [shown] {
        return shown.update([](bool current) { return !current; });
    }, theme::button_style()).style(NodeStyleBuilder{}.width(112).height(36).build());
    auto phase_button = cx::button(theme::button_label("Shift"_str), [phase] {
        return phase.update([](bool current) { return !current; });
    }, theme::button_style()).style(NodeStyleBuilder{}.width(80).height(34).build());
    auto theme_button = cx::button(theme::button_label("Accent"_str), [theme_choice] {
        return theme_choice.update([](bool current) { return !current; });
    }, theme::button_style()).style(NodeStyleBuilder{}.width(80).height(34).build());
    auto appearance = cx::column({label("Appearance"_str, 12, theme::muted),
        cx::native(LayoutKind::Leaf).style(examples::block(172, 40, accent, 8)),
        cx::row({rstd::move(phase_button), rstd::move(theme_button)}).gap(12)})
        .style(examples::block(234, 158, theme::surface, 12)).gap(9).padding(18);
    auto mobile = moving.unwrap()->operator()();
    auto destinations = cx::row({
        cx::stack({is_moved ? cx::fragment() : mobile})
            .style(examples::block(144, 52, theme::canvas, 8)).padding(8).key(cx::Key{1}),
        cx::column({is_moved ? mobile : cx::fragment()})
            .style(examples::block(144, 52, theme::canvas, 8)).padding(8).key(cx::Key{2}),
    }).gap(16);
    auto move_button = cx::button(theme::button_label("Move"_str), [moved] {
        return moved.update([](bool current) { return !current; });
    }, theme::button_style()).style(NodeStyleBuilder{}.width(112).height(36).build());
    auto open = cx::button(theme::button_label("Open dialog"_str), [dialog_shown] { return dialog_shown.set(true); },
        theme::button_style()).style(NodeStyleBuilder{}.width(124).height(36).vertical(Alignment::Center).build());
    Vec<cx::Element> overlays;
    if (dialog_value.unwrap().value()) {
        auto close = cx::button(theme::button_label("Close"_str, theme::surface),
            [dialog_shown] { return dialog_shown.set(false); }, theme::button_style(true))
            .style(NodeStyleBuilder{}.width(112).height(36).build());
        auto panel = cx::column({theme::label("Dialog"_str, 22, theme::ink, true), rstd::move(close)})
            .style(examples::block(320, 152, theme::surface, 12)).padding(24).gap(24);
        overlays.push(cx::dialog(rstd::move(panel), [dialog_shown](PopupCloseReason) { return dialog_shown.set(false); }));
    }
    auto section_style = NodeStyleBuilder{}.width(486).vertical(Alignment::Center).hit_test_visible(false).build();
    auto body = cx::column({
        cx::row({Title("vui composition"_str).style(NodeStyleBuilder{}.width(598).build()), rstd::move(open)}).gap(12),
        cx::row({cx::row(rstd::move(repeated)).gap(16), rstd::move(appearance)}).gap(16),
        cx::column({cx::row({theme::label("Reorder"_str, 18, theme::ink, true).style(section_style),
                rstd::move(reverse_button), rstd::move(toggle_button)}).gap(12),
            cx::row(rstd::move(keyed)).gap(16)}).gap(10),
        cx::column({cx::row({theme::label("Movable content"_str, 18, theme::ink, true)
                .style(NodeStyleBuilder{}.width(570).vertical(Alignment::Center).hit_test_visible(false).build()),
                rstd::move(move_button)}).gap(12), rstd::move(destinations)})
            .style(examples::block(734, 140, theme::surface, 12)).padding(20).gap(12),
    }).style(NodeStyleBuilder{}.width(800).height(720).build()).gap(24).padding(32);
    return Ok(cx::provide({theme::local_accent.provides(accent)}, cx::popup_host(rstd::move(body), rstd::move(overlays))));
}
} // namespace composition_example
