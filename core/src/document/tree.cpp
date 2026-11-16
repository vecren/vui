module vui;
import :document.interface;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
void UiDocument::changed(bool layout, Option<UiId> target) {
    if (target.is_some()) {
        bool known{};
        for (auto id : input_changes_)
            known |= id == *target;
        if (!known)
            input_changes_.push(UiId(*target));
        auto node = resolve(*target);
        if (node && node->viewport.is_some())
            for (auto child : node->children) {
                bool present{};
                for (auto changed_id : input_changes_)
                    present |= changed_id == child;
                if (!present)
                    input_changes_.push(UiId(child));
            }
    }
    if (revision_ == ~Serial{})
        rstd::panic("vui document revision exhausted");
    ++revision_;
    layout_dirty_ = layout_dirty_ || layout;
    if (layout) {
        if (target.is_some())
            dirty_measure(*target);
        else
            for (auto &slot : slots_)
                if (slot.node.is_some()) {
                    slot.node->measure_dirty = true;
                    slot.node->arrange_dirty = true;
                    slot.node->subtree_arrange_dirty = true;
                }
    }
}

auto UiDocument::children_access(UiId id, const UiTreeAccess &access) -> Result<empty, UiError> {
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (!access.valid(session_) || (node->children_owner.is_some() && node->children_owner->alive() && !access.allows(*node->children_owner)))
        return Err(UiError::PropertyOwned);
    return Ok(empty{});
}

auto UiDocument::relation_access(UiId id, const UiTreeAccess &access) -> Result<empty, UiError> {
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (!access.valid(session_)) return Err(UiError::PropertyOwned);
    return node->parent.is_some() ? children_access(*node->parent, access) : Result<empty, UiError>(Ok(empty{}));
}

void UiDocument::remount(UiId id) {
    detach_animations(id);
    auto &node = *resolve(id);
    for (auto &observation : node.phase_observations) {
        if (observation) observation.get()->close();
        observation = {};
    }
    node.paint_dirty = true;
    node.measure_dirty = true;
    node.arrange_dirty = true;
    node.subtree_arrange_dirty = true;
    if (node.mount_generation == ~Serial{})
        rstd::panic("vui mount generation exhausted");
    for (unsigned property{}; property < static_cast<unsigned>(PresentationProperty::Count); ++property)
        revoke_property(node, static_cast<PresentationProperty>(property));
    ++node.mount_generation;
    for (auto child : node.children)
        remount(child);
}

auto UiDocument::mounted(UiId id) -> bool {
    auto cursor = Some(id);
    while (cursor.is_some()) {
        if (cursor == root_)
            return true;
        auto node = resolve(*cursor);
        if (!node)
            return false;
        cursor = node->parent;
    }
    return false;
}

void UiDocument::erase(UiId id) {
    auto &slot = slots_[usize(id.slot)];
    if (slot.node->viewport.is_some() && (*slot.node->viewport)->observer.is_some()) (*(*slot.node->viewport)->observer).get()->changed.close();
    if (slot.node->layout_binding.is_some())
        slot.node->layout_binding->disconnect();
    if (slot.node->paint_binding.is_some())
        slot.node->paint_binding->disconnect();
    for (const auto &entry : slot.node->modifiers) entry.binding.disconnect();
    if (auto state = slot.node->subcomposition.upgrade(); state.is_some()) state->get()->guard.disconnect();
    auto children = rstd::move(slot.node->children);
    for (auto child : children)
        erase(child);
    slot.node = None();
    if (slot.generation != ~Serial{})
        ++slot.generation;
}

auto UiDocument::claim_children(UiId id, const UiTreeOwner &owner) -> Result<empty, UiError> {
    if (!can_route()) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    auto permit = owner.permit();
    if (!accepts_children(node->kind) || !permit.alive() || permit.document() != session_ ||
        (node->children_owner.is_some() && node->children_owner->alive() && !permit.same_owner(*node->children_owner)))
        return Err(UiError::PropertyOwned);
    if (node->children_owner.is_some() && permit.same_owner(*node->children_owner)) return Ok(empty{});
    node->children_owner = Some(rstd::move(permit)); changed(false); return Ok(empty{});
}

auto UiDocument::claim_children(UiId id) -> Result<UiTreeOwner, UiError> {
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (!node->children.is_empty()) return Err(UiError::PropertyOwned);
    auto owner = make_tree_owner();
    auto claimed = claim_children(id, owner); if (claimed.is_err()) return Err(claimed.unwrap_err());
    return Ok(rstd::move(owner));
}

auto UiDocument::release_children(UiId id, const UiTreeOwner &owner) -> Result<empty, UiError> {
    if (!can_route()) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (!owner.alive() || owner.permit().document() != session_ || !node->children_owner.is_some() || !owner.permit().same_owner(*node->children_owner))
        return Err(UiError::PropertyOwned);
    node->children_owner = None(); changed(false); return Ok(empty{});
}

auto UiDocument::parent_relation(UiId child) -> Result<Option<UiRelation>, UiError> {
    auto node = resolve(child);
    if (!node)
        return Err(UiError::InvalidId);
    return Ok(node->parent.is_some()
                  ? Some(UiRelation{*node->parent, child, node->relation_generation})
                  : None<UiRelation>());
}

