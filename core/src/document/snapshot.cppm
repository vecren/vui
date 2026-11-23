export module vui:document.snapshot;
import :scene.composition;
import :node.types;
import :diagnostics;
import :modifier.protocol;
using namespace rstd::prelude;

export namespace vui {
class PaintedSubtree {
    struct Entry {
        RenderProperties properties;
        DisplayList content;
        Option<Index> parent;
    };
    Vec<Entry> entries_;
    friend class UiDocument;
};

struct MutationResult {
    Serial id{};
    Option<UiError> error;
};
struct UiVisual {
    UiId id;
    RenderProperties properties;
    DisplayList content;
    Vec<UiId> children;
    bool hit_test_visible{}, enabled{}, focusable{};
    Option<UiId> parent;
    Serial mount_generation{};
    Option<ViewportMetrics> viewport;
    Rect layout_bounds;
    Rect layout_rect;
    Option<Rect> layout_slot;
    bool hit_self{true};
    Option<Rect> text_input_area;
    ModifierGeometry content_geometry;
    auto content_position(Point local) const -> Option<Point> {
        auto inverse = content_geometry.local_to_node.inverse();
        if (inverse.is_none()) return None();
        auto point = inverse->map(local);
        return finite(point.x) && finite(point.y) ? Some(point) : None<Point>();
    }
};
enum class RevealStatus { Contained, Partial, Hidden, NonInvertible };
struct BringIntoViewResult {
    RevealStatus status{RevealStatus::Contained};
    Index scrolled_viewports{};
};
struct UiMountState {
    bool mounted{};
    Serial generation{};
};
struct RelativeGeometry {
    Rect bounds, reference_bounds;
    Transform local_to_reference;
    Serial mount_generation{}, reference_generation{};
    bool visible{};
    auto operator==(const RelativeGeometry &) const -> bool = default;
};
struct UpdateStats {
    Serial measurements{}, recordings{}, text_layouts{}, arrangements{}, layout_passes{};
    Serial measure_cache_hits{}, arrange_cache_hits{};
    Serial collected_nodes{}, geometry_overrides{};
    void add(UpdateStats other) {
        measurements += other.measurements;
        recordings += other.recordings;
        text_layouts += other.text_layouts;
        arrangements += other.arrangements;
        layout_passes += other.layout_passes;
        measure_cache_hits += other.measure_cache_hits;
        arrange_cache_hits += other.arrange_cache_hits;
        collected_nodes += other.collected_nodes;
        geometry_overrides += other.geometry_overrides;
    }
};
struct UpdateTimings {
    Serial document{}, revision{}, layout_revision{};
    CpuTiming update, layout, paint, collect;
    UpdateStats work;
    Option<UiError> error;
};
template <class Snapshot>
auto surface_transform(const Snapshot &snapshot, UiId id) -> Result<Transform, UiError> {
    Transform result;
    auto cursor = Some(id);
    unsigned depth{};
    while (cursor.is_some() && depth++ < 256) {
        auto node = snapshot.get(*cursor);
        if (!node)
            return Err(UiError::InvalidId);
        result = node->properties.transform * result;
        cursor = node->parent;
    }
    if (cursor.is_some() || !result.valid())
        return Err(UiError::InvalidValue);
    return Ok(result);
}
template <class Snapshot>
auto content_surface_transform(const Snapshot &snapshot, UiId id) -> Result<Transform, UiError> {
    auto node = snapshot.get(id);
    if (!node) return Err(UiError::InvalidId);
    auto outer = surface_transform(snapshot, id);
    if (outer.is_err()) return Err(outer.unwrap_err());
    auto transform = outer.unwrap() * node->content_geometry.local_to_node;
    return transform.valid() ? Result<Transform, UiError>(Ok(transform))
                             : Result<Transform, UiError>(Err(UiError::InvalidValue));
}
template <class Snapshot>
auto between_transform(const Snapshot &snapshot, UiId from, UiId to) -> Result<Transform, UiError> {
    auto source = surface_transform(snapshot, from), target = surface_transform(snapshot, to);
    if (source.is_err())
        return Err(source.unwrap_err());
    if (target.is_err())
        return Err(target.unwrap_err());
    auto inverse = target.unwrap().inverse();
    if (inverse.is_none())
        return Err(UiError::InvalidValue);
    auto ancestors = [&](UiId id) {
        Vec<UiId> result;
        for (auto cursor = Some(id); cursor.is_some();) {
            result.push(UiId(*cursor));
            cursor = snapshot.get(*cursor)->parent;
        }
        return result;
    };
    auto source_path = ancestors(from), target_path = ancestors(to);
    auto source_end = source_path.len(), target_end = target_path.len();
    while (source_end != usize() && target_end != usize() &&
           source_path[source_end - usize(1)] == target_path[target_end - usize(1)]) {
        --source_end;
        --target_end;
    }
    if (source_end != source_path.len()) {
        Transform local_source, local_target;
        for (usize i{}; i < source_end; ++i)
            local_source = snapshot.get(source_path[i])->properties.transform * local_source;
        for (usize i{}; i < target_end; ++i)
            local_target = snapshot.get(target_path[i])->properties.transform * local_target;
        auto local_inverse = local_target.inverse();
        if (local_inverse.is_some())
            return Ok(*local_inverse * local_source);
    }
    return Ok(*inverse * source.unwrap());
}
template <class Snapshot>
auto between_content_transform(const Snapshot &snapshot, UiId from, UiId to) -> Result<Transform, UiError> {
    auto source = snapshot.get(from), target = snapshot.get(to);
    if (!source || !target) return Err(UiError::InvalidId);
    auto outer = between_transform(snapshot, from, to);
    if (outer.is_err()) return Err(outer.unwrap_err());
    auto inverse = target->content_geometry.local_to_node.inverse();
    if (inverse.is_none()) return Err(UiError::InvalidValue);
    auto transform = *inverse * outer.unwrap() * source->content_geometry.local_to_node;
    return transform.valid() ? Result<Transform, UiError>(Ok(transform))
                             : Result<Transform, UiError>(Err(UiError::InvalidValue));
}
template <class Snapshot>
auto relative_geometry(const Snapshot &snapshot, UiId from, UiId to) -> Option<RelativeGeometry> {
    auto source = snapshot.get(from), reference = snapshot.get(to);
    if (!source || !reference) return None();
    auto transform = between_transform(snapshot, from, to);
    if (transform.is_err()) return None();
    bool visible = true;
    UiId targets[]{from, to};
    for (auto id : targets) {
        auto cursor = Some(id);
        unsigned depth{};
        while (cursor.is_some() && depth++ < 256) {
            auto node = snapshot.get(*cursor);
            if (!node) return None();
            visible = visible && node->properties.visible;
            cursor = node->parent;
        }
        if (cursor.is_some()) return None();
    }
    return Some(RelativeGeometry{source->layout_bounds, reference->layout_bounds,
                                 transform.unwrap(), source->mount_generation,
                                 reference->mount_generation, visible});
}
class UiDocument;
class UiUpdate {
    struct Data {
        Serial document{}, revision{};
        Option<UiId> root;
        Vec<UiVisual> visuals;
        Vec<Index> indices;
        UpdateStats stats;
        Serial layout_revision{};
        Option<UpdateTimings> timings;
        Option<Serial> animation_time;
    };
    rstd::sync::Arc<Data> data_;
    explicit UiUpdate(Data data) : data_(rstd::sync::Arc<Data>::make(rstd::move(data))) {}
    friend class UiDocument;

