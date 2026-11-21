export module vui.compose:controls;
export import vui.controls;
import :runtime;
import :element;
using namespace rstd::prelude;

export namespace vui::compose {
class ActionContext {
    EventContext &native_;
    UiId target_;
    BindingGuard guard_;
  public:
    ActionContext(EventContext &context, UiId target, BindingGuard guard) : native_(context), target_(target), guard_(guard) {}
    ActionContext(const ActionContext &) = delete;
    auto native() -> EventContext & { return native_; }
    auto document() -> UiDocument & { return native_.document(); }
    auto guard() const -> BindingGuard { return guard_; }
    template <class F> auto defer(F callback) -> ComposeResult<Serial> {
        auto result = document().defer(target_, guard_, rstd::move(callback));
        return result.is_ok() ? ComposeResult<Serial>(Ok(result.unwrap()))
            : ComposeResult<Serial>(Err(ComposeError(ComposeStage::Action, Some(result.unwrap_err()))));
    }
};
struct ClickFunction {
    Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(ActionContext &, const ClickEvent &)>>> invoke;
    template <class F> explicit ClickFunction(F callback)
        : invoke(Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(ActionContext &, const ClickEvent &)>>>::make(
            [callback = rstd::move(callback)](ActionContext &context, const ClickEvent &event) mutable -> ComposeResult<empty> {
                if constexpr (requires { callback(context, event); }) {
                    if constexpr (rstd::mtp::same_as<decltype(callback(context, event)), void>) {
                        callback(context, event); return Ok(empty{});
                    } else return callback(context, event);
                } else {
                    if constexpr (rstd::mtp::same_as<decltype(callback()), void>) {
                        callback(); return Ok(empty{});
                    } else return callback();
                }
            })) {}
};
struct ButtonClickSlot {
    rstd::rc::Rc<ClickFunction> callback;
    rstd::rc::Weak<ComposeSignal> signal;
    Option<Binding> binding;
    Binding action;
    rstd::source_location source;
    Serial revision{};
};
struct ButtonElement {
    rstd::rc::Rc<ClickFunction> clicked;
    ButtonStyle appearance;
    ButtonOptions options;
    auto prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef>;
};
template <class F>
auto button(Element content, F clicked, ButtonStyle appearance = {}, ButtonOptions options = {}, rstd::source_location source = rstd::source_location::current()) -> Element {
    Vec<Element> children; children.push(rstd::move(content));
    return element(ButtonElement{rstd::rc::make_rc<ClickFunction>(rstd::move(clicked)), appearance, options}, rstd::move(children), source);
}
template <class F>
auto button(ref<str> title, F clicked, ButtonStyle appearance = {}, ButtonOptions options = {}, rstd::source_location source = rstd::source_location::current()) -> Element {
    return button(text(title).style(NodeStyleBuilder{}.hit_test_visible(false)
        .horizontal(Alignment::Center).vertical(Alignment::Center).build()), rstd::move(clicked), appearance, options, source);
}
struct ScrollElement {
    ScrollStyle scroll;
    auto prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef>;
};
auto scroll_view(Element content, ScrollStyle scroll = {}, rstd::source_location source = rstd::source_location::current()) -> Element;
struct ScrollbarElement {
    ScrollbarOptions options;
    auto prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef>;
};
auto scrollbar(Element thumb, ScrollbarOptions options = {}, rstd::source_location source = rstd::source_location::current()) -> Element;
} // namespace vui::compose
