module vui.compose;
import :runtime;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui::compose {
auto ComposeBuild::observe(vui::state::GraphView graph) -> ComposeResult<vui::state::ObservationBatch *> {
    for (auto &entry : observations) if (entry.graph.id() == graph.id()) return Ok(&entry.batch);
    auto batch = vui::state::ObservationBatch::begin(graph);
    if (batch.is_err()) return Err(state_error(batch.unwrap_err()));
    observations.push({graph, batch.unwrap()}); return Ok(&observations[observations.len() - usize(1)].batch);
}

void ComposeBuild::index_movable(const rstd::rc::Rc<ComposeInstance> &instance) {
    if (!instance) return;
    if (instance->element.data().kind == ElementKind::Movable)
        movable.push({instance->element.data().movable.clone(), instance.clone()});
    for (const auto &child : instance->children) index_movable(child);
}

auto ComposeBuild::previous_movable(const rstd::rc::Rc<MovableIdentity> &identity) const
    -> rstd::rc::Rc<ComposeInstance> {
    for (const auto &entry : movable) if (entry.identity.get() == identity.get()) return entry.instance.clone();
    return {};
}

auto ComposeBuild::claim_movable(rstd::rc::Rc<MovableIdentity> identity, rstd::rc::Rc<ComposeInstance> instance)
    -> ComposeResult<usize> {
    auto owner = identity->owner.upgrade();
    if (owner.is_some() && !owner->get()->closed && owner->get()->lifetime.alive() && owner->get() != signal.get()) {
        auto error = ComposeError(ComposeStage::Key);
        error.operation = String::make("movable content belongs to another composition"_str);
        return Err(rstd::move(error));
    }
    for (const auto &claim : claims) if (claim.identity.get() == identity.get()) {
        auto error = ComposeError(ComposeStage::Key);
        error.operation = String::make("movable content is emitted more than once"_str);
        return Err(rstd::move(error));
    }
    auto position = claims.len();
    MovableClaim claim{rstd::move(identity), rstd::move(instance)};
    auto previous = previous_movable(claim.identity);
    if (previous) committed_roots(*previous, claim.before);
    claims.push(rstd::move(claim));
    return Ok(position);
}

auto ComposeBuild::claim_reused(const rstd::rc::Rc<ComposeInstance> &instance, bool root_claimed)
    -> ComposeResult<empty> {
    if (!root_claimed && instance->element.data().kind == ElementKind::Movable) {
        auto claimed = claim_movable(instance->element.data().movable.clone(), instance.clone());
        if (claimed.is_err()) return Err(claimed.unwrap_err());
        committed_roots(*instance, claims[claimed.unwrap()].after);
    }
    for (const auto &child : instance->children) {
        auto claimed = claim_reused(child);
        if (claimed.is_err()) return claimed;
    }
    return Ok(empty{});
}

auto ComposeBuild::retains(const rstd::rc::Rc<ComposeInstance> &instance) const -> bool {
    for (const auto &claim : claims) if (claim.instance.get() == instance.get()) return true;
    return false;
}

auto ComposeBuild::preserve_moves() -> ComposeResult<empty> {
    for (const auto &claim : claims) for (const auto &root : claim.after) {
        if (root.existing.is_none()) continue;
        bool retained{};
        for (const auto &previous : claim.before) retained |= previous.existing == root.existing;
        if (!retained) continue;
        auto mount = session.document().mount_state(*root.existing);
        if (mount.is_err()) return Err(ComposeError(ComposeStage::External, Some(mount.unwrap_err())));
        if (mount->mounted) edit.preserve_mount(edit.existing(*root.existing));
    }
    return Ok(empty{});
}

auto ComposeBuild::retired_roots() const -> Vec<UiId> {
    Vec<UiId> roots;
    auto collect = [&](auto &&self, const rstd::rc::Rc<ComposeInstance> &instance, bool covered) -> void {
        if (retains(instance)) return;
        if (instance->element.data().precompose) covered = false;
        if (!covered && instance->id) {
            bool included{};
            for (auto id : roots) included |= id == *instance->id;
            if (!included) roots.push(UiId(*instance->id));
            covered = true;
        }
        for (const auto &child : instance->children) self(self, child, covered);
    };
    for (const auto &instance : retired) collect(collect, instance, false);
    return roots;
}

namespace {
auto passive_native_tree(const Element &element, Index &budget) -> bool {
    if (!budget) return false;
    --budget;
    const auto &data = element.data();
    if (data.invalid || data.precompose || data.reference || !data.behaviors.is_empty() ||
        !data.modifiers.is_empty())
        return false;
    if (data.kind == ElementKind::Native) {
        if (data.layout == LayoutKind::Custom || data.layout == LayoutKind::Viewport || data.layout == LayoutKind::Scene)
            return false;
    } else if (data.kind != ElementKind::Fragment) {
        return false;
    }
    for (const auto &child : data.children)
        if (!passive_native_tree(child, budget)) return false;
    return true;
}
}

auto ComposeBuild::dirty(const ComposeInstance &instance, const CompositionEnvironment &environment) -> bool {
    if (instance.element.data().precompose) return false;
    if (instance.scope && (instance.scope->dirty || environment.changed(instance.scope->local_reads.as_slice()))) return true;
    for (const auto &mount : instance.mounts) if (!mount->attempted) return true;
    for (const auto &behavior : instance.attached) if (!behavior->attempted) return true;
    auto nested = environment;
    if (instance.element.data().kind == ElementKind::Provider) {
        auto provided = environment.extended(instance.element.data().locals.as_slice());
        if (provided.is_err()) return true;
        nested = provided.unwrap();
    }
    for (const auto &child : instance.children) if (dirty(*child.get(), nested)) return true;
    return false;
}

auto ComposeBuild::same_roots(const Vec<ComposeRoot> &before, const Vec<ComposeRoot> &after) -> bool {
    if (before.len() != after.len()) return false;
    for (usize i{}; i < before.len(); ++i)
        if (after[i].existing.is_none() || before[i].existing != after[i].existing) return false;
    return true;
}

void ComposeBuild::children(UiNodeRef node, const Vec<ComposeRoot> &roots) {
    Vec<UiNodeRef> refs;
    for (auto root : roots) refs.push(root.prepared.is_some() ? *root.prepared : edit.existing(*root.existing));
    edit.children(node, rstd::move(refs));
}

auto CompositionContext::identity(Serial site) -> CallIdentity {
    auto value = CallIdentity(site);
    for (const auto &group : groups_) value.groups.push(Key(group));
    for (auto &entry : occurrences_)
        if (entry.identity.same_group(value)) { value.occurrence = entry.next++; return value; }
    occurrences_.push({CallIdentity(value), 1}); return value;
}

auto CompositionContext::slot(Key key) -> Key {
    auto value = CallIdentity();
    for (const auto &group : groups_) value.groups.push(Key(group));
    return value.memory_key(Some(rstd::move(key)));
}

auto CompositionContext::group(Key key, Serial site, rstd::source_location source) -> GroupGuard {
    auto depth = groups_.len();
    auto visit = CallIdentity(site);
    for (const auto &group : groups_) visit.groups.push(Key(group));
    visit.groups.push(Key(key));
    for (const auto &previous : groups_seen_)
        if (previous.same_group(visit)) {
            failure_ = Some(ComposeError(ComposeStage::Key));
            failure_->operation = String::make("duplicate composition group"_str);
            failure_->source = source;
            break;
        }
    groups_seen_.push(rstd::move(visit));
    groups_.push(Key{site}); groups_.push(rstd::move(key));
    return GroupGuard(*this, depth);
}

auto CompositionContext::node(Key key) -> ComposeResult<NodeRef> {
    auto slot = draft_.memory<rstd::rc::Rc<NodeLink>>(MemoryKind::Node, rstd::move(key), []() -> ComposeResult<rstd::rc::Rc<NodeLink>> {
        return Ok(rstd::rc::make_rc<NodeLink>());
    });
    if (slot.is_err()) return Err(slot.unwrap_err());
    auto owned = slot.unwrap();
    return Ok(NodeRef(static_cast<MemoryValue<rstd::rc::Rc<NodeLink>> &>(*owned.get()->payload.get()).value.clone()));
}

void CompositionRuntime::cleanup(const rstd::rc::Rc<ComposeInstance> &instance, const ComposeBuild *retained) {
    if (retained && retained->retains(instance)) return;
    auto value = instance.clone();
    for (auto &child : value.get()->children) cleanup(child, retained);
    for (usize i = value->attached.len(); i > usize(); --i) value.get()->attached[i - usize(1)].get()->close();
    value.get()->attached.clear();
    if (value->scope) {
        auto &scope = *value.get()->scope.get(); scope.alive = false;
        for (usize i = scope.effects.len(); i > usize(); --i) scope.effects[i - usize(1)].get()->lease = None();
        for (auto &slot : scope.memories) slot.get()->alive = false;
        scope.effects.clear(); scope.reads.clear(); scope.local_reads.clear(); scope.memories.clear();
    }
    for (usize i = value->mounts.len(); i > usize(); --i) value.get()->mounts[i - usize(1)].get()->lease = None();
    value.get()->mounts.clear(); value.get()->native.clear();
}

void CompositionRuntime::invalidate_links(const rstd::rc::Rc<ComposeInstance> &instance, const ComposeBuild *retained) {
    if (retained && retained->retains(instance)) return;
    auto value = instance.clone(); value.get()->link.get()->active = false; value.get()->link.get()->alive = false;
    if (value->element.data().reference.is_some()) {
        auto link = value->element.data().reference->link(); link.get()->active = false; link.get()->alive = false;
    }
    for (const auto &child : value->children) invalidate_links(child, retained);
}

auto CompositionRuntime::retained_references(const ComposeInstance &instance, UiDocument &document,
                                Vec<NodeLink *> &references) -> ComposeResult<empty> {
    if (instance.id.is_some() && !document.valid(*instance.id))
        return Err(ComposeError(ComposeStage::External, Some(UiError::InvalidId)));
    if (instance.element.data().reference.is_some()) {
        auto link = instance.element.data().reference->link();
        for (auto previous : references) if (previous == link.get()) return Err(ComposeError(ComposeStage::Key));
        references.push(link.get());
    }
    for (const auto &child : instance.children) {
        auto result = retained_references(*child.get(), document, references);
        if (result.is_err()) return result;
    }
    return Ok(empty{});
}

auto CompositionRuntime::resolve(ComposeDraft &draft, UiDocument &document, const UiDocument::PreparedEdit &prepared,
                    Vec<Element> &old_elements, Vec<NodeLink *> &references, usize &scopes) -> ComposeResult<empty> {
    if (draft.reused) return retained_references(*draft.instance.get(), document, references);
    old_elements.push(Element(draft.instance->element));
    if (draft.scope.is_some()) ++scopes;
    if (draft.node.is_some()) draft.resolved = Some(prepared.id(*draft.node));
    if (draft.element.data().reference.is_some()) {
        auto link = draft.element.data().reference->link();
        for (auto previous : references) if (previous == link.get()) return Err(ComposeError(ComposeStage::Key));
        references.push(link.get());
        if (!link->alive || (link->active && (draft.fresh || draft.instance->id != Some(link->id))))
            return Err(ComposeError(ComposeStage::Closed));
    }
    for (auto &child : draft.children) {
        auto result = resolve(*child.get(), document, prepared, old_elements, references, scopes); if (result.is_err()) return result;
    }
    for (const auto &child : draft.reused_children) {
        auto result = retained_references(*child.get(), document, references); if (result.is_err()) return result;
    }
    return Ok(empty{});
}

void CompositionRuntime::commit(ComposeDraft &draft, Vec<OldScope> &old) {
    if (draft.reused) return;
    auto &instance = *draft.instance.get();
    if (draft.scope.is_some()) {
        auto &scope = (**draft.scope).scope;
        old.push({scope.clone(), rstd::move(scope.get()->memories), rstd::move(scope.get()->effects), rstd::move(scope.get()->reads)});
        scope.get()->memories = rstd::move((**draft.scope).memories);
        scope.get()->effects = rstd::move((**draft.scope).effects);
        scope.get()->reads = rstd::move((**draft.scope).reads); scope.get()->dirty = false;
        scope.get()->local_reads = rstd::move((**draft.scope).local_reads);
        scope.get()->deferred_actions = rstd::move((**draft.scope).deferred_actions);
        for (auto &slot : scope.get()->memories) slot.get()->committed = true;
        instance.scope = scope.clone();
    }
    if (instance.scope) {
        instance.scope.get()->precomposed = draft.element.data().precompose;
        if (draft.precompose_failed) instance.scope.get()->dirty = true;
        if (draft.promoted) instance.scope.get()->deferred_actions.clear();
    }
    if (instance.element.data().reference.is_some()) {
        auto old_link = instance.element.data().reference->link();
        auto next_link = draft.element.data().reference.is_some() ? draft.element.data().reference->link() : rstd::rc::Rc<NodeLink>{};
        if (old_link.get() != next_link.get()) { old_link.get()->active = false; old_link.get()->alive = false; }
    }
    instance.element = draft.element; instance.native = rstd::move(draft.native);
    instance.attached.clear();
    for (auto &behavior : draft.attached) {
        behavior.slot.get()->config = behavior.config.clone();
        instance.attached.push(behavior.slot.clone());
    }
    if (draft.output.is_some()) instance.output = Some(Element(*draft.output));
    if (draft.fresh) instance.mounts = rstd::move(draft.mounts);
    instance.owns_children = draft.owns_children;
    if (draft.resolved.is_some()) instance.id = draft.resolved;
    if (instance.id.is_some()) { instance.link.get()->id = *instance.id; instance.link.get()->active = true; }
    if (draft.element.data().reference.is_some() && instance.id.is_some()) {
        auto link = draft.element.data().reference->link(); link.get()->id = *instance.id; link.get()->active = true;
    }
    for (auto &child : draft.children) {
        commit(*child.get(), old);
    }
    instance.children = rstd::move(draft.committed_children);
}

void CompositionRuntime::prepare_native(ComposeDraft &draft, UiSession &session, UiCommitReport &report) {
    if (draft.reused || signal->closed) return;
    const auto &data = draft.element.data();
    if (data.precompose) {
        auto &instance = *draft.instance.get();
        if (!data.premeasure || !instance.output || draft.precompose_failed) return;
        if (instance.children.is_empty()) {
            Index budget = 128;
            if (!passive_native_tree(*instance.output, budget)) return;
            ComposeBuild build(session, graph_.view(), signal, children_claim_);
            auto staged = build.reconcile(*instance.output, {}, true);
            if (staged.is_err()) return;
            auto tree = staged.unwrap();
            auto prepared = session.document().prepare(rstd::move(build.edit));
            if (prepared.is_err()) return;
            Vec<Element> old_elements;
            Vec<NodeLink *> references;
            usize scopes{};
            if (resolve(*tree.get(), session.document(), *prepared, old_elements, references, scopes).is_err())
                return;
            auto published = session.document().publish(prepared.unwrap());
            if (published.is_err()) return;
            Vec<OldScope> old;
            commit(*tree.get(), old);
            instance.children.push(tree->instance.clone());
            report.created += published->created;
            report.revision = published->revision;
            signal.get()->committed_revision = report.revision;
        }
        Vec<ComposeRoot> roots;
        for (const auto &child : instance.children) ComposeBuild::committed_roots(*child, roots);
        for (const auto &root : roots)
            if (root.existing) (void)session.document().premeasure_detached(*root.existing, *data.premeasure);
        return;
    }
    for (auto &child : draft.children) prepare_native(*child.get(), session, report);
}

void CompositionRuntime::activate(ComposeDraft &draft, CommittedContext &context) {
    if (draft.reused || draft.element.data().precompose || signal->closed) return;
    for (const auto &behavior : draft.attached) {
        if (signal->closed) return;
        auto slot = behavior.slot.clone();
        BehaviorContext attached(context.session(), *draft.instance->id, slot->role);
        auto report = [&](ComposeError error) {
            error.stage = ComposeStage::Effect; error.node = draft.instance->id;
            if (!error.source.line()) error.source = draft.element.data().source;
            if (error.path.is_empty()) error.path = slot->key.label();
            signal.get()->report(rstd::move(error));
        };
        auto updated = behavior.config->update(*slot.get()->state.get(), attached);
        if (updated.is_err()) { slot.get()->attempted = true; report(updated.unwrap_err()); continue; }
        if (slot->modifier.is_some() && slot->modifier->alive()) {
            auto invalidated = attached.document().invalidate_modifier(attached.target(), slot->role);
            if (invalidated.is_err()) report(ComposeError(ComposeStage::Effect, Some(invalidated.unwrap_err())));
        }
        if (slot->attempted) continue;
        slot.get()->attempted = true;
        if (behavior.config->modifier() && slot->modifier.is_none()) {
            auto bound = behavior.config->bind_modifier(attached.document(), attached.target(), slot->role, *slot.get()->state.get());
            if (bound.is_err()) { report(ComposeError(ComposeStage::Effect, Some(bound.unwrap_err()))); continue; }
            slot.get()->modifier = Some(bound.unwrap());
        }
        auto started = behavior.config->attach(*slot.get()->state.get(), attached);
        if (started.is_ok()) slot.get()->lease = Some(started.unwrap());
        else report(started.unwrap_err());
    }
    if (draft.instance->id.is_some()) {
        Vec<NodeStateKey> order;
        for (const auto &behavior : draft.attached)
            if (behavior.slot->modifier.is_some() && behavior.slot->modifier->alive())
                order.push(NodeStateKey(behavior.slot->role));
        if (!order.is_empty()) {
            auto ordered = context.session().document().set_modifier_order(*draft.instance->id, rstd::move(order));
            if (ordered.is_err()) {
                auto error = ComposeError(ComposeStage::Effect, Some(ordered.unwrap_err()));
                error.node = draft.instance->id; error.source = draft.element.data().source;
                signal.get()->report(rstd::move(error));
            }
        }
    }
    if (draft.scope.is_some() || draft.promoted)
        for (auto &effect : draft.instance.get()->scope.get()->effects) {
            if (signal->closed) return;
            if (effect->attempted) continue;
            effect.get()->attempted = true;
            auto started = effect.get()->start.get()->invoke->operator()(context);
            if (started.is_ok()) effect.get()->lease = Some(started.unwrap());
            else {
                auto error = started.unwrap_err(); error.stage = ComposeStage::Effect;
                if (!error.source.line()) error.source = effect->start->source;
                if (error.path.is_empty()) error.path = effect->key.label();
                signal.get()->report(rstd::move(error));
            }
        }
    for (auto &mount : draft.instance.get()->mounts) {
        if (signal->closed) return;
        if (mount->attempted) continue;
        mount.get()->attempted = true;
        auto result = mount.get()->start.get()->invoke->operator()(context);
        if (result.is_ok()) mount.get()->lease = Some(result.unwrap());
        else {
            auto error = result.unwrap_err(); error.stage = ComposeStage::Effect;
            if (!error.source.line()) error.source = mount->start->source;
            signal.get()->report(rstd::move(error));
        }
    }
    for (auto &child : draft.children) activate(*child.get(), context);
}

auto CompositionRuntime::locate(ComposeDraft &draft, UiNodeRef ref, ComposeError &error) -> bool {
    if (draft.node == Some(ref)) { error.source = draft.element.data().source; return true; }
    for (usize i{}; i < draft.children.len(); ++i) {
        auto &child = draft.children[i];
        if (locate(*child.get(), ref, error)) {
            const auto &key = child->element.data().key;
            auto label = key.is_some() ? key->label() : rstd::format("{}", child->position);
            error.path = rstd::format("{}/{}", label.as_str(), error.path.as_str()); return true;
        }
    }
    return false;
}

auto CompositionRuntime::perform(UiSession &session) -> ComposeResult<CommitReport> {
    if (!session.document().valid(mount_)) {
        close(session);
        return Err(ComposeError(ComposeStage::External, Some(UiError::InvalidId)));
    }
    if (!content_) return Ok(CommitReport{session.document().revision(), attempt_});
    ComposeBuild build(session, graph_.view(), signal, children_claim_);
    build.index_movable(root_);
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Component;
    data.get()->component_type = rstd::any::TypeId::of<RootTag>(); data.get()->view = content_; data.get()->source = content_source_;
    auto built = build.reconcile(Element(rstd::move(data)), root_, !root_ || root_->scope->dirty);
    if (built.is_err()) {
        auto error = built.unwrap_err(); if (!error.source.line()) error.source = content_source_;
        return Err(rstd::move(error));
    }
    auto draft = built.unwrap();
    auto moves = build.preserve_moves();
    if (moves.is_err()) return Err(moves.unwrap_err());
    auto retired_roots = build.retired_roots();
    Vec<ComposeRoot> before;
    if (root_) ComposeBuild::committed_roots(*root_.get(), before);
    if (!ComposeBuild::same_roots(before, draft->roots)) build.children(build.edit.existing(mount_), draft->roots);
    auto prepared_result = session.document().prepare(rstd::move(build.edit));
    if (prepared_result.is_err()) {
        auto failure = prepared_result.unwrap_err();
        ComposeError error(ComposeStage::Prepare, Some(failure.code)); error.source = content_source_;
        error.operation = String::make(ui_edit_operation_name(failure.operation));
        if (failure.node.is_some()) locate(*draft.get(), *failure.node, error);
        return Err(rstd::move(error));
    }
    auto prepared = prepared_result.unwrap();
    for (auto &prepare : build.preparers) {
        auto reading = session.document().acquire_read();
        if (reading.is_err()) return Err(ComposeError(ComposeStage::Prepare, Some(reading.unwrap_err())));
        auto guard = reading.unwrap();
        auto result = (*prepare)(prepared); if (result.is_err()) return Err(result.unwrap_err());
    }
    Vec<Element> old_elements; Vec<NodeLink *> references; usize scopes{};
    auto resolved = resolve(*draft.get(), session.document(), prepared, old_elements, references, scopes);
    if (resolved.is_err()) return Err(resolved.unwrap_err());
    EventRouter::BehaviorEdit behaviors;
    for (auto &prepare : build.behaviors) {
        auto result = (*prepare)(session.input(), behaviors, prepared);
        if (result.is_err()) { ComposeError error(ComposeStage::Input); error.input = Some(result.unwrap_err()); return Err(rstd::move(error)); }
    }
    for (auto &observation : build.observations) {
        auto ready = observation.batch.ready(); if (ready.is_err()) return Err(state_error(ready.unwrap_err()));
        observation.batch.finish_reads();
    }
    Vec<OldScope> old;
    old.reserve(scopes);
    auto published = session.document().publish(rstd::move(prepared));
    if (published.is_err()) return Err(ComposeError(ComposeStage::Publish, Some(published.unwrap_err())));
    auto report = published.unwrap();
    signal.get()->published = true; signal.get()->committed_revision = report.revision;
    commit(*draft.get(), old); root_ = draft.get()->instance.clone();
    for (auto &claim : build.claims) claim.identity.get()->owner = signal.downgrade();
    for (auto &observation : build.observations) observation.batch.publish();
    auto input_published = session.input().publish_behaviors(rstd::move(behaviors));
    if (input_published.is_err()) {
        ComposeError error(ComposeStage::Input); error.input = Some(input_published.unwrap_err()); signal.get()->report(rstd::move(error));
    }
    for (usize i = build.retired_behaviors.len(); i > usize(); --i) build.retired_behaviors[i - usize(1)].get()->close();
    build.retired_behaviors.clear();
    build.observations.clear();
    auto cancelled = session.input().cancel_detached();
    if (cancelled.is_err()) { ComposeError error(ComposeStage::Input); error.input = Some(cancelled.unwrap_err()); signal.get()->report(rstd::move(error)); }
    for (auto &instance : build.retired) cleanup(instance, &build);
    for (usize i = old.len(); i > usize(); --i) {
        auto &entry = old[i - usize(1)];
        for (usize i = entry.effects.len(); i > usize(); --i) {
            auto slot = entry.effects[i - usize(1)].clone(); bool retained{};
            for (const auto &current : entry.scope->effects) retained |= current.get() == slot.get();
            if (!retained) slot.get()->lease = None();
        }
        for (auto &slot : entry.memories) {
            bool retained{}; for (const auto &current : entry.scope->memories) retained |= current.get() == slot.get();
            if (!retained) slot.get()->alive = false;
        }
    }
    old.clear();
    for (auto &instance : build.retired) invalidate_links(instance, &build);
    for (auto id : report.retired) {
        bool present{}; for (auto previous : retired_roots) present |= previous == id;
        if (!present) retired_roots.push(UiId(id));
    }
    Vec<UiId> removed;
    for (auto id : retired_roots) {
        bool nested{};
        for (auto other : retired_roots) if (id != other && session.document().valid(other)) {
            auto descendant = session.document().is_descendant(other, id);
            nested |= descendant.is_ok() && descendant.unwrap();
        }
        if (!nested) removed.push(UiId(id));
    }
    for (auto id : removed) if (session.document().valid(id)) session.document().destroy(id, children_claim_.access()).unwrap();
    old_elements.clear();
    report.retirement = None();
    prepare_native(*draft.get(), session, report);
    session.document().notify_edit_targets();
    CommittedContext context(session);
    for (auto &action : build.actions) {
        if (signal->closed) break;
        auto applied = (*action)(context); if (applied.is_err()) signal.get()->report(applied.unwrap_err());
    }
    activate(*draft.get(), context);
    return Ok(CommitReport{report.revision, attempt_, build.evaluations, report.created, report.updated, removed.len(), build.comparisons, build.skipped});
}

auto CompositionRuntime::update(UiSession &session) -> Result<empty, UiError> {
    signal.get()->pending = false; signal.get()->error = None(); ++attempt_;
    signal.get()->published = false;
    signal.get()->attempt = attempt_;
    auto result = perform(session);
    if (result.is_ok()) last_ = Some(result.unwrap());
    else {
        auto error = result.unwrap_err();
        if (error.stage == ComposeStage::External) close(session);
        signal.get()->report(rstd::move(error));
    }
    return Ok(empty{});
}

void CompositionRuntime::retry_effects() {
    auto retry = [&](auto &&self, ComposeInstance &instance) -> void {
        if (instance.scope)
            for (auto &slot : instance.scope.get()->effects)
                if (slot->attempted && slot->lease.is_none()) {
                    slot.get()->attempted = false; instance.scope.get()->dirty = true;
                }
        for (auto &slot : instance.mounts)
            if (slot->attempted && slot->lease.is_none()) slot.get()->attempted = false;
        for (auto &slot : instance.attached)
            if (slot->attempted && slot->lease.is_none()) slot.get()->attempted = false;
        for (auto &child : instance.children) self(self, *child.get());
    };
    if (root_) retry(retry, *root_.get());
    signal.get()->invalidate();
}

void CompositionRuntime::close(UiSession &session) {
    signal.get()->closed = true; signal.get()->pending = false;
    if (root_) {
        Vec<UiId> nodes; native_roots(*root_.get(), nodes);
        for (auto id : nodes) if (session.document().valid(id)) session.input().cancel_subtree(id);
        cleanup(root_);
        invalidate_links(root_);
        for (auto id : nodes) if (session.document().valid(id)) session.document().destroy(id, children_claim_.access());
        root_ = {};
    }
    graph_.close(); content_ = {}; children_claim_.release();
}

auto CompositionRuntime::last_result() const -> Option<ComposeResult<CommitReport>> {
    if (signal->error.is_some()) return Some(ComposeResult<CommitReport>(Err(ComposeError(*signal->error))));
    if (last_.is_some()) return Some(ComposeResult<CommitReport>(Ok(*last_)));
    return None();
}

auto Composition::create(UiSession &session, UiId mount) -> ComposeResult<Composition> {
    auto &document = session.document();
    if (!document.valid(mount)) return Err(ComposeError(ComposeStage::Prepare, Some(UiError::InvalidId)));
    auto claim = document.claim_children(mount);
    if (claim.is_err()) return Err(ComposeError(ComposeStage::Prepare, Some(claim.unwrap_err())));
    auto runtime = rstd::rc::make_rc<CompositionRuntime>(document, mount, claim.unwrap());
    auto registered = session.updates().bind(runtime);
    if (registered.is_err()) return Err(ComposeError(ComposeStage::Prepare, Some(registered.unwrap_err())));
    return Ok(Composition(rstd::move(runtime), registered.unwrap()));
}

auto Composition::set_content(Element value, rstd::source_location source) -> ComposeResult<empty> {
    return set_content([value = rstd::move(value)](CompositionContext &) -> ComposeResult<Element> {
        return Ok(Element(value));
    }, source);
}

auto compatible(const ElementData &a, const ElementData &b) -> bool {
    if (a.kind != b.kind) return false;
    if (a.precompose && !b.precompose && !a.retain_native) return false;
    if (a.kind == ElementKind::Native) return a.layout == b.layout;
    if (a.kind == ElementKind::Component) return a.component_type == b.component_type;
    if (a.kind == ElementKind::Custom) return a.adapter->type == b.adapter->type;
    if (a.kind == ElementKind::Movable) return a.movable.get() == b.movable.get();
    return true;
}

auto same_identity(const ElementData &a, const ElementData &b) -> bool {
    if (a.kind == ElementKind::Movable || b.kind == ElementKind::Movable)
        return a.kind == b.kind && a.movable.get() == b.movable.get();
    if (a.identity.is_some() != b.identity.is_some() || a.key.is_some() != b.key.is_some()) return false;
    if (a.identity.is_some() && !a.identity->same_group(*b.identity)) return false;
    if (a.key.is_some()) return *a.key == *b.key;
    return a.identity.is_some() && a.identity->occurrence == b.identity->occurrence;
}

auto ComposeBuild::reconcile(Element value, rstd::rc::Rc<ComposeInstance> previous, bool force, Index depth,
                             CompositionEnvironment environment)
    -> ComposeResult<rstd::rc::Rc<ComposeDraft>> {
    if (++nodes > usize(65536) || depth >= 128) return Err(ComposeError(ComposeStage::Limit));
    const auto &data = value.data();
    if (data.invalid) return Err(ComposeError(ComposeStage::Prepare, Some(UiError::InvalidValue)));
    if (data.kind == ElementKind::Movable && !data.movable) return Err(ComposeError(ComposeStage::Type));
    if (previous && previous->id.is_some() && !session.document().valid(*previous->id))
        return Err(ComposeError(ComposeStage::External, Some(UiError::InvalidId)));
    if (previous && !compatible(data, previous->element.data())) { retire(previous); previous = {}; }
    if (data.kind == ElementKind::Movable) {
        auto indexed = previous_movable(data.movable);
        if (previous && previous.get() != indexed.get()) retire(previous);
        previous = rstd::move(indexed);
    }
    auto plan = rstd::rc::make_rc<ComposeDraft>(); auto &draft = *plan.get();
    draft.element = value; draft.fresh = !previous;
    draft.instance = previous ? previous.clone() : rstd::rc::make_rc<ComposeInstance>();
    Option<usize> movable_claim;
    if (data.kind == ElementKind::Movable) {
        auto claimed = claim_movable(data.movable.clone(), draft.instance.clone());
        if (claimed.is_err()) return Err(claimed.unwrap_err());
        movable_claim = Some(claimed.unwrap());
    }
    draft.promoted = previous && previous->element.data().precompose && !data.precompose;
    if (previous)
        force |= data.retain_native != previous->element.data().retain_native ||
                 data.premeasure != previous->element.data().premeasure;
    if (previous && !force && !draft.promoted && !dirty(*previous.get(), environment)) {
        auto claimed = claim_reused(previous, data.kind == ElementKind::Movable);
        if (claimed.is_err()) return Err(claimed.unwrap_err());
        draft.reused = true; committed_roots(*previous.get(), draft.roots);
        if (movable_claim.is_some()) for (auto root : draft.roots) claims[*movable_claim].after.push(ComposeRoot(root));
        return Ok(rstd::move(plan));
    }
    if (data.retain_native) {
        auto scope = draft.instance->scope ? draft.instance->scope.clone() : rstd::rc::make_rc<ScopeState>();
        draft.scope = Some(Box<ScopeDraft>::make(rstd::move(scope)));
        (**draft.scope).precompose = true;
        draft.output = Some(Element{});
        Index budget = 128;
        for (const auto &child : draft.instance->children) {
            if (passive_native_tree(child->element, budget)) {
                draft.reused_children.push(child.clone());
                draft.committed_children.push(child.clone());
            } else {
                retire(child.clone());
            }
        }
        return Ok(rstd::move(plan));
    }
    for (const auto &payload : draft.instance->native) draft.native.push(payload.clone());
    draft.owns_children = draft.instance->owns_children;
    Vec<Element> children;
    if (data.kind == ElementKind::Component || data.kind == ElementKind::Movable) {
        auto refresh_modifiers = force;
        auto scope = draft.instance->scope ? draft.instance->scope.clone() : rstd::rc::make_rc<ScopeState>();
        auto scope_dirty = scope->dirty || environment.changed(scope->local_reads.as_slice());
        if (force && !draft.fresh && (!scope_dirty || (data.precompose && draft.instance->element.data().precompose)) && data.parameters) {
            const auto &previous_parameters = draft.instance->element.data().parameters;
            ++comparisons;
            if (previous_parameters && data.parameters->type() == previous_parameters->type() &&
                data.parameters->equal(*previous_parameters.get())) { force = false; ++skipped; }
        }
        if (force || (scope_dirty && !data.precompose) || draft.fresh) {
            ++evaluations; draft.scope = Some(Box<ScopeDraft>::make(scope));
            (**draft.scope).precompose = data.precompose;
            CompositionContext context(*this, **draft.scope, environment);
            auto evaluate = data.view.clone();
            auto evaluated = [&]() -> ComposeResult<Element> {
                compiler::Activation activation(context);
                auto read = session.document().acquire_read();
                if (read.is_err()) return Err(ComposeError(ComposeStage::Evaluation, Some(read.unwrap_err())));
                auto guard = read.unwrap(); return evaluate.get()->invoke->operator()(context);
            }();
            if (context.failure().is_some() || evaluated.is_err()) {
                if (!data.precompose)
                    return Err(context.failure() ? ComposeError(*context.failure()) : evaluated.unwrap_err());
                draft.precompose_failed = true;
                draft.scope = None();
            } else {
                draft.output = Some(evaluated.unwrap());
                force = true;
            }
        } else if (draft.instance->output.is_some()) draft.output = Some(Element(*draft.instance->output));
        else if (!data.precompose) return Err(ComposeError(ComposeStage::Type));
        if (data.precompose) {
            for (const auto &child : draft.instance->children) {
                if (data.premeasure && draft.scope.is_none() && !draft.precompose_failed) {
                    draft.reused_children.push(child.clone());
                    draft.committed_children.push(child.clone());
                } else {
                    retire(child.clone());
                }
            }
            return Ok(rstd::move(plan));
        } else {
            if (draft.promoted && draft.scope.is_none())
                for (const auto &action : scope->deferred_actions)
                    actions.push(CommitAction::make([action = action.clone()](CommittedContext &context) mutable {
                        return (*action.get())->operator()(context);
                    }));
            auto modified = apply_modifiers(Element(*draft.output), data.modifiers);
            if (modified.is_err()) return Err(modified.unwrap_err());
            children.push(modified.unwrap());
        }
        if (refresh_modifiers && (!data.modifiers.is_empty() || !draft.instance->element.data().modifiers.is_empty())) force = true;
    } else {
        for (const auto &child : data.children) children.push(Element(child));
        if ((data.kind == ElementKind::Fragment || data.kind == ElementKind::Provider) && !data.modifiers.is_empty()) {
            if (children.len() != usize(1)) {
                auto error = ComposeError(ComposeStage::Type);
                error.operation = String::make("component modifiers require exactly one root"_str); return Err(rstd::move(error));
            }
            auto modified = apply_modifiers(rstd::move(children[usize()]), data.modifiers);
            if (modified.is_err()) return Err(modified.unwrap_err());
            children[usize()] = modified.unwrap();
        }
        if (data.kind == ElementKind::Native) {
            if (force || draft.fresh) {
                auto node = draft.fresh ? edit.create(data.layout, data.style) : edit.existing(*draft.instance->id);
                draft.node = Some(node);
                if (draft.fresh) edit.style(node, data.style);
                else edit.style(node, NodeStylePatch::difference(draft.instance->element.data().style, data.style));
                if (data.text.is_some()) {
                    if (draft.fresh) edit.text(node, UiTextValue(*data.text));
                    else edit.text(node, UiTextValue(*data.text), *draft.instance->element.data().text);
                }
                if (data.image.is_some()) {
                    auto &source = *data.image;
                    UiImageValue value{source.data.is_some() ? Some(ImageData(*source.data)) : None<ImageData>(),
                        source.fit, source.sampling, source.tint, source.radial};
                    if (draft.fresh) edit.image(node, rstd::move(value));
                    else edit.image(node, rstd::move(value), *draft.instance->element.data().image);
                }
            }
        } else if (data.kind == ElementKind::Custom) {
            if (force || draft.fresh) {
                PrepareContext context(*this, draft);
                auto adapter = data.adapter.clone();
                auto prepared = [&]() -> ComposeResult<UiNodeRef> {
                    auto read = session.document().acquire_read();
                    if (read.is_err()) return Err(ComposeError(ComposeStage::Prepare, Some(read.unwrap_err())));
                    auto guard = read.unwrap(); return adapter.get()->value->prepare(context, draft.instance->id, data.style);
                }();
                if (prepared.is_err()) return Err(prepared.unwrap_err()); draft.node = Some(prepared.unwrap());
            }
        }
    }
    if (draft.owns_children && !children.is_empty()) return Err(ComposeError(ComposeStage::Prepare, Some(UiError::InvalidValue)));
    if (data.kind == ElementKind::Provider) {
        auto provided = environment.extended(data.locals.as_slice());
        if (provided.is_err()) return Err(provided.unwrap_err());
        environment = provided.unwrap();
    }
    Vec<bool> used;
    for (usize i{}; i < draft.instance->children.len(); ++i) used.push(false);
    Vec<ComposeRoot> descendants;
    for (usize i{}; i < children.len(); ++i) {
        const auto &child = children[i].data();
        if (identified(child))
            for (usize j{}; j < i; ++j)
                if (same_identity(child, children[j].data()))
                    return Err(ComposeError(ComposeStage::Key));
        rstd::rc::Rc<ComposeInstance> old;
        for (usize j{}; j < draft.instance->children.len(); ++j) {
            if (used[j]) continue;
            const auto &candidate = draft.instance->children[j]->element.data();
            bool match = identified(child) ? same_identity(child, candidate) : !identified(candidate) && i == j;
            if (match) { old = draft.instance->children[j].clone(); used[j] = true; break; }
        }
        if (old && !force && child.precompose == old->element.data().precompose &&
            child.retain_native == old->element.data().retain_native &&
            child.premeasure == old->element.data().premeasure &&
            compatible(child, old->element.data()) && !dirty(*old.get(), environment)) {
            auto claimed = claim_reused(old);
            if (claimed.is_err()) return Err(claimed.unwrap_err());
            committed_roots(*old.get(), descendants);
            draft.reused_children.push(old.clone()); draft.committed_children.push(rstd::move(old)); continue;
        }
        auto built = reconcile(children[i], rstd::move(old), force, depth + 1, environment);
        if (built.is_err()) {
            auto error = built.unwrap_err();
            if (!error.source.line()) error.source = child.source;
            auto label = child.key.is_some() ? child.key->label() : child.identity.is_some()
                ? rstd::format("{}#{}", child.identity->site, child.identity->occurrence) : rstd::format("{}", i);
            error.path = rstd::format("{}/{}", label.as_str(), error.path.as_str()); return Err(rstd::move(error));
        }
        auto child_plan = built.unwrap();
        child_plan.get()->position = i;
        for (auto root : child_plan->roots) descendants.push(ComposeRoot(root));
        draft.committed_children.push(child_plan->instance.clone());
        draft.children.push(rstd::move(child_plan));
    }
    for (usize i{}; i < used.len(); ++i) if (!used[i]) retire(draft.instance->children[i].clone());
    if (data.kind == ElementKind::Native || data.kind == ElementKind::Custom) {
        Vec<NodeStateKey> input_order;
        for (usize i{}; i < data.behaviors.len(); ++i) {
            const auto &declaration = data.behaviors[i];
            for (usize j{}; j < i; ++j)
                if (declaration.key == data.behaviors[j].key) return Err(ComposeError(ComposeStage::Key));
            rstd::rc::Rc<AttachedBehaviorSlot> slot;
            for (const auto &previous : draft.instance->attached) if (previous->key == declaration.key) {
                if (previous->config->type() != declaration.config->type()) return Err(ComposeError(ComposeStage::Type));
                slot = previous.clone(); break;
            }
            auto created = !slot;
            {
                auto reading = session.document().acquire_read();
                if (reading.is_err()) return Err(ComposeError(ComposeStage::Prepare, Some(reading.unwrap_err())));
                auto guard = reading.unwrap();
                if (created) {
                    auto made = declaration.config->create(); if (made.is_err()) return Err(made.unwrap_err());
                    slot = rstd::rc::make_rc<AttachedBehaviorSlot>(Key(declaration.key), made.unwrap(), declaration.config.clone());
                }
                auto valid = declaration.config->validate(*slot.get()->state.get());
                if (valid.is_err()) return Err(valid.unwrap_err());
            }
            if (declaration.config->input()) {
                input_order.push(NodeStateKey(slot->role));
                if (created) {
                    if (draft.node.is_none()) draft.node = Some(edit.existing(*draft.instance->id));
                    behaviors.push(Box<rstd::dyn<rstd::FnMut<Result<empty, InputError>(EventRouter &, EventRouter::BehaviorEdit &, const UiDocument::PreparedEdit &)>>>::make(
                        [slot = slot.clone(), node = *draft.node](EventRouter &input, EventRouter::BehaviorEdit &edit, const UiDocument::PreparedEdit &document) mutable {
                            auto binding = slot->config->prepare_input(input, edit, document, node, slot->role, *slot.get()->state.get());
                            if (binding.is_err()) return Result<empty, InputError>(Err(binding.unwrap_err()));
                            slot.get()->input = Some(binding.unwrap()); return Result<empty, InputError>(Ok(empty{}));
                        }));
                }
            }
            draft.attached.push({rstd::move(slot), declaration.config.clone()});
        }
        for (const auto &previous : draft.instance->attached) {
            bool retained{};
            for (const auto &next : draft.attached) retained |= next.slot.get() == previous.get();
            if (retained) continue;
            retired_behaviors.push(previous.clone());
            if (previous->input.is_some()) {
                auto target = *draft.instance->id; auto role = previous->role;
                behaviors.push(Box<rstd::dyn<rstd::FnMut<Result<empty, InputError>(EventRouter &, EventRouter::BehaviorEdit &, const UiDocument::PreparedEdit &)>>>::make(
                    [target, role](EventRouter &input, EventRouter::BehaviorEdit &edit, const UiDocument::PreparedEdit &) {
                        return input.retire_behavior(edit, target, role);
                    }));
            }
        }
        if (!input_order.is_empty()) {
            if (draft.node.is_none()) draft.node = Some(edit.existing(*draft.instance->id));
            behaviors.push(Box<rstd::dyn<rstd::FnMut<Result<empty, InputError>(EventRouter &, EventRouter::BehaviorEdit &, const UiDocument::PreparedEdit &)>>>::make(
                [node = *draft.node, order = rstd::move(input_order)](EventRouter &input, EventRouter::BehaviorEdit &edit, const UiDocument::PreparedEdit &document) mutable {
                    return input.prepare_behavior_order(edit, document.id(node), rstd::move(order));
                }));
        }
        if (!draft.owns_children) {
            if (draft.node.is_none()) draft.node = Some(edit.existing(*draft.instance->id));
            auto kind = session.document().node_kind(edit, *draft.node);
            if (kind.is_err()) return Err(ComposeError(ComposeStage::Prepare, Some(kind.unwrap_err())));
            if (accepts_children(kind.unwrap())) edit.claim_children(*draft.node, tree_owner);
        }
        Vec<ComposeRoot> before;
        for (const auto &child : draft.instance->children) committed_roots(*child.get(), before);
        if (!draft.owns_children && !same_roots(before, descendants)) {
            if (draft.node.is_none()) draft.node = Some(edit.existing(*draft.instance->id));
            this->children(*draft.node, descendants);
        }
        draft.roots.push({draft.node, draft.instance->id});
    } else draft.roots = rstd::move(descendants);
    if (movable_claim.is_some()) for (auto root : draft.roots) claims[*movable_claim].after.push(ComposeRoot(root));
    return Ok(rstd::move(plan));
}
} // namespace vui::compose