auto UiDocument::is_descendant(UiId root, UiId child) -> Result<bool, UiError> {
    if (!resolve(root) || !resolve(child))
        return Err(UiError::InvalidId);
    auto cursor = Some(child);
    for (Index depth{}; cursor.is_some() && depth < depth_limit_; ++depth) {
        if (*cursor == root)
            return Ok(true);
        cursor = resolve(*cursor)->parent;
    }
    return Ok(false);
}

auto UiDocument::child_relation(UiId parent, UiId child) -> Result<UiRelation, UiError> {
    auto p = resolve(parent), c = resolve(child);
    if (!p || !c || c->parent != Some(parent))
        return Err(UiError::InvalidId);
    return Ok(UiRelation{parent, child, c->relation_generation});
}

auto UiDocument::attach(UiId parent, UiId child, const UiTreeAccess &access) -> Result<empty, UiError> {
    if (routing_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto p = resolve(parent), c = resolve(child);
    if (!p || !c)
        return Err(UiError::InvalidId);
    auto target_access = children_access(parent, access); if (target_access.is_err()) return target_access;
    auto source_access = relation_access(child, access); if (source_access.is_err()) return source_access;
    if (!accepts_children(p->kind))
        return Err(UiError::MissingChild);
    if (p->kind == LayoutKind::Viewport && !p->children.is_empty() &&
        p->children[usize()] != child)
        return Err(UiError::InvalidValue);
    if (root_.is_some() && child == *root_)
        return Err(UiError::Root);
    Index ancestor_depth{};
    auto ancestor = Some(parent);
    while (ancestor.is_some()) {
        if (*ancestor == child)
            return Err(UiError::Cycle);
        ancestor = resolve(*ancestor)->parent;
        ++ancestor_depth;
    }
    struct Visit {
        UiId id;
        Index depth;
    };
    Vec<Visit> visits;
    visits.push({child, ancestor_depth});
    for (usize i{}; i < visits.len(); ++i) {
        auto visit = visits[i];
        if (visit.depth >= depth_limit_)
            return Err(UiError::Limit);
        for (auto descendant : resolve(visit.id)->children)
            visits.push({descendant, visit.depth + 1});
    }
    if (c->parent.is_some() && *c->parent == parent)
        return Ok(empty{});
    if (c->parent.is_some()) {
        dirty_measure(*c->parent);
        auto previous = resolve(*c->parent);
        previous->children.retain([&](UiId value) { return value != child; });
    }
    ++c->relation_generation;
    c->parent = Some(parent);
    p->children.push(UiId(child));
    remount(child);
    changed(true, Some(parent));
    notify_targets();
    return Ok(empty{});
}

auto UiDocument::mount(UiId root) -> Result<empty, UiError> {
    if (routing_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(root);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->parent.is_some())
        return Err(UiError::Root);
    if (root_.is_some() && *root_ == root)
        return Ok(empty{});
    if (root_.is_some())
        remount(*root_);
    remount(root);
    root_ = Some(root);
    node->position = {};
    changed(true, Some(root));
    notify_targets();
    return Ok(empty{});
}

auto UiDocument::detach(UiId id, const UiTreeAccess &access) -> Result<empty, UiError> {
    if (routing_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    auto allowed = relation_access(id, access); if (allowed.is_err()) return allowed;
    remount(id);
    if (node->parent.is_some()) {
        ++node->relation_generation;
        dirty_measure(*node->parent);
        resolve(*node->parent)->children.retain([&](UiId child) { return child != id; });
        node->parent = None();
    }
    if (root_.is_some() && *root_ == id)
        root_ = None();
    changed(true, Some(id));
    notify_targets();
    return Ok(empty{});
}

auto UiDocument::destroy(UiId id, const UiTreeAccess &access) -> Result<empty, UiError> {
    auto result = detach(id, access);
    if (result.is_err())
        return result;
    erase(id);
    notify_targets();
    return Ok(empty{});
}

auto UiDocument::reorder(UiId parent, UiId child, usize destination, const UiTreeAccess &access) -> Result<empty, UiError> {
    if (routing_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(parent);
    if (!node)
        return Err(UiError::InvalidId);
    auto allowed = children_access(parent, access); if (allowed.is_err()) return allowed;
    if (destination >= node->children.len())
        return Err(UiError::MissingChild);
    usize index{};
    while (index < node->children.len() && node->children[index] != child)
        ++index;
    if (index == node->children.len())
        return Err(UiError::MissingChild);
    if (index != destination) {
        auto id = node->children[index];
        if (index < destination)
            for (auto i = index; i < destination; ++i)
                node->children[i] = node->children[i + usize(1)];
        else
            for (auto i = index; i > destination; --i)
                node->children[i] = node->children[i - usize(1)];
        node->children[destination] = id;
        changed(true, Some(parent));
    }
    return Ok(empty{});
}

auto UiDocument::mount_state(UiId id) -> Result<UiMountState, UiError> {
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    return Ok(UiMountState{mounted(id), node->mount_generation});
}
} // namespace vui