  public:
    UiUpdate(const UiUpdate &other) : data_(other.data_.clone()) {}
    auto operator=(const UiUpdate &other) -> UiUpdate & {
        if (this != &other)
            data_ = other.data_.clone();
        return *this;
    }
    UiUpdate(UiUpdate &&) = default;
    auto operator=(UiUpdate &&) -> UiUpdate & = default;
    auto document() const -> Serial { return data_->document; }
    auto revision() const -> Serial { return data_->revision; }
    auto root() const -> Option<UiId> { return data_->root; }
    auto visuals() const -> slice<UiVisual> { return data_->visuals.as_slice(); }
    auto stats() const -> UpdateStats { return data_->stats; }
    auto timings() const -> Option<UpdateTimings> { return data_->timings; }
    auto layout_revision() const -> Serial { return data_->layout_revision; }
    auto animation_sample_time() const -> Option<Serial> { return data_->animation_time; }
    auto layout_rect(UiId id) const -> Result<Rect, UiError> {
        auto node = get(id);
        if (!node)
            return Err(UiError::InvalidId);
        return Ok(Rect(node->layout_rect));
    }
    auto layout_slot(UiId id) const -> Result<Option<Rect>, UiError> {
        auto node = get(id);
        if (!node)
            return Err(UiError::InvalidId);
        return Ok(Option<Rect>(node->layout_slot));
    }
    auto local_to_surface(UiId id) const -> Result<Transform, UiError> {
        return surface_transform(*this, id);
    }
    auto transform_to(UiId from, UiId to) const -> Result<Transform, UiError> {
        return between_transform(*this, from, to);
    }
    auto content_to_surface(UiId id) const -> Result<Transform, UiError> {
        return content_surface_transform(*this, id);
    }
    auto content_transform_to(UiId from, UiId to) const -> Result<Transform, UiError> {
        return between_content_transform(*this, from, to);
    }
    auto map_point(UiId from, UiId to, Point point) const -> Result<Point, UiError> {
        auto transform = transform_to(from, to);
        if (transform.is_err())
            return Err(transform.unwrap_err());
        auto mapped = transform.unwrap().map(point);
        if (!finite(mapped.x) || !finite(mapped.y))
            return Err(UiError::InvalidValue);
        return Ok(mapped);
    }
    auto get(UiId id) const -> const UiVisual * {
        if (!id.valid() || id.session != data_->document || usize(id.slot) >= data_->indices.len())
            return nullptr;
        auto index = data_->indices[usize(id.slot)];
        if (index == ~Index{})
            return nullptr;
        const auto &visual = data_->visuals[usize(index)];
        return visual.id == id ? &visual : nullptr;
    }
};

class InputSnapshot {
    struct Data {
        UiUpdate base;
        Vec<UiVisual> changes;
        Serial revision;
        Data(UiUpdate value, Vec<UiVisual> updates, Serial rev)
            : base(rstd::move(value)), changes(rstd::move(updates)), revision(rev) {}
    };
    rstd::sync::Arc<Data> data_;
    friend class UiDocument;
    static auto freeze(UiUpdate base) -> rstd::sync::Arc<Data> {
        auto revision = base.revision();
        return rstd::sync::Arc<Data>::make(rstd::move(base), Vec<UiVisual>{}, revision);
    }
    InputSnapshot(UiUpdate base, Vec<UiVisual> changes, Serial revision)
        : data_(rstd::sync::Arc<Data>::make(rstd::move(base), rstd::move(changes), revision)) {}

