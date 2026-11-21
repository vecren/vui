module vui.compose;
import :controls;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui::compose {
auto ButtonElement::prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef> {
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

auto ScrollElement::prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef> {
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

auto scroll_view(Element content, ScrollStyle scroll, rstd::source_location source) -> Element {
    Vec<Element> children; children.push(rstd::move(content));
    auto viewport = native(LayoutKind::Viewport, rstd::move(children));
    Vec<Element> nodes; nodes.push(rstd::move(viewport));
    return element(ScrollElement{scroll}, rstd::move(nodes), source);
}

auto ScrollbarElement::prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef> {
    if (!options.valid()) return Err(ComposeError(ComposeStage::Prepare, Some(UiError::InvalidValue)));
    style.focusable = true;
    const auto created = previous.is_none();
    auto node = created ? context.edit().create(LayoutKind::Stack, style) : context.edit().existing(*previous);
    if (auto before = context.previous_style(); before.is_some()) {
        before->focusable = true;
        context.edit().style(node, NodeStylePatch::difference(*before, style));
    } else context.edit().style(node, style);
    auto thumb = rstd::rc::make_rc<UiId>();
    context.on_prepared([node, thumb, created, options = options, &session = context.session()]
        (UiDocument::PreparedEdit &edit) mutable -> ComposeResult<empty> {
        auto children = edit.children(node);
        if (children.len() != usize(1)) return Err(ComposeError(ComposeStage::Prepare, Some(UiError::MissingChild)));
        *thumb.get() = children[usize()];
        if (created) {
            auto stored = edit.state(node, ScrollbarState(session, *thumb, options));
            if (stored.is_err()) return Err(ComposeError(ComposeStage::Prepare, Some(stored.unwrap_err())));
        }
        return Ok(empty{});
    });
    context.behavior<ScrollbarState>(node);
    auto target = context.target();
    context.on_commit([target, thumb, created, options = options](CommittedContext &committed) -> ComposeResult<empty> {
        auto id = committed.resolve(target);
        if (id.is_err()) return Err(id.unwrap_err());
        auto &ui = committed.session().document();
        auto state = ui.state<ScrollbarState>(*id);
        if (!state) return Err(ComposeError(ComposeStage::Effect, Some(UiError::InvalidId)));
        auto configured = created ? state->connect(*id) : state->configure(*id, *thumb, options);
        if (configured.is_err()) {
            auto failure = configured.unwrap_err();
            ComposeError error(ComposeStage::Effect, failure.ui);
            error.input = failure.input;
            return Err(error);
        }
        return Ok(empty{});
    });
    return Ok(node);
}

auto scrollbar(Element thumb, ScrollbarOptions options, rstd::source_location source) -> Element {
    Vec<Element> children;
    children.push(rstd::move(thumb));
    return element(ScrollbarElement{options}, rstd::move(children), source);
}
} // namespace vui::compose
