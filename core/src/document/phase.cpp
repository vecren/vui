module vui;
import :document.interface;
using namespace rstd::prelude;

namespace vui {
auto DocumentPhaseTransaction::protect(state::GraphView graph) -> Option<state::StateError> {
    for (const auto &entry : protections_)
        if (entry.graph.id() == graph.id()) return None();
    if (protections_.len() >= usize(4096)) return Some(state::StateError::Capacity);
    auto epoch = graph.epoch();
    if (epoch.is_err()) return Some(epoch.unwrap_err());
    auto guard = state::protect_writes(graph);
    if (guard.is_err()) return Some(guard.unwrap_err());
    protections_.push({graph, guard.unwrap(), epoch.unwrap()});
    return None();
}

void DocumentPhaseTransaction::report(UiId node, UiPhase phase, state::StateError error) {
    if (diagnostics_.len() < usize(256)) diagnostics_.push({node, phase, error});
}

void DocumentPhaseTransaction::stage(rstd::rc::Rc<DocumentPhaseObservation> observation, bool completed) {
    for (auto &entry : candidates)
        if (entry.observation->target == observation->target && entry.observation->phase == observation->phase) {
            entry = {rstd::move(observation), completed};
            return;
        }
    candidates.push({rstd::move(observation), completed});
}

auto DocumentPhaseTransaction::ready() const -> Option<state::StateError> {
    for (const auto &entry : protections_) {
        auto epoch = entry.graph.epoch();
        if (epoch.is_err()) return Some(epoch.unwrap_err());
        if (epoch.unwrap() != entry.epoch) return Some(state::StateError::Reentrant);
    }
    return None();
}

DocumentPhaseReadScope::~DocumentPhaseReadScope() {
    if (!finished_ && (previous_ || !observation_->dependencies.is_empty()))
        transaction_.stage(observation_.clone(), false);
}

auto DocumentPhaseReadScope::observed(const state::GraphView &graph, const state::NodeId &value)
    -> Option<state::StateError> {
    if (error_.is_some()) return error_;
    for (const auto &entry : observation_->dependencies)
        if (entry.value == value) return None();
    auto error = observation_->dependencies.len() >= usize(4096)
                     ? Some(state::StateError::Capacity) : transaction_.protect(graph);
    if (error.is_some()) {
        error_ = error;
        transaction_.report(observation_->target, observation_->phase, *error);
        return error;
    }
    observation_.get()->dependencies.push({graph, value});
    return None();
}

auto DocumentPhaseReadScope::finish(bool executed) -> Option<UiError> {
    if (finished_) return error_.is_some() ? Some(UiError::InvalidValue) : None<UiError>();
    finished_ = true;
    if (executed && (previous_ || !observation_->dependencies.is_empty()))
        transaction_.stage(observation_.clone(), error_.is_none());
    return error_.is_some() ? Some(UiError::InvalidValue) : None<UiError>();
}

auto UiDocument::observe_phase(UiId id, UiPhase phase) -> DocumentPhaseReadScope {
    auto &node = *resolve(id);
    return DocumentPhaseReadScope(*phase_transaction_, id, phase, node.mount_generation,
                                  bool(node.phase_observations[static_cast<unsigned>(phase)]));
}

void UiDocument::schedule_phase(rstd::rc::Rc<DocumentPhaseObservation> observation) {
    auto node = resolve(observation->target);
    if (!observation->active || observation->pending || !node ||
        node->mount_generation != observation->mount || !mounted(observation->target)) return;
    observation.get()->pending = true;
    phase_invalidations_.push(observation.downgrade());
    changed(false, Some(UiId(observation->target)));
    bindings_.wake();
}

void UiDocument::apply_phase_invalidations() {
    auto pending = rstd::exchange(phase_invalidations_, Vec<rstd::rc::Weak<DocumentPhaseObservation>>{});
    for (const auto &weak : pending) {
        auto observation = weak.upgrade();
        if (observation.is_none()) continue;
        auto &value = *observation->get();
        value.pending = false;
        auto node = resolve(value.target);
        if (!value.active || !node || node->mount_generation != value.mount || !mounted(value.target)) continue;
        if (value.phase == UiPhase::Measure) { dirty_measure(value.target); layout_dirty_ = true; }
        else if (value.phase == UiPhase::Placement) { dirty_arrange(value.target); layout_dirty_ = true; }
        else node->paint_dirty = true;
    }
}

auto UiDocument::publish_phase_reads(DocumentPhaseTransaction &transaction) -> Option<UiError> {
    if (auto error = transaction.ready(); error.is_some()) {
        for (const auto &entry : transaction.candidates)
            transaction.report(entry.observation->target, entry.observation->phase, *error);
        return Some(UiError::InvalidValue);
    }
    for (auto &entry : transaction.candidates) {
        if (!entry.completed) return Some(UiError::InvalidValue);
        auto observation = entry.observation.clone();
        auto node = resolve(observation->target);
        if (!node || node->mount_generation != observation->mount) return Some(UiError::InvalidId);
        auto &previous = node->phase_observations[static_cast<unsigned>(observation->phase)];
        bool same = previous && previous->dependencies.len() == observation->dependencies.len();
        for (const auto &dependency : observation->dependencies) {
            if (!same) break;
            bool found{};
            for (const auto &old : previous->dependencies) found |= old.value == dependency.value;
            same = found;
        }
        if (same) {
            entry.observation = previous.clone();
            entry.reused = true;
            continue;
        }
        for (const auto &dependency : observation->dependencies) {
            auto subscription = dependency.graph.watch(dependency.value,
                [this, guard = lifetime(), weak = observation.downgrade()](state::Invalidation) {
                    auto value = weak.upgrade();
                    if (guard.alive() && value.is_some()) schedule_phase(rstd::move(*value));
                });
            if (subscription.is_err()) {
                transaction.report(observation->target, observation->phase, subscription.unwrap_err());
                return Some(UiError::InvalidValue);
            }
            observation.get()->subscriptions.push(subscription.unwrap());
        }
    }
    for (auto &entry : transaction.candidates) {
        if (entry.reused) continue;
        auto observation = entry.observation.clone();
        auto &slot = resolve(observation->target)->phase_observations[static_cast<unsigned>(observation->phase)];
        if (slot) slot.get()->close();
        slot = observation->dependencies.is_empty() ? rstd::rc::Rc<DocumentPhaseObservation>{} : observation;
        observation.get()->active = true;
    }
    transaction.published = true;
    return None();
}

void UiDocument::discard_phase_reads(DocumentPhaseTransaction &transaction) {
    if (transaction.published) return;
    for (const auto &entry : transaction.candidates) {
        auto node = resolve(entry.observation->target);
        if (!node) continue;
        if (entry.observation->phase == UiPhase::Measure) { dirty_measure(entry.observation->target); layout_dirty_ = true; }
        else if (entry.observation->phase == UiPhase::Placement) { dirty_arrange(entry.observation->target); layout_dirty_ = true; }
        else node->paint_dirty = true;
    }
}

auto UiDocument::invalidate_placement(UiId id) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    if (!resolve(id)) return Err(UiError::InvalidId);
    dirty_arrange(id);
    layout_dirty_ = true;
    changed(false, Some(id));
    bindings_.wake();
    return Ok(empty{});
}

void UiDocument::clear_phase_observations() {
    for (auto &slot : slots_)
        if (slot.node.is_some())
            for (auto &observation : slot.node->phase_observations) {
                if (observation) observation.get()->close();
                observation = {};
            }
    phase_invalidations_.clear();
}
}
