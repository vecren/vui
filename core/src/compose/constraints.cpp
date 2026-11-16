module vui.compose;
import :constraints;
using namespace rstd::prelude;

namespace vui::compose::compiler {
struct ConstraintsTag {};
struct ConstraintContentTag {};
struct ConstraintBoundary {
    Option<SubcompositionLease> lease;
};
struct ConstraintParameters final : ComponentParameters {
    rstd::rc::Rc<ConstraintBody> body;
    Option<LayoutConstraints> constraints;
    ConstraintParameters(rstd::rc::Rc<ConstraintBody> value, Option<LayoutConstraints> limits = {})
        : body(rstd::move(value)), constraints(limits) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<ConstraintParameters>(); }
    auto equal(const ComponentParameters &other) const -> bool override {
        if (other.type() != type()) return false;
        const auto &value = static_cast<const ConstraintParameters &>(other);
        return body.get() == value.body.get() && constraints == value.constraints;
    }
};

struct ConstraintBoundaryElement {
    rstd::rc::Rc<ConstraintBoundary> owner;
    State<Option<SubcompositionRequest>> state;
    auto prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef> {
        auto node = previous.is_some() ? context.edit().existing(*previous) : context.edit().create(LayoutKind::Stack, style);
        context.style(node, style);
        auto target = context.target();
        context.on_mount([owner = owner, state = state, target](CommittedContext &committed) mutable -> ComposeResult<EffectLease> {
            auto id = target.id();
            if (id.is_err()) return Err(id.unwrap_err());
            auto bound = committed.session().document().bind_subcomposition(id.unwrap(),
                [state](SubcompositionRequest request) -> Result<empty, UiError> {
                    auto written = state.set(Some(request));
                    if (written.is_err()) return Err(written.unwrap_err().stage == ComposeStage::Closed
                        ? UiError::Cancelled : UiError::InvalidValue);
                    return Ok(empty{});
                });
            if (bound.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(bound.unwrap_err())));
            owner.get()->lease = Some(bound.unwrap());
            return Ok(EffectLease([owner]() mutable { owner.get()->lease = None(); }));
        });
        return Ok(node);
    }
};

auto constraint_element(Key key, rstd::rc::Rc<ConstraintBody> body, rstd::source_location source) -> Element {
    auto parameters = rstd::rc::Rc<ComponentParameters>(new ConstraintParameters(body));
    return component<ConstraintsTag>(rstd::move(key),
        [body = rstd::move(body), source](CompositionContext &context) mutable -> ComposeResult<Element> {
            auto requested = context.state<Option<SubcompositionRequest>>(Key{1}, None());
            if (requested.is_err()) return Err(requested.unwrap_err());
            auto state = requested.unwrap();
            auto boundary = context.remember<rstd::rc::Rc<ConstraintBoundary>>(Key{2}, []() -> ComposeResult<rstd::rc::Rc<ConstraintBoundary>> {
                return Ok(rstd::rc::make_rc<ConstraintBoundary>());
            });
            if (boundary.is_err()) return Err(boundary.unwrap_err());
            auto opened = boundary.unwrap().get();
            if (opened.is_err()) return Err(opened.unwrap_err());
            auto owner = opened.unwrap()->clone();
            auto observed = context.read(state.value());
            if (observed.is_err()) return Err(observed.unwrap_err());
            auto request = observed.unwrap().value();
            Vec<Element> children;
            if (request.is_some()) {
                children.push(component<ConstraintContentTag>(Key{1},
                    [body, constraints = request->constraints](CompositionContext &child) mutable {
                        return body.get()->invoke->operator()(child, constraints);
                    }, source, rstd::rc::Rc<ComponentParameters>(new ConstraintParameters(body, Some(request->constraints)))));
                context.on_commit([owner, request = *request](CommittedContext &committed) -> ComposeResult<empty> {
                    if (owner->lease.is_none()) return Err(ComposeError(ComposeStage::Action, Some(UiError::Cancelled)));
                    auto completed = committed.session().document().complete_subcomposition(*owner->lease, request);
                    if (completed.is_err()) return Err(ComposeError(ComposeStage::Action, Some(completed.unwrap_err())));
                    return Ok(empty{});
                });
            }
            return Ok(element(ConstraintBoundaryElement{owner, state}, rstd::move(children), source));
        }, source, rstd::move(parameters));
}
}
