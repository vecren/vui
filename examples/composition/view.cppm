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
        paint.rect({18, size.height - 8, size.width - 36, 3},
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

template <class F>
auto Content([[vui::content]] F content) {
    return content();
}

auto card(cx::Element value, cx::NodeRef target) -> cx::Element {
    return rstd::move(value).style(NodeStyleBuilder{}.width(224).height(158)
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
    auto reverse_button = cx::button(theme::button_label("反转顺序"_str, theme::surface), [order] {
        return order.update([](bool current) { return !current; });
    }, theme::button_style(true)).style(NodeStyleBuilder{}.width(144).height(40).build());
    auto toggle_button = cx::button(theme::button_label(is_visible ? "移除计数器 05"_str : "恢复计数器 05"_str), [shown] {
        return shown.update([](bool current) { return !current; });
    }, theme::button_style()).style(NodeStyleBuilder{}.width(176).height(40).build());
    auto phase_button = cx::button(theme::button_label("切换位置与强调色"_str), [phase] {
        return phase.update([](bool current) { return !current; });
    }, theme::button_style()).style(NodeStyleBuilder{}.width(176).height(40).build());
    auto theme_button = cx::button(theme::button_label("切换主题色"_str), [theme_choice] {
        return theme_choice.update([](bool current) { return !current; });
    }, theme::button_style()).style(NodeStyleBuilder{}.width(144).height(40).build());
    auto extra = [] [[vui::compose]] { return Badge(6); };
    auto saved_badge = cx::node();
    if (saved_badge.is_err()) return Err(saved_badge.unwrap_err());
    auto badge_target = saved_badge.unwrap();
    auto badge = Badge(1).key(cx::Key{Serial(7)}).reference(badge_target);
    if (!reversed) badge = rstd::move(badge).padding(2);
    auto observed = cx::effect(cx::EffectKeys{cx::Key{Serial(reversed)}},
        [badge_target, reversed](cx::CommittedContext &context) -> cx::ComposeResult<cx::EffectLease> {
            auto resolved = context.resolve(badge_target);
            if (resolved.is_err()) return Err(resolved.unwrap_err());
            auto node = resolved.unwrap();
            auto configured = context.session().document().base_style(node);
            if (configured.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(configured.unwrap_err())));
            rstd::io::eprintln("composition badge reversed={} slot={} generation={} padding={}",
                reversed, node.slot, node.generation, configured.unwrap().layout.padding);
            return Ok(cx::EffectLease{});
        });
    if (observed.is_err()) return Err(observed.unwrap_err());
    auto stored_content = MakeContent(9);
    auto stored = stored_content();
    if (stored.is_err()) return Err(stored.unwrap_err());
    auto remembered = cx::remember<String>(cx::EffectKeys{cx::Key{Serial(reversed)}}, [reversed] {
        rstd::io::eprintln("composition remembered reversed={}", reversed);
        return String::make(reversed ? "顺序已反转。移除再恢复的卡片会从 0 开始。"_str
            : "点击 + / - 改变计数，试试重排与移除卡片。"_str);
    });
    if (remembered.is_err()) return Err(remembered.unwrap_err());
    auto caption = remembered.unwrap().get();
    if (caption.is_err()) return Err(caption.unwrap_err());
    auto title = rstd::format("vui {}", "composition"_str);
    auto owned = rstd::format("{}", "ok"_str);
    int samples[]{1, 2, 3};
    return Ok(cx::provide({theme::local_accent.provides(accent)}, cx::column({
        cx::column({Title(title.as_str()),
            label("每张卡片独立计数，移动位置也能保留。"_str, 14, theme::muted)}).gap(8),
        cx::column({theme::label("独立计数"_str, 18, theme::ink, true),
            cx::row(rstd::move(repeated)).gap(16)}).gap(10),
        cx::column({theme::label("试着改变顺序"_str, 18, theme::ink, true),
            cx::row(rstd::move(keyed)).gap(16)}).gap(10),
        cx::row({rstd::move(reverse_button), rstd::move(toggle_button), rstd::move(phase_button), rstd::move(theme_button)}).gap(12),
        label(caption.unwrap()->as_str(), 13, theme::muted),
        cx::row({label("内容样本"_str, 12, theme::muted)
                .style(NodeStyleBuilder{}.vertical(Alignment::Center).hit_test_visible(false).build()),
            rstd::move(badge), Badge(6).key(cx::Key{Serial(7)}),
            Content([] { return Badge(2); }), extra(), stored.unwrap(),
            OwnedBadge(owned), SampleBadge(slice<int>::from_raw_parts(samples, usize(3)))}).gap(8),
    }).style(NodeStyleBuilder{}.width(800).height(720).build()).gap(18).padding(32)));
}
} // namespace composition_example
