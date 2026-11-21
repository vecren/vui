export module vui.compose:popup;
export import vui.controls;
import :runtime;
import :element;
using namespace rstd::prelude;

export namespace vui::compose {
struct PopupOptions {
    bool modal{true};
    Option<NodeRef> initial_focus;
    Option<ScopeId> parent_scope;
    RestoreFocus restore_focus{RestoreFocus::Previous};
    EscapePolicy escape{EscapePolicy::RequestClose};
    OutsidePressPolicy outside_press{OutsidePressPolicy::RequestClose};
    Color backdrop{0, 0, 0, .28f};
    Insets content_insets;
    PopupOptions() = default;
    PopupOptions(const PopupOptions &other)
        : modal(other.modal), initial_focus(other.initial_focus.is_some() ? Some(NodeRef(*other.initial_focus)) : None<NodeRef>()),
          parent_scope(other.parent_scope), restore_focus(other.restore_focus), escape(other.escape),
          outside_press(other.outside_press), backdrop(other.backdrop), content_insets(other.content_insets) {}
    PopupOptions(PopupOptions &&) = default;
    auto operator=(PopupOptions other) -> PopupOptions & {
        modal = other.modal; initial_focus = rstd::move(other.initial_focus); parent_scope = other.parent_scope;
        restore_focus = other.restore_focus; escape = other.escape; outside_press = other.outside_press;
        backdrop = other.backdrop; content_insets = other.content_insets; return *this;
    }
};
struct PopupFunction {
    Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(PopupCloseReason)>>> invoke;
    template <class F> explicit PopupFunction(F callback)
        : invoke(Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(PopupCloseReason)>>>::make(
            [callback = rstd::move(callback)](PopupCloseReason reason) mutable -> ComposeResult<empty> {
                if constexpr (rstd::mtp::same_as<decltype(callback(reason)), void>) {
                    callback(reason); return Ok(empty{});
                } else return callback(reason);
            })) {}
};
struct PopupElement {
    rstd::rc::Rc<PopupFunction> callback;
    PopupOptions options;
    auto prepare(PrepareContext &, Option<UiId>, NodeStyle) -> ComposeResult<UiNodeRef>;
};
template <class F>
auto popup(Element content, [[clang::annotate("vui.compose.deferred.v2")]] F on_close_requested,
           PopupOptions options = {}, rstd::source_location source = rstd::source_location::current()) -> Element {
    Vec<Element> children; children.push(rstd::move(content));
    auto style = NodeStyleBuilder{}.background(options.backdrop).visible(false).focusable(true)
        .hit_self(options.modal).horizontal(Alignment::Stretch).vertical(Alignment::Stretch).build();
    return element(PopupElement{rstd::rc::make_rc<PopupFunction>(rstd::move(on_close_requested)), rstd::move(options)},
                   rstd::move(children), source).style(style);
}
template <class F>
auto dialog(Element content, [[clang::annotate("vui.compose.deferred.v2")]] F on_close_requested,
            PopupOptions options = {}, rstd::source_location source = rstd::source_location::current()) -> Element {
    options.modal = true;
    return popup(rstd::move(content), rstd::move(on_close_requested), rstd::move(options), source);
}
auto popup_host(Element body, Vec<Element> overlays = {},
                rstd::source_location source = rstd::source_location::current()) -> Element;
inline auto popup_host(Element body, initializer_list<Element> overlays,
                       rstd::source_location source = rstd::source_location::current()) -> Element {
    return popup_host(rstd::move(body), elements(overlays), source);
}
}
