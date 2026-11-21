export module vui.examples.gallery:controls;
import :style;
import :parts;
import :glass;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
namespace {
struct PowerLayout {
    static auto label_width(Scalar width) -> Scalar { return rstd::min(Scalar(78), width * .28); }
    static auto value_width(Scalar width) -> Scalar { return rstd::min(Scalar(60), width * .23); }
    static auto slider_width(Scalar width) -> Scalar { return width - label_width(width) - value_width(width); }

    auto measure(LayoutMeasureContext &context, LayoutConstraints limits) -> Result<Size, UiError> {
        const auto width = limits.max_width.unwrap_or(240);
        const auto height = limits.max_height.unwrap_or(44);
        const Scalar widths[]{label_width(width), slider_width(width), value_width(width)};
        for (unsigned i{}; i < 3; ++i) {
            auto child = context.measure_child(usize(i), {Some(Scalar(widths[i])), Some(Scalar(height))});
            if (child.is_err()) return Err(child.unwrap_err());
        }
        return Ok(Size{width, height});
    }

    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        const Scalar widths[]{label_width(size.width), slider_width(size.width), value_width(size.width)};
        Scalar x{};
        for (unsigned i{}; i < 3; ++i) {
            auto child = context.arrange_child(usize(i), {x, 0, widths[i], size.height});
            if (child.is_err()) return child;
            x += widths[i];
        }
        return Ok(empty{});
    }
};

struct PowerPaint {
    UiSession &session;

    auto paint(PaintContext &context) -> Result<empty, UiError> {
        auto state = Slider::status(session.document(), context.id());
        if (state.is_err()) return Err(state.unwrap_err());
        const auto size = context.size();
        const auto radius = rstd::min(Scalar(10), rstd::min(size.width, size.height) / 2);
        if (radius <= 0) return Ok(empty{});
        const auto x = radius + (size.width - 2 * radius) * state->ratio;
        const auto y = size.height / 2;
        const auto thickness = rstd::min(Scalar(6), radius);
        context.rect({0, y - thickness / 2, size.width, thickness}, selected_surface, thickness / 2);
        context.rect({0, y - thickness / 2, x, thickness}, muted, thickness / 2);
        if (session.input().focused() == Some(context.id())) {
            context.rect({x - radius, y - radius, 2 * radius, 2 * radius}, foreground, radius);
            const auto inset = radius * .88;
            context.rect({x - inset, y - inset, 2 * inset, 2 * inset}, panel_surface, inset);
        }
        const auto grip = radius * .7;
        context.rect({x - grip, y - grip, 2 * grip, 2 * grip}, foreground, grip);
        return Ok(empty{});
    }
};

struct InputLayout {
    static auto count_width(Scalar width) -> Scalar { return rstd::min(Scalar(92), width * .36); }
    static auto gap(Scalar width) -> Scalar { return rstd::min(Scalar(10), width * .06); }

    auto measure(LayoutMeasureContext &context, LayoutConstraints limits) -> Result<Size, UiError> {
        const auto width = limits.max_width.unwrap_or(240);
        const auto count = count_width(width);
        auto field = context.measure_child(usize(0), {Some(width - count - gap(width)), limits.max_height});
        if (field.is_err()) return Err(field.unwrap_err());
        auto button = context.measure_child(usize(1), {Some(Scalar(count)), limits.max_height});
        if (button.is_err()) return Err(button.unwrap_err());
        return Ok(Size{width, rstd::cmp::max(field->height, button->height)});
    }

    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        const auto count = count_width(size.width);
        const auto field = size.width - count - gap(size.width);
        auto placed = context.arrange_child(usize(0), {0, 0, field, size.height});
        if (placed.is_err()) return placed;
        return context.arrange_child(usize(1), {size.width - count, 0, count, size.height});
    }
};

