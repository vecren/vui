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
    auto prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef> {
        auto runtime = context.runtime<ButtonClickSlot>([]() -> ComposeResult<rstd::rc::Rc<ButtonClickSlot>> {
            return Ok(rstd::rc::make_rc<ButtonClickSlot>());
        });
        if (runtime.is_err()) return Err(runtime.unwrap_err());
        auto slot = runtime.unwrap();
        auto prepared = Button::prepare(context.document(), context.edit(), previous, style, appearance, options, context.previous_style());
        if (prepared.is_err()) return Err(ComposeError(ComposeStage::Prepare, prepared.unwrap_err().ui));
        auto node = prepared.unwrap();
        if (context.created()) {
            slot.get()->signal = context.signal();
            auto weak = slot.downgrade();
            auto connected = Button::prepare_clicked(context.document(), context.edit(), node,
                [weak](EventContext &event, const ClickEvent &click) {
                    auto value = weak.upgrade();
                    if (value.is_none()) return;
                    auto owner = value->get()->signal.upgrade();
                    if (owner.is_none() || owner->get()->closed || !value->get()->callback) return;
                    auto callback = value->get()->callback.clone();
                    ActionContext context(event, click.target, value->get()->action.guard());
                    auto result = callback.get()->invoke->operator()(context, click);
                    if (result.is_err()) {
                        auto error = result.unwrap_err(); error.stage = ComposeStage::Action;
                        error.node = Some(UiId(click.target));
                        if (!error.source.line()) error.source = value->get()->source;
                        owner->get()->report(rstd::move(error), Some(Serial(value->get()->revision)));
                    }
                });
            if (connected.is_err()) return Err(ComposeError(ComposeStage::Prepare, connected.unwrap_err().ui));
            slot.get()->binding = Some(connected.unwrap()); context.behavior<ButtonState>(node);
        }
        auto action = context.document().bindings().make();
        context.on_commit([slot, callback = clicked, action = rstd::move(action), source = context.source()](CommittedContext &) mutable -> ComposeResult<empty> {
            slot.get()->action = rstd::move(action);
            slot.get()->source = source;
            auto owner = slot.get()->signal.upgrade();
            if (owner.is_some()) slot.get()->revision = owner->get()->committed_revision;
            slot.get()->callback = rstd::move(callback); return Ok(empty{});
        });
        return Ok(node);
    }
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
    auto prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef> {
        auto made = ScrollView::prepare(context.document(), context.edit(), previous, style, scroll, context.previous_style());
        if (made.is_err()) return Err(ComposeError(ComposeStage::Prepare, Some(made.unwrap_err())));
        auto prepared = made.unwrap();
        context.on_prepared([prepared, scroll = scroll](UiDocument::PreparedEdit &edit) -> ComposeResult<empty> {
            auto result = ScrollView::prepare_state(edit, prepared, scroll);
            return result.is_ok() ? ComposeResult<empty>(Ok(empty{})) : ComposeResult<empty>(Err(ComposeError(ComposeStage::Prepare, Some(result.unwrap_err()))));
        });
        context.behavior<ScrollState>(prepared.node);
        auto target = context.target();
        auto lifetime = context.document().lifetime(); auto document = &context.document();
        context.on_mount([target, lifetime, document](CommittedContext &context) -> ComposeResult<EffectLease> {
            auto id = context.resolve(target); if (id.is_err()) return Err(id.unwrap_err());
            auto connected = ScrollView::connect(context.session(), id.unwrap());
            if (connected.is_err()) return Err(ComposeError(ComposeStage::Effect, connected.unwrap_err().ui));
            return Ok(EffectLease([target, lifetime, document] {
                auto id = target.id();
                if (lifetime.alive() && id.is_ok() && document->valid(id.unwrap())) ScrollView::stop(*document, id.unwrap());
            }));
        });
        if (prepared.changed)
            context.on_commit([target](CommittedContext &context) -> ComposeResult<empty> {
                auto id = context.resolve(target); if (id.is_err()) return Err(id.unwrap_err());
                ScrollView::finish_configuration(context.session().document(), id.unwrap()); return Ok(empty{});
            });
        return Ok(prepared.node);
    }
};
inline auto scroll_view(Element content, ScrollStyle scroll = {}, rstd::source_location source = rstd::source_location::current()) -> Element {
    Vec<Element> children; children.push(rstd::move(content));
    auto viewport = native(LayoutKind::Viewport, rstd::move(children));
    Vec<Element> nodes; nodes.push(rstd::move(viewport));
    return element(ScrollElement{scroll}, rstd::move(nodes), source);
}
} // namespace vui::compose
