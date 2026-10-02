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
    static auto visit(const UiUpdate &update, UiId id, Point parent_point, Vec<UiId> &path,
                      Index depth) -> Option<HitResult> {
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
        path.push(UiId(id));
        for (auto i = node->children.len(); i > usize(); --i) {
            auto result = visit(update, node->children[i - usize(1)], local, path, depth + 1);
            if (result.is_some())
                return result;
        }
        if (node->layout_bounds.contains(local)) {
            Vec<UiId> route;
            route.reserve(path.len());
            for (auto ancestor : path)
                route.push(UiId(ancestor));
            return Some(HitResult{id, local, update.revision(), rstd::move(route)});
        }
        path.pop();
        return None();
    }

  public:
    static auto at(const UiUpdate &update, Point point) -> Option<HitResult> {
        if (!finite(point.x) || !finite(point.y) || update.root().is_none())
            return None();
        Vec<UiId> path;
        return visit(update, *update.root(), point, path, 0);
    }
};
} // namespace vui
