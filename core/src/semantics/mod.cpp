module vui;
import :semantics;
import :document.reveal;
import :document.snapshot;
import :node.state;
import :input.scopes;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
namespace {
struct StoredSemantics {
    Option<SemanticsProperties> defaults, custom;

    auto get() const -> const SemanticsProperties * {
        return custom.is_some() ? &*custom : defaults.is_some() ? &*defaults : nullptr;
    }
};

auto semantics_key() -> NodeStateKey { return NodeStateKey::of<Semantics>(); }

auto copy_properties(const SemanticsProperties &value) -> SemanticsProperties {
    return {value.role, String::make(value.label.as_str()), String::make(value.value.as_str()),
            value.actions, value.merge_descendants, value.checked};
}

auto equal_properties(const SemanticsProperties &a, const SemanticsProperties &b) -> bool {
    return a.role == b.role && a.label.as_str() == b.label.as_str() &&
           a.value.as_str() == b.value.as_str() && a.actions == b.actions &&
           a.merge_descendants == b.merge_descendants && a.checked == b.checked;
}

auto map_bounds(Rect rect, Transform transform) -> Rect {
    auto origin = transform.map({rect.x, rect.y});
    auto end_x = transform.map({rect.x + rect.width, rect.y});
    auto end_y = transform.map({rect.x, rect.y + rect.height});
    auto opposite = transform.map({rect.x + rect.width, rect.y + rect.height});
    for (auto corner : {origin, end_x, end_y, opposite})
        if (!finite(corner.x) || !finite(corner.y)) return {};
    using rstd::cmp::max;
    using rstd::cmp::min;
    auto left = min(min(origin.x, end_x.x), min(end_y.x, opposite.x));
    auto top = min(min(origin.y, end_x.y), min(end_y.y, opposite.y));
    auto right = max(max(origin.x, end_x.x), max(end_y.x, opposite.x));
    auto bottom = max(max(origin.y, end_x.y), max(end_y.y, opposite.y));
    return {left, top, right - left, bottom - top};
}

auto intersect(Rect a, Rect b) -> Rect {
    auto left = rstd::cmp::max(a.x, b.x), top = rstd::cmp::max(a.y, b.y);
    auto right = rstd::cmp::min(a.x + a.width, b.x + b.width);
    auto bottom = rstd::cmp::min(a.y + a.height, b.y + b.height);
    return {left, top, right - left, bottom - top};
}

auto has_area(Rect rect) -> bool { return rect.valid() && rect.width > 0 && rect.height > 0; }

auto axis_aligned(Transform transform) -> bool {
    return (transform.b == 0 && transform.c == 0) || (transform.a == 0 && transform.d == 0);
}

struct ClipChain {
    Rect bounds;
    Transform transform;
    const ClipChain *previous;
};

auto project(Rect bounds, Transform transform, Rect clip, const ClipChain *chain) -> Rect {
    auto visible = intersect(clip, map_bounds(bounds, transform));
    if (!has_area(visible) || (axis_aligned(transform) && !chain)) return visible;
    DocumentRevealRegion region(bounds);
    if (region.map(transform).is_err() || region.clip(clip).is_err()) return {};
    for (; chain; chain = chain->previous) {
        auto inverse = chain->transform.inverse();
        if (inverse.is_none() || region.map(*inverse).is_err() || region.clip(chain->bounds).is_err() ||
            region.map(chain->transform).is_err())
            return {};
        if (region.points.is_empty()) return {};
    }
    auto result = region.bounds();
    return result.is_ok() ? result.unwrap() : Rect{};
}

void append_label(String &label, ref<str> text) {
    if (text.is_empty()) return;
    if (!label.is_empty()) label.push_str(" "_str);
    label.push_str(text);
}
} // namespace

auto SemanticsSnapshot::get(SemanticsId id) const -> const SemanticsNode * {
    for (const auto &node : nodes())
        if (node.id == id) return &node;
    return nullptr;
}

auto Semantics::set(UiId id, SemanticsProperties properties) -> Result<empty, UiError> {
    return set_impl(id, rstd::move(properties), false);
}

auto Semantics::set_default(UiId id, SemanticsProperties properties) -> Result<empty, UiError> {
    return set_impl(id, rstd::move(properties), true);
}

auto Semantics::set_impl(UiId id, SemanticsProperties properties, bool defaults) -> Result<empty, UiError> {
    if (document_.routing() || !document_.can_animate()) return Err(UiError::Routing);
    constexpr auto valid_actions = semantics_action_mask(SemanticsAction::Focus) * 2u - 1u;
    if (static_cast<unsigned>(properties.role) > static_cast<unsigned>(SemanticsRole::Image) ||
        (properties.actions & ~valid_actions))
        return Err(UiError::InvalidValue);
    if (auto stored = document_.state<StoredSemantics>(id, semantics_key())) {
        auto &slot = defaults ? stored->defaults : stored->custom;
        if (slot.is_some() && equal_properties(*slot, properties)) return Ok(empty{});
        slot = Some(rstd::move(properties));
    } else {
        StoredSemantics state;
        (defaults ? state.defaults : state.custom) = Some(rstd::move(properties));
        auto attached = document_.attach_state(id, rstd::move(state), semantics_key());
        if (attached.is_err()) return Err(attached.unwrap_err());
    }
    document_.bindings().wake();
    return Ok(empty{});
}

