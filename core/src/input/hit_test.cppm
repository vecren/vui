export module vui:input;
import :document;
using namespace rstd::prelude;

export namespace vui {
struct HitResult {
    UiId target;
    Point local;
    Serial ui_revision{};
    Vec<UiId> path;
};
class HitTest {
    template <class Snapshot>
    static auto target(const Snapshot &update, UiId id, Point parent_point, Index depth)
        -> Option<UiId> {
        if (depth >= 256)
            return None();
        const auto *node = update.get(id);
        if (!node || !node->properties.visible || !node->enabled || !node->hit_test_visible)
            return None();
        auto inverse = node->properties.transform.inverse();
        if (inverse.is_none())
            return None();
        auto local = inverse->map(parent_point);
        if (!finite(local.x) || !finite(local.y) || !node->properties.clip.contains(local))
            return None();
        for (auto i = node->children.len(); i > usize(); --i) {
            auto hit = target(update, node->children[i - usize(1)], local, depth + 1);
            if (hit.is_some())
                return hit;
        }
        return node->layout_bounds.contains(local) ? Some(UiId(id)) : None<UiId>();
    }

  public:
    template <class Snapshot>
    static auto target_at(const Snapshot &update, Point point) -> Option<UiId> {
        if (!finite(point.x) || !finite(point.y) || update.root().is_none())
            return None();
        return target(update, *update.root(), point, 0);
    }
    template <class Snapshot>
    static auto at(const Snapshot &update, Point point) -> Option<HitResult> {
        if (!finite(point.x) || !finite(point.y) || update.root().is_none())
            return None();
        auto hit = target_at(update, point);
        if (hit.is_none())
            return None();
        auto matrix = update.local_to_surface(*hit);
        if (matrix.is_err())
            return None();
        auto inverse = matrix.unwrap().inverse();
        if (inverse.is_none())
            return None();
        Vec<UiId> route;
        auto cursor = hit;
        while (cursor.is_some()) {
            route.push(UiId(*cursor));
            cursor = update.get(*cursor)->parent;
        }
        for (usize i{}; i < route.len() / usize(2); ++i)
            rstd::swap(route[i], route[route.len() - usize(1) - i]);
        return Some(HitResult{*hit, inverse->map(point), update.revision(), rstd::move(route)});
    }
};
} // namespace vui
