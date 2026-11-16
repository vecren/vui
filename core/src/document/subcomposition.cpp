module vui;
import :document.interface;
using namespace rstd::prelude;

namespace vui {
auto UiDocument::install_subcomposition(UiId id, rstd::rc::Rc<DocumentSubcomposition> state)
    -> Result<empty, UiError> {
    if (!can_route()) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (!accepts_children(node->kind)) return Err(UiError::InvalidValue);
    if (auto current = node->subcomposition.upgrade(); current.is_some() && current->get()->guard.alive())
        return Err(UiError::PropertyOwned);
    subcomposition_nodes_.retain([&](UiId target) {
        auto value = resolve(target);
        if (!value) return false;
        auto receiver = value->subcomposition.upgrade();
        return receiver.is_some() && receiver->get()->guard.alive();
    });
    bool known{};
    for (auto target : subcomposition_nodes_) known |= target == id;
    if (!known && subcomposition_nodes_.len() >= usize(4096)) return Err(UiError::Limit);
    state.get()->mount = node->mount_generation;
    node->subcomposition = state.downgrade();
    if (!known) subcomposition_nodes_.push(UiId(id));
    changed(true, Some(id));
    return Ok(empty{});
}

auto UiDocument::require_subcomposition(UiId id, LayoutConstraints constraints) -> Option<UiError> {
    auto &node = *resolve(id);
    auto receiver = node.subcomposition.upgrade();
    if (receiver.is_none() || !receiver->get()->guard.alive()) return None();
    auto &state = *receiver->get();
    if (state.mount != node.mount_generation) {
        state.mount = node.mount_generation;
        state.completed = None();
        state.request = None();
        state.dispatched = false;
    }
    if (state.completed == Some(constraints) && state.request.is_none()) return None();
    if (state.request.is_none() || state.request->constraints != constraints) {
        state.request = Some(SubcompositionRequest{id, new_session(), constraints});
        state.dispatched = false;
    }
    return Some(UiError::LayoutPending);
}

auto UiDocument::complete_subcomposition(const SubcompositionLease &lease, SubcompositionRequest request)
    -> Result<bool, UiError> {
    if (!can_route()) return Err(UiError::Routing);
    if (!lease.alive() || !lease.state_) return Err(UiError::Cancelled);
    auto node = resolve(request.target);
    if (!node) return Err(UiError::InvalidId);
    auto owner = lease.state_.clone();
    auto &state = *owner.get();
    auto current = node->subcomposition.upgrade();
    if (!lease.alive() || current.is_none() || current->get() != &state) return Err(UiError::Cancelled);
    if (state.mount != node->mount_generation || state.request != Some(request)) return Ok(false);
    state.completed = Some(request.constraints);
    state.request = None();
    state.dispatched = false;
    changed(true, Some(request.target));
    return Ok(true);
}

auto UiDocument::subcomposition_requests() -> Vec<SubcompositionRequest> {
    Vec<SubcompositionRequest> result;
    for (auto target : subcomposition_nodes_) {
        auto node = resolve(target);
        if (!node || !mounted(target)) continue;
        auto state = node->subcomposition.upgrade();
        if (state.is_some() && state->get()->guard.alive() && state->get()->request.is_some())
            result.push(SubcompositionRequest(*state->get()->request));
    }
    return result;
}

auto UiDocument::pending_subcompositions() -> usize {
    usize result{};
    for (auto target : subcomposition_nodes_) {
        auto node = resolve(target);
        if (!node || !mounted(target)) continue;
        auto state = node->subcomposition.upgrade();
        if (state.is_some() && state->get()->guard.alive() && state->get()->request.is_some()) ++result;
    }
    return result;
}

auto UiDocument::dispatch_subcompositions() -> Result<usize, UiError> {
    if (!can_route() || subcomposition_dispatching_) return Err(UiError::Routing);
    subcomposition_dispatching_ = true;
    struct Guard { bool &busy; ~Guard() { busy = false; } } guard{subcomposition_dispatching_};
    Vec<rstd::rc::Rc<DocumentSubcomposition>> ready;
    for (auto target : subcomposition_nodes_) {
        auto node = resolve(target);
        if (!node || !mounted(target)) continue;
        auto state = node->subcomposition.upgrade();
        if (state.is_some() && state->get()->guard.alive() && state->get()->request.is_some() && !state->get()->dispatched)
            ready.push(rstd::move(*state));
    }
    usize dispatched{};
    for (auto &state : ready) {
        if (!state->guard.alive() || state->request.is_none()) continue;
        auto node = resolve(state->target);
        if (!node || !mounted(state->target) || node->mount_generation != state->mount) continue;
        auto current = node->subcomposition.upgrade();
        if (current.is_none() || current->get() != state.get()) continue;
        auto request = *state->request;
        state.get()->dispatched = true;
        auto result = state.get()->callback->operator()(request);
        if (result.is_err()) { state.get()->dispatched = false; return Err(result.unwrap_err()); }
        ++dispatched;
    }
    return Ok(dispatched);
}

void UiDocument::report_subcomposition_limit() {
    for (const auto &request : subcomposition_requests())
        layout_diagnostics_.push({request.target, LayoutIssue::Limit});
}

void UiDocument::close_subcompositions() {
    for (auto target : subcomposition_nodes_) {
        auto node = resolve(target);
        if (!node) continue;
        auto state = node->subcomposition.upgrade();
        if (state.is_some()) state->get()->guard.disconnect();
        node->subcomposition = {};
    }
    subcomposition_nodes_.clear();
}
}