auto Semantics::clear(UiId id) -> Result<empty, UiError> {
    if (document_.routing() || !document_.can_animate()) return Err(UiError::Routing);
    if (!document_.valid(id)) return Err(UiError::InvalidId);
    if (auto stored = document_.state<StoredSemantics>(id, semantics_key()); stored && stored->custom.is_some()) {
        stored->custom = None();
        document_.bindings().wake();
    }
    return Ok(empty{});
}

auto Semantics::snapshot() const -> SemanticsSnapshot {
    Vec<SemanticsNode> nodes;
    auto snapshot = input_.snapshot();
    if (snapshot.is_none() || snapshot->root().is_none()) return SemanticsSnapshot(rstd::move(nodes));
    auto focused = input_.focused();
    struct Merge {
        usize owner;
        bool collect_label;
    };
    auto visit = [&](auto &self, UiId id, Transform transform, Rect clip, const ClipChain *chain, bool enabled,
                     Option<SemanticsId> parent, Option<Merge> merge, Index depth) -> void {
        if (depth >= 256) return;
        auto visual = snapshot->get(id);
        if (!visual || !document_.valid(id) || !visual->properties.visible || visual->properties.opacity == 0)
            return;
        const auto &properties = visual->properties;
        transform = transform * properties.transform;
        if (transform.inverse().is_none()) return;
        ClipChain own_clips[2];
        auto restrict_clip = [&](const Clip &local_clip, Transform local_transform, ClipChain &entry) {
            if (!local_clip.enabled) return;
            auto world = transform * local_transform;
            auto bounds = local_clip.bounds();
            clip = intersect(clip, map_bounds(bounds, world));
            if (!axis_aligned(world)) {
                entry = {bounds, world, chain};
                chain = &entry;
            }
        };
        restrict_clip(properties.clip, properties.clip_transform, own_clips[0]);
        restrict_clip(properties.layout_clip, properties.layout_clip_transform, own_clips[1]);
        if (!has_area(clip)) return;

        enabled = enabled && visual->enabled;
        auto stored = document_.state<StoredSemantics>(id, semantics_key());
        auto description = stored ? stored->get() : nullptr;
        if (description && input_.allows_semantics(*snapshot, id)) {
            auto bounds = project(visual->layout_bounds, transform, clip, chain);
            auto mount = document_.mount_state(id);
            if (has_area(bounds) && mount.is_ok() && mount.unwrap().mounted &&
                mount.unwrap().generation == visual->mount_generation) {
                const auto &value = *description;
                auto independent = value.actions || value.role == SemanticsRole::Dialog || value.merge_descendants;
                if (merge.is_some() && !independent) {
                    if (merge->collect_label) append_label(nodes[merge->owner].properties.label, value.label.as_str());
                } else {
                    auto exported = copy_properties(value);
                    if (!enabled) exported.actions = 0;
                    else if (!input_.eligible(*snapshot, id, false))
                        exported.actions &= ~semantics_action_mask(SemanticsAction::Focus);
                    SemanticsId identity{id, visual->mount_generation};
                    merge = value.merge_descendants
                                ? Some(Merge{nodes.len(), value.label.is_empty()})
                                : None<Merge>();
                    nodes.push({identity, parent, bounds, rstd::move(exported), enabled, focused == Some(id)});
                    parent = Some(identity);
                }
            }
        }
        for (auto child : visual->children)
            self(self, child, transform, clip, chain, enabled, parent, merge, depth + 1);
    };
    if (auto root = snapshot->get(*snapshot->root())) {
        auto surface = root->layout_slot.is_some() ? *root->layout_slot : root->layout_bounds;
        visit(visit, root->id, {}, surface, nullptr, true, None(), None(), 0);
    }
    return SemanticsSnapshot(rstd::move(nodes));
}

auto Semantics::perform(SemanticsId id, SemanticsAction action) -> Result<bool, InputError> {
    auto mask = semantics_action_mask(action);
    if (!mask) return Err(InputError::InvalidEvent);
    auto mount = document_.mount_state(id.node);
    if (mount.is_err() || !mount.unwrap().mounted || mount.unwrap().generation != id.mount_generation)
        return Ok(false);
    for (auto current = Some(id.node); current.is_some();) {
        auto style = document_.resolved_style(*current);
        auto relation = document_.parent_relation(*current);
        if (style.is_err() || relation.is_err() || !style->visible || style->opacity == 0 || !style->enabled)
            return Ok(false);
        auto parent = relation.unwrap();
        current = parent.is_some() ? Some(parent->parent) : None<UiId>();
    }
    auto state = snapshot();
    auto node = state.get(id);
    if (!node || !(node->properties.actions & mask)) return Ok(false);
    return input_.dispatch_semantics(id.node, action);
}
} // namespace vui