auto mount_power(UiSession &session, UiId parent) -> Result<empty, UiError> {
    auto &ui = session.document();
    auto deck = ui.append<CustomLayout>(parent, panel().height(64).build());
    if (deck.is_err()) return Err(deck.unwrap_err());
    auto layout = ui.install_layout(*deck, PowerLayout{});
    if (layout.is_err()) return layout;
    auto glass_paint = ui.install_paint(*deck, GlassPaint{});
    if (glass_paint.is_err()) return glass_paint;
    auto name = append_label(ui, *deck, "Power"_str, label_style().build(), text_format(16, true), foreground);
    if (name.is_err()) return Err(name.unwrap_err());

    auto slider = Slider::create(session, fill().build(), {.maximum = 100, .step = 5});
    if (slider.is_err()) return Err(slider.unwrap_err().ui.unwrap_or(UiError::InvalidValue));
    auto control = slider.unwrap();
    auto attached = ui.attach(*deck, control);
    if (attached.is_err()) return attached;

    auto value = append_label(ui, *deck, "60%"_str, label_style().horizontal(Alignment::End).build(),
                              text_format(16, true), foreground);
    if (value.is_err()) return Err(value.unwrap_err());
    auto initialized = Slider::set_value(ui, control, 60);
    if (initialized.is_err()) return initialized;
    auto paint = ui.install_paint(control, PowerPaint{session});
    if (paint.is_err()) return paint;
    auto changed = Slider::on_changed(ui, control, [&ui, control, text = value.unwrap()](Scalar value) {
        auto number = rstd::format("{}%", unsigned(value));
        ui.set_text(text, number.as_str()).unwrap();
        ui.invalidate_paint(control).unwrap();
    });
    if (changed.is_err()) return Err(changed.unwrap_err().ui.unwrap_or(UiError::InvalidValue));

    auto invalidate = [](EventContext &context, const RoutedEvent &event) {
        context.document().invalidate_paint(event.current).unwrap();
    };
    auto focused = session.input().on(control, EventType::FocusGained, invalidate);
    if (focused.is_err()) return Err(UiError::InvalidValue);
    auto blurred = session.input().on(control, EventType::FocusLost, invalidate);
    if (blurred.is_err()) return Err(UiError::InvalidValue);
    return ui.attach_state(control, changed.unwrap(), NodeStateKey::of<Binding>());
}

auto mount_text_field(UiSession &session, UiId outline) -> Result<empty, UiError> {
    auto &ui = session.document();
    TextFieldStyle appearance;
    appearance.format = text_format(16);
    appearance.text = foreground;
    appearance.placeholder = muted;
    appearance.caret = foreground;
    appearance.composition = muted;
    appearance.selection = selected_surface;
    auto style = NodeStyleBuilder{}.horizontal(Alignment::Stretch).height(42).padding(8)
        .background(field_surface).radius(7).build();
    auto field = TextField::create(session, style, ""_str, appearance, {}, "Type here"_str);
    if (field.is_err()) return Err(UiError::InvalidValue);
    auto id = field.unwrap();
    auto attached = ui.attach(outline, id);
    if (attached.is_err()) return attached;
    auto border = [outline](EventContext &context, const RoutedEvent &event) {
        auto color = event.input.type == EventType::FocusGained ? muted : edge;
        context.document().set_background(outline, color, 8).unwrap();
    };
    if (session.input().on(id, EventType::FocusGained, border).is_err() ||
        session.input().on(id, EventType::FocusLost, border).is_err()) return Err(UiError::InvalidValue);
    return Ok(empty{});
}

auto effects_switch(bool checked, auto on_change) -> cx::Element {
    auto thumb = cx::native(LayoutKind::Leaf).style(NodeStyleBuilder{}.width(26).height(26)
        .horizontal(checked ? Alignment::End : Alignment::Start).vertical(Alignment::Center)
        .background(foreground).radius(13).hit_test_visible(false).build());
    return control_finish(cx::button(rstd::move(thumb),
        [on_change, checked] { return on_change(!checked); },
        ButtonStyleBuilder{button_style(checked)}.radius(20).build(), {.second_key = Key::Space})
        .style(NodeStyleBuilder{}.width(74).height(40).padding(6).horizontal(Alignment::End)
            .vertical(Alignment::Center).build()), false);
}
} // namespace

[[vui::compose]]
auto counter() -> cx::ComposeResult<cx::Element> {
    auto state = cx::state(0);
    if (state.is_err()) return Err(state.unwrap_err());
    auto count = state.unwrap();
    auto value = cx::read(count.value());
    if (value.is_err()) return Err(value.unwrap_err());
    auto title = rstd::format("Count {}", value->value());
    return Ok(control_finish(cx::button(button_label(title.as_str()),
        [count] { return count.update([](int previous) { return previous + 1; }); }, button_style())
        .style(fill().height(44).build())));
}

