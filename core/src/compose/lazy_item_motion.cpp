module vui.compose;
import :lazy_item_motion;
import :runtime;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui::compose {
namespace {
constexpr Index max_ghosts = 16;
constexpr Index max_ghost_nodes = 128;

auto same_item(Index index, const Option<Key> &key, Index other_index, const Option<Key> &other_key) -> bool {
    return key == other_key && (key || index == other_index);
}

auto has_key(const LazyContent &content, Index index, const Option<Key> &key) -> bool {
    if (!key) return index < content.count();
    if (!content.count()) return false;
    const auto center = index < content.count() ? index : content.count() - 1;
    if (content.describe(center).key == key) return true;
    constexpr Index radius = 128;
    for (Index delta = 1; delta <= radius; ++delta) {
        if (delta <= center && content.describe(center - delta).key == key) return true;
        if (delta < content.count() - center && content.describe(center + delta).key == key) return true;
    }
    return false;
}

auto animation_options(AnimationOptions options) -> AnimationOptions {
    options.end = AnimationEndPolicy::CommitFinal;
    return options;
}

auto ui_error(AnimationError error) -> UiError {
    return error == AnimationError::Limit ? UiError::Limit : UiError::InvalidValue;
}

void cancel(UiDocument &doc, Option<AnimationId> id) {
    if (id && doc.animation_state(*id).is_ok()) (void)doc.cancel_animation(*id);
}

auto translation(Point value) -> Transform {
    return {.tx = value.x, .ty = value.y};
}
}

struct LazyItemAnimator::LayerElement {
    rstd::rc::Rc<Layer> state;
    auto prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef> {
        auto root = previous ? context.edit().existing(*previous) : context.edit().create(LayoutKind::Stack, style);
        context.owns_children();
        context.on_mount([state = state, target = context.target()](CommittedContext &committed) mutable -> ComposeResult<EffectLease> {
            auto id = committed.resolve(target);
            if (id.is_err()) return Err(id.unwrap_err());
            state.get()->id = Some(id.unwrap());
            return Ok(EffectLease([state = state]() mutable { state.get()->id = None(); }));
        });
        return Ok(root);
    }
};

auto LazyItemAnimator::layer(rstd::source_location source) const -> Element {
    NodeStyle style{.hit_test_visible = false, .enabled = false, .hit_self = false};
    return element(LayerElement{layer_.clone()}, {}, source).key(Key("$lazy-exiting-items"_str)).style(style);
}

auto LazyItemAnimator::prepare_removals(UiDocument &document, const LazyContent &content) const
    -> Vec<LazyItemGhost> {
    Vec<LazyItemGhost> removed;
    if (!content_ || content_->identity() == content.identity()) return removed;
    Index active{};
    for (const auto &entry : exiting_) if (document.valid(entry.node)) ++active;
    for (const auto &item : items_) {
        const auto &geometry = item.geometry;
        if (active + removed.len().to_primitive() >= max_ghosts) break;
        if (!geometry.options.fade_out || !geometry.options.fade_out->duration_ns ||
            has_key(content, geometry.index, geometry.key)) continue;
        auto snapshot = document.capture_painted_subtree(geometry.node, max_ghost_nodes);
        if (snapshot.is_ok()) {
            auto key = geometry.key ? Some(Key(*geometry.key)) : None<Key>();
            removed.push({geometry.index, rstd::move(key), snapshot.unwrap(), *geometry.options.fade_out});
        }
    }
    return removed;
}

auto LazyItemAnimator::start_removals(UiDocument &document, Vec<LazyItemGhost> ghosts) -> Result<empty, UiError> {
    if (ghosts.is_empty() || !layer_->id) return Ok(empty{});
    for (auto &ghost : ghosts) {
        auto appended = document.append_painted_subtree(*layer_->id, rstd::move(ghost.paint));
        if (appended.is_err()) return Err(appended.unwrap_err());
        auto id = appended.unwrap();
        auto animated = document.animate_opacity(id, 0, animation_options(ghost.options));
        if (animated.is_err()) {
            (void)document.destroy(id);
            return Err(ui_error(animated.unwrap_err()));
        }
        auto done = document.on_animation_end(animated.unwrap(), [id](UiDocument &doc, const AnimationEvent &) {
            if (doc.valid(id)) (void)doc.destroy(id);
        });
        if (done.is_err()) {
            (void)document.destroy(id);
            return Err(ui_error(done.unwrap_err()));
        }
        exiting_.push({id, ghost.index, rstd::move(ghost.key)});
    }
    return Ok(empty{});
}

auto LazyItemAnimator::update(UiDocument &doc, const LazyContent &content, Vec<LazyAnimatedItem> geometry,
                              Point viewport_origin, Point anchor_correction) -> Result<empty, UiError> {
    auto same = [](const LazyAnimatedItem &a, const LazyAnimatedItem &b) {
        return a.node == b.node && same_item(a.index, a.key, b.index, b.key);
    };
    const bool changed = content_ && content_->identity() != content.identity();
    for (const auto &old : items_) {
        bool current{};
        for (const auto &item : geometry) current |= same(old.geometry, item);
        if (current || !doc.valid(old.geometry.node)) continue;
        cancel(doc, old.move);
        cancel(doc, old.opacity);
        if (old.move) (void)doc.set_transform(old.geometry.node, {});
        if (old.opacity) (void)doc.set_opacity(old.geometry.node, 1);
    }
    for (usize i{}; i < exiting_.len(); ++i) {
        auto &entry = exiting_[i];
        if (!doc.valid(entry.node)) continue;
        bool back{};
        for (const auto &item : geometry)
            back |= same_item(entry.index, entry.key, item.index, item.key);
        if (back) {
            auto destroyed = doc.destroy(entry.node);
            if (destroyed.is_err()) return destroyed;
        } else if (!entry.position_adjusted && changed) {
            auto transformed = doc.set_transform(entry.node, translation(anchor_correction));
            if (transformed.is_err()) return transformed;
            entry.position_adjusted = true;
        }
    }
    exiting_.retain([&](const Exiting &entry) { return doc.valid(entry.node); });
    Vec<Item> next;
    next.reserve(geometry.len());
    for (usize i{}; i < geometry.len(); ++i) {
        auto &item = geometry[i];
        if (!item.options.valid()) return Err(UiError::InvalidValue);
        const Item *before = nullptr;
        for (const auto &old : items_) {
            if (!same(old.geometry, item)) continue;
            before = &old;
            break;
        }
        Item current{rstd::move(item)};
        const auto &value = current.geometry;
        if (before) {
            current.move = before->move;
            current.opacity = before->opacity;
            Point delta{before->geometry.viewport_bounds.x - viewport_origin_.x -
                            value.viewport_bounds.x + viewport_origin.x + anchor_correction.x,
                        before->geometry.viewport_bounds.y - viewport_origin_.y -
                            value.viewport_bounds.y + viewport_origin.y + anchor_correction.y};
            if (!value.sticky && !before->geometry.sticky && value.options.placement &&
                value.options.placement->duration_ns &&
                (__builtin_fabs(delta.x) > .01 || __builtin_fabs(delta.y) > .01)) {
                auto style = doc.resolved_style(value.node);
                if (style.is_err()) return Err(style.unwrap_err());
                delta.x += style->transform.tx;
                delta.y += style->transform.ty;
                cancel(doc, before->move);
                auto transform = doc.set_transform(value.node, translation(delta));
                if (transform.is_err()) return transform;
                auto animated = doc.animate_transform(value.node, {}, animation_options(*value.options.placement));
                if (animated.is_err()) {
                    (void)doc.set_transform(value.node, {});
                    return Err(ui_error(animated.unwrap_err()));
                }
                current.move = Some(animated.unwrap());
            }
        } else if (changed && value.options.fade_in && value.options.fade_in->duration_ns &&
                   !has_key(*content_, value.index, value.key)) {
            auto opacity = doc.set_opacity(value.node, 0);
            if (opacity.is_err()) return opacity;
            auto animated = doc.animate_opacity(value.node, 1, animation_options(*value.options.fade_in));
            if (animated.is_err()) {
                (void)doc.set_opacity(value.node, 1);
                return Err(ui_error(animated.unwrap_err()));
            }
            current.opacity = Some(animated.unwrap());
        }
        next.push(rstd::move(current));
    }
    content_ = Some(LazyContent(content));
    items_ = rstd::move(next);
    viewport_origin_ = viewport_origin;
    return Ok(empty{});
}

void LazyItemAnimator::reset() {
    exiting_.clear();
    items_.clear();
    content_ = None();
    viewport_origin_ = {};
}
} // namespace vui::compose
