module vui;
import :document.interface;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
void UiDocument::sweep_bindings() {
    for (auto id : bound_nodes_) {
        auto value = resolve(id);
        if (!value)
            continue;
        auto &node = *value;
        if (node.layout_binding.is_some() && !node.layout_binding->alive()) {
            node.layout_strategy = None();
            node.layout_binding = None();
            changed(true, Some(id));
        }
        if (node.paint_binding.is_some() && !node.paint_binding->alive()) {
            node.painter = None();
            node.paint_binding = None();
            node.paint_dirty = true;
            changed(false);
        }
    }
    bound_nodes_.retain([&](UiId id) {
        auto node = resolve(id);
        return node && (node->layout_binding.is_some() || node->paint_binding.is_some());
    });
}

void UiDocument::track_binding(UiId id) {
    for (auto node : bound_nodes_)
        if (node == id)
            return;
    bound_nodes_.push(UiId(id));
}

auto UiDocument::acquire_read() -> Result<ReadScope, UiError> {
    if (!can_route()) return Err(UiError::Routing);
    evaluating_ = true; return Ok(ReadScope(*this));
}

UiDocument::~UiDocument() { bindings_.close(); }

auto UiDocument::acquire_route() -> Result<RouteScope, UiError> {
    if (!can_route())
        return Err(UiError::Routing);
    routing_ = true;
    return Ok(RouteScope(*this));
}

auto UiDocument::cancel_mutations() -> Result<Vec<MutationResult>, UiError> {
    if (routing_ || draining_ || animation_dispatching_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto pending = rstd::exchange(mutations_, Vec<Box<Mutation>>{});
    Vec<MutationResult> results;
    for (const auto &mutation : pending)
        results.push({mutation->id, Some(UiError::Cancelled)});
    return Ok(rstd::move(results));
}

auto UiDocument::flush_mutations() -> Result<Vec<MutationResult>, UiError> {
    if (routing_ || draining_ || animation_dispatching_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    draining_ = true;
    auto pending = rstd::exchange(mutations_, Vec<Box<Mutation>>{});
    Vec<MutationResult> results;
    for (auto &mutation : pending) {
        auto result = mutation->apply(*this);
        results.push(
            {mutation->id, result.is_err() ? Some(result.unwrap_err()) : None<UiError>()});
    }
    draining_ = false;
    return Ok(rstd::move(results));
}
} // namespace vui