  public:
    InputSnapshot(UiUpdate base) : data_(freeze(rstd::move(base))) {}
    InputSnapshot(const InputSnapshot &other) : data_(other.data_.clone()) {}
    auto operator=(const InputSnapshot &other) -> InputSnapshot & {
        data_ = other.data_.clone();
        return *this;
    }
    InputSnapshot(InputSnapshot &&) = default;
    auto operator=(InputSnapshot &&) -> InputSnapshot & = default;
    auto document() const -> Serial { return data_->base.document(); }
    auto revision() const -> Serial { return data_->revision; }
    auto layout_revision() const -> Serial { return data_->base.layout_revision(); }
    auto root() const -> Option<UiId> { return data_->base.root(); }
    struct Visuals {
        const InputSnapshot *snapshot;
        struct Iterator {
            const InputSnapshot *snapshot;
            usize index;
            auto operator*() const -> const UiVisual & {
                return *snapshot->get(snapshot->data_->base.visuals()[index].id);
            }
            auto operator++() -> Iterator & {
                ++index;
                return *this;
            }
            auto operator!=(const Iterator &other) const -> bool { return index != other.index; }
        };
        auto begin() const -> Iterator { return {snapshot, {}}; }
        auto end() const -> Iterator { return {snapshot, snapshot->data_->base.visuals().len()}; }
        auto len() const -> usize { return snapshot->data_->base.visuals().len(); }
    };
    auto visuals() const -> Visuals { return {this}; }
    auto get(UiId id) const -> const UiVisual * {
        for (const auto &node : data_->changes)
            if (node.id == id)
                return &node;
        return data_->base.get(id);
    }
    auto layout_rect(UiId id) const -> Result<Rect, UiError> {
        auto node = get(id);
        if (!node)
            return Err(UiError::InvalidId);
        return Ok(Rect(node->layout_rect));
    }
    auto layout_slot(UiId id) const -> Result<Option<Rect>, UiError> {
        auto node = get(id);
        if (!node)
            return Err(UiError::InvalidId);
        return Ok(Option<Rect>(node->layout_slot));
    }
    auto local_to_surface(UiId id) const -> Result<Transform, UiError> {
        return surface_transform(*this, id);
    }
    auto transform_to(UiId from, UiId to) const -> Result<Transform, UiError> {
        return between_transform(*this, from, to);
    }
    auto content_to_surface(UiId id) const -> Result<Transform, UiError> {
        return content_surface_transform(*this, id);
    }
    auto content_transform_to(UiId from, UiId to) const -> Result<Transform, UiError> {
        return between_content_transform(*this, from, to);
    }
    auto map_point(UiId from, UiId to, Point point) const -> Result<Point, UiError> {
        auto transform = transform_to(from, to);
        if (transform.is_err())
            return Err(transform.unwrap_err());
        auto mapped = transform.unwrap().map(point);
        if (!finite(mapped.x) || !finite(mapped.y))
            return Err(UiError::InvalidValue);
        return Ok(mapped);
    }
};

} // namespace vui
