export module vui:document;
import :composition;
import :node.types;
import :animation;
import :layout;
import :diagnostics;
import :binding;
import :signal;
import :layout.protocol;
import :paint.protocol;
import :motion.properties;
import :state.targets;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui {
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
auto between_transform(const Snapshot &snapshot, UiId from, UiId to) -> Result<Transform, UiError> {
    auto source = surface_transform(snapshot, from), target = surface_transform(snapshot, to);
    if (source.is_err())
        return Err(source.unwrap_err());
    if (target.is_err())
        return Err(target.unwrap_err());
    auto inverse = target.unwrap().inverse();
    if (inverse.is_none())
        return Err(UiError::InvalidValue);
    return Ok(*inverse * source.unwrap());
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

class UiDocument {
    struct ViewportObserver {
        Signal<void(ViewportMetrics)> changed;
        Vec<BindingGuard> subscribers;
        Option<ViewportMetrics> published;
        Serial mount{};
    };
    struct ViewportLayout {
        ScrollAxes axes;
        Point requested;
        ViewportMetrics metrics;
        Option<rstd::rc::Rc<ViewportObserver>> observer;
    };
    struct State {
        virtual ~State() = default;
        virtual auto type() const -> rstd::any::TypeId = 0;
    };
    template <class T> struct StoredState : State {
        T value;
        explicit StoredState(T value) : value(rstd::move(value)) {}
        auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<T>(); }
    };
    struct Mutation {
        Serial id;
        explicit Mutation(Serial value) : id(value) {}
        virtual ~Mutation() = default;
        virtual auto apply(UiDocument &) -> Result<empty, UiError> = 0;
    };
    template <class F> struct Deferred : Mutation {
        F callback;
        Deferred(Serial id, F value) : Mutation(id), callback(rstd::move(value)) {}
        auto apply(UiDocument &document) -> Result<empty, UiError> override {
            return callback(document);
        }
    };
    struct TextContent {
        String value, family{String::make("sans"_str)};
        String language{String::make("und"_str)};
        Scalar size{16};
        TextDirection direction{TextDirection::Auto};
        TextWrap wrap{TextWrap::WordOrCharacter};
        TextAlignment alignment{TextAlignment::Natural};
        Scalar line_spacing{};
        bool bold{}, italic{}, dirty{true};
        Color color{0, 0, 0, 1};
        TextColors colors;
        TextDecorations decorations;
        TextEffects effects;
        Option<Scalar> width;
        Option<TextLayout> layout;
        Option<TextError> error;
        TextBoundaries boundaries;
    };
    struct ImageContent {
        Option<ImageData> data;
        ImageFit fit{ImageFit::Contain};
        ImageSampling sampling{ImageSampling::Linear};
        Color tint{1, 1, 1, 1};
    };
    struct SceneContent {
        Option<ContentSource> source;
        ImageFit fit{ImageFit::Contain};
        ImageSampling sampling{ImageSampling::Linear};
        Color placeholder{0.15, 0.15, 0.15, 1};
    };
    struct AnimatedStyle {
        Option<Transform> transform;
        Option<Scalar> opacity, width, height;
        Option<Color> background;
    };
    struct AnimationCallback {
        virtual ~AnimationCallback() = default;
        virtual void invoke(UiDocument &, const AnimationEvent &) = 0;
    };
    template <class F> struct AnimationFunction : AnimationCallback {
        F callback;
        explicit AnimationFunction(F value) : callback(rstd::move(value)) {}
        void invoke(UiDocument &document, const AnimationEvent &event) override {
            callback(document, event);
        }
    };
    struct RunningAnimation {
        AnimationState state;
        AnimationValue from;
        Option<Box<AnimationCallback>> callback;
    };
    struct AnimationCompletion {
        AnimationEvent event;
        Option<Box<AnimationCallback>> callback;
    };
    struct Node {
        LayoutKind kind;
        NodeStyle style;
        Option<UiId> parent;
        Vec<UiId> children;
        Size measured, desired;
        Option<LayoutConstraints> measure_constraints;
        bool measure_dirty{true}, arrange_dirty{true}, subtree_arrange_dirty{true};
        bool pending_layout{}, constraint_conflict{};
        Option<Rect> arranged_slot;
        Option<Box<rstd::dyn<Layout>>> layout_strategy;
        Option<BindingGuard> layout_binding;
        Option<Box<rstd::dyn<Paint>>> painter;
        Option<BindingGuard> paint_binding;
        Point position;
        Transform layout_mapping;
        Option<Size> layout_local_size;
        Clip layout_clip;
        bool layout_has_text_input_area{};
        Option<Rect> layout_text_input_area;
        bool paint_dirty{true};
        DisplayList content;
        Serial mount_generation{1}, relation_generation{1};
        Option<Box<State>> state;
        Option<Box<ViewportLayout>> viewport;
        Option<Box<TextContent>> text;
        Option<Box<ImageContent>> image;
        Option<Box<SceneContent>> scene;
        AnimatedStyle animated;
        rstd::rc::Weak<PropertyLeaseState> property_owner[7];
        rstd::rc::Weak<UiValueWriterState> value_writer[3];
        Node(LayoutKind value, NodeStyle properties) : kind(value), style(properties) {}
    };
    struct Slot {
        Serial generation{1};
        Option<Node> node;
    };
    Serial session_{new_session()}, revision_{1};
    Vec<Slot> slots_;
    Option<UiUpdate> input_base_;
    Vec<UiId> input_changes_;
    Vec<UiId> bound_nodes_;
    Option<UiId> root_;
    bool layout_dirty_{true}, layout_active_{}, layout_again_{};
    Serial layout_revision_{};
    Vec<UiId> pending_layout_;
    Option<UiId> layout_requester_;
    Serial layout_work_{};
    Vec<LayoutDiagnostic> layout_diagnostics_;
    Size viewport_;
    Option<Box<TextService>> text_service_;
    Vec<RunningAnimation> animations_;
    Vec<AnimationCompletion> animation_completions_;
    Vec<AnimationEvent> animation_events_;
    Option<Serial> animation_time_;
    Serial next_animation_{1};
    usize dispatching_animation_count_{};
    bool animation_dispatching_{}, animations_closed_{};
    UpdateStats stats_;
    UpdateStats input_stats_;
    bool updating_{}, painting_{};
    BindingRegistry bindings_;
    Binding lease_owner_{bindings_.make()};
    Vec<rstd::rc::Weak<UiTargetState>> value_targets_;
    Vec<UiId> observed_viewports_;
    bool pending_viewports_{};
    Serial next_lease_{1};
    Vec<PaintDiagnostic> paint_diagnostics_;
    UpdateTimings *active_timings_{};
    Option<UpdateTimings> last_timings_;
    Index depth_limit_{256};
    bool routing_{}, draining_{};
    Serial rejected_cleanups_{};
    Serial next_mutation_{1};
    Vec<Box<Mutation>> mutations_;

    void notify_targets() {
        Vec<rstd::rc::Rc<UiTargetState>> changed_targets;
        for (auto &weak : value_targets_) {
            auto target = weak.upgrade();
            if (target.is_none())
                continue;
            auto &state = *target->get();
            auto node = resolve(state.node);
            UiTargetStatus next{node != nullptr, node && mounted(state.node),
                                node ? node->mount_generation : 0};
            if (state.status != next) {
                state.status = next;
                changed_targets.push(rstd::move(*target));
            }
        }
        value_targets_.retain([](const auto &weak) { return !weak.expired(); });
        for (auto &target : changed_targets) {
            auto &state = *target.get();
            Vec<rstd::rc::Rc<UiTargetObserver>> callbacks;
            for (auto &observer : state.observers)
                if (observer->guard.alive())
                    callbacks.push(observer.clone());
            for (auto &observer : callbacks)
                if (observer->guard.alive())
                    observer.get()->callback->operator()(state.status);
            state.observers.retain([](const auto &observer) { return observer->guard.alive(); });
        }
    }
    auto value_occupied(Node &node, UiValueProperty property) -> bool {
        auto writer = node.value_writer[unsigned(property)].upgrade();
        return writer.is_some() && (*writer)->active;
    }
    auto write_text(UiId id, Node &node, String value, TextBoundaries boundaries = {}) -> bool {
        auto &text = **node.text;
        if (text.value.as_str() == value.as_str() && (!boundaries.present() || boundaries == text.boundaries))
            return false;
        if (text.value.as_str() != value.as_str()) { text.colors = {}; text.decorations = {}; }
        text.value = rstd::move(value);
        text.boundaries = rstd::move(boundaries);
        text.dirty = true;
        changed(true, Some(id));
        return true;
    }
    auto write_visible(UiId id, Node &node, bool value) -> bool {
        if (node.style.visible == value)
            return false;
        node.style.visible = value;
        changed(false, Some(id));
        return true;
    }
    auto write_image(UiId id, Node &node, Option<ImageData> data) -> bool {
        auto &image = **node.image;
        if (image.data.is_some() == data.is_some() &&
            (data.is_none() || image.data->id() == data->id()))
            return false;
        auto old_size = image.data.is_some() ? image.data->natural_size() : Size{};
        auto new_size = data.is_some() ? data->natural_size() : Size{};
        image.data = rstd::move(data);
        node.paint_dirty = true;
        changed(old_size != new_size, Some(id));
        return true;
    }
    template <class T>
    auto value_target(UiId id, UiValueProperty property) -> Result<UiPropertyTarget<T>, UiError> {
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if ((property == UiValueProperty::Text && node->text.is_none()) ||
            (property == UiValueProperty::Image && node->image.is_none()))
            return Err(UiError::InvalidValue);
        value_targets_.retain([](const auto &weak) { return !weak.expired(); });
        for (auto &weak : value_targets_) {
            auto target = weak.upgrade();
            if (target.is_some() && (*target)->node == id && (*target)->property == property)
                return Ok(UiPropertyTarget<T>(rstd::move(*target)));
        }
        if (value_targets_.len() >= usize(4096))
            return Err(UiError::Limit);
        auto target = rstd::rc::make_rc<UiTargetState>(
            UiTargetState{session_,
                          id,
                          property,
                          lease_owner_.guard(),
                          {true, mounted(id), node->mount_generation},
                          {}});
        value_targets_.push(target.downgrade());
        return Ok(UiPropertyTarget<T>(rstd::move(target)));
    }

    void sweep_bindings() {
        for (auto id : bound_nodes_) {
            auto value = resolve(id);
            if (!value)
                continue;
            auto &node = *value;
            if (node.layout_binding.is_some() && !node.layout_binding->alive()) {
                node.layout_strategy = None();
                node.layout_binding = None();
                changed(true, Some(id));
            }
            if (node.paint_binding.is_some() && !node.paint_binding->alive()) {
                node.painter = None();
                node.paint_binding = None();
                node.paint_dirty = true;
                changed(false);
            }
        }
        bound_nodes_.retain([&](UiId id) {
            auto node = resolve(id);
            return node && (node->layout_binding.is_some() || node->paint_binding.is_some());
        });
    }
    void track_binding(UiId id) {
        for (auto node : bound_nodes_)
            if (node == id)
                return;
        bound_nodes_.push(UiId(id));
    }
    auto resolve(UiId id) -> Node * {
        if (!id.valid() || id.session != session_ || usize(id.slot) >= slots_.len())
            return nullptr;
        auto &slot = slots_[usize(id.slot)];
        return slot.generation == id.generation && slot.node.is_some() ? &*slot.node : nullptr;
    }
    void dirty_measure(UiId id) {
        auto cursor = Some(id);
        while (cursor.is_some()) {
            auto node = resolve(*cursor);
            if (!node)
                break;
            node->measure_dirty = true;
            node->arrange_dirty = true;
            node->subtree_arrange_dirty = true;
            cursor = node->parent;
        }
    }
    void dirty_arrange(UiId id) {
        auto node = resolve(id);
        node->arrange_dirty = true;
        auto cursor = Some(id);
        while (cursor.is_some()) {
            auto ancestor = resolve(*cursor);
            ancestor->subtree_arrange_dirty = true;
            cursor = ancestor->parent;
        }
    }
    void changed(bool layout, Option<UiId> target = {}) {
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
    void request_layout(UiId id) {
        auto &node = *resolve(id);
        if (!node.pending_layout) {
            node.pending_layout = true;
            pending_layout_.push(UiId(id));
        }
        layout_again_ = true;
        layout_requester_ = Some(id);
    }
    void apply_layout_requests() {
        auto requests = rstd::exchange(pending_layout_, Vec<UiId>{});
        for (auto id : requests) {
            auto node = resolve(id);
            if (node) {
                node->pending_layout = false;
                dirty_measure(id);
            }
        }
    }
    static auto effective_style(const Node &node) -> NodeStyle {
        auto result = node.style;
        if (node.animated.transform.is_some())
            result.transform = *node.animated.transform;
        if (node.animated.opacity.is_some())
            result.opacity = *node.animated.opacity;
        if (node.animated.width.is_some())
            result.layout.width = node.animated.width;
        if (node.animated.height.is_some())
            result.layout.height = node.animated.height;
        if (node.animated.background.is_some())
            result.background = *node.animated.background;
        return result;
    }
    static auto property_index(PresentationProperty property) -> unsigned {
        return static_cast<unsigned>(property);
    }
    auto property_occupied(Node &node, PresentationProperty property) -> bool {
        auto owner = node.property_owner[property_index(property)].upgrade();
        return owner.is_some() && owner->get()->active && owner->get()->owner.alive() &&
               (owner->get()->driver.is_none() || owner->get()->driver->alive());
    }
    void revoke_property(Node &node, PresentationProperty property) {
        auto owner = node.property_owner[property_index(property)].upgrade();
        if (owner.is_some())
            owner->get()->active = false;
        node.property_owner[property_index(property)] = {};
    }
    auto read_property(UiId id, Node &node, PresentationProperty property, bool base = false)
        -> PropertyWrite {
        auto style = base ? node.style : effective_style(node);
        PropertyWrite result{{id, property}};
        switch (property) {
        case PresentationProperty::Transform:
            result.value = AnimationValue::Matrix(style.transform);
            break;
        case PresentationProperty::Opacity:
            result.value = AnimationValue::Number(style.opacity);
            break;
        case PresentationProperty::Width:
            result.value = AnimationValue::Number(
                style.layout.width.is_some() ? *style.layout.width : node.measured.width);
            result.automatic_size = style.layout.width.is_none();
            break;
        case PresentationProperty::Height:
            result.value = AnimationValue::Number(
                style.layout.height.is_some() ? *style.layout.height : node.measured.height);
            result.automatic_size = style.layout.height.is_none();
            break;
        case PresentationProperty::Background:
            result.value = AnimationValue::Tint(style.background);
            break;
        case PresentationProperty::Clip:
            result.clip = style.clip;
            break;
        case PresentationProperty::ViewportOffset:
            result.point = node.viewport.is_some() ? (*node.viewport)->requested : Point{};
            break;
        }
        return result;
    }
    auto valid_property(const PropertyWrite &write, const Node &node) const -> bool {
        auto property = write.key.property;
        if (property_index(property) >= 7)
            return false;
        if (write.automatic_size)
            return property == PresentationProperty::Width ||
                   property == PresentationProperty::Height;
        if (property == PresentationProperty::Clip)
            return write.clip.valid() && node.viewport.is_none();
        if (property == PresentationProperty::ViewportOffset)
            return node.viewport.is_some() && finite(write.point.x) && finite(write.point.y);
        return write.value.valid(static_cast<AnimationProperty>(property_index(property)));
    }
    void apply_property(Node &node, const PropertyWrite &write) {
        auto before = effective_style(node);
        auto offset = node.viewport.is_some() ? (*node.viewport)->metrics.offset : Point{};
        switch (write.key.property) {
        case PresentationProperty::Transform:
            node.style.transform = write.value.as_Matrix().value;
            break;
        case PresentationProperty::Opacity:
            node.style.opacity = write.value.as_Number().value;
            break;
        case PresentationProperty::Width:
            node.style.layout.width =
                write.automatic_size ? None<Scalar>() : Some(Scalar(write.value.as_Number().value));
            break;
        case PresentationProperty::Height:
            node.style.layout.height =
                write.automatic_size ? None<Scalar>() : Some(Scalar(write.value.as_Number().value));
            break;
        case PresentationProperty::Background:
            node.style.background = write.value.as_Tint().value;
            break;
        case PresentationProperty::Clip:
            node.style.clip = write.clip;
            break;
        case PresentationProperty::ViewportOffset: {
            auto &viewport = **node.viewport;
            viewport.requested = write.point;
            viewport.metrics.offset = {bounded(write.point.x, 0, Some(viewport.metrics.maximum.x)),
                                       bounded(write.point.y, 0, Some(viewport.metrics.maximum.y))};
            break;
        }
        }
        auto after = effective_style(node);
        auto next_offset = node.viewport.is_some() ? (*node.viewport)->metrics.offset : Point{};
        if (before.background != after.background)
            node.paint_dirty = true;
        if (before.transform != after.transform || before.opacity != after.opacity ||
            before.background != after.background || before.clip != after.clip ||
            before.layout != after.layout || offset != next_offset)
            changed(before.layout != after.layout, Some(UiId(write.key.target)));
    }
    auto animation_index(AnimationId id) const -> Option<usize> {
        for (usize i{}; i < animations_.len(); ++i)
            if (animations_[i].state.id == id)
                return Some(i);
        return None();
    }
    auto animation_reservations() const -> usize {
        return animations_.len() + animation_completions_.len() + animation_events_.len() +
               dispatching_animation_count_;
    }
    void apply_overlay(UiId id, Node &node, AnimationProperty property,
                       const AnimationValue *value) {
        auto before = effective_style(node);
        switch (property) {
        case AnimationProperty::Transform:
            node.animated.transform =
                value ? Some(Transform(value->as_Matrix().value)) : None<Transform>();
            break;
        case AnimationProperty::Opacity:
            node.animated.opacity = value ? Some(Scalar(value->as_Number().value)) : None<Scalar>();
            break;
        case AnimationProperty::Width:
            node.animated.width = value ? Some(Scalar(value->as_Number().value)) : None<Scalar>();
            break;
        case AnimationProperty::Height:
            node.animated.height = value ? Some(Scalar(value->as_Number().value)) : None<Scalar>();
            break;
        case AnimationProperty::Background:
            node.animated.background = value ? Some(Color(value->as_Tint().value)) : None<Color>();
            break;
        }
        auto after = effective_style(node);
        if (before.background != after.background)
            node.paint_dirty = true;
        if (before.transform != after.transform || before.opacity != after.opacity ||
            before.background != after.background || before.layout != after.layout)
            changed(before.layout != after.layout, Some(id));
    }
    void end_animation(usize index, AnimationEndReason reason, bool clear_overlay = true) {
        auto track = animations_.remove(index);
        if (auto node = resolve(track.state.target)) {
            if (reason == AnimationEndReason::Finished &&
                track.state.options.end == AnimationEndPolicy::CommitFinal) {
                const auto &value = track.state.target_value;
                switch (track.state.property) {
                case AnimationProperty::Transform:
                    node->style.transform = value.as_Matrix().value;
                    break;
                case AnimationProperty::Opacity:
                    node->style.opacity = value.as_Number().value;
                    break;
                case AnimationProperty::Width:
                    node->style.layout.width = Some(Scalar(value.as_Number().value));
                    break;
                case AnimationProperty::Height:
                    node->style.layout.height = Some(Scalar(value.as_Number().value));
                    break;
                case AnimationProperty::Background:
                    node->style.background = value.as_Tint().value;
                    break;
                }
            }
            if (clear_overlay)
                apply_overlay(track.state.target, *node, track.state.property, nullptr);
        }
        animation_completions_.push(AnimationCompletion{
            {track.state.id, track.state.target, track.state.property, reason,
             animation_time_.is_some() ? *animation_time_ : 0, track.state.mount_generation},
            rstd::move(track.callback)});
    }
    void detach_animations(UiId id) {
        for (auto i = animations_.len(); i > usize(); --i)
            if (animations_[i - usize(1)].state.target == id)
                end_animation(i - usize(1), AnimationEndReason::TargetDetached);
    }
    auto start_animation(UiId id, AnimationProperty property, AnimationValue to,
                         AnimationOptions options) -> Result<AnimationId, AnimationError> {
        if ((layout_active_ || painting_))
            return Err(AnimationError::Reentrant);
        if (animations_closed_)
            return Err(AnimationError::Closed);
        auto node = resolve(id);
        if (!node || !mounted(id))
            return Err(AnimationError::InvalidTarget);
        if (!options.valid() || !to.valid(property))
            return Err(AnimationError::InvalidValue);
        if (animation_reservations() >= usize(4096) || next_animation_ == ~Serial{})
            return Err(AnimationError::Limit);
        revoke_property(*node, static_cast<PresentationProperty>(static_cast<unsigned>(property)));
        auto style = effective_style(*node);
        auto from = AnimationValue::Number(0);
        switch (property) {
        case AnimationProperty::Transform:
            from = AnimationValue::Matrix(style.transform);
            break;
        case AnimationProperty::Opacity:
            from = AnimationValue::Number(style.opacity);
            break;
        case AnimationProperty::Background:
            from = AnimationValue::Tint(style.background);
            break;
        case AnimationProperty::Width:
            if (style.layout.width.is_none() && layout_dirty_)
                return Err(AnimationError::LayoutPending);
            from = AnimationValue::Number(style.layout.width.is_some() ? *style.layout.width
                                                                       : node->measured.width);
            break;
        case AnimationProperty::Height:
            if (style.layout.height.is_none() && layout_dirty_)
                return Err(AnimationError::LayoutPending);
            from = AnimationValue::Number(style.layout.height.is_some() ? *style.layout.height
                                                                        : node->measured.height);
            break;
        }
        for (usize i{}; i < animations_.len(); ++i)
            if (animations_[i].state.target == id && animations_[i].state.property == property) {
                end_animation(i, AnimationEndReason::Replaced, false);
                break;
            }
        AnimationId handle{session_, next_animation_++};
        animations_.push(
            RunningAnimation{{handle, id, property, AnimationValue(from), rstd::move(to), options,
                              animation_time_, node->mount_generation},
                             AnimationValue(from),
                             {}});
        apply_overlay(id, *node, property, &from);
        return Ok(handle);
    }
    void remount(UiId id) {
        detach_animations(id);
        auto &node = *resolve(id);
        node.measure_dirty = true;
        node.arrange_dirty = true;
        node.subtree_arrange_dirty = true;
        if (node.mount_generation == ~Serial{})
            rstd::panic("vui mount generation exhausted");
        for (unsigned property{}; property < 7; ++property)
            revoke_property(node, static_cast<PresentationProperty>(property));
        ++node.mount_generation;
        for (auto child : node.children)
            remount(child);
    }
    static auto limit(Scalar value, Scalar maximum) -> Scalar {
        return value < 0 ? 0 : value > maximum ? maximum : value;
    }
    static auto constrain(Scalar value, Option<Scalar> maximum) -> Scalar {
        return maximum.is_some() ? limit(value, *maximum) : value;
    }
    static auto inset(Option<Scalar> value, Scalar padding) -> Option<Scalar> {
        return value.is_some() ? Some(limit(*value - 2 * padding, *value)) : None<Scalar>();
    }
    auto mounted(UiId id) -> bool {
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
    static void clamp_viewport(ViewportLayout &view) {
        view.metrics.offset = {limit(view.requested.x, view.metrics.maximum.x),
                               limit(view.requested.y, view.metrics.maximum.y)};
        view.requested = view.metrics.offset;
    }
    static auto intersection(Option<Scalar> a, Option<Scalar> b) -> Option<Scalar> {
        if (a.is_none())
            return b;
        if (b.is_none())
            return a;
        return Some(*a < *b ? Scalar(*a) : Scalar(*b));
    }
    static auto maximum(Scalar a, Scalar b) -> Scalar { return a > b ? a : b; }
    static auto bounded(Scalar value, Scalar minimum, Option<Scalar> maximum) -> Scalar {
        return constrain(value < minimum ? minimum : value, maximum);
    }
    auto layout_failure(UiId id, LayoutIssue issue, UiError error = UiError::LayoutFailed)
        -> Option<UiError> {
        layout_diagnostics_.push(LayoutDiagnostic{id, issue});
        dirty_measure(id);
        return Some(error);
    }
    struct MeasureContext final : LayoutMeasureContext {
        UiDocument &document;
        Node &node;
        UiId id;
        Index depth;
        Vec<bool> visited;
        Option<UiError> error;
        MeasureContext(UiDocument &d, Node &n, UiId i, Index level)
            : document(d), node(n), id(i), depth(level) {
            for (usize j{}; j < node.children.len(); ++j)
                visited.push(false);
        }
        auto node_id() const -> UiId override { return id; }
        auto style() const -> LayoutStyle override { return effective_style(node).layout; }
        auto child_relation(usize index) const -> Result<UiRelation, UiError> override {
            if (index >= node.children.len())
                return Err(UiError::MissingChild);
            auto child = node.children[index];
            return Ok(UiRelation{id, child, document.resolve(child)->relation_generation});
        }
        auto child_count() const -> usize override { return node.children.len(); }
        auto child_id(usize index) const -> Result<UiId, UiError> override {
            if (index >= node.children.len())
                return Err(UiError::MissingChild);
            return Ok(UiId(node.children[index]));
        }
        auto measure_child(usize index, LayoutConstraints constraints)
            -> Result<Size, UiError> override {
            if (error.is_some())
                return Err(*error);
            if (index >= child_count() || !constraints.valid()) {
                error = document.layout_failure(id, LayoutIssue::InvalidChild);
                return Err(*error);
            }
            error = document.measure(node.children[index], constraints, depth + 1);
            if (error.is_some())
                return Err(*error);
            visited[index] = true;
            return Ok(Size(document.resolve(node.children[index])->desired));
        }
        void request_layout() override { document.request_layout(id); }
        auto complete() -> bool {
            for (auto done : visited)
                if (!done)
                    return false;
            return true;
        }
    };
    struct ArrangeContext final : LayoutArrangeContext {
        UiDocument &document;
        Node &node;
        UiId id;
        Index depth;
        Scalar padding;
        Vec<bool> visited;
        Option<UiError> error;
        ArrangeContext(UiDocument &d, Node &n, UiId i, Index level, Scalar p)
            : document(d), node(n), id(i), depth(level), padding(p) {
            for (usize j{}; j < node.children.len(); ++j)
                visited.push(false);
        }
        auto node_id() const -> UiId override { return id; }
        auto style() const -> LayoutStyle override { return effective_style(node).layout; }
        auto child_relation(usize index) const -> Result<UiRelation, UiError> override {
            if (index >= node.children.len())
                return Err(UiError::MissingChild);
            auto child = node.children[index];
            return Ok(UiRelation{id, child, document.resolve(child)->relation_generation});
        }
        auto child_count() const -> usize override { return node.children.len(); }
        auto child_id(usize index) const -> Result<UiId, UiError> override {
            if (index >= node.children.len())
                return Err(UiError::MissingChild);
            return Ok(UiId(node.children[index]));
        }
        auto desired_child(usize index) -> Result<Size, UiError> override {
            if (error.is_some())
                return Err(*error);
            if (index >= child_count()) {
                error = document.layout_failure(id, LayoutIssue::InvalidChild);
                return Err(*error);
            }
            return Ok(Size(document.resolve(node.children[index])->desired));
        }
        auto child_text(usize index) -> Result<TextPlacement, UiError> override {
            if (index >= child_count()) return Err(UiError::MissingChild);
            const auto &child = *document.resolve(node.children[index]);
            if (child.text.is_none()) return Err(UiError::InvalidValue);
            const auto &text = **child.text;
            if (text.dirty || text.layout.is_none()) return Err(UiError::LayoutPending);
            return Ok(TextPlacement{*text.layout, text_origin(child)});
        }
        auto set_text_input_area(Option<Rect> area) -> Result<empty, UiError> override {
            if (area.is_some() && !area->valid()) return Err(UiError::InvalidValue);
            if (area.is_some()) { area->x += padding; area->y += padding; }
            node.layout_has_text_input_area = true; node.layout_text_input_area = area;
            return Ok(empty{});
        }
        void request_paint() override { node.paint_dirty = true; }
        auto arrange_child(usize index, Rect slot) -> Result<empty, UiError> override {
            if (error.is_some())
                return Err(*error);
            if (index >= child_count() || visited[index] || !slot.valid()) {
                error = document.layout_failure(id, LayoutIssue::InvalidChild);
                return Err(*error);
            }
            slot.x += padding;
            slot.y += padding;
            error = document.arrange(node.children[index], slot, depth + 1);
            if (error.is_some())
                return Err(*error);
            visited[index] = true;
            return Ok(empty{});
        }
        auto place_child(usize index, LayoutPlacement placement)
            -> Result<empty, UiError> override {
            if (error.is_some())
                return Err(*error);
            if (index >= child_count() || visited[index] || !placement.valid()) {
                error = document.layout_failure(id, LayoutIssue::InvalidChild);
                return Err(*error);
            }
            placement.slot.x += padding;
            placement.slot.y += padding;
            auto child = node.children[index];
            error = document.measure(child,
                                     {Some(placement.local_size.width),
                                      Some(placement.local_size.height), placement.local_size.width,
                                      placement.local_size.height},
                                     depth + 1);
            if (error.is_none())
                error = document.arrange(child, placement.slot, depth + 1, Some(placement));
            if (error.is_some())
                return Err(*error);
            visited[index] = true;
            return Ok(empty{});
        }
        auto set_clip(Clip clip) -> Result<empty, UiError> override {
            if (!clip.valid()) {
                error = document.layout_failure(id, LayoutIssue::InvalidChild);
                return Err(*error);
            }
            clip.rect.x += padding;
            clip.rect.y += padding;
            node.layout_clip = clip;
            return Ok(empty{});
        }
        void request_layout() override { document.request_layout(id); }
        auto complete() -> bool {
            for (auto done : visited)
                if (!done)
                    return false;
            return true;
        }
    };
    auto measure(UiId id, LayoutConstraints available, Index depth) -> Option<UiError> {
        if (depth >= depth_limit_ || ++layout_work_ > 1'000'000)
            return layout_failure(id, LayoutIssue::Limit, UiError::Limit);
        auto &node = *resolve(id);
        if (!node.measure_dirty && node.measure_constraints == Some(available)) {
            ++stats_.measure_cache_hits;
            return None();
        }
        node.measure_dirty = true;
        node.arrange_dirty = true;
        ++stats_.measurements;
        const auto style = effective_style(node).layout;
        auto max_width = intersection(style.max_width, available.max_width);
        auto max_height = intersection(style.max_height, available.max_height);
        auto min_width = maximum(style.min_width, available.min_width);
        auto min_height = maximum(style.min_height, available.min_height);
        node.constraint_conflict =
            (available.max_width.is_some() &&
             (min_width > *available.max_width ||
              (style.width.is_some() && *style.width > *available.max_width))) ||
            (available.max_height.is_some() &&
             (min_height > *available.max_height ||
              (style.height.is_some() && *style.height > *available.max_height))) ||
            (style.max_width.is_some() && *style.max_width < available.min_width) ||
            (style.max_height.is_some() && *style.max_height < available.min_height);
        // Parent constraints take precedence over a child's preferred range.
        if (max_width.is_some())
            max_width = Some(maximum(*max_width, available.min_width));
        if (max_height.is_some())
            max_height = Some(maximum(*max_height, available.min_height));
        min_width = constrain(min_width, max_width);
        min_height = constrain(min_height, max_height);
        auto width =
            style.width.is_some() ? Some(bounded(*style.width, min_width, max_width)) : max_width;
        auto height = style.height.is_some() ? Some(bounded(*style.height, min_height, max_height))
                                             : max_height;
        LayoutConstraints inner{inset(width, style.padding), inset(height, style.padding)};
        if (node.viewport.is_some()) {
            if ((*node.viewport)->axes.horizontal)
                inner.max_width = None();
            if ((*node.viewport)->axes.vertical)
                inner.max_height = None();
        }
        Size desired;
        if (node.text.is_some()) {
            if (text_service_.is_none())
                return Some(UiError::TextServiceMissing);
            auto &text = **node.text;
            auto wrap_width =
                text.wrap == TextWrap::None && text.alignment == TextAlignment::Natural
                    ? None<Scalar>()
                    : inner.max_width;
            if (text.dirty || text.layout.is_none() || text.width != wrap_width) {
                ++stats_.text_layouts;
                TextRequest request{text.value.as_str(), text.family.as_str(), text.language.as_str(),
                    text.size, wrap_width, text.direction, text.wrap, text.bold, text.italic, text.alignment, text.line_spacing};
                if (text.boundaries.present()) request.boundaries = &text.boundaries;
                else if (text.layout.is_some() && text.layout->text() == text.value.as_str())
                    request.boundaries = &text.layout->boundaries();
                auto result = (*text_service_)->layout(request);
                if (result.is_err()) {
                    text.error = Some(result.unwrap_err());
                    return Some(UiError::TextLayoutFailed);
                }
                text.layout = Some(rstd::move(result).unwrap());
                text.width = wrap_width;
                text.error = None();
                text.dirty = false;
                node.paint_dirty = true;
            }
            auto bounds = text.layout->logical_bounds();
            desired = {bounds.width, bounds.height};
        }
        if (node.image.is_some() && (*node.image)->data.is_some())
            desired = (*node.image)->data->natural_size();

        if (node.layout_strategy.is_some() || node.kind == LayoutKind::Custom ||
            (node.kind == LayoutKind::Item && !node.children.is_empty())) {
            if (node.layout_strategy.is_none())
                return layout_failure(id, LayoutIssue::Strategy);
            MeasureContext context(*this, node, id, depth);
            auto result = (*node.layout_strategy)->measure(context, inner);
            if (context.error.is_some())
                return context.error;
            if (result.is_err())
                return layout_failure(id, LayoutIssue::Strategy, result.unwrap_err());
            if (!context.complete())
                return layout_failure(id, LayoutIssue::MissingChild);
            desired = result.unwrap();
        } else {
            for (auto child : node.children) {
                auto error = measure(child, inner, depth + 1);
                if (error.is_some())
                    return error;
                auto size = resolve(child)->desired;
                if (node.kind == LayoutKind::Row) {
                    desired.width += size.width;
                    desired.height = maximum(desired.height, size.height);
                } else if (node.kind == LayoutKind::Column) {
                    desired.height += size.height;
                    desired.width = maximum(desired.width, size.width);
                } else {
                    desired.width = maximum(desired.width, size.width);
                    desired.height = maximum(desired.height, size.height);
                }
            }
            if (node.children.len() > usize(1)) {
                auto gap = style.gap *
                           static_cast<Scalar>((node.children.len() - usize(1)).to_primitive());
                if (node.kind == LayoutKind::Row)
                    desired.width += gap;
                if (node.kind == LayoutKind::Column)
                    desired.height += gap;
            }
        }
        if (!desired.valid())
            return layout_failure(id, LayoutIssue::Strategy, UiError::InvalidValue);
        desired.width += 2 * style.padding;
        desired.height += 2 * style.padding;
        if (!desired.valid())
            return layout_failure(id, LayoutIssue::Strategy, UiError::InvalidValue);
        node.desired = {width.is_some() && (style.width.is_some() || node.viewport.is_some())
                            ? *width
                            : bounded(desired.width, min_width, max_width),
                        height.is_some() && (style.height.is_some() || node.viewport.is_some())
                            ? *height
                            : bounded(desired.height, min_height, max_height)};
        node.measure_constraints = Some(available);
        node.measure_dirty = false;
        return None();
    }
    static auto aligned_offset(Scalar remaining, Alignment align) -> Scalar {
        return align == Alignment::Center ? remaining / 2 : align == Alignment::End ? remaining : 0;
    }
    auto arrange(UiId id, Rect slot, Index depth, Option<LayoutPlacement> mapped = {})
        -> Option<UiError> {
        if (depth >= depth_limit_ || ++layout_work_ > 1'000'000)
            return layout_failure(id, LayoutIssue::Limit, UiError::Limit);
        if (!slot.valid())
            return layout_failure(id, LayoutIssue::Strategy, UiError::InvalidValue);
        auto &node = *resolve(id);
        if (!node.arrange_dirty && !node.subtree_arrange_dirty &&
            node.arranged_slot == Some(slot) &&
            node.layout_mapping == (mapped.is_some() ? mapped->local_to_slot : Transform{}) &&
            node.layout_local_size ==
                (mapped.is_some() ? Some(mapped->local_size) : None<Size>())) {
            ++stats_.arrange_cache_hits;
            return None();
        }
        auto allocated = slot;
        auto mapping = mapped.is_some() ? mapped->local_to_slot : Transform{};
        auto local_size = mapped.is_some() ? Some(mapped->local_size) : None<Size>();
        if (mapped.is_some()) {
            slot.width = mapped->local_size.width;
            slot.height = mapped->local_size.height;
        }
        node.layout_mapping = mapping;
        node.layout_local_size = local_size;
        const auto style = effective_style(node).layout;
        auto horizontal_stretch = style.horizontal == Alignment::Stretch && style.width.is_none();
        auto vertical_stretch = style.vertical == Alignment::Stretch && style.height.is_none();
        const auto previous = *node.measure_constraints;
        auto min_width = maximum(style.min_width, previous.min_width);
        auto min_height = maximum(style.min_height, previous.min_height);
        auto max_width = style.max_width.is_some()
                             ? Some(maximum(*style.max_width, previous.min_width))
                             : None<Scalar>();
        auto max_height = style.max_height.is_some()
                              ? Some(maximum(*style.max_height, previous.min_height))
                              : None<Scalar>();
        max_width = intersection(max_width, Some(slot.width));
        max_height = intersection(max_height, Some(slot.height));
        auto width =
            bounded(horizontal_stretch ? slot.width : node.desired.width, min_width, max_width);
        auto height =
            bounded(vertical_stretch ? slot.height : node.desired.height, min_height, max_height);
        if (node.desired.width > width || node.desired.height > height ||
            (horizontal_stretch && node.desired.width != width) ||
            (vertical_stretch && node.desired.height != height)) {
            auto error =
                measure(id,
                        {Some(width), max_height, constrain(previous.min_width, Some(width)),
                         constrain(previous.min_height, max_height)},
                        depth);
            if (error.is_some())
                return error;
            if (!horizontal_stretch)
                width = constrain(node.desired.width, Some(width));
            if (!vertical_stretch)
                height = bounded(node.desired.height, min_height, max_height);
        }
        ++stats_.arrangements;
        Size measured{width, height};
        bool visit_children =
            node.arrange_dirty || node.subtree_arrange_dirty || node.measured != measured;
        if (node.measured != measured)
            node.paint_dirty = true;
        node.measured = measured;
        node.position = {slot.x + aligned_offset(slot.width - width, style.horizontal),
                         slot.y + aligned_offset(slot.height - height, style.vertical)};
        Size inner{limit(width - 2 * style.padding, width),
                   limit(height - 2 * style.padding, height)};
        if (visit_children) {
            node.layout_clip = {};
            node.layout_has_text_input_area = false;
            node.layout_text_input_area = None();
        }
        if (visit_children && node.layout_strategy.is_some()) {
            ArrangeContext context(*this, node, id, depth, style.padding);
            auto result = (*node.layout_strategy)->arrange(context, inner);
            if (context.error.is_some())
                return context.error;
            if (result.is_err())
                return layout_failure(id, LayoutIssue::Strategy, result.unwrap_err());
            if (!context.complete())
                return layout_failure(id, LayoutIssue::MissingChild);
        } else if (visit_children) {
            Scalar offset{};
            for (auto child : node.children) {
                auto &item = *resolve(child);
                Rect child_slot{style.padding, style.padding, inner.width, inner.height};
                if (node.kind == LayoutKind::Row) {
                    child_slot.x += offset;
                    child_slot.width = item.desired.width;
                    offset += item.desired.width + style.gap;
                }
                if (node.kind == LayoutKind::Column) {
                    child_slot.y += offset;
                    child_slot.height = item.desired.height;
                    offset += item.desired.height + style.gap;
                }
                if (node.viewport.is_some()) {
                    if ((*node.viewport)->axes.horizontal)
                        child_slot.width = maximum(child_slot.width, item.desired.width);
                    if ((*node.viewport)->axes.vertical)
                        child_slot.height = maximum(child_slot.height, item.desired.height);
                }
                auto error = arrange(child, child_slot, depth + 1);
                if (error.is_some())
                    return error;
            }
        }
        if (node.viewport.is_some()) {
            auto &view = **node.viewport;
            view.metrics.viewport = inner;
            view.metrics.extent =
                node.children.is_empty() ? Size{} : resolve(node.children[usize()])->measured;
            view.metrics.maximum = {
                view.axes.horizontal ? maximum(view.metrics.extent.width - inner.width, 0) : 0,
                view.axes.vertical ? maximum(view.metrics.extent.height - inner.height, 0) : 0};
            clamp_viewport(view);
        }
        node.arranged_slot = Some(allocated);
        node.arrange_dirty = false;
        node.subtree_arrange_dirty = false;
        return None();
    }
    static auto text_origin(const Node &node) -> Point {
        auto bounds = (*node.text)->layout->logical_bounds();
        return {node.style.layout.padding - bounds.x, node.style.layout.padding - bounds.y};
    }
    static auto place_image_content(const Node &node) -> Option<ImagePlacement> {
        const auto &image = **node.image;
        if (image.data.is_none())
            return None();
        auto padding = node.style.layout.padding;
        return Some(place_image(*image.data,
                                {padding, padding,
                                 limit(node.measured.width - 2 * padding, node.measured.width),
                                 limit(node.measured.height - 2 * padding, node.measured.height)},
                                image.fit)
                        .unwrap());
    }
    auto paint(UiId id, Node &node) -> Option<UiError> {
        painting_ = true;
        struct Guard {
            bool &active;
            ~Guard() { active = false; }
        } guard{painting_};
        CpuTimer timer(active_timings_ ? &active_timings_->paint : nullptr);
        DisplayListRecorder recorder;
        auto style = effective_style(node);
        if (style.background.a > 0)
            recorder.rect({0, 0, node.measured.width, node.measured.height}, style.background,
                          style.radius);
        if (node.text.is_some() && (*node.text)->layout.is_some()) {
            auto &text = **node.text;
            recorder.text(TextLayout(*text.layout), text_origin(node), text.color, text.effects, TextColors(text.colors), TextDecorations(text.decorations));
        }
        if (node.image.is_some() && (*node.image)->data.is_some()) {
            auto &image = **node.image;
            auto placed = *place_image_content(node);
            recorder.image(ImageData(*image.data), placed.destination, placed.source,
                           image.sampling, image.tint);
        }
        if (node.scene.is_some()) {
            const auto &scene = **node.scene;
            auto padding = node.style.layout.padding;
            Rect box{padding, padding,
                     limit(node.measured.width - 2 * padding, node.measured.width),
                     limit(node.measured.height - 2 * padding, node.measured.height)};
            if (scene.source.is_some())
                recorder.external(*scene.source, box, scene.fit, scene.sampling, scene.placeholder);
            else
                recorder.rect(box, scene.placeholder);
        }
        bool repaint{};
        if (node.painter.is_some()) {
            PaintContext context(id, node.measured, recorder, style);
            auto result = (*node.painter)->paint(context);
            if (result.is_err()) {
                paint_diagnostics_.push({id, {}, result.unwrap_err()});
                return Some(result.unwrap_err());
            }
            repaint = context.repaint_requested();
        } else if (node.kind != LayoutKind::Leaf && node.kind != LayoutKind::Text &&
                   node.kind != LayoutKind::Image && node.kind != LayoutKind::Scene) {
            recorder.children();
        }
        auto recorded = rstd::move(recorder).finish();
        if (recorded.is_err()) {
            paint_diagnostics_.push({id, Some(recorded.unwrap_err()), UiError::PaintFailed});
            return Some(UiError::PaintFailed);
        }
        auto content = rstd::move(recorded).unwrap();
        if (!node.children.is_empty() && !content.has_children()) {
            paint_diagnostics_.push({id, {}, UiError::MissingChild});
            return Some(UiError::PaintFailed);
        }
        node.content = rstd::move(content);
        node.paint_dirty = repaint;
        if (repaint)
            changed(false);
        ++stats_.recordings;
        return None();
    }
    auto make_visual(UiId id) -> Result<UiVisual, UiError> {
        auto &node = *resolve(id);
        Transform placement;
        placement.tx = node.position.x;
        placement.ty = node.position.y;
        if (node.parent.is_some()) {
            auto parent = resolve(*node.parent);
            if (parent->viewport.is_some()) {
                placement.tx -= (*parent->viewport)->metrics.offset.x;
                placement.ty -= (*parent->viewport)->metrics.offset.y;
            }
        }
        auto clip = node.style.clip;
        if (node.viewport.is_some()) {
            auto size = (*node.viewport)->metrics.viewport;
            clip = {true,
                    {node.style.layout.padding, node.style.layout.padding, size.width, size.height},
                    0};
        }
        Rect layout_bounds{0, 0, node.measured.width, node.measured.height};
        auto content_bounds = layout_bounds;
        if (auto extent = node.content.bounds(); extent.is_some()) {
            auto left = rstd::cmp::min(Scalar(0), extent->x),
                 top = rstd::cmp::min(Scalar(0), extent->y);
            content_bounds = {
                left, top, rstd::cmp::max(layout_bounds.width, extent->x + extent->width) - left,
                rstd::cmp::max(layout_bounds.height, extent->y + extent->height) - top};
        }
        auto style = effective_style(node);
        auto transform = node.layout_mapping == Transform{}
                             ? placement * style.transform
                             : placement * node.layout_mapping * style.transform;
        RenderProperties properties{transform,     content_bounds, clip,
                                    style.opacity, style.visible,  node.layout_clip};
        if (!properties.valid())
            return Err(UiError::InvalidValue);
        UiVisual visual{
            id,
            properties,
            node.content,
            {},
            node.style.hit_test_visible,
            node.style.enabled,
            node.style.focusable,
            node.parent,
            node.mount_generation,
            node.viewport.is_some() ? Some(ViewportMetrics((*node.viewport)->metrics))
                                    : None<ViewportMetrics>(),
            layout_bounds,
            {node.position.x, node.position.y, node.measured.width, node.measured.height},
            node.arranged_slot,
            node.style.hit_self,
            node.layout_has_text_input_area ? node.layout_text_input_area : node.style.text_input_area};
        for (auto child : node.children)
            visual.children.push(UiId(child));
        return Ok(rstd::move(visual));
    }
    auto collect(UiId id, UiUpdate::Data &output, Index depth, bool recording = true)
        -> Option<UiError> {
        ++stats_.collected_nodes;
        if (depth >= depth_limit_)
            return Some(UiError::Limit);
        auto &node = *resolve(id);
        if (node.constraint_conflict)
            layout_diagnostics_.push(LayoutDiagnostic{id, LayoutIssue::ConstraintConflict});
        if (recording && node.paint_dirty) {
            auto error = paint(id, node);
            if (error.is_some())
                return error;
        }
        auto built = make_visual(id);
        if (built.is_err())
            return Some(built.unwrap_err());
        auto visual = rstd::move(built).unwrap();
        output.indices[usize(id.slot)] = static_cast<Index>(output.visuals.len().to_primitive());
        output.visuals.push(rstd::move(visual));
        for (auto child : node.children) {
            auto error = collect(child, output, depth + 1, recording);
            if (error.is_some())
                return error;
        }
        return None();
    }
    void erase(UiId id) {
        auto &slot = slots_[usize(id.slot)];
        if (slot.node->viewport.is_some() && (*slot.node->viewport)->observer.is_some()) (*(*slot.node->viewport)->observer).get()->changed.close();
        if (slot.node->layout_binding.is_some())
            slot.node->layout_binding->disconnect();
        if (slot.node->paint_binding.is_some())
            slot.node->paint_binding->disconnect();
        auto children = rstd::move(slot.node->children);
        for (auto child : children)
            erase(child);
        slot.node = None();
        if (slot.generation != ~Serial{})
            ++slot.generation;
    }

  public:
    class RouteScope {
        UiDocument *document_;
        friend class UiDocument;
        explicit RouteScope(UiDocument &document) : document_(&document) {}

      public:
        RouteScope(const RouteScope &) = delete;
        auto operator=(const RouteScope &) -> RouteScope & = delete;
        RouteScope(RouteScope &&other) : document_(rstd::exchange(other.document_, nullptr)) {}
        ~RouteScope() {
            if (document_)
                document_->routing_ = false;
        }
    };
    UiDocument() = default;
    ~UiDocument() { bindings_.close(); }
    UiDocument(const UiDocument &) = delete;
    auto operator=(const UiDocument &) -> UiDocument & = delete;
    auto session() const -> Serial { return session_; }
    auto bindings() -> BindingRegistry & { return bindings_; }
    auto lifetime() const -> BindingGuard { return lease_owner_.guard(); }
    auto paint_diagnostics() const -> slice<PaintDiagnostic> {
        return paint_diagnostics_.as_slice();
    }
    auto invalidate_paint(UiId id) -> Result<empty, UiError> {
        if (layout_active_ || painting_)
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        node->paint_dirty = true;
        changed(false);
        return Ok(empty{});
    }
    auto revision() const -> Serial { return revision_; }
    auto routing() const -> bool { return routing_; }
    auto can_route() const -> bool {
        return !routing_ && !draining_ && !animation_dispatching_ && !layout_active_ && !painting_;
    }
    auto acquire_route() -> Result<RouteScope, UiError> {
        if (!can_route())
            return Err(UiError::Routing);
        routing_ = true;
        return Ok(RouteScope(*this));
    }
    auto pending_mutations() const -> usize { return mutations_.len(); }
    auto cancel_mutations() -> Result<Vec<MutationResult>, UiError> {
        if (routing_ || draining_ || animation_dispatching_ || (layout_active_ || painting_))
            return Err(UiError::Routing);
        auto pending = rstd::exchange(mutations_, Vec<Box<Mutation>>{});
        Vec<MutationResult> results;
        for (const auto &mutation : pending)
            results.push({mutation->id, Some(UiError::Cancelled)});
        return Ok(rstd::move(results));
    }
    template <class F> auto defer(F callback) -> Result<Serial, UiError> {
        if (layout_active_ || painting_)
            return Err(UiError::Routing);
        if (mutations_.len() >= usize(1024))
            return Err(UiError::Limit);
        return defer_cleanup(rstd::move(callback));
    }
    // A receiver's final reference can be released while layout or paint still runs.
    template <class F> auto defer_cleanup(F callback) -> Result<Serial, UiError> {
        if (mutations_.len() >= usize(4096) || next_mutation_ == ~Serial{}) {
            if (rejected_cleanups_ != ~Serial{})
                ++rejected_cleanups_;
            return Err(UiError::Limit);
        }
        auto id = next_mutation_++;
        auto item = Box<Deferred<F>>::make(id, rstd::move(callback));
        mutations_.push(
            Box<Mutation>::from_raw(rstd::move(item).into_raw().template cast<Mutation>()));
        bindings_.wake();
        return Ok(id);
    }
    auto rejected_cleanup_count() const -> Serial { return rejected_cleanups_; }
    template <class F>
    auto defer(UiId target, BindingGuard guard, F callback) -> Result<Serial, UiError> {
        auto mount = mount_state(target);
        if (mount.is_err())
            return Err(mount.unwrap_err());
        auto generation = mount.unwrap().generation;
        return defer([target, generation, guard, callback = rstd::move(callback)](
                         UiDocument &document) mutable -> Result<empty, UiError> {
            auto current = document.mount_state(target);
            if (!guard.alive() || current.is_err() || current.unwrap().generation != generation)
                return Err(UiError::Cancelled);
            return callback(document);
        });
    }
    template <class T, class F>
    auto defer(UiId target, BindingGuard guard, rstd::rc::Weak<T> receiver, F callback)
        -> Result<Serial, UiError> {
        auto mount = mount_state(target);
        if (mount.is_err())
            return Err(mount.unwrap_err());
        auto generation = mount.unwrap().generation;
        return defer([target, generation, guard, receiver = rstd::move(receiver),
                      callback = rstd::move(callback)](
                         UiDocument &document) mutable -> Result<empty, UiError> {
            auto current = document.mount_state(target);
            auto value = receiver.upgrade();
            if (!guard.alive() || value.is_none() || current.is_err() ||
                current.unwrap().generation != generation)
                return Err(UiError::Cancelled);
            return callback(*value->get(), document);
        });
    }
    auto flush_mutations() -> Result<Vec<MutationResult>, UiError> {
        if (routing_ || draining_ || animation_dispatching_ || (layout_active_ || painting_))
            return Err(UiError::Routing);
        draining_ = true;
        auto pending = rstd::exchange(mutations_, Vec<Box<Mutation>>{});
        Vec<MutationResult> results;
        for (auto &mutation : pending) {
            auto result = mutation->apply(*this);
            results.push(
                {mutation->id, result.is_err() ? Some(result.unwrap_err()) : None<UiError>()});
        }
        draining_ = false;
        return Ok(rstd::move(results));
    }
    auto valid(UiId id) const -> bool {
        if (!id.valid() || id.session != session_ || usize(id.slot) >= slots_.len())
            return false;
        const auto &slot = slots_[usize(id.slot)];
        return slot.generation == id.generation && slot.node.is_some();
    }
    template <class T> auto attach_state(UiId id, T value) -> Result<empty, UiError> {
        if (routing_ || (layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->state.is_some())
            return Err(UiError::InvalidValue);
        auto state = Box<StoredState<T>>::make(rstd::move(value));
        node->state =
            Some(Box<State>::from_raw(rstd::move(state).into_raw().template cast<State>()));
        return Ok(empty{});
    }
    template <class T> auto state(UiId id) -> T * {
        auto node = resolve(id);
        if (!node || node->state.is_none() || (**node->state).type() != rstd::any::TypeId::of<T>())
            return nullptr;
        return &static_cast<StoredState<T> &>(**node->state).value;
    }
    template <class T> auto create(NodeStyle style = {}) -> Result<UiId, UiError> {
        if (routing_ || (layout_active_ || painting_))
            return Err(UiError::Routing);
        if (!style.valid() || (T::layout == LayoutKind::Viewport && style.clip.enabled))
            return Err(UiError::InvalidValue);
        usize index{};
        while (index < slots_.len() &&
               (slots_[index].node.is_some() || slots_[index].generation == ~Serial{}))
            ++index;
        if (index.to_primitive() >= 1'000'000)
            return Err(UiError::Limit);
        if (index == slots_.len())
            slots_.push(Slot{});
        slots_[index].node = Some(Node(T::layout, style));
        if constexpr (T::layout == LayoutKind::Viewport)
            slots_[index].node->viewport = Some(Box<ViewportLayout>::make());
        if constexpr (T::layout == LayoutKind::Text)
            slots_[index].node->text = Some(Box<TextContent>::make());
        if constexpr (T::layout == LayoutKind::Image)
            slots_[index].node->image = Some(Box<ImageContent>::make());
        if constexpr (T::layout == LayoutKind::Scene)
            slots_[index].node->scene = Some(Box<SceneContent>::make());
        changed(false);
        return Ok(
            UiId{session_, static_cast<Index>(index.to_primitive()), slots_[index].generation});
    }
    auto parent_relation(UiId child) -> Result<Option<UiRelation>, UiError> {
        auto node = resolve(child);
        if (!node)
            return Err(UiError::InvalidId);
        return Ok(node->parent.is_some()
                      ? Some(UiRelation{*node->parent, child, node->relation_generation})
                      : None<UiRelation>());
    }
    auto is_descendant(UiId root, UiId child) -> Result<bool, UiError> {
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
    auto child_relation(UiId parent, UiId child) -> Result<UiRelation, UiError> {
        auto p = resolve(parent), c = resolve(child);
        if (!p || !c || c->parent != Some(parent))
            return Err(UiError::InvalidId);
        return Ok(UiRelation{parent, child, c->relation_generation});
    }
    auto attach(UiId parent, UiId child) -> Result<empty, UiError> {
        if (routing_ || (layout_active_ || painting_))
            return Err(UiError::Routing);
        auto p = resolve(parent), c = resolve(child);
        if (!p || !c)
            return Err(UiError::InvalidId);
        if (p->kind == LayoutKind::Leaf || p->kind == LayoutKind::Text ||
            p->kind == LayoutKind::Image || p->kind == LayoutKind::Scene)
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
    template <class T> auto append(UiId parent, NodeStyle style = {}) -> Result<UiId, UiError> {
        if (!valid(parent))
            return Err(UiError::InvalidId);
        auto result = create<T>(style);
        if (result.is_err())
            return result;
        auto id = result.unwrap();
        auto attached = attach(parent, id);
        if (attached.is_err()) {
            erase(id);
            return Err(attached.unwrap_err());
        }
        return Ok(id);
    }
    auto mount(UiId root) -> Result<empty, UiError> {
        if (routing_ || (layout_active_ || painting_))
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
    auto detach(UiId id) -> Result<empty, UiError> {
        if (routing_ || (layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
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
    auto destroy(UiId id) -> Result<empty, UiError> {
        auto result = detach(id);
        if (result.is_err())
            return result;
        erase(id);
        notify_targets();
        return Ok(empty{});
    }
    auto reorder(UiId parent, UiId child, usize destination) -> Result<empty, UiError> {
        if (routing_ || (layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(parent);
        if (!node)
            return Err(UiError::InvalidId);
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
    auto mount_state(UiId id) -> Result<UiMountState, UiError> {
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        return Ok(UiMountState{mounted(id), node->mount_generation});
    }
    auto text_target(UiId id) -> Result<UiPropertyTarget<String>, UiError> {
        return value_target<String>(id, UiValueProperty::Text);
    }
    auto visible_target(UiId id) -> Result<UiPropertyTarget<bool>, UiError> {
        return value_target<bool>(id, UiValueProperty::Visible);
    }
    auto image_target(UiId id) -> Result<UiPropertyTarget<Option<ImageData>>, UiError> {
        return value_target<Option<ImageData>>(id, UiValueProperty::Image);
    }
    template <class T>
    auto claim_value_writer(const UiPropertyTarget<T> &target)
        -> Result<UiValueWriter<T>, UiError> {
        if (layout_active_ || painting_)
            return Err(UiError::Routing);
        if (target.document() != session_ || !target.status().valid)
            return Err(UiError::InvalidId);
        auto node = resolve(target.node());
        if (value_occupied(*node, target.state_->property))
            return Err(UiError::PropertyOwned);
        auto writer = rstd::rc::make_rc<UiValueWriterState>(target.state_);
        node->value_writer[unsigned(target.state_->property)] = writer.downgrade();
        return Ok(UiValueWriter<T>(rstd::move(writer)));
    }
    template <class T, class F>
    auto observe_target(UiPropertyTarget<T> target, F callback)
        -> Result<UiTargetSubscription, UiError> {
        if (target.document() != session_ || !target.status().valid)
            return Err(UiError::InvalidId);
        auto &state = *target.state_.get();
        state.observers.retain([](const auto &observer) { return observer->guard.alive(); });
        if (state.observers.len() >= usize(4096))
            return Err(UiError::Limit);
        auto lifetime = bindings_.make();
        state.observers.push(rstd::rc::make_rc<UiTargetObserver>(UiTargetObserver{
            lifetime.guard(),
            Box<rstd::dyn<rstd::FnMut<void(UiTargetStatus)>>>::make(rstd::move(callback))}));
        return Ok(rstd::move(lifetime));
    }
    auto apply_values(UiValueBatch batch) -> Result<usize, UiValueBatchError> {
        if (layout_active_ || painting_)
            return Err(UiValueBatchError{usize(), UiError::Routing});
        if (batch.size() > usize(4096))
            return Err(UiValueBatchError{usize(), UiError::Limit});
        for (usize i{}; i < batch.entries_.len(); ++i) {
            const auto &entry = batch.entries_[i];
            if (!entry.writer || !entry.writer->active)
                return Err(UiValueBatchError{i, UiError::Cancelled});
            const auto &target = *entry.writer->target;
            auto node = resolve(target.node);
            if (target.document != session_ || !node)
                return Err(UiValueBatchError{i, UiError::InvalidId});
            if (!mounted(target.node) || node->mount_generation != entry.generation)
                return Err(UiValueBatchError{i, UiError::Cancelled});
            auto owner = node->value_writer[unsigned(target.property)].upgrade();
            if (owner.is_none() || owner->get() != entry.writer.get())
                return Err(UiValueBatchError{i, UiError::PropertyOwned});
        }
        usize changed_count{};
        for (auto &entry : batch.entries_) {
            const auto &target = *entry.writer->target;
            auto node = resolve(target.node);
            switch (target.property) {
            case UiValueProperty::Text:
                changed_count += usize(write_text(target.node, *node, rstd::move(entry.text)));
                break;
            case UiValueProperty::Visible:
                changed_count += usize(write_visible(target.node, *node, entry.visible));
                break;
            case UiValueProperty::Image:
                changed_count += usize(write_image(target.node, *node, rstd::move(entry.image)));
                break;
            }
        }
        return Ok(changed_count);
    }
    auto take_over(slice<PropertyKey> keys, TakeoverPolicy policy = TakeoverPolicy::PreserveCurrent)
        -> Result<PresentationLease, UiError> {
        if (layout_active_ || painting_)
            return Err(UiError::Routing);
        if (keys.is_empty() || keys.len() > usize(256) || !lease_owner_.alive())
            return Err(UiError::InvalidValue);
        if (next_lease_ == ~Serial{})
            return Err(UiError::Limit);
        for (usize i{}; i < keys.len(); ++i) {
            auto node = resolve(keys[i].target);
            if (!node || !mounted(keys[i].target))
                return Err(UiError::InvalidId);
            if (property_index(keys[i].property) >= 7)
                return Err(UiError::InvalidValue);
            for (usize j{}; j < i; ++j)
                if (keys[j] == keys[i])
                    return Err(UiError::InvalidValue);
            if ((keys[i].property == PresentationProperty::Width ||
                 keys[i].property == PresentationProperty::Height) &&
                layout_dirty_)
                return Err(UiError::LayoutPending);
            if (keys[i].property == PresentationProperty::ViewportOffset &&
                node->viewport.is_none())
                return Err(UiError::InvalidValue);
            if (keys[i].property == PresentationProperty::Clip && node->viewport.is_some())
                return Err(UiError::InvalidValue);
            if (policy == TakeoverPolicy::RejectOccupied) {
                if (property_occupied(*node, keys[i].property))
                    return Err(UiError::PropertyOwned);
                for (const auto &animation : animations_)
                    if (animation.state.target == keys[i].target &&
                        property_index(keys[i].property) ==
                            static_cast<unsigned>(animation.state.property))
                        return Err(UiError::PropertyOwned);
            }
        }
        auto state = rstd::rc::make_rc<PropertyLeaseState>();
        auto stored = state.get();
        stored->document = session_;
        stored->id = next_lease_++;
        stored->owner = lease_owner_.guard();
        for (const auto &key : keys) {
            auto &node = *resolve(key.target);
            stored->keys.push(PropertyKey(key));
            stored->mounts.push(Serial(node.mount_generation));
            stored->originals.push(read_property(key.target, node, key.property, true));
            auto current = read_property(key.target, node, key.property);
            stored->current.push(PropertyWrite(current));
            revoke_property(node, key.property);
            for (auto i = animations_.len(); i > usize(); --i) {
                auto &animation = animations_[i - usize(1)];
                if (animation.state.target == key.target &&
                    property_index(key.property) == static_cast<unsigned>(animation.state.property))
                    end_animation(i - usize(1), AnimationEndReason::Replaced, true);
            }
            apply_property(node, current);
            node.property_owner[property_index(key.property)] = state.downgrade();
        }
        return Ok(PresentationLease(rstd::move(state)));
    }
    auto write_presentation(PresentationLease &lease, const PresentationBatch &batch,
                            Option<BindingGuard> driver = {}) -> Result<empty, UiError> {
        if (layout_active_ || painting_)
            return Err(UiError::Routing);
        if (!lease.active() || lease.state_.get()->document != session_)
            return Err(UiError::PropertyOwned);
        if (driver.is_some() && !driver->alive())
            return Err(UiError::PropertyOwned);
        auto state = lease.state_.get();
        if (driver.is_some() && state->driver.is_some() && !driver->same_binding(*state->driver))
            return Err(UiError::PropertyOwned);
        for (usize i{}; i < state->keys.len(); ++i) {
            auto node = resolve(state->keys[i].target);
            if (!node || node->mount_generation != state->mounts[i])
                return Err(UiError::InvalidId);
            auto owner = node->property_owner[property_index(state->keys[i].property)].upgrade();
            if (owner.is_none() || owner->get() != state)
                return Err(UiError::PropertyOwned);
        }
        for (usize i{}; i < batch.writes().len(); ++i) {
            const auto &write = batch.writes()[i];
            auto node = resolve(write.key.target);
            if (!node || !valid_property(write, *node))
                return Err(UiError::InvalidValue);
            bool found{};
            for (const auto &key : state->keys)
                found |= key == write.key;
            if (!found)
                return Err(UiError::PropertyOwned);
            for (usize j{}; j < i; ++j)
                if (batch.writes()[j].key == write.key)
                    return Err(UiError::InvalidValue);
        }
        if (driver.is_some())
            state->driver = rstd::move(driver);
        for (const auto &write : batch.writes()) {
            auto &node = *resolve(write.key.target);
            apply_property(node, write);
            for (auto &current : state->current)
                if (current.key == write.key)
                    current = write;
        }
        return Ok(empty{});
    }
    auto release_presentation(PresentationLease &lease,
                              LeaseEndPolicy policy = LeaseEndPolicy::CommitCurrent)
        -> Result<empty, UiError> {
        if (layout_active_ || painting_)
            return Err(UiError::Routing);
        if (!lease.active() || lease.state_.get()->document != session_)
            return Err(UiError::PropertyOwned);
        if (policy == LeaseEndPolicy::RestoreBase) {
            for (const auto &write : lease.state_.get()->originals)
                if (!resolve(write.key.target))
                    return Err(UiError::InvalidId);
            for (const auto &write : lease.state_.get()->originals)
                apply_property(*resolve(write.key.target), write);
        }
        lease.revoke();
        return Ok(empty{});
    }
    auto base_style(UiId id) -> Result<NodeStyle, UiError> {
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        return Ok(NodeStyle(node->style));
    }
    auto resolved_style(UiId id) -> Result<NodeStyle, UiError> {
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        return Ok(effective_style(*node));
    }
    auto animate_transform(UiId id, Transform to, AnimationOptions options = {})
        -> Result<AnimationId, AnimationError> {
        return start_animation(id, AnimationProperty::Transform, AnimationValue::Matrix(to),
                               options);
    }
    auto animate_opacity(UiId id, Scalar to, AnimationOptions options = {})
        -> Result<AnimationId, AnimationError> {
        return start_animation(id, AnimationProperty::Opacity, AnimationValue::Number(to), options);
    }
    auto animate_width(UiId id, Scalar to, AnimationOptions options = {})
        -> Result<AnimationId, AnimationError> {
        return start_animation(id, AnimationProperty::Width, AnimationValue::Number(to), options);
    }
    auto animate_height(UiId id, Scalar to, AnimationOptions options = {})
        -> Result<AnimationId, AnimationError> {
        return start_animation(id, AnimationProperty::Height, AnimationValue::Number(to), options);
    }
    auto animate_background(UiId id, Color to, AnimationOptions options = {})
        -> Result<AnimationId, AnimationError> {
        return start_animation(id, AnimationProperty::Background, AnimationValue::Tint(to),
                               options);
    }
    auto animation_state(AnimationId id) const -> Result<AnimationState, AnimationError> {
        auto index = animation_index(id);
        if (index.is_none())
            return Err(AnimationError::InvalidHandle);
        return Ok(AnimationState(animations_[*index].state));
    }
    auto retarget_animation(AnimationId id, AnimationValue to, AnimationOptions options)
        -> Result<empty, AnimationError> {
        if ((layout_active_ || painting_))
            return Err(AnimationError::Reentrant);
        if (animations_closed_)
            return Err(AnimationError::Closed);
        auto index = animation_index(id);
        if (index.is_none())
            return Err(AnimationError::InvalidHandle);
        auto &track = animations_[*index];
        if (!options.valid() || !to.valid(track.state.property))
            return Err(AnimationError::InvalidValue);
        track.from = track.state.value;
        track.state.target_value = rstd::move(to);
        track.state.options = options;
        track.state.started_ns = animation_time_;
        return Ok(empty{});
    }
    auto retarget_animation(AnimationId id, AnimationValue to) -> Result<empty, AnimationError> {
        auto index = animation_index(id);
        if (index.is_none())
            return Err(AnimationError::InvalidHandle);
        return retarget_animation(id, rstd::move(to), animations_[*index].state.options);
    }
    auto cancel_animation(AnimationId id) -> Result<empty, AnimationError> {
        if ((layout_active_ || painting_))
            return Err(AnimationError::Reentrant);
        auto index = animation_index(id);
        if (index.is_none())
            return Err(AnimationError::InvalidHandle);
        end_animation(*index, AnimationEndReason::Cancelled);
        return Ok(empty{});
    }
    template <class F>
    auto on_animation_end(AnimationId id, F callback) -> Result<empty, AnimationError> {
        if ((layout_active_ || painting_))
            return Err(AnimationError::Reentrant);
        auto index = animation_index(id);
        if (index.is_none())
            return Err(AnimationError::InvalidHandle);
        auto item = Box<AnimationFunction<F>>::make(rstd::move(callback));
        animations_[*index].callback = Some(Box<AnimationCallback>::from_raw(
            rstd::move(item).into_raw().template cast<AnimationCallback>()));
        return Ok(empty{});
    }
    template <class F>
    auto on_animation_end(AnimationId id, BindingGuard guard, F callback)
        -> Result<empty, AnimationError> {
        return on_animation_end(id, [guard, callback = rstd::move(callback)](
                                        UiDocument &document, const AnimationEvent &event) mutable {
            if (!guard.alive())
                return;
            if (event.reason == AnimationEndReason::Finished) {
                auto mount = document.mount_state(event.target);
                if (mount.is_err() || mount.unwrap().generation != event.mount_generation)
                    return;
            }
            callback(document, event);
        });
    }
    template <class T, class F>
    auto on_animation_end(AnimationId id, BindingGuard guard, rstd::rc::Weak<T> receiver,
                          F callback) -> Result<empty, AnimationError> {
        return on_animation_end(
            id, [guard, receiver = rstd::move(receiver), callback = rstd::move(callback)](
                    UiDocument &document, const AnimationEvent &event) mutable {
                auto value = receiver.upgrade();
                if (!guard.alive() || value.is_none())
                    return;
                if (event.reason == AnimationEndReason::Finished) {
                    auto mount = document.mount_state(event.target);
                    if (mount.is_err() || mount.unwrap().generation != event.mount_generation)
                        return;
                }
                callback(*value->get(), document, event);
            });
    }
    auto animations_active() const -> bool { return !animations_.is_empty(); }
    auto animation_sample_time() const -> Option<Serial> { return animation_time_; }
    auto pending_animation_callbacks() const -> bool { return !animation_completions_.is_empty(); }
    auto poll_animation_events() -> Vec<AnimationEvent> {
        return rstd::exchange(animation_events_, Vec<AnimationEvent>{});
    }
    auto sample_animations(Serial time) -> Result<empty, AnimationError> {
        if (!can_route())
            return Err(AnimationError::Reentrant);
        if ((layout_active_ || painting_))
            return Err(AnimationError::Reentrant);
        if (animations_closed_)
            return Ok(empty{});
        if (animation_time_.is_some() && time < *animation_time_)
            time = *animation_time_;
        animation_time_ = Some(time);
        for (usize i{}; i < animations_.len();) {
            auto &track = animations_[i];
            if (track.state.started_ns.is_none())
                track.state.started_ns = Some(time);
            auto elapsed = time - *track.state.started_ns;
            auto duration = track.state.options.duration_ns;
            bool finished = elapsed >= duration;
            Scalar t = finished ? 1 : Scalar(elapsed) / Scalar(duration);
            if (track.state.options.easing == AnimationEasing::EaseInOut)
                t = t * t * (3 - 2 * t);
            track.state.value = track.from.interpolate(track.state.target_value, t).unwrap();
            if (!finished || track.state.options.end == AnimationEndPolicy::CommitFinal)
                apply_overlay(track.state.target, *resolve(track.state.target),
                              track.state.property, &track.state.value);
            if (finished)
                end_animation(i, AnimationEndReason::Finished);
            else
                ++i;
        }
        return Ok(empty{});
    }
    auto dispatch_animation_callbacks() -> Result<empty, AnimationError> {
        if (routing_ || draining_ || animation_dispatching_ || (layout_active_ || painting_))
            return Err(AnimationError::Reentrant);
        animation_dispatching_ = true;
        auto pending = rstd::exchange(animation_completions_, Vec<AnimationCompletion>{});
        dispatching_animation_count_ = pending.len();
        for (auto &completion : pending) {
            --dispatching_animation_count_;
            animation_events_.push(AnimationEvent(completion.event));
            if (completion.callback.is_some())
                (*completion.callback)->invoke(*this, completion.event);
        }
        animation_dispatching_ = false;
        return Ok(empty{});
    }
    auto close_animations() -> Result<empty, AnimationError> {
        if (!can_route())
            return Err(AnimationError::Reentrant);
        animations_closed_ = true;
        while (!animations_.is_empty())
            end_animation(usize(), AnimationEndReason::Cancelled);
        return dispatch_animation_callbacks();
    }
    auto set_scene_source(UiId id, Option<ContentSource> source) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->scene.is_none() || (source.is_some() && !source->valid()))
            return Err(UiError::InvalidValue);
        auto &scene = **node->scene;
        if (scene.source.is_some() == source.is_some() &&
            (source.is_none() || scene.source->id() == source->id()))
            return Ok(empty{});
        scene.source = rstd::move(source);
        node->paint_dirty = true;
        changed(false);
        return Ok(empty{});
    }
    auto set_scene_presentation(UiId id, ImageFit fit, ImageSampling sampling,
                                Color placeholder = {0.15, 0.15, 0.15, 1})
        -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->scene.is_none() || !placeholder.valid() ||
            (fit != ImageFit::Fill && fit != ImageFit::Contain && fit != ImageFit::Cover) ||
            (sampling != ImageSampling::Nearest && sampling != ImageSampling::Linear))
            return Err(UiError::InvalidValue);
        auto &scene = **node->scene;
        if (scene.fit != fit || scene.sampling != sampling || scene.placeholder != placeholder) {
            scene.fit = fit;
            scene.sampling = sampling;
            scene.placeholder = placeholder;
            node->paint_dirty = true;
            changed(false);
        }
        return Ok(empty{});
    }
    auto scene_placement(UiId id) -> Result<Option<ScenePlacement>, UiError> {
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->scene.is_none())
            return Err(UiError::InvalidValue);
        if (layout_dirty_ || !mounted(id))
            return Err(UiError::LayoutPending);
        const auto &scene = **node->scene;
        auto frame = scene.source.is_some() ? scene.source->latest() : Option<TextureFrame>{};
        if (frame.is_none())
            return Ok(Option<ScenePlacement>{});
        auto info = frame->info();
        auto padding = node->style.layout.padding;
        auto placed = place_image(
            Size{Scalar(info.width), Scalar(info.height)},
            {padding, padding, limit(node->measured.width - 2 * padding, node->measured.width),
             limit(node->measured.height - 2 * padding, node->measured.height)},
            scene.fit);
        if (placed.is_err())
            return Err(UiError::InvalidValue);
        return Ok(Some(ScenePlacement{info, placed.unwrap()}));
    }
    auto set_image(UiId id, Option<ImageData> data) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->image.is_none())
            return Err(UiError::InvalidValue);
        if (value_occupied(*node, UiValueProperty::Image))
            return Err(UiError::PropertyOwned);
        write_image(id, *node, rstd::move(data));
        return Ok(empty{});
    }
    auto set_image_tint(UiId id, Color tint) -> Result<empty, UiError> {
        if (layout_active_ || painting_)
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->image.is_none() || !tint.valid())
            return Err(UiError::InvalidValue);
        if ((*node->image)->tint != tint) {
            (*node->image)->tint = tint;
            node->paint_dirty = true;
            changed(false, Some(id));
        }
        return Ok(empty{});
    }
    auto set_image_fit(UiId id, ImageFit fit) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->image.is_none() ||
            (fit != ImageFit::Fill && fit != ImageFit::Contain && fit != ImageFit::Cover))
            return Err(UiError::InvalidValue);
        if ((*node->image)->fit != fit) {
            (*node->image)->fit = fit;
            node->paint_dirty = true;
            changed(false);
        }
        return Ok(empty{});
    }
    auto set_image_sampling(UiId id, ImageSampling sampling) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->image.is_none() ||
            (sampling != ImageSampling::Nearest && sampling != ImageSampling::Linear))
            return Err(UiError::InvalidValue);
        if ((*node->image)->sampling != sampling) {
            (*node->image)->sampling = sampling;
            node->paint_dirty = true;
            changed(false);
        }
        return Ok(empty{});
    }
    auto image_placement(UiId id) -> Result<Option<ImagePlacement>, UiError> {
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->image.is_none())
            return Err(UiError::InvalidValue);
        if (layout_dirty_ || !mounted(id))
            return Err(UiError::LayoutPending);
        return Ok(place_image_content(*node));
    }
    auto set_text_service(Box<TextService> service) -> Result<empty, UiError> {
        if (routing_ || (layout_active_ || painting_))
            return Err(UiError::Routing);
        text_service_ = Some(rstd::move(service));
        return text_fonts_changed();
    }
    auto text_fonts_changed() -> Result<empty, UiError> {
        if (routing_ || (layout_active_ || painting_))
            return Err(UiError::Routing);
        for (usize index{}; index < slots_.len(); ++index) {
            auto &slot = slots_[index];
            if (slot.node.is_some() && slot.node->text.is_some()) {
                (*slot.node->text)->dirty = true;
                dirty_measure(
                    {session_, static_cast<Index>(index.to_primitive()), slot.generation});
            }
        }
        layout_dirty_ = true;
        changed(false);
        return Ok(empty{});
    }
    auto set_text(UiId id, ref<str> value) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->text.is_none())
            return Err(UiError::InvalidValue);
        if (value_occupied(*node, UiValueProperty::Text))
            return Err(UiError::PropertyOwned);
        if ((**node->text).value.as_str() != value)
            write_text(id, *node, String::make(value));
        return Ok(empty{});
    }
    auto set_text(UiId id, const TextBoundaries &value) -> Result<empty, UiError> {
        if (layout_active_ || painting_) return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node) return Err(UiError::InvalidId);
        if (node->text.is_none() || !value.present()) return Err(UiError::InvalidValue);
        if (value_occupied(*node, UiValueProperty::Text)) return Err(UiError::PropertyOwned);
        if ((**node->text).boundaries != value)
            write_text(id, *node, String::make(value.text()), TextBoundaries(value));
        return Ok(empty{});
    }
    auto set_text_format(UiId id, TextFormat format) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->text.is_none() || !format.valid())
            return Err(UiError::InvalidValue);
        auto &text = **node->text;
        if (text.family.as_str() != format.family || text.language.as_str() != format.language ||
            text.size != format.size || text.direction != format.direction ||
            text.wrap != format.wrap || text.bold != format.bold || text.italic != format.italic ||
            text.alignment != format.alignment || text.line_spacing != format.line_spacing) {
            text.family = String::make(format.family);
            text.language = String::make(format.language);
            text.size = format.size;
            text.direction = format.direction;
            text.wrap = format.wrap;
            text.bold = format.bold;
            text.italic = format.italic;
            text.alignment = format.alignment;
            text.line_spacing = format.line_spacing;
            text.dirty = true;
            changed(true, Some(id));
        }
        return Ok(empty{});
    }
    auto set_text_effects(UiId id, TextEffects effects) -> Result<empty, UiError> {
        if (layout_active_ || painting_) return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node) return Err(UiError::InvalidId);
        if (node->text.is_none() || !effects.valid()) return Err(UiError::InvalidValue);
        if ((*node->text)->effects != effects) {
            (*node->text)->effects = effects;
            node->paint_dirty = true;
            changed(false);
        }
        return Ok(empty{});
    }
    auto set_text_color(UiId id, Color color) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->text.is_none() || !color.valid())
            return Err(UiError::InvalidValue);
        if ((*node->text)->color != color) {
            (*node->text)->color = color;
            node->paint_dirty = true;
            changed(false);
        }
        return Ok(empty{});
    }
    auto set_text_colors(UiId id, TextColors colors) -> Result<empty, UiError> {
        if (layout_active_ || painting_) return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node) return Err(UiError::InvalidId);
        if (node->text.is_none() || !colors.matches((*node->text)->value.as_str())) return Err(UiError::InvalidValue);
        if ((*node->text)->colors != colors) {
            (*node->text)->colors = rstd::move(colors);
            node->paint_dirty = true;
            changed(false);
        }
        return Ok(empty{});
    }
    auto set_text_colors(UiId id, slice<TextColorSpan> spans) -> Result<empty, UiError> {
        if (layout_active_ || painting_) return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node) return Err(UiError::InvalidId);
        if (node->text.is_none()) return Err(UiError::InvalidValue);
        auto colors = TextColors::create((*node->text)->value.as_str(), spans);
        if (colors.is_err()) return Err(UiError::InvalidValue);
        return set_text_colors(id, rstd::move(colors).unwrap());
    }
    auto set_text_decorations(UiId id, TextDecorations decorations) -> Result<empty, UiError> {
        if (layout_active_ || painting_) return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node) return Err(UiError::InvalidId);
        if (node->text.is_none() || !decorations.matches((*node->text)->value.as_str())) return Err(UiError::InvalidValue);
        if ((*node->text)->decorations != decorations) {
            (*node->text)->decorations = rstd::move(decorations);
            node->paint_dirty = true;
            changed(false);
        }
        return Ok(empty{});
    }
    auto set_text_decorations(UiId id, slice<TextDecorationSpan> spans) -> Result<empty, UiError> {
        if (layout_active_ || painting_) return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node) return Err(UiError::InvalidId);
        if (node->text.is_none()) return Err(UiError::InvalidValue);
        auto decorations = TextDecorations::create((*node->text)->value.as_str(), spans);
        if (decorations.is_err()) return Err(UiError::InvalidValue);
        return set_text_decorations(id, rstd::move(decorations).unwrap());
    }
    auto text_layout(UiId id) -> Result<TextLayout, UiError> {
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->text.is_none())
            return Err(UiError::InvalidValue);
        auto &text = **node->text;
        if (layout_dirty_ || text.dirty || text.layout.is_none() || !mounted(id))
            return Err(UiError::LayoutPending);
        return Ok(TextLayout(*text.layout));
    }
    auto text_placement(UiId id) -> Result<TextPlacement, UiError> {
        auto result = text_layout(id);
        if (result.is_err())
            return Err(result.unwrap_err());
        auto layout = rstd::move(result).unwrap();
        return Ok(TextPlacement{rstd::move(layout), text_origin(*resolve(id))});
    }
    auto text_error(UiId id) -> Result<Option<TextError>, UiError> {
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->text.is_none())
            return Err(UiError::InvalidValue);
        return Ok(Option<TextError>((*node->text)->error));
    }
    template <class T>
        requires rstd::Impled<T, Layout>
    auto install_layout(UiId id, T strategy) -> Result<empty, UiError> {
        if (layout_active_ || painting_ || routing_)
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        node->layout_strategy = Some(Box<rstd::dyn<Layout>>::make(rstd::move(strategy)));
        node->layout_binding = None();
        changed(true, Some(id));
        return Ok(empty{});
    }
    auto set_layout_strategy(UiId id, Box<LayoutStrategy> strategy) -> Result<empty, UiError> {
        struct Adapter {
            Box<LayoutStrategy> strategy;
            auto measure(LayoutMeasureContext &context, LayoutConstraints c)
                -> Result<Size, UiError> {
                return strategy->measure(context, c);
            }
            auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
                return strategy->arrange(context, size);
            }
        };
        return install_layout(id, Adapter{rstd::move(strategy)});
    }
    template <class T>
        requires rstd::Impled<T, Layout>
    auto attach_layout(UiId id) -> Result<empty, UiError> {
        if (!state<T>(id))
            return Err(UiError::InvalidId);
        struct Adapter {
            UiDocument *document;
            UiId id;
            auto measure(LayoutMeasureContext &context, LayoutConstraints c)
                -> Result<Size, UiError> {
                auto receiver = document->state<T>(id);
                if (!receiver)
                    return Err(UiError::Cancelled);
                return rstd::as<Layout>(*receiver).measure(context, c);
            }
            auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
                auto receiver = document->state<T>(id);
                if (!receiver)
                    return Err(UiError::Cancelled);
                return rstd::as<Layout>(*receiver).arrange(context, size);
            }
        };
        return install_layout(id, Adapter{this, id});
    }
    template <class T>
        requires rstd::Impled<T, Layout>
    auto bind_layout(UiId id, rstd::rc::Weak<T> receiver) -> Result<Binding, UiError> {
        auto binding = bindings_.make(receiver);
        struct Adapter {
            rstd::rc::Weak<T> receiver;
            BindingGuard guard;
            auto measure(LayoutMeasureContext &context, LayoutConstraints c)
                -> Result<Size, UiError> {
                auto value = receiver.upgrade();
                if (!guard.alive() || value.is_none())
                    return Err(UiError::Cancelled);
                return rstd::as<Layout>(*value->get()).measure(context, c);
            }
            auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
                auto value = receiver.upgrade();
                if (!guard.alive() || value.is_none())
                    return Err(UiError::Cancelled);
                return rstd::as<Layout>(*value->get()).arrange(context, size);
            }
        };
        auto result = install_layout(id, Adapter{rstd::move(receiver), binding.guard()});
        if (result.is_err())
            return Err(result.unwrap_err());
        resolve(id)->layout_binding = Some(binding.guard());
        track_binding(id);
        return Ok(rstd::move(binding));
    }
    template <class T>
        requires rstd::Impled<T, Paint>
    auto install_paint(UiId id, T painter) -> Result<empty, UiError> {
        if (layout_active_ || painting_ || routing_)
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        node->painter = Some(Box<rstd::dyn<Paint>>::make(rstd::move(painter)));
        node->paint_binding = None();
        return invalidate_paint(id);
    }
    template <class T>
        requires rstd::Impled<T, Paint>
    auto attach_paint(UiId id) -> Result<empty, UiError> {
        if (!state<T>(id))
            return Err(UiError::InvalidId);
        struct Adapter {
            UiDocument *document;
            UiId id;
            auto paint(PaintContext &context) -> Result<empty, UiError> {
                auto receiver = document->state<T>(id);
                if (!receiver)
                    return Err(UiError::Cancelled);
                return rstd::as<Paint>(*receiver).paint(context);
            }
        };
        return install_paint(id, Adapter{this, id});
    }
    template <class T>
        requires rstd::Impled<T, Paint>
    auto bind_paint(UiId id, rstd::rc::Weak<T> receiver) -> Result<Binding, UiError> {
        auto binding = bindings_.make(receiver);
        struct Adapter {
            rstd::rc::Weak<T> receiver;
            BindingGuard guard;
            auto paint(PaintContext &context) -> Result<empty, UiError> {
                auto value = receiver.upgrade();
                if (!guard.alive() || value.is_none())
                    return Err(UiError::Cancelled);
                return rstd::as<Paint>(*value->get()).paint(context);
            }
        };
        auto result = install_paint(id, Adapter{rstd::move(receiver), binding.guard()});
        if (result.is_err())
            return Err(result.unwrap_err());
        resolve(id)->paint_binding = Some(binding.guard());
        track_binding(id);
        return Ok(rstd::move(binding));
    }
    auto invalidate_layout(UiId id) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        if (!resolve(id))
            return Err(UiError::InvalidId);
        changed(true, Some(id));
        return Ok(empty{});
    }
    auto layout_diagnostics() const -> slice<LayoutDiagnostic> {
        return layout_diagnostics_.as_slice();
    }
    auto set_layout(UiId id, LayoutStyle value) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if ((property_occupied(*node, PresentationProperty::Width) &&
             value.width != node->style.layout.width) ||
            (property_occupied(*node, PresentationProperty::Height) &&
             value.height != node->style.layout.height))
            return Err(UiError::PropertyOwned);
        if (!value.valid())
            return Err(UiError::InvalidValue);
        auto before = effective_style(*node);
        if (node->style.layout.padding != value.padding)
            node->paint_dirty = true;
        node->style.layout = value;
        auto after = effective_style(*node).layout;
        if (before.layout != after) {
            auto dimensions = before.layout;
            dimensions.horizontal = after.horizontal;
            dimensions.vertical = after.vertical;
            if (dimensions == after && before.layout.horizontal != Alignment::Stretch &&
                after.horizontal != Alignment::Stretch &&
                before.layout.vertical != Alignment::Stretch &&
                after.vertical != Alignment::Stretch) {
                dirty_arrange(id);
                layout_dirty_ = true;
                changed(false);
            } else
                changed(true, Some(id));
        }
        return Ok(empty{});
    }
    auto set_background(UiId id, Color value, Scalar radius = 0) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (property_occupied(*node, PresentationProperty::Background))
            return Err(UiError::PropertyOwned);
        if (!value.valid() || !finite(radius) || radius < 0)
            return Err(UiError::InvalidValue);
        auto before = effective_style(*node);
        node->style.background = value;
        node->style.radius = radius;
        auto after = effective_style(*node);
        if (before.background != after.background || before.radius != after.radius) {
            node->paint_dirty = true;
            changed(false, Some(id));
        }
        return Ok(empty{});
    }
    auto set_transform(UiId id, Transform value) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (property_occupied(*node, PresentationProperty::Transform))
            return Err(UiError::PropertyOwned);
        if (!value.valid())
            return Err(UiError::InvalidValue);
        auto before = effective_style(*node).transform;
        node->style.transform = value;
        if (before != effective_style(*node).transform)
            changed(false, Some(id));
        return Ok(empty{});
    }
    auto set_opacity(UiId id, Scalar value) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (property_occupied(*node, PresentationProperty::Opacity))
            return Err(UiError::PropertyOwned);
        if (!finite(value) || value < 0 || value > 1)
            return Err(UiError::InvalidValue);
        auto before = effective_style(*node).opacity;
        node->style.opacity = value;
        if (before != effective_style(*node).opacity)
            changed(false, Some(id));
        return Ok(empty{});
    }
    auto set_clip(UiId id, Clip value) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (property_occupied(*node, PresentationProperty::Clip))
            return Err(UiError::PropertyOwned);
        if (!value.valid() || (node->viewport.is_some() && value.enabled))
            return Err(UiError::InvalidValue);
        if (node->style.clip != value) {
            node->style.clip = value;
            changed(false);
        }
        return Ok(empty{});
    }
    auto set_visible(UiId id, bool value) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (value_occupied(*node, UiValueProperty::Visible))
            return Err(UiError::PropertyOwned);
        write_visible(id, *node, value);
        return Ok(empty{});
    }
    auto set_hit_self(UiId id, bool value) -> Result<empty, UiError> {
        if (layout_active_ || painting_)
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->style.hit_self != value) {
            node->style.hit_self = value;
            changed(false, Some(id));
        }
        return Ok(empty{});
    }
    auto set_text_input_area(UiId id, Option<Rect> area) -> Result<empty, UiError> {
        if (layout_active_ || painting_) return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node) return Err(UiError::InvalidId);
        if (area.is_some() && !area->valid()) return Err(UiError::InvalidValue);
        if (node->style.text_input_area != area) {
            node->style.text_input_area = area;
            changed(false, Some(id));
        }
        return Ok(empty{});
    }
    auto set_interaction(UiId id, bool hit_test_visible, bool enabled, bool focusable)
        -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->style.hit_test_visible != hit_test_visible || node->style.enabled != enabled ||
            node->style.focusable != focusable) {
            node->style.hit_test_visible = hit_test_visible;
            node->style.enabled = enabled;
            node->style.focusable = focusable;
            changed(false, Some(id));
        }
        return Ok(empty{});
    }
    auto set_viewport_axes(UiId id, ScrollAxes axes) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->viewport.is_none())
            return Err(UiError::InvalidValue);
        auto &view = **node->viewport;
        if (view.axes != axes) {
            view.axes = axes;
            changed(true, Some(id));
        }
        return Ok(empty{});
    }
    auto set_viewport_offset(UiId id, Point offset) -> Result<empty, UiError> {
        if ((layout_active_ || painting_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (property_occupied(*node, PresentationProperty::ViewportOffset))
            return Err(UiError::PropertyOwned);
        if (node->viewport.is_none() || !finite(offset.x) || !finite(offset.y))
            return Err(UiError::InvalidValue);
        auto &view = **node->viewport;
        if (!layout_dirty_ && mounted(id))
            offset = {limit(offset.x, view.metrics.maximum.x),
                      limit(offset.y, view.metrics.maximum.y)};
        if (view.requested != offset) {
            view.requested = offset;
            if (!layout_dirty_ && mounted(id))
                clamp_viewport(view);
            else {
                dirty_arrange(id);
                layout_dirty_ = true;
            }
            changed(false, Some(id));
        }
        return Ok(empty{});
    }
    auto viewport_metrics(UiId id) -> Result<ViewportMetrics, UiError> {
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->viewport.is_none())
            return Err(UiError::InvalidValue);
        if (layout_dirty_ || !mounted(id))
            return Err(UiError::LayoutPending);
        return Ok(ViewportMetrics((*node->viewport)->metrics));
    }
    // Observers receive a completed layout snapshot; structural changes must be deferred.
    auto on_viewport_changed(UiId id, auto callback) -> Result<Binding, UiError> {
        auto node = resolve(id); if (!node) return Err(UiError::InvalidId);
        if (node->viewport.is_none()) return Err(UiError::InvalidValue);
        auto &view = **node->viewport;
        if (view.observer.is_none()) {
            view.observer = Some(rstd::rc::make_rc<ViewportObserver>()); observed_viewports_.push(UiId(id));
        }
        auto observer = *view.observer;
        auto connected = observer.get()->changed.connect(bindings_, rstd::move(callback));
        if (connected.is_err()) return Err(UiError::Limit);
        auto binding = rstd::move(connected).unwrap(); observer.get()->subscribers.push(binding.guard());
        observer.get()->published = None(); pending_viewports_ = true; bindings_.wake();
        return Ok(rstd::move(binding));
    }
    auto pending_viewport_callbacks() const -> bool { return pending_viewports_; }
    template <class Snapshot> auto dispatch_viewport_callbacks(const Snapshot &snapshot) -> Result<empty, UiError> {
        auto scope = acquire_route(); if (scope.is_err()) return Err(scope.unwrap_err());
        if (snapshot.document() != session_) return Err(UiError::InvalidValue);
        pending_viewports_ = false;
        auto observed = Vec<UiId>::from(observed_viewports_.as_slice());
        for (auto id : observed) {
            auto node = resolve(id); if (!node || node->viewport.is_none() || (*node->viewport)->observer.is_none()) continue;
            auto observer = *(*node->viewport)->observer;
            observer.get()->subscribers.retain([](const auto &guard) { return guard.alive(); });
            if (observer->subscribers.is_empty()) { observer.get()->changed.close(); (*node->viewport)->observer = None(); continue; }
            auto visual = snapshot.get(id);
            if (!visual || visual->viewport.is_none()) { observer.get()->published = None(); continue; }
            auto metrics = *visual->viewport;
            if (observer->published == Some(metrics) && observer->mount == visual->mount_generation) continue;
            observer.get()->published = Some(metrics); observer.get()->mount = visual->mount_generation;
            auto emitted = observer.get()->changed.emit(metrics); if (emitted.is_err()) return Err(UiError::Routing);
        }
        observed_viewports_.retain([this](UiId id) { auto node = resolve(id); return node && node->viewport.is_some() && (*node->viewport)->observer.is_some(); });
        return Ok(empty{});
    }
    auto viewport_content(UiId id) -> Result<Option<UiId>, UiError> {
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node->viewport.is_none())
            return Err(UiError::InvalidValue);
        return Ok(node->children.is_empty() ? None<UiId>() : Some(UiId(node->children[usize()])));
    }
    template <class Snapshot>
    auto bring_into_view(const Snapshot &update, UiId target, Option<Rect> region = {})
        -> Result<BringIntoViewResult, UiError> {
        if (!can_route())
            return Err(UiError::Routing);
        if (update.document() != session_ || !valid(target))
            return Err(UiError::InvalidId);
        if (layout_dirty_ || update.revision() != revision_)
            return Err(UiError::LayoutPending);
        auto item = update.get(target);
        if (!item || !mounted(target))
            return Err(UiError::InvalidId);
        auto rect = region.is_some() ? *region : item->layout_bounds;
        if (!rect.valid())
            return Err(UiError::InvalidValue);
        Vec<UiId> path;
        auto cursor = Some(target);
        while (cursor.is_some()) {
            if (path.len() >= usize(depth_limit_))
                return Err(UiError::Limit);
            auto node = update.get(*cursor);
            if (!node)
                return Err(UiError::InvalidId);
            path.push(UiId(*cursor));
            cursor = node->parent;
        }
        if (path.is_empty() || Some(path[path.len() - usize(1)]) != root_)
            return Err(UiError::InvalidId);
        for (auto id : path) {
            const auto &node = *update.get(id);
            if (!node.properties.visible)
                return Ok(BringIntoViewResult{RevealStatus::Hidden});
            if (node.properties.transform.inverse().is_none())
                return Ok(BringIntoViewResult{RevealStatus::NonInvertible});
            if (node.viewport.is_some() &&
                (node.viewport->viewport.width == 0 || node.viewport->viewport.height == 0))
                return Ok(BringIntoViewResult{RevealStatus::Partial});
        }
        struct OffsetChange {
            UiId id;
            Point offset;
        };
        Vec<OffsetChange> changes;
        BringIntoViewResult result;
        auto nearest = [](Scalar start, Scalar size, Scalar low, Scalar extent) -> Scalar {
            auto end = start + size, high = low + extent;
            if (start < low && end > high)
                return 0;
            if (start < low)
                return size <= extent ? start - low : end - high;
            if (end > high)
                return size <= extent ? end - high : start - low;
            return 0;
        };
        for (auto id : path) {
            const auto &node = *update.get(id);
            const auto &clip = node.properties.clip;
            if (node.viewport.is_some() && id != target) {
                const auto &metrics = *node.viewport;
                Point requested{metrics.offset.x + nearest(rect.x, rect.width, clip.rect.x,
                                                           metrics.viewport.width),
                                metrics.offset.y + nearest(rect.y, rect.height, clip.rect.y,
                                                           metrics.viewport.height)};
                if (!finite(requested.x) || !finite(requested.y))
                    return Err(UiError::InvalidValue);
                Point offset{limit(requested.x, metrics.maximum.x),
                             limit(requested.y, metrics.maximum.y)};
                rect.x -= offset.x - metrics.offset.x;
                rect.y -= offset.y - metrics.offset.y;
                if (!rect.valid())
                    return Err(UiError::InvalidValue);
                if (offset != metrics.offset)
                    changes.push(OffsetChange{id, offset});
            }
            Clip clips[]{node.properties.clip, node.properties.layout_clip};
            for (const auto &clip : clips)
                if (clip.enabled) {
                    auto right = rect.x + rect.width, bottom = rect.y + rect.height;
                    auto clip_right = clip.rect.x + clip.rect.width,
                         clip_bottom = clip.rect.y + clip.rect.height;
                    if (rect.x < clip.rect.x || rect.y < clip.rect.y || right > clip_right ||
                        bottom > clip_bottom)
                        result.status = RevealStatus::Partial;
                    auto left = maximum(rect.x, clip.rect.x), top = maximum(rect.y, clip.rect.y);
                    right = right < clip_right ? right : clip_right;
                    bottom = bottom < clip_bottom ? bottom : clip_bottom;
                    if (right < left || bottom < top) {
                        result.status = RevealStatus::Partial;
                        break;
                    }
                    rect = {left, top, right - left, bottom - top};
                }
            if (node.parent.is_some()) {
                auto matrix = node.properties.transform;
                Point corners[]{matrix.map({rect.x, rect.y}),
                                matrix.map({rect.x + rect.width, rect.y}),
                                matrix.map({rect.x, rect.y + rect.height}),
                                matrix.map({rect.x + rect.width, rect.y + rect.height})};
                auto left = corners[0].x, right = left, top = corners[0].y, bottom = top;
                for (auto corner : corners) {
                    if (!finite(corner.x) || !finite(corner.y))
                        return Err(UiError::InvalidValue);
                    left = corner.x < left ? corner.x : left;
                    right = maximum(right, corner.x);
                    top = corner.y < top ? corner.y : top;
                    bottom = maximum(bottom, corner.y);
                }
                rect = {left, top, right - left, bottom - top};
                if (!rect.valid())
                    return Err(UiError::InvalidValue);
            }
        }
        for (const auto &change : changes) {
            auto &view = **resolve(change.id)->viewport;
            view.requested = change.offset;
            view.metrics.offset = change.offset;
            ++result.scrolled_viewports;
            changed(false, Some(UiId(change.id)));
        }
        return Ok(result);
    }

  private:
    auto update_impl(Size viewport, bool recording = true) -> Result<UiUpdate::Data, UiError> {
        if (routing_ || animation_dispatching_ || (layout_active_ || painting_))
            return Err(UiError::Routing);
        if (!viewport.valid())
            return Err(UiError::InvalidValue);
        if (viewport != viewport_) {
            viewport_ = viewport;
            changed(true, root_);
        }
        paint_diagnostics_ = {};
        sweep_bindings();
        UiUpdate::Data output{session_, revision_, root_, {}, {}, {}, layout_revision_, {}};
        output.indices.reserve(slots_.len());
        for (usize i{}; i < slots_.len(); ++i)
            output.indices.push(~Index{});
        if (root_.is_some()) {
            if (layout_dirty_) {
                CpuTimer layout(active_timings_ ? &active_timings_->layout : nullptr);
                layout_active_ = true;
                struct LayoutScope {
                    bool &active;
                    ~LayoutScope() { active = false; }
                } scope{layout_active_};
                layout_diagnostics_ = {};
                layout_work_ = 0;
                for (Index pass{}; pass < 16; ++pass) {
                    ++stats_.layout_passes;
                    layout_again_ = false;
                    layout_requester_ = None();
                    auto error = measure(*root_, {Some(viewport.width), Some(viewport.height)}, 0);
                    if (error.is_none())
                        error = arrange(*root_, {0, 0, viewport.width, viewport.height}, 0);
                    apply_layout_requests();
                    if (error.is_some())
                        return Err(*error);
                    if (!layout_again_) {
                        layout_dirty_ = false;
                        break;
                    }
                }
                if (layout_dirty_) {
                    layout_failure(layout_requester_.is_some() ? *layout_requester_ : *root_,
                                   LayoutIssue::Limit, UiError::Limit);
                    return Err(UiError::Limit);
                }
                if (layout_revision_ == ~Serial{})
                    rstd::panic("vui layout revision exhausted");
                ++layout_revision_;
            }
            layout_diagnostics_ = {};
            CpuTimer collect_time(active_timings_ ? &active_timings_->collect : nullptr);
            auto error = collect(*root_, output, 0, recording);
            if (error.is_some())
                return Err(*error);
        }
        if (root_.is_none() && layout_dirty_) {
            if (layout_revision_ == ~Serial{})
                rstd::panic("vui layout revision exhausted");
            ++layout_revision_;
            layout_dirty_ = false;
            layout_diagnostics_ = {};
        }
        output.layout_revision = layout_revision_;
        output.stats = stats_;
        return Ok(rstd::move(output));
    }

  public:
    auto input_stats() const -> UpdateStats { return input_stats_; }
    auto input_snapshot(Size viewport, DiagnosticsOptions options = {})
        -> Result<InputSnapshot, UiError> {
        if (updating_ || !can_route())
            return Err(UiError::Routing);
        input_stats_ = {};
        sweep_bindings();
        if (layout_dirty_ || viewport != viewport_ || input_base_.is_none()) {
            auto base = update(viewport, options, false);
            if (base.is_err())
                return Err(base.unwrap_err());
            auto frozen = rstd::move(base).unwrap();
            input_stats_ = frozen.stats();
            return Ok(InputSnapshot(rstd::move(frozen)));
        }
        Vec<UiVisual> changes;
        last_timings_ = None();
        for (auto id : input_changes_) {
            auto node = resolve(id);
            if (!node || !mounted(id))
                continue;
            auto visual = make_visual(id);
            if (visual.is_err())
                return Err(visual.unwrap_err());
            changes.push(rstd::move(visual).unwrap());
            ++input_stats_.geometry_overrides;
        }
        return Ok(InputSnapshot(UiUpdate(*input_base_), rstd::move(changes), revision_));
    }
    auto last_update_timings() const -> Option<UpdateTimings> { return last_timings_; }
    auto update(Size viewport, DiagnosticsOptions options = {}, bool recording = true)
        -> Result<UiUpdate, UiError> {
        if (updating_)
            return Err(UiError::Routing);
        updating_ = true;
        stats_ = {};
        UpdateTimings sample;
        active_timings_ = options.timings ? &sample : nullptr;
        CpuTimer timer(active_timings_ ? &sample.update : nullptr);
        auto result = update_impl(viewport, recording);
        timer.pause();
        active_timings_ = nullptr;
        updating_ = false;
        last_timings_ = None();
        if (options.timings) {
            sample.document = session_;
            sample.revision = revision_;
            sample.layout_revision = layout_revision_;
            sample.work = stats_;
            if (result.is_err())
                sample.error = Some(result.unwrap_err());
            last_timings_ = Some(sample);
        }
        if (result.is_err())
            return Err(result.unwrap_err());
        auto output = result.unwrap();
        output.timings = last_timings_;
        auto snapshot = UiUpdate(rstd::move(output));
        input_base_ = Some(UiUpdate(snapshot));
        input_changes_.clear();
        return Ok(rstd::move(snapshot));
    }
};
} // namespace vui
