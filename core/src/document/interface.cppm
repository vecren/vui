export module vui:document.interface;
import :scene.composition;
import :node.types;
import :node.ownership;
import :node.state;
import :node.style_patch;
import :animation.types;
import :layout.types;
import :diagnostics;
import :binding.scope;
import :binding.signal;
import :layout.protocol;
import :paint.protocol;
import :modifier.protocol;
import :motion.properties;
import :state.targets;
import :document.edit;
import :document.snapshot;
import :document.layout;
import :document.lifecycle;
import :document.text;
import :document.content;
import :document.animation;
import :document.tree;
import :document.phase;
import :document.subcomposition;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui {
class UiDocument {
    friend class UiSession;
    using GeometryObserver = DocumentGeometryObserver;
    using ViewportObserver = DocumentViewportObserver;
    using ViewportLayout = DocumentViewportLayout;
    using State = UiNodeState;
    template <class T> using StoredState = UiStoredState<T>;
    using Mutation = DocumentMutation;
    template <class F> using Deferred = DocumentDeferred<F>;
    using TextContent = DocumentTextContent;
    static auto format_changed(const TextContent &text, TextFormat format) -> bool;
    static void assign_format(TextContent &text, TextFormat format);
    static auto text_format(const TextContent &text) -> TextFormat;
    static auto merge_format(TextFormat current, TextFormat before, TextFormat value) -> TextFormat;
    using ImageContent = DocumentImageContent;
    using SceneContent = DocumentSceneContent;
    using AnimatedStyle = DocumentAnimatedStyle;
    using AnimationCallback = DocumentAnimationCallback;
    template <class F> using AnimationFunction = DocumentAnimationFunction<F>;
    using RunningAnimation = DocumentRunningAnimation;
    using AnimationCompletion = DocumentAnimationCompletion;
    using Node = DocumentNode;
    using Slot = DocumentSlot;
    Serial session_{new_session()}, revision_{1};
    Vec<Slot> slots_;
    Option<UiUpdate> input_base_;
    Vec<UiId> input_changes_;
    Vec<UiId> bound_nodes_;
    Vec<UiId> subcomposition_nodes_;
    bool subcomposition_dispatching_{};
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
    Vec<rstd::rc::Rc<GeometryObserver>> observed_geometry_;
    bool pending_geometry_{};
    Serial next_lease_{1};
    Vec<PaintDiagnostic> paint_diagnostics_;
    UpdateTimings *active_timings_{};
    Option<UpdateTimings> last_timings_;
    Index depth_limit_{256};
    bool routing_{}, draining_{}, evaluating_{}, editing_{};
    Serial rejected_cleanups_{};
    DocumentPhaseTransaction *phase_transaction_{};
    Vec<UiPhaseDiagnostic> phase_diagnostics_;
    Vec<rstd::rc::Weak<DocumentPhaseObservation>> phase_invalidations_;
    Serial next_mutation_{1};
    Vec<Box<Mutation>> mutations_;

    void notify_targets();
    auto value_occupied(Node &node, UiValueProperty property) -> bool;
    auto write_text(UiId id, Node &node, String value, TextBoundaries boundaries = {}) -> bool;
    auto write_character_spacing(UiId id, Node &node, Scalar value) -> bool;
    auto write_text_color(UiId id, Node &node, Color value) -> bool;
    auto write_visible(UiId id, Node &node, bool value) -> bool;
    auto write_image(UiId id, Node &node, Option<ImageData> data) -> bool;
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

    void sweep_bindings();
    void track_binding(UiId id);
    auto resolve(UiId id) -> Node * {
        if (!id.valid() || id.session != session_ || usize(id.slot) >= slots_.len())
            return nullptr;
        auto &slot = slots_[usize(id.slot)];
        return slot.generation == id.generation && slot.node.is_some() ? &*slot.node : nullptr;
    }
    void dirty_measure(UiId id);
    void dirty_arrange(UiId id);
    auto observe_phase(UiId id, UiPhase phase) -> DocumentPhaseReadScope;
    void schedule_phase(rstd::rc::Rc<DocumentPhaseObservation> observation);
    void apply_phase_invalidations();
    auto publish_phase_reads(DocumentPhaseTransaction &transaction) -> Option<UiError>;
    void discard_phase_reads(DocumentPhaseTransaction &transaction);
    void changed(bool layout, Option<UiId> target = {});
    void request_layout(UiId id);
    void apply_layout_requests();
    static auto effective_style(const Node &node, NodeStyle result) -> NodeStyle {
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
    static auto effective_style(const Node &node) -> NodeStyle { return effective_style(node, node.style); }
    static auto property_index(PresentationProperty property) -> unsigned {
        return static_cast<unsigned>(property);
    }
    auto property_occupied(Node &node, PresentationProperty property) -> bool {
        auto owner = node.property_owner[property_index(property)].upgrade();
        return owner.is_some() && owner->get()->active && owner->get()->owner.alive() &&
               (owner->get()->driver.is_none() || owner->get()->driver->alive());
    }
    auto children_access(UiId id, const UiTreeAccess &access) -> Result<empty, UiError>;
    auto relation_access(UiId id, const UiTreeAccess &access) -> Result<empty, UiError>;
    auto validate_style(Node &node, const NodeStylePatch &patch) -> Result<NodeStyle, UiError>;
    void style_changed(UiId id, Node &node, const NodeStyle &base);
    void revoke_property(Node &node, PresentationProperty property);
    auto read_property(UiId id, Node &node, PresentationProperty property, bool base = false)
        -> PropertyWrite;
    auto valid_property(const PropertyWrite &write, const Node &node) const -> bool;
    void apply_property(Node &node, const PropertyWrite &write);
    auto animation_index(AnimationId id) const -> Option<usize>;
    auto animation_reservations() const -> usize;
    void apply_overlay(UiId id, Node &node, AnimationProperty property,
                       const AnimationValue *value);
    void end_animation(usize index, AnimationEndReason reason, bool clear_overlay = true);
    void detach_animations(UiId id);
    auto start_animation(UiId id, AnimationProperty property, AnimationValue to,
                         AnimationOptions options) -> Result<AnimationId, AnimationError>;
    void remount(UiId id, slice<UiId> preserved = {});
    static auto limit(Scalar value, Scalar maximum) -> Scalar {
        return value < 0 ? 0 : value > maximum ? maximum : value;
    }
    static auto constrain(Scalar value, Option<Scalar> maximum) -> Scalar {
        return maximum.is_some() ? limit(value, *maximum) : value;
    }
    static auto inset(Option<Scalar> value, Scalar padding) -> Option<Scalar> {
        return value.is_some() ? Some(limit(*value - 2 * padding, *value)) : None<Scalar>();
    }
    auto mounted(UiId id) -> bool;
    static auto viewport_offset(const ViewportLayout &view, Point requested) -> Point {
        if (view.metrics.boundary==ViewportBoundary::Clamped)
            return {limit(requested.x,view.metrics.maximum.x),limit(requested.y,view.metrics.maximum.y)};
        return {view.axes.horizontal ? requested.x : 0,view.axes.vertical ? requested.y : 0};
    }
    static void clamp_viewport(ViewportLayout &view) {
        view.metrics.offset = viewport_offset(view,view.requested);
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
        -> Option<UiError>;
    struct MeasureContext;
    struct ArrangeContext;
    struct ModifierMeasure;
    struct ModifierArrange;
    struct ModifierDraw;
    auto measure_content(UiId, LayoutConstraints, Index) -> Result<Size, UiError>;
    auto measure_modifier(UiId, usize, LayoutConstraints, Index) -> Result<Size, UiError>;
    auto arrange_content(UiId, Size, Index, Scalar) -> Option<UiError>;
    auto arrange_modifier(UiId, usize, Size, Transform, Index) -> Option<UiError>;
    auto paint_content(UiId, Node &, DisplayListRecorder &, bool &) -> Option<UiError>;
    auto paint_modifier(UiId, Node &, usize, DisplayListRecorder &, Transform, bool &) -> Option<UiError>;
    auto register_modifier(UiId, NodeStateKey, BindingGuard, Box<ModifierReceiver>) -> Result<empty, UiError>;
    auto install_subcomposition(UiId, rstd::rc::Rc<DocumentSubcomposition>) -> Result<empty, UiError>;
    auto require_subcomposition(UiId, LayoutConstraints) -> Option<UiError>;
    void report_subcomposition_limit();
    void close_subcompositions();
    auto measure(UiId id, LayoutConstraints available, Index depth) -> Option<UiError>;
    static auto aligned_offset(Scalar remaining, Alignment align) -> Scalar {
        return align == Alignment::Center ? remaining / 2 : align == Alignment::End ? remaining : 0;
    }
    auto arrange(UiId id, Rect slot, Index depth, Option<LayoutPlacement> mapped = {})
        -> Option<UiError>;
    static auto text_origin(const Node &node) -> Point {
        const auto &text = **node.text;
        auto bounds = text.layout->logical_bounds();
        // Explicit alignment keeps the request box origin when content overflows it.
        auto aligned = text.width.is_some() && text.alignment != TextAlignment::Natural;
        auto padding = node.modifiers.is_empty() ? node.style.layout.padding : Scalar(0);
        return {padding - (aligned ? 0 : bounds.x), padding - bounds.y};
    }
    static auto place_image_content(const Node &node) -> Option<ImagePlacement>;
    auto paint(UiId id, Node &node) -> Option<UiError>;
    auto make_visual(UiId id) -> Result<UiVisual, UiError>;
    auto collect(UiId id, UiUpdate::Data &output, Index depth, bool recording = true)
        -> Option<UiError>;
    void erase(UiId id);

  public:
    class ReadScope {
        UiDocument *document_;
        friend class UiDocument;
        explicit ReadScope(UiDocument &value) : document_(&value) {}
      public:
        ReadScope(const ReadScope &) = delete;
        ReadScope(ReadScope &&other) : document_(rstd::exchange(other.document_, nullptr)) {}
        ~ReadScope() { if (document_) document_->evaluating_ = false; }
    };
    auto acquire_read() -> Result<ReadScope, UiError>;
    class PreparedEdit {
        struct Entry {
            UiId id;
            LayoutKind kind{LayoutKind::Item};
            Option<Node> fresh;
            Option<NodeStyle> style;
            Option<Box<TextContent>> text;
            Option<Box<ImageContent>> image;
            Vec<UiId> children;
            Option<UiId> parent;
            bool relation_changed{}, children_changed{}, created{}, updated{};
            Vec<UiNodeStateSlot> states;
            Vec<UiNodeStateUpdate> patches;
            struct StateType { NodeStateKey key; rstd::any::TypeId type; };
            Vec<StateType> state_types;
            Option<UiTreePermit> children_owner;
            Option<NodeStyle> previous_style;
            Option<ScrollAxes> axes;
        };
        struct Retirement final : UiEditRetirement { Vec<Entry> entries; };
        Serial session_{}, base_{}, batch_{};
        BindingGuard lifetime_;
        UiTreeAccess access_;
        UiDocument *document_{};
        Vec<Entry> entries_;
        Vec<UiId> retired_;
        Vec<UiId> preserved_;
        usize slots_{};
        Option<Box<Retirement>> retirement_;
        friend class UiDocument;
        PreparedEdit() = default;

      public:
        PreparedEdit(const PreparedEdit &) = delete;
        PreparedEdit(PreparedEdit &&) = default;
        auto operator=(PreparedEdit &&) -> PreparedEdit & = default;
        auto id(UiNodeRef ref) const -> UiId { return ref.batch == batch_ && ref.index < entries_.len() ? entries_[ref.index].id : UiId{}; }
        auto retired() const -> slice<UiId> { return retired_.as_slice(); }
        auto state_type(UiNodeRef ref, NodeStateKey key = {}) const -> Option<rstd::any::TypeId> {
            if (ref.batch == batch_ && ref.index < entries_.len())
                for (const auto &state : entries_[ref.index].state_types) if (state.key == key) return Some(rstd::any::TypeId(state.type));
            return None();
        }
        auto children(UiNodeRef ref) const -> slice<UiId> { return ref.batch == batch_ && ref.index < entries_.len() ? entries_[ref.index].children.as_slice() : slice<UiId>{}; }
        auto viewport_axes(UiId id, ScrollAxes value) -> Result<empty, UiError> {
            for (auto &entry : entries_) if (entry.id == id) {
                if (entry.kind != LayoutKind::Viewport) return Err(UiError::InvalidValue);
                entry.axes = Some(value); return Ok(empty{});
            }
            return Err(UiError::InvalidId);
        }
        template <class T> auto state(UiNodeRef ref, T value, NodeStateKey key = {}) -> Result<empty, UiError> {
            if (!lifetime_.alive()) return Err(UiError::Cancelled);
            if (ref.batch != batch_ || ref.index >= entries_.len()) return Err(UiError::InvalidId);
            auto &entry = entries_[ref.index];
            if (state_type(ref, key).is_some()) return Err(UiError::InvalidValue);
            auto node = entry.fresh.is_some() ? &*entry.fresh : document_->resolve(entry.id);
            if (!node) return Err(UiError::InvalidId);
            node->states.reserve(entry.states.len() + usize(1));
            auto stored = Box<UiStoredState<T>>::make(rstd::move(value));
            entry.states.push({key, Box<UiNodeState>::from_raw(rstd::move(stored).into_raw().template cast<UiNodeState>())});
            entry.state_types.push({key, rstd::any::TypeId::of<T>()}); return Ok(empty{});
        }
    };
    auto node_kind(const UiEditBatch &edit, UiNodeRef ref) -> Result<LayoutKind, UiError>;
    auto prepare(UiEditBatch edit) -> Result<PreparedEdit, UiEditError>;
    auto publish(PreparedEdit edit) -> Result<UiCommitReport, UiError>;
    void notify_edit_targets() { if (!editing_ && !evaluating_) notify_targets(); }
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
    ~UiDocument();
    UiDocument(const UiDocument &) = delete;
    auto operator=(const UiDocument &) -> UiDocument & = delete;
    auto session() const -> Serial { return session_; }
    auto root() const -> Option<UiId> { return root_; }
    auto bindings() -> BindingRegistry & { return bindings_; }
    auto make_tree_owner() -> UiTreeOwner { return UiTreeOwner(session_, bindings_.make()); }
    auto claim_children(UiId id, const UiTreeOwner &owner) -> Result<empty, UiError>;
    auto claim_children(UiId id) -> Result<UiTreeOwner, UiError>;
    auto release_children(UiId id, const UiTreeOwner &owner) -> Result<empty, UiError>;
    auto lifetime() const -> BindingGuard { return lease_owner_.guard(); }
    auto paint_diagnostics() const -> slice<PaintDiagnostic> {
        return paint_diagnostics_.as_slice();
    }
    auto invalidate_paint(UiId id) -> Result<empty, UiError>;
    auto invalidate_placement(UiId id) -> Result<empty, UiError>;
    auto phase_diagnostics() const -> slice<UiPhaseDiagnostic> { return phase_diagnostics_.as_slice(); }
    void clear_phase_observations();
    auto revision() const -> Serial { return revision_; }
    auto routing() const -> bool { return routing_ || evaluating_ || editing_; }
    auto can_route() const -> bool {
        return !routing_ && !draining_ && !animation_dispatching_ && !layout_active_ && !painting_ && !evaluating_ && !editing_;
    }
    auto can_animate() const -> bool { return !layout_active_ && !painting_ && !evaluating_ && !editing_; }
    auto acquire_route() -> Result<RouteScope, UiError>;
    auto pending_mutations() const -> usize { return mutations_.len() + phase_invalidations_.len(); }
    auto cancel_mutations() -> Result<Vec<MutationResult>, UiError>;
    template <class F> auto defer(F callback) -> Result<Serial, UiError> {
        if (layout_active_ || painting_ || evaluating_ || editing_)
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
    auto flush_mutations() -> Result<Vec<MutationResult>, UiError>;
    auto valid(UiId id) const -> bool {
        if (!id.valid() || id.session != session_ || usize(id.slot) >= slots_.len())
            return false;
        const auto &slot = slots_[usize(id.slot)];
        return slot.generation == id.generation && slot.node.is_some();
    }
    template <class T> auto attach_state(UiId id, T value, NodeStateKey key = {}) -> Result<empty, UiError> {
        if (routing_ || (layout_active_ || painting_ || evaluating_ || editing_))
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        if (node_state(node->states, key))
            return Err(UiError::InvalidValue);
        auto state = Box<StoredState<T>>::make(rstd::move(value));
        node->states.push({key, Box<State>::from_raw(rstd::move(state).into_raw().template cast<State>())});
        changed(false, Some(id));
        return Ok(empty{});
    }
    template <class T> auto state(UiId id, NodeStateKey key = {}) -> T * {
        auto node = resolve(id);
        auto state = node ? node_state(node->states, key) : nullptr;
        if (!state || state->type() != rstd::any::TypeId::of<T>())
            return nullptr;
        return &static_cast<StoredState<T> &>(*state).value;
    }
    template <class T> auto create(NodeStyle style = {}) -> Result<UiId, UiError> {
        if (routing_ || (layout_active_ || painting_ || evaluating_ || editing_))
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
    auto parent_relation(UiId child) -> Result<Option<UiRelation>, UiError>;
    auto is_descendant(UiId root, UiId child) -> Result<bool, UiError>;
    auto child_relation(UiId parent, UiId child) -> Result<UiRelation, UiError>;
    auto attach(UiId parent, UiId child, const UiTreeAccess &access = {}) -> Result<empty, UiError>;
    template <class T> auto append(UiId parent, NodeStyle style = {}, const UiTreeAccess &access = {}) -> Result<UiId, UiError> {
        if (!valid(parent))
            return Err(UiError::InvalidId);
        auto allowed = children_access(parent, access); if (allowed.is_err()) return Err(allowed.unwrap_err());
        auto result = create<T>(style);
        if (result.is_err())
            return result;
        auto id = result.unwrap();
        auto attached = attach(parent, id, access);
        if (attached.is_err()) {
            erase(id);
            return Err(attached.unwrap_err());
        }
        return Ok(id);
    }
    auto mount(UiId root) -> Result<empty, UiError>;
    auto detach(UiId id, const UiTreeAccess &access = {}) -> Result<empty, UiError>;
    auto destroy(UiId id, const UiTreeAccess &access = {}) -> Result<empty, UiError>;
    auto reorder(UiId parent, UiId child, usize destination, const UiTreeAccess &access = {}) -> Result<empty, UiError>;
    auto mount_state(UiId id) -> Result<UiMountState, UiError>;
    auto text_target(UiId id) -> Result<UiPropertyTarget<String>, UiError>;
    auto visible_target(UiId id) -> Result<UiPropertyTarget<bool>, UiError>;
    auto image_target(UiId id) -> Result<UiPropertyTarget<Option<ImageData>>, UiError>;
    template <class T>
    auto claim_value_writer(const UiPropertyTarget<T> &target)
        -> Result<UiValueWriter<T>, UiError> {
        if (layout_active_ || painting_ || evaluating_ || editing_)
            return Err(UiError::Routing);
        if (target.document() != session_ || !target.status().valid)
            return Err(UiError::InvalidId);
        auto node = resolve(target.node());
        if (target.state_->property == UiValueProperty::Visible && property_occupied(*node, PresentationProperty::Visible))
            return Err(UiError::PropertyOwned);
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
    auto apply_values(UiValueBatch batch) -> Result<usize, UiValueBatchError>;
    auto take_over(slice<PropertyKey> keys, TakeoverPolicy policy = TakeoverPolicy::PreserveCurrent)
        -> Result<PresentationLease, UiError>;
    auto write_presentation(PresentationLease &lease, const PresentationBatch &batch,
                            Option<BindingGuard> driver = {}) -> Result<empty, UiError>;
    auto set_presentation(const PresentationBatch &batch) -> Result<empty, UiError>;
    auto release_presentation(PresentationLease &lease,
                              LeaseEndPolicy policy = LeaseEndPolicy::CommitCurrent)
        -> Result<empty, UiError>;
    auto base_style(UiId id) -> Result<NodeStyle, UiError>;
    auto resolved_style(UiId id) -> Result<NodeStyle, UiError>;
    auto animate_transform(UiId id, Transform to, AnimationOptions options = {})
        -> Result<AnimationId, AnimationError>;
    auto animate_opacity(UiId id, Scalar to, AnimationOptions options = {})
        -> Result<AnimationId, AnimationError>;
    auto animate_width(UiId id, Scalar to, AnimationOptions options = {})
        -> Result<AnimationId, AnimationError>;
    auto animate_height(UiId id, Scalar to, AnimationOptions options = {})
        -> Result<AnimationId, AnimationError>;
    auto animate_background(UiId id, Color to, AnimationOptions options = {})
        -> Result<AnimationId, AnimationError>;
    auto animation_state(AnimationId id) const -> Result<AnimationState, AnimationError>;
    auto retarget_animation(AnimationId id, AnimationValue to, AnimationOptions options)
        -> Result<empty, AnimationError>;
    auto retarget_animation(AnimationId id, AnimationValue to) -> Result<empty, AnimationError>;
    auto cancel_animation(AnimationId id) -> Result<empty, AnimationError>;
    template <class F>
    auto on_animation_end(AnimationId id, F callback) -> Result<empty, AnimationError> {
        if ((layout_active_ || painting_ || evaluating_ || editing_))
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
    auto poll_animation_events() -> Vec<AnimationEvent>;
    auto sample_animations(Serial time) -> Result<empty, AnimationError>;
    auto dispatch_animation_callbacks() -> Result<empty, AnimationError>;
    auto close_animations() -> Result<empty, AnimationError>;
    auto set_scene_source(UiId id, Option<ContentSource> source) -> Result<empty, UiError>;
    auto set_scene_presentation(UiId id, ImageFit fit, ImageSampling sampling,
                                Color placeholder = {0.15, 0.15, 0.15, 1})
        -> Result<empty, UiError>;
    auto scene_placement(UiId id) -> Result<Option<ScenePlacement>, UiError>;
    auto set_image(UiId id, Option<ImageData> data) -> Result<empty, UiError>;
    auto set_image_tint(UiId id, Color tint) -> Result<empty, UiError>;
    auto set_image_radial_fill(UiId id, ImageRadialFill radial) -> Result<empty, UiError>;
    auto set_image_fit(UiId id, ImageFit fit) -> Result<empty, UiError>;
    auto set_image_sampling(UiId id, ImageSampling sampling) -> Result<empty, UiError>;
    auto image_placement(UiId id) -> Result<Option<ImagePlacement>, UiError>;
    auto set_text_service(Box<TextService> service) -> Result<empty, UiError>;
    auto text_fonts_changed() -> Result<empty, UiError>;
    auto set_text(UiId id, ref<str> value) -> Result<empty, UiError>;
    auto set_text(UiId id, const TextBoundaries &value) -> Result<empty, UiError>;
    auto set_text_format(UiId id, TextFormat format) -> Result<empty, UiError>;

    auto set_text_font_spans(UiId id, TextFontSpans spans) -> Result<empty,UiError>;
    auto set_text_font_spans(UiId id, slice<TextFontSpan> spans) -> Result<empty,UiError>;
    auto set_text_effects(UiId id, TextEffects effects) -> Result<empty, UiError>;
    auto set_text_character_spacing(UiId id, Scalar value) -> Result<empty, UiError>;
    auto set_text_color(UiId id, Color color) -> Result<empty, UiError>;
    auto set_text_colors(UiId id, TextColors colors) -> Result<empty, UiError>;
    auto set_text_colors(UiId id, slice<TextColorSpan> spans) -> Result<empty, UiError>;
    auto set_text_decorations(UiId id, TextDecorations decorations) -> Result<empty, UiError>;
    auto set_text_decorations(UiId id, slice<TextDecorationSpan> spans) -> Result<empty, UiError>;
    auto set_text_inline_images(UiId id, TextInlineImages images) -> Result<empty, UiError>;
    auto text_layout(UiId id) -> Result<TextLayout, UiError>;
    auto text_placement(UiId id) -> Result<TextPlacement, UiError>;
    auto text_error(UiId id) -> Result<Option<TextError>, UiError>;
    template <class T>
        requires (rstd::Impled<T, LayoutModifier> || rstd::Impled<T, DrawModifier>)
    auto bind_modifier(UiId id, rstd::rc::Weak<T> receiver, NodeStateKey role)
        -> Result<Binding, UiError> {
        auto binding = bindings_.make(receiver);
        struct Adapter final : ModifierReceiver {
            rstd::rc::Weak<T> receiver;
            explicit Adapter(rstd::rc::Weak<T> value) : receiver(rstd::move(value)) {}
            auto layout() const -> bool override { return rstd::Impled<T, LayoutModifier>; }
            auto drawing() const -> bool override { return rstd::Impled<T, DrawModifier>; }
            auto measure(ModifierMeasureContext &context, LayoutConstraints constraints)
                -> Result<Size, UiError> override {
                if constexpr (rstd::Impled<T, LayoutModifier>) {
                    auto value = receiver.upgrade();
                    if (value.is_none()) return Err(UiError::Cancelled);
                    return rstd::as<LayoutModifier>(*value->get()).measure(context, constraints);
                } else return Err(UiError::InvalidValue);
            }
            auto arrange(ModifierArrangeContext &context, Size size)
                -> Result<empty, UiError> override {
                if constexpr (rstd::Impled<T, LayoutModifier>) {
                    auto value = receiver.upgrade();
                    if (value.is_none()) return Err(UiError::Cancelled);
                    return rstd::as<LayoutModifier>(*value->get()).arrange(context, size);
                } else return Err(UiError::InvalidValue);
            }
            auto draw(ModifierDrawContext &context) -> Result<empty, UiError> override {
                if constexpr (rstd::Impled<T, DrawModifier>) {
                    auto value = receiver.upgrade();
                    if (value.is_none()) return Err(UiError::Cancelled);
                    return rstd::as<DrawModifier>(*value->get()).draw(context);
                } else return Err(UiError::InvalidValue);
            }
        };
        auto adapter = Box<Adapter>::make(rstd::move(receiver));
        auto installed = register_modifier(id, role, binding.guard(),
            Box<ModifierReceiver>::from_raw(rstd::move(adapter).into_raw().template cast<ModifierReceiver>()));
        if (installed.is_err()) return Err(installed.unwrap_err());
        return Ok(rstd::move(binding));
    }
    auto set_modifier_order(UiId, Vec<NodeStateKey>) -> Result<empty, UiError>;
    auto invalidate_modifier(UiId, NodeStateKey) -> Result<empty, UiError>;
    auto modifier_geometry(UiId, NodeStateKey) -> Result<ModifierGeometry, UiError>;
    auto content_geometry(UiId) -> Result<ModifierGeometry, UiError>;
    template <class F>
    auto bind_subcomposition(UiId id, F callback) -> Result<SubcompositionLease, UiError> {
        auto binding = bindings_.make();
        auto state = rstd::rc::make_rc<DocumentSubcomposition>(id, binding.guard(),
            Box<rstd::dyn<rstd::FnMut<Result<empty, UiError>(SubcompositionRequest)>>>::make(rstd::move(callback)));
        auto installed = install_subcomposition(id, state.clone());
        if (installed.is_err()) return Err(installed.unwrap_err());
        return Ok(SubcompositionLease(rstd::move(state), rstd::move(binding)));
    }
    auto complete_subcomposition(const SubcompositionLease &, SubcompositionRequest) -> Result<bool, UiError>;
    auto dispatch_subcompositions() -> Result<usize, UiError>;
    auto pending_subcompositions() -> usize;
    auto subcomposition_requests() -> Vec<SubcompositionRequest>;
    template <class T>
        requires rstd::Impled<T, Layout>
    auto install_layout(UiId id, T strategy) -> Result<empty, UiError> {
        if (layout_active_ || painting_ || evaluating_ || editing_ || routing_)
            return Err(UiError::Routing);
        auto node = resolve(id);
        if (!node)
            return Err(UiError::InvalidId);
        node->layout_strategy = Some(Box<rstd::dyn<Layout>>::make(rstd::move(strategy)));
        node->layout_binding = None();
        changed(true, Some(id));
        return Ok(empty{});
    }
    auto set_layout_strategy(UiId id, Box<LayoutStrategy> strategy) -> Result<empty, UiError>;
    template <class T>
        requires rstd::Impled<T, Layout>
    auto attach_layout(UiId id, NodeStateKey role = {}) -> Result<empty, UiError> {
        if (!state<T>(id, role))
            return Err(UiError::InvalidId);
        struct Adapter {
            UiDocument *document;
            UiId id;
            NodeStateKey role;
            auto measure(LayoutMeasureContext &context, LayoutConstraints c)
                -> Result<Size, UiError> {
                auto receiver = document->state<T>(id, role);
                if (!receiver)
                    return Err(UiError::Cancelled);
                return rstd::as<Layout>(*receiver).measure(context, c);
            }
            auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
                auto receiver = document->state<T>(id, role);
                if (!receiver)
                    return Err(UiError::Cancelled);
                return rstd::as<Layout>(*receiver).arrange(context, size);
            }
        };
        return install_layout(id, Adapter{this, id, role});
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
        if (layout_active_ || painting_ || evaluating_ || editing_ || routing_)
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
    auto attach_paint(UiId id, NodeStateKey role = {}) -> Result<empty, UiError> {
        if (!state<T>(id, role))
            return Err(UiError::InvalidId);
        struct Adapter {
            UiDocument *document;
            UiId id;
            NodeStateKey role;
            auto paint(PaintContext &context) -> Result<empty, UiError> {
                auto receiver = document->state<T>(id, role);
                if (!receiver)
                    return Err(UiError::Cancelled);
                return rstd::as<Paint>(*receiver).paint(context);
            }
        };
        return install_paint(id, Adapter{this, id, role});
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
    auto invalidate_layout(UiId id) -> Result<empty, UiError>;
    auto layout_diagnostics() const -> slice<LayoutDiagnostic> {
        return layout_diagnostics_.as_slice();
    }
    auto patch_style(UiId id, const NodeStylePatch &patch) -> Result<empty, UiError>;
    auto set_layout(UiId id, LayoutStyle value) -> Result<empty, UiError>;
    auto set_background(UiId id, Color value, Scalar radius = 0) -> Result<empty, UiError>;
    auto set_transform(UiId id, Transform value) -> Result<empty, UiError>;
    auto set_opacity(UiId id, Scalar value) -> Result<empty, UiError>;
    auto set_content_tint(UiId id, Color value) -> Result<empty, UiError>;
    auto set_paint_color(UiId id, Color value) -> Result<empty, UiError>;
    auto set_clip(UiId id, Clip value) -> Result<empty, UiError>;
    auto set_visible(UiId id, bool value) -> Result<empty, UiError>;
    auto set_hit_self(UiId id, bool value) -> Result<empty, UiError>;
    auto set_text_input_area(UiId id, Option<Rect> area) -> Result<empty, UiError>;
    auto set_interaction(UiId id, bool hit_test_visible, bool enabled, bool focusable) -> Result<empty, UiError>;
    auto set_viewport_axes(UiId id, ScrollAxes axes) -> Result<empty, UiError>;
    auto set_viewport_boundary(UiId id, ViewportBoundary boundary) -> Result<empty,UiError>;
    auto set_viewport_offset(UiId id, Point offset) -> Result<empty, UiError>;
    auto viewport_metrics(UiId id) -> Result<ViewportMetrics, UiError>;
    // Callbacks observe a completed snapshot; structural changes must be deferred.
    auto on_geometry_changed(UiId source, UiId reference, auto callback) -> Result<Binding, UiError> {
        if (!valid(source) || !valid(reference)) return Err(UiError::InvalidId);
        observed_geometry_.retain([](const auto &observer) { return observer->subscriber.alive(); });
        if (observed_geometry_.len() >= usize(4096)) return Err(UiError::Limit);
        auto observer = rstd::rc::make_rc<GeometryObserver>(source, reference);
        auto connected = observer.get()->changed.connect(bindings_, rstd::move(callback));
        if (connected.is_err()) return Err(UiError::Limit);
        auto binding = rstd::move(connected).unwrap();
        observer.get()->subscriber = binding.guard();
        observed_geometry_.push(rstd::move(observer));
        pending_geometry_ = true; bindings_.wake();
        return Ok(rstd::move(binding));
    }
    auto pending_geometry_callbacks() const -> bool { return pending_geometry_; }
    template <class Snapshot> auto dispatch_geometry_callbacks(const Snapshot &snapshot) -> Result<empty, UiError> {
        auto scope = acquire_route(); if (scope.is_err()) return Err(scope.unwrap_err());
        if (snapshot.document() != session_) return Err(UiError::InvalidValue);
        pending_geometry_ = false;
        auto observers = Vec<rstd::rc::Rc<GeometryObserver>>::from(observed_geometry_.as_slice());
        for (auto &observer : observers) {
            if (snapshot.revision() != revision_) { pending_geometry_ = true; return Ok(empty{}); }
            if (!observer->subscriber.alive()) continue;
            auto geometry = relative_geometry(snapshot, observer->source, observer->reference);
            if (observer->initialized && observer->published == geometry) continue;
            observer.get()->initialized = true; observer.get()->published = geometry;
            auto emitted = observer.get()->changed.emit(geometry);
            if (emitted.is_err()) return Err(UiError::Routing);
        }
        observed_geometry_.retain([](const auto &observer) { return observer->subscriber.alive(); });
        return Ok(empty{});
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
    auto viewport_content(UiId id) -> Result<Option<UiId>, UiError>;
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
        DocumentRevealRegion visible(rect);
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
                auto inverse = node.properties.clip_transform.inverse();
                if (inverse.is_none()) return Ok(BringIntoViewResult{RevealStatus::NonInvertible});
                auto mapped = visible.map(*inverse);
                if (mapped.is_err()) return Err(mapped.unwrap_err());
                auto bounds = visible.bounds();
                if (bounds.is_err()) return Err(bounds.unwrap_err());
                rect = bounds.unwrap();
                const auto &metrics = *node.viewport;
                const auto viewport_bounds = clip.bounds();
                Point requested{metrics.offset.x + nearest(rect.x, rect.width, viewport_bounds.x,
                                                           metrics.viewport.width),
                                metrics.offset.y + nearest(rect.y, rect.height, viewport_bounds.y,
                                                           metrics.viewport.height)};
                if (!finite(requested.x) || !finite(requested.y))
                    return Err(UiError::InvalidValue);
                Point offset{limit(requested.x, metrics.maximum.x),
                             limit(requested.y, metrics.maximum.y)};
                auto shifted = visible.map({1, 0, 0, 1, metrics.offset.x - offset.x,
                                           metrics.offset.y - offset.y});
                if (shifted.is_err()) return Err(shifted.unwrap_err());
                mapped = visible.map(node.properties.clip_transform);
                if (mapped.is_err()) return Err(mapped.unwrap_err());
                if (offset != metrics.offset)
                    changes.push(OffsetChange{id, offset});
            }
            Clip clips[]{node.properties.clip, node.properties.layout_clip};
            Transform coordinates[]{node.properties.clip_transform, node.properties.layout_clip_transform};
            for (unsigned index = 0; index < 2; ++index) {
                if (!clips[index].enabled) continue;
                auto inverse = coordinates[index].inverse();
                if (inverse.is_none()) return Ok(BringIntoViewResult{RevealStatus::NonInvertible});
                auto mapped = visible.map(*inverse);
                if (mapped.is_err()) return Err(mapped.unwrap_err());
                auto clipped = visible.clip(clips[index].bounds());
                if (clipped.is_err()) return Err(clipped.unwrap_err());
                if (clipped.unwrap()) result.status = RevealStatus::Partial;
                mapped = visible.map(coordinates[index]);
                if (mapped.is_err()) return Err(mapped.unwrap_err());
            }
            if (visible.points.is_empty()) { result.status = RevealStatus::Partial; break; }
            if (node.parent.is_some()) {
                auto mapped = visible.map(node.properties.transform);
                if (mapped.is_err()) return Err(mapped.unwrap_err());
            }
            auto bounds = visible.bounds();
            if (bounds.is_err()) return Err(bounds.unwrap_err());
            rect = bounds.unwrap();
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
    auto update_impl(Size viewport, bool recording = true) -> Result<UiUpdate::Data, UiError>;

  public:
    auto input_stats() const -> UpdateStats { return input_stats_; }
    auto update_stats() const -> UpdateStats { return stats_; }
    auto input_snapshot(Size viewport, DiagnosticsOptions options = {})
        -> Result<InputSnapshot, UiError>;
    auto last_update_timings() const -> Option<UpdateTimings> { return last_timings_; }
    auto update(Size viewport, DiagnosticsOptions options = {}, bool recording = true)
        -> Result<UiUpdate, UiError>;
};
} // namespace vui
