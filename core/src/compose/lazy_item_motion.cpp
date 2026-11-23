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

struct BoundaryItem {
    usize slot;
    Index index;
    Rect bounds;
    bool before;
    Scalar distance{}, position{};
};

auto outside(const LazyItemViewport &viewport, Index index) -> Option<bool> {
    if (viewport.first && index < *viewport.first) return Some(true);
    if (viewport.last && index > *viewport.last) return Some(false);
    return None();
}

void position_at_edges(Vec<BoundaryItem> &items, const LazyItemViewport &viewport) {
    rstd::slice_::sort_unstable_by(items.as_mut_slice().as_mut_ref(), [](const BoundaryItem &a, const BoundaryItem &b) {
        if (a.before != b.before) return a.before;
        return a.before ? a.index > b.index : a.index < b.index;
    });
    const auto horizontal = viewport.horizontal;
    const auto extent = horizontal ? viewport.size.width : viewport.size.height;
    for (usize i{}; i < items.len(); ++i) {
        auto &item = items[i];
        const auto across = horizontal ? item.bounds.y : item.bounds.x;
        const auto end = across + (horizontal ? item.bounds.height : item.bounds.width);
        const auto length = horizontal ? item.bounds.width : item.bounds.height;
        Scalar occupied{};
        for (usize j{}; j < i; ++j) {
            const auto &other = items[j];
            if (item.before != other.before) continue;
            const auto start = horizontal ? other.bounds.y : other.bounds.x;
            const auto stop = start + (horizontal ? other.bounds.height : other.bounds.width);
            if (start < end && across < stop) occupied = rstd::cmp::max(occupied, other.distance);
        }
        item.distance = occupied + length;
        item.position = item.before != viewport.reverse ? -item.distance : extent + occupied;
    }
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

auto start_placement(UiDocument &doc, UiId node, Point offset, AnimationOptions options)
    -> Result<AnimationId, UiError> {
    auto transformed = doc.set_transform(node, translation(offset));
    if (transformed.is_err()) return Err(transformed.unwrap_err());
    auto animated = doc.animate_transform(node, {}, animation_options(options));
    if (animated.is_ok()) return Ok(animated.unwrap());
    (void)doc.set_transform(node, {});
    return Err(ui_error(animated.unwrap_err()));
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

auto LazyItemAnimator::capture_changes(UiDocument &document, const LazyContent &content) const
    -> Vec<LazyItemGhost> {
    Vec<LazyItemGhost> ghosts;
    if (!content_ || content_->identity() == content.identity()) return ghosts;
    Index active{};
    for (const auto &entry : exiting_) if (document.valid(entry.node)) ++active;
    const auto at_limit = [&] { return active + ghosts.len().to_primitive() >= max_ghosts; };
    const auto capture = [&](const LazyAnimatedItem &geometry, AnimationOptions options, Option<Index> destination) {
        auto snapshot = document.capture_painted_subtree(geometry.node, max_ghost_nodes);
        if (snapshot.is_err()) return;
        auto style = document.resolved_style(geometry.node);
        if (style.is_err()) return;
        const Point position{geometry.viewport_bounds.x - viewport_.origin.x + style->transform.tx,
                             geometry.viewport_bounds.y - viewport_.origin.y + style->transform.ty};
        auto key = geometry.key ? Some(Key(*geometry.key)) : None<Key>();
        ghosts.push({geometry.index, rstd::move(key), snapshot.unwrap(), options, geometry.viewport_bounds,
                     position, destination});
    };
    struct Move {
        const LazyAnimatedItem *item;
        Index index;
        AnimationOptions options;
    };
    Vec<Move> moves;
    for (const auto &item : items_) {
        if (at_limit()) break;
        const auto &geometry = item.geometry;
        const auto index = nearby_lazy_index(content, geometry.index, geometry.key);
        if (!index) {
            auto fade = geometry.options.fade_out;
            if (fade && fade->duration_ns) capture(geometry, *fade, {});
        } else if (!geometry.sticky && *index != geometry.index) {
            const auto metadata = content.describe(*index);
            const auto placement = metadata.animation ? metadata.animation->placement : None<AnimationOptions>();
            if (placement && placement->duration_ns) moves.push({&geometry, *index, *placement});
        }
    }
    for (const auto &move : moves) {
        if (at_limit()) break;
        capture(*move.item, move.options, Some(Index(move.index)));
    }
    return ghosts;
}

auto LazyItemAnimator::start_exiting(UiDocument &doc, LazyItemGhost ghost, Point offset,
                                   Option<Scalar> edge, bool horizontal) -> Result<empty, UiError> {
    auto appended = doc.append_painted_subtree(*layer_->id, rstd::move(ghost.paint));
    if (appended.is_err()) return Err(appended.unwrap_err());
    const auto id = appended.unwrap();
    auto placed = doc.set_transform(id, translation(offset));
    if (placed.is_err()) {
        (void)doc.destroy(id);
        return placed;
    }
    auto target = offset;
    if (edge) {
        if (horizontal) target.x = *edge - ghost.visual_position.x;
        else target.y = *edge - ghost.visual_position.y;
    }
    auto options = animation_options(ghost.options);
    auto animated = edge ? doc.animate_transform(id, translation(target), options)
                         : doc.animate_opacity(id, 0, options);
    if (animated.is_err()) {
        (void)doc.destroy(id);
        return Err(ui_error(animated.unwrap_err()));
    }
    auto done = doc.on_animation_end(animated.unwrap(), [id](UiDocument &document, const AnimationEvent &) {
        if (document.valid(id)) (void)document.destroy(id);
    });
    if (done.is_err()) {
        (void)doc.destroy(id);
        return Err(ui_error(done.unwrap_err()));
    }
    auto moving_origin = edge ? Some(Point(ghost.visual_position)) : None<Point>();
    exiting_.push({id, ghost.index, rstd::move(ghost.key), moving_origin});
    return Ok(empty{});
}

auto LazyItemAnimator::update(UiDocument &doc, const LazyContent &content, Vec<LazyAnimatedItem> geometry,
                              const Vec<LazyVisibleItemInfo> &visible, LazyItemViewport viewport,
                              Point anchor_correction) -> Result<empty, UiError> {
    auto same = [](const LazyAnimatedItem &a, const LazyAnimatedItem &b) {
        return a.node == b.node && same_item(a.index, a.key, b.index, b.key);
    };
    const bool changed = content_ && content_->identity() != content.identity();
    const bool same_direction = viewport.horizontal == viewport_.horizontal && viewport.reverse == viewport_.reverse;
    for (const auto &old : items_) {
        bool current{};
        for (const auto &item : geometry) current |= same(old.geometry, item);
        if (current || !doc.valid(old.geometry.node)) continue;
        cancel(doc, old.move);
        cancel(doc, old.opacity);
        if (old.move) (void)doc.set_transform(old.geometry.node, {});
        if (old.opacity) (void)doc.set_opacity(old.geometry.node, 1);
    }
    Vec<BoundaryItem> entering;
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
            Point delta{before->geometry.viewport_bounds.x - viewport_.origin.x -
                            value.viewport_bounds.x + viewport.origin.x + anchor_correction.x,
                        before->geometry.viewport_bounds.y - viewport_.origin.y -
                            value.viewport_bounds.y + viewport.origin.y + anchor_correction.y};
            if (!value.sticky && !before->geometry.sticky && value.options.placement &&
                value.options.placement->duration_ns &&
                (__builtin_fabs(delta.x) > .01 || __builtin_fabs(delta.y) > .01)) {
                auto style = doc.resolved_style(value.node);
                if (style.is_err()) return Err(style.unwrap_err());
                delta.x += style->transform.tx;
                delta.y += style->transform.ty;
                cancel(doc, before->move);
                auto animated = start_placement(doc, value.node, delta, *value.options.placement);
                if (animated.is_err()) return Err(animated.unwrap_err());
                current.move = Some(animated.unwrap());
            }
        } else if (changed) {
            const auto old_index = nearby_lazy_index(*content_, value.index, value.key);
            const auto move = value.options.placement && value.options.placement->duration_ns &&
                              !value.sticky && same_direction;
            Option<Point> resume;
            if (move) {
                for (const auto &entry : exiting_) {
                    if (!entry.moving_origin || !same_item(entry.index, entry.key, value.index, value.key)) continue;
                    auto style = doc.resolved_style(entry.node);
                    if (style.is_err()) continue;
                    resume = Some(Point{entry.moving_origin->x + style->transform.tx - value.viewport_bounds.x,
                                        entry.moving_origin->y + style->transform.ty - value.viewport_bounds.y});
                    break;
                }
            }
            if (resume) {
                auto animated = start_placement(doc, value.node, *resume, *value.options.placement);
                if (animated.is_err()) return Err(animated.unwrap_err());
                current.move = Some(animated.unwrap());
            } else if (old_index) {
                const auto edge = outside(viewport_, *old_index);
                if (move && *old_index != value.index && edge)
                    entering.push({i, *old_index, value.viewport_bounds, *edge});
            } else if (value.options.fade_in && value.options.fade_in->duration_ns) {
                auto opacity = doc.set_opacity(value.node, 0);
                if (opacity.is_err()) return opacity;
                auto animated = doc.animate_opacity(value.node, 1, animation_options(*value.options.fade_in));
                if (animated.is_err()) {
                    (void)doc.set_opacity(value.node, 1);
                    return Err(ui_error(animated.unwrap_err()));
                }
                current.opacity = Some(animated.unwrap());
            }
        }
        next.push(rstd::move(current));
    }
    position_at_edges(entering, viewport);
    for (const auto &entry : entering) {
        auto &item = next[entry.slot];
        const auto &bounds = item.geometry.viewport_bounds;
        const Point delta = viewport.horizontal ? Point{entry.position - bounds.x, 0}
                                                : Point{0, entry.position - bounds.y};
        auto animated = start_placement(doc, item.geometry.node, delta, *item.geometry.options.placement);
        if (animated.is_err()) return Err(animated.unwrap_err());
        item.move = Some(animated.unwrap());
    }
    for (const auto &entry : exiting_) {
        if (!doc.valid(entry.node)) continue;
        for (const auto &item : visible) {
            if (!same_item(entry.index, entry.key, item.index, item.key)) continue;
            auto destroyed = doc.destroy(entry.node);
            if (destroyed.is_err()) return destroyed;
            break;
        }
    }
    exiting_.retain([&](const Exiting &entry) { return doc.valid(entry.node); });
    if (layer_->id && (!pending_.is_empty() || !exiting_.is_empty())) {
        auto fixed = doc.set_transform(*layer_->id, translation({-viewport.origin.x, -viewport.origin.y}));
        if (fixed.is_err()) return fixed;
        const Point offset{viewport.origin.x + anchor_correction.x, viewport.origin.y + anchor_correction.y};
        Vec<BoundaryItem> leaving;
        for (usize i{}; i < pending_.len(); ++i) {
            auto &ghost = pending_[i];
            if (!ghost.destination) {
                auto result = start_exiting(doc, rstd::move(ghost), offset, {}, viewport.horizontal);
                if (result.is_err()) return result;
            } else if (same_direction) {
                bool still_visible{};
                for (const auto &item : visible) still_visible |= item.index == *ghost.destination;
                if (still_visible) continue;
                const auto edge = outside(viewport, *ghost.destination);
                if (edge) leaving.push({i, *ghost.destination, ghost.bounds, *edge});
            }
        }
        position_at_edges(leaving, viewport);
        for (const auto &entry : leaving) {
            auto result = start_exiting(doc, rstd::move(pending_[entry.slot]), offset,
                                        Some(Scalar(entry.position)), viewport.horizontal);
            if (result.is_err()) return result;
        }
    }
    pending_.clear();
    content_ = Some(LazyContent(content));
    items_ = rstd::move(next);
    viewport_ = viewport;
    return Ok(empty{});
}

void LazyItemAnimator::reset() {
    exiting_.clear();
    items_.clear();
    pending_.clear();
    content_ = None();
    viewport_ = {};
}
} // namespace vui::compose