[[vui::compose]]
auto controls(cx::Borrowed<state::Cell<bool>> dialog_open, Scalar width) -> cx::ComposeResult<cx::Element> {
    const auto interior = rstd::cmp::max(Scalar(0), width - 2 * panel_padding);
    const auto compact = interior < 390;

    auto effects_state = cx::state(true);
    if (effects_state.is_err()) return Err(effects_state.unwrap_err());
    auto effects = effects_state.unwrap();
    auto effects_value = cx::read(effects.value());
    if (effects_value.is_err()) return Err(effects_value.unwrap_err());

    auto difficulty_state = cx::state(1u);
    if (difficulty_state.is_err()) return Err(difficulty_state.unwrap_err());
    auto difficulty = difficulty_state.unwrap();
    auto difficulty_value = cx::read(difficulty.value());
    if (difficulty_value.is_err()) return Err(difficulty_value.unwrap_err());

    const auto action_width = rstd::cmp::max(Scalar(0), (interior - 8) / 2);
    auto actions = cx::row({
        control_finish(cx::button(button_label("Open dialog"_str),
            [dialog_open] { dialog_open->set(true).unwrap(); }, button_style(true))
            .style(NodeStyleBuilder{}.width(rstd::min(Scalar(156), action_width)).height(44).build())),
        cx::button(button_label("Disabled"_str, muted), [] {},
            ButtonStyleBuilder{button_style()}.normal(field_surface).build())
            .style(NodeStyleBuilder{}.width(rstd::min(Scalar(126), action_width)).height(44).enabled(false).build()),
    }).gap(8);

    auto toggle = effects_switch(effects_value->value(), [effects](bool next) { return effects.set(next); });
    auto switch_row = cx::stack({label("Effects"_str, 16, foreground, true), rstd::move(toggle)})
        .style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).height(44).build());

    const ref<str> choices[]{"Easy"_str, "Normal"_str, "Hard"_str};
    const auto segment = rstd::min(Scalar(84), rstd::cmp::max(Scalar(0), interior - 8) / 3);
    Vec<cx::Element> modes;
    for (unsigned i{}; i < 3; ++i) {
        auto active = difficulty_value->value() == i;
        modes.push(control_finish(cx::button(button_label(choices[i]),
            [difficulty, i] { return difficulty.set(i); }, button_style(active))
            .style(NodeStyleBuilder{}.height(44).width(segment).build())).key(cx::Key{i + 1}));
    }
    auto selector = cx::row(rstd::move(modes))
        .style(NodeStyleBuilder{}.horizontal(compact ? Alignment::Start : Alignment::End).build()).gap(4);
    auto difficulty_label = label("Difficulty"_str, 16, foreground, true);
    auto options = compact
        ? cx::column({rstd::move(difficulty_label), rstd::move(selector)}).gap(8)
        : cx::stack({rstd::move(difficulty_label), rstd::move(selector)})
            .style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).height(44).build());
    auto count = counter();
    if (count.is_err()) return Err(count.unwrap_err());
    auto outline = native_part(mount_text_field).style(NodeStyleBuilder{}.horizontal(Alignment::Stretch)
        .background(edge).radius(8).padding(1).build());
    auto input = layout_part(InputLayout{}, cx::elements({rstd::move(outline), count.unwrap()}))
        .style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).hit_self(false).build());
    auto settings = glass(LayoutKind::Column, cx::elements({rstd::move(input), divider(), rstd::move(actions), divider(),
        rstd::move(switch_row), divider(), rstd::move(options)})).style(panel().build()).gap(12);
    auto content = cx::column({native_part(mount_power).style(NodeStyleBuilder{}.horizontal(Alignment::Stretch)
        .hit_self(false).build()), rstd::move(settings)})
        .style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).hit_self(false).build()).gap(12);
    return Ok(cx::scroll_view(rstd::move(content), touch_scroll()).style(fill().build()));
}

} // namespace examples::gallery::detail
