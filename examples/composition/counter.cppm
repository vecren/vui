export module vui.examples.composition.counter;
import vui.compose;
import vui.examples;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace composition_example {
using namespace vui;
namespace cx = vui::compose;
namespace theme = examples::theme;

auto label(ref<str> value, Scalar size, Color color = theme::ink) -> cx::Element {
    return theme::label(value, size, color);
}

[[vui::compose]]
auto Title(ref<str> value) -> cx::Element {
    return theme::label(value, 28, theme::ink, true);
}

auto badge_label(ref<str> value, Scalar width) -> cx::Element {
    return cx::stack({theme::button_label(value, theme::accent, 13)})
        .style(examples::block(width, 28, theme::tint, 8));
}

[[vui::compose]]
auto Badge(int value) -> cx::Element {
    auto title = rstd::format("{:02}", value);
    return badge_label(title.as_str(), 44);
}

[[vui::compose]]
auto OwnedBadge(const String &value) -> cx::Element {
    return badge_label(value.as_str(), 36);
}

[[vui::compose]]
auto SampleBadge(slice<int> values) -> cx::Element {
    int total{};
    for (int value : values) total += value;
    auto title = rstd::format("{}", total);
    return badge_label(title.as_str(), 36);
}

auto MakeContent(int id) -> cx::Content {
    return cx::Content([id] [[vui::compose]] () -> cx::ComposeResult<cx::Element> {
        return Ok(Badge(id));
    });
}

auto MakeIncrement(cx::State<int> count) -> cx::Action<cx::ComposeResult<empty>()> {
    return cx::Action<cx::ComposeResult<empty>()>{[count] {
        return count.update([](int current) { return current + 1; });
    }};
}

[[vui::compose]]
auto Counter(int id, cx::NodeRef target) -> cx::ComposeResult<cx::Element> {
    auto saved = cx::state(0);
    if (saved.is_err()) return Err(saved.unwrap_err());
    auto count = saved.unwrap();
    auto value = cx::read(count.value());
    if (value.is_err()) return Err(value.unwrap_err());
    auto mounted = cx::effect({}, [id, target](cx::CommittedContext &context) -> cx::ComposeResult<cx::EffectLease> {
        auto resolved = context.resolve(target);
        if (resolved.is_err()) return Err(resolved.unwrap_err());
        auto node = resolved.unwrap();
        auto configured = context.session().document().base_style(node);
        if (configured.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(configured.unwrap_err())));
        auto style = configured.unwrap();
        rstd::io::eprintln("composition mounted id={} slot={} generation={} padding={} gap={} width={} height={}",
            id, node.slot, node.generation, style.layout.padding, style.layout.gap,
            style.layout.width.unwrap_or(0), style.layout.height.unwrap_or(0));
        return Ok(cx::EffectLease([id, node] {
            rstd::io::eprintln("composition released id={} slot={} generation={}", id, node.slot, node.generation);
        }));
    });
    if (mounted.is_err()) return Err(mounted.unwrap_err());
    auto title = rstd::format("计数器 {:02}", id);
    auto number = rstd::format("{}", value.unwrap().value());
    auto minus = cx::button(theme::button_label("-"_str, theme::ink, 20), [count] {
        return count.update([](int current) { return current - 1; });
    }, theme::button_style()).style(NodeStyleBuilder{}.width(80).height(34).build());
    auto plus = cx::button(theme::button_label("+"_str, theme::surface, 20), MakeIncrement(count), theme::button_style(true))
        .style(NodeStyleBuilder{}.width(80).height(34).build());
    return Ok(cx::column({
        label(title.as_str(), 12, theme::muted),
        theme::label(number.as_str(), 32, theme::ink, true),
        cx::row({rstd::move(minus), rstd::move(plus)}).gap(12),
    }));
}
} // namespace composition_example
