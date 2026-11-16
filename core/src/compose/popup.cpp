module vui.compose;
import :popup;
using namespace rstd::prelude;

namespace vui::compose {
namespace {
struct PopupLayout {
    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError> {
        if (context.child_count() != usize(1)) return Err(UiError::InvalidValue);
        auto child = context.measure_child(usize(), {constraints.max_width, constraints.max_height});
        if (child.is_err()) return Err(child.unwrap_err());
        return Ok(Size{constraints.max_width.unwrap_or(child->width), constraints.max_height.unwrap_or(child->height)});
    }
    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        auto child = context.desired_child(usize());
        if (child.is_err()) return Err(child.unwrap_err());
        return context.arrange_child(usize(), {(size.width - child->width) / 2,
            (size.height - child->height) / 2, child->width, child->height});
    }
};
struct PopupSlot {
    rstd::rc::Rc<PopupFunction> callback;
    rstd::rc::Weak<ComposeSignal> signal;
    Option<vui::Popup> popup;
    Option<Binding> binding;
    vui::PopupOptions options;
    UiId root, content;
    rstd::source_location source;
    Serial revision{};
};
struct PopupCandidate { UiId root, content; };
auto popup_error(ControlError error) -> ComposeError {
    ComposeError result(ComposeStage::Effect, error.ui); result.input = error.input; return result;
}
auto options_for(const PopupOptions &options, UiId root, CommittedContext &context) -> ComposeResult<vui::PopupOptions> {
    vui::PopupOptions result;
    result.modal = options.modal; result.parent_scope = options.parent_scope;
    result.restore_focus = options.restore_focus; result.escape = options.escape;
    result.outside_press = options.outside_press; result.backdrop = options.backdrop;
    if (options.initial_focus.is_some()) {
        auto target = context.resolve(*options.initial_focus);
        if (target.is_err()) return Err(target.unwrap_err());
        result.initial_focus = Some(target.unwrap());
    } else if (options.modal) result.initial_focus = Some(root);
    return Ok(result);
}
}

auto PopupElement::prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style)
    -> ComposeResult<UiNodeRef> {
    auto made = context.runtime<PopupSlot>([]() -> ComposeResult<rstd::rc::Rc<PopupSlot>> {
        return Ok(rstd::rc::make_rc<PopupSlot>());
    });
    if (made.is_err()) return Err(made.unwrap_err());
    auto slot = made.unwrap();
    auto node = previous.is_some() ? context.edit().existing(*previous) : context.edit().create(LayoutKind::Custom, style);
    context.style(node, style);
    if (context.created()) context.edit().state(node, PopupLayout{});
    auto candidate = rstd::rc::make_rc<PopupCandidate>();
    context.on_prepared([candidate, node](UiDocument::PreparedEdit &edit) mutable -> ComposeResult<empty> {
        auto children = edit.children(node);
        if (children.len() != usize(1)) return Err(ComposeError(ComposeStage::Prepare, Some(UiError::InvalidValue)));
        candidate.get()->root = edit.id(node); candidate.get()->content = children[usize()];
        return Ok(empty{});
    });
    auto signal = context.signal();
    context.on_commit([slot, candidate, callback = callback, options = options, signal, source = context.source()]
        (CommittedContext &committed) mutable -> ComposeResult<empty> {
        slot.get()->callback = rstd::move(callback); slot.get()->signal = signal;
        slot.get()->source = source; slot.get()->root = candidate->root;
        auto owner = signal.upgrade();
        if (owner.is_some()) slot.get()->revision = owner->get()->committed_revision;
        auto configured = options_for(options, candidate->root, committed);
        if (configured.is_err()) return Err(configured.unwrap_err());
        auto values = configured.unwrap();
        if (slot->popup.is_some()) {
            if (slot->content != candidate->content) {
                auto changed = slot.get()->popup->set_content(candidate->content);
                if (changed.is_err()) return Err(popup_error(changed.unwrap_err()));
                slot.get()->content = candidate->content;
            }
            if (slot->options != values) {
                auto changed = slot.get()->popup->set_options(values);
                if (changed.is_err()) return Err(popup_error(changed.unwrap_err()));
            }
        }
        slot.get()->root = candidate->root; slot.get()->content = candidate->content;
        slot.get()->options = values;
        return Ok(empty{});
    });
    auto target = context.target();
    context.on_mount([slot, target](CommittedContext &committed) mutable -> ComposeResult<EffectLease> {
        auto root = committed.resolve(target);
        if (root.is_err()) return Err(root.unwrap_err());
        auto layout = committed.session().document().attach_layout<PopupLayout>(root.unwrap());
        if (layout.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(layout.unwrap_err())));
        auto bound = vui::Popup::bind(committed.session(), root.unwrap(), slot->content, slot->options);
        if (bound.is_err()) return Err(popup_error(bound.unwrap_err()));
        auto popup = bound.unwrap();
        auto binding = popup.on_close_requested([weak = slot.downgrade()](PopupCloseReason reason) {
            auto receiver = weak.upgrade();
            if (receiver.is_none()) return;
            auto slot = rstd::move(*receiver);
            auto owner = slot->signal.upgrade();
            if (owner.is_none() || owner->get()->closed || !slot->callback) return;
            auto callback = slot->callback.clone();
            auto result = callback.get()->invoke->operator()(reason);
            if (result.is_err()) {
                auto error = result.unwrap_err(); error.stage = ComposeStage::Action;
                error.node = Some(UiId(slot->root));
                if (!error.source.line()) error.source = slot->source;
                owner->get()->report(rstd::move(error), Some(Serial(slot->revision)));
            }
        });
        if (binding.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(UiError::Limit)));
        auto opened = popup.open();
        if (opened.is_err()) return Err(popup_error(opened.unwrap_err()));
        slot.get()->binding = Some(binding.unwrap()); slot.get()->popup = Some(rstd::move(popup));
        return Ok(EffectLease([weak = slot.downgrade()] {
            auto slot = weak.upgrade();
            if (slot.is_some()) { slot->get()->binding = None(); slot->get()->popup = None(); }
        }));
    });
    return Ok(node);
}

auto popup_host(Element body, Vec<Element> overlays, rstd::source_location source) -> Element {
    Vec<Element> children; children.push(rstd::move(body));
    for (auto &overlay : overlays) children.push(rstd::move(overlay));
    return stack(rstd::move(children), source).style(NodeStyleBuilder{}.hit_self(false).build());
}
}
