export module vui.compose:runtime;
import :element;
import :types;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui::compose {
struct MemoryPayload {
    virtual ~MemoryPayload() = default;
    virtual auto type() const -> rstd::any::TypeId = 0;
};
template <class T> struct MemoryValue final : MemoryPayload {
    T value;
    explicit MemoryValue(T source) : value(rstd::move(source)) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<T>(); }
};
enum class MemoryKind { Remember, State, Node };
struct MemorySlot {
    Key key;
    MemoryKind kind;
    EffectKeys dependencies;
    rstd::rc::Rc<MemoryPayload> payload;
    bool alive{true}, committed{};
    MemorySlot(Key name, MemoryKind value, rstd::rc::Rc<MemoryPayload> data, EffectKeys keys = {})
        : key(rstd::move(name)), kind(value), dependencies(rstd::move(keys)), payload(rstd::move(data)) {}
};
template <class T> class Remembered {
    rstd::rc::Weak<MemorySlot> slot_;
  public:
    explicit Remembered(rstd::rc::Weak<MemorySlot> slot) : slot_(rstd::move(slot)) {}
    auto get() const -> ComposeResult<T *> {
        auto slot = slot_.upgrade();
        if (slot.is_none() || !slot->get()->alive) return Err(ComposeError(ComposeStage::Closed));
        return Ok(&static_cast<MemoryValue<T> &>(*slot->get()->payload.get()).value);
    }
};
template <class T> class State {
    rstd::rc::Weak<MemorySlot> slot_;
    vui::state::Value<T> value_;
    auto cell() const -> ComposeResult<rstd::rc::Rc<MemorySlot>> {
        auto slot = slot_.upgrade();
        if (slot.is_none() || !slot->get()->alive || !slot->get()->committed)
            return Err(ComposeError(ComposeStage::Closed));
        return Ok(rstd::move(*slot));
    }
  public:
    State(rstd::rc::Weak<MemorySlot> slot, vui::state::Value<T> value) : slot_(rstd::move(slot)), value_(value) {}
    auto value() const -> vui::state::Value<T> { return value_; }
    auto set(T value) const -> ComposeResult<empty> {
        auto opened = cell(); if (opened.is_err()) return Err(opened.unwrap_err());
        auto slot = opened.unwrap();
        auto &cell = static_cast<MemoryValue<vui::state::Cell<T>> &>(*slot.get()->payload.get()).value;
        auto result = cell.set(rstd::move(value));
        return result.is_ok() ? ComposeResult<empty>(Ok(empty{})) : ComposeResult<empty>(Err(state_error(result.unwrap_err())));
    }
    template <class F> auto update(F change) const -> ComposeResult<empty> {
        auto opened = cell(); if (opened.is_err()) return Err(opened.unwrap_err());
        auto slot = opened.unwrap();
        auto &source = static_cast<MemoryValue<vui::state::Cell<T>> &>(*slot.get()->payload.get()).value;
        auto started = vui::state::begin_update(value_.graph());
        if (started.is_err()) return Err(state_error(started.unwrap_err()));
        auto batch = started.unwrap(); auto read = batch.read(source);
        if (read.is_err()) return Err(state_error(read.unwrap_err()));
        auto written = batch.set(source, change(read.unwrap().value()));
        if (written.is_err()) return Err(state_error(written.unwrap_err()));
        auto committed = batch.commit();
        return committed.is_ok() ? ComposeResult<empty>(Ok(empty{})) : ComposeResult<empty>(Err(state_error(committed.unwrap_err())));
    }
};
struct EffectFunction {
    rstd::source_location source;
    Box<rstd::dyn<rstd::FnMut<ComposeResult<EffectLease>(CommittedContext &)>>> invoke;
    template <class F> explicit EffectFunction(F start, rstd::source_location location = {})
        : source(location), invoke(Box<rstd::dyn<rstd::FnMut<ComposeResult<EffectLease>(CommittedContext &)>>>::make(rstd::move(start))) {}
};
struct EffectSlot {
    Key key;
    EffectKeys keys;
    rstd::rc::Rc<EffectFunction> start;
    Option<EffectLease> lease;
    bool attempted{};
    EffectSlot(Key name, EffectKeys values, rstd::rc::Rc<EffectFunction> callback)
        : key(rstd::move(name)), keys(rstd::move(values)), start(rstd::move(callback)) {}
};
struct DependencyWatch {
    vui::state::NodeId id;
    vui::state::Subscription subscription;
    DependencyWatch(vui::state::NodeId value, vui::state::Subscription watch)
        : id(value), subscription(rstd::move(watch)) {}
};
struct ScopeState {
    bool dirty{true}, alive{true};
    Vec<rstd::rc::Rc<MemorySlot>> memories;
    Vec<rstd::rc::Rc<EffectSlot>> effects;
    Vec<rstd::rc::Rc<DependencyWatch>> reads;
};
struct ScopeDraft {
    rstd::rc::Rc<ScopeState> scope;
    Vec<rstd::rc::Rc<MemorySlot>> memories;
    Vec<rstd::rc::Rc<EffectSlot>> effects;
    Vec<rstd::rc::Rc<DependencyWatch>> reads;
    explicit ScopeDraft(rstd::rc::Rc<ScopeState> value) : scope(rstd::move(value)) {}
    template <class T, class F>
    auto memory(MemoryKind kind, Key key, F make, EffectKeys dependencies = {}) -> ComposeResult<rstd::rc::Rc<MemorySlot>> {
        if (memories.len() >= usize(4096)) return Err(ComposeError(ComposeStage::Limit));
        for (const auto &slot : memories)
            if (slot->kind == kind && slot->key == key) return Err(ComposeError(ComposeStage::Key));
        for (auto &slot : scope.get()->memories)
            if (slot->kind == kind && slot->key == key) {
                if (slot->payload->type() != rstd::any::TypeId::of<T>()) return Err(ComposeError(ComposeStage::Type));
                if (slot->dependencies == dependencies) { memories.push(slot.clone()); return Ok(slot.clone()); }
                break;
            }
        auto made = make(); if (made.is_err()) return Err(made.unwrap_err());
        auto value = rstd::rc::Rc<MemoryPayload>(new MemoryValue<T>(rstd::move(made.unwrap())));
        auto slot = rstd::rc::make_rc<MemorySlot>(rstd::move(key), kind, rstd::move(value), rstd::move(dependencies));
        memories.push(slot.clone()); return Ok(rstd::move(slot));
    }
};
struct NativePayload {
    virtual ~NativePayload() = default;
    virtual auto type() const -> rstd::any::TypeId = 0;
};
template <class T> struct NativeValue final : NativePayload {
    rstd::rc::Rc<T> value;
    explicit NativeValue(rstd::rc::Rc<T> source) : value(rstd::move(source)) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<T>(); }
};
struct AttachedBehaviorSlot {
    Key key;
    NodeStateKey role{rstd::any::TypeId::of<AttachedBehaviorSlot>(), new_session()};
    rstd::rc::Rc<AttachedBehaviorState> state;
    rstd::rc::Rc<AttachedBehaviorConfig> config;
    Option<Binding> input;
    Option<EffectLease> lease;
    bool attempted{};
    AttachedBehaviorSlot(Key name, rstd::rc::Rc<AttachedBehaviorState> value, rstd::rc::Rc<AttachedBehaviorConfig> descriptor)
        : key(rstd::move(name)), state(rstd::move(value)), config(rstd::move(descriptor)) {}
    void close() { if (input.is_some()) input->disconnect(); lease = None(); }
};
struct AttachedBehaviorDraft {
    rstd::rc::Rc<AttachedBehaviorSlot> slot;
    rstd::rc::Rc<AttachedBehaviorConfig> config;
};
struct ComposeInstance {
    Element element;
    Option<Element> output;
    Option<UiId> id;
    rstd::rc::Rc<ScopeState> scope;
    Vec<rstd::rc::Rc<NativePayload>> native;
    Vec<rstd::rc::Rc<ComposeInstance>> children;
    Vec<rstd::rc::Rc<EffectSlot>> mounts;
    Vec<rstd::rc::Rc<AttachedBehaviorSlot>> attached;
    bool owns_children{};
    rstd::rc::Rc<NodeLink> link{rstd::rc::make_rc<NodeLink>()};
};
struct ComposeRoot {
    Option<UiNodeRef> prepared;
    Option<UiId> existing;
};
struct ComposeDraft {
    Element element;
    Option<Element> output;
    rstd::rc::Rc<ComposeInstance> instance;
    Option<Box<ScopeDraft>> scope;
    Vec<rstd::rc::Rc<NativePayload>> native;
    Vec<rstd::rc::Rc<ComposeDraft>> children;
    Vec<rstd::rc::Rc<ComposeInstance>> reused_children;
    Vec<rstd::rc::Rc<ComposeInstance>> committed_children;
    Vec<ComposeRoot> roots;
    Option<UiNodeRef> node;
    Option<UiId> resolved;
    Vec<rstd::rc::Rc<EffectSlot>> mounts;
    Vec<AttachedBehaviorDraft> attached;
    usize position{};
    bool fresh{}, reused{}, owns_children{};
};
class ComposeBuild {
    struct Observation {
        vui::state::GraphView graph;
        vui::state::ObservationBatch batch;
    };
  public:
    UiSession &session;
    const UiTreeOwner &tree_owner;
    vui::state::GraphView local;
    rstd::rc::Rc<ComposeSignal> signal;
    UiEditBatch edit;
    Vec<Observation> observations;
    Vec<rstd::rc::Rc<ComposeInstance>> retired;
    Vec<rstd::rc::Rc<AttachedBehaviorSlot>> retired_behaviors;
    Vec<Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(CommittedContext &)>>>> actions;
    Vec<Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(UiDocument::PreparedEdit &)>>>> preparers;
    Vec<Box<rstd::dyn<rstd::FnMut<Result<empty, InputError>(EventRouter &, EventRouter::BehaviorEdit &, const UiDocument::PreparedEdit &)>>>> behaviors;
    Serial evaluations{}, comparisons{}, skipped{};
    usize nodes{};
    ComposeBuild(UiSession &ui, vui::state::GraphView graph, rstd::rc::Rc<ComposeSignal> source, const UiTreeOwner &owner)
        : session(ui), tree_owner(owner), local(graph), signal(rstd::move(source)), edit(owner.access()) {}
    auto observe(vui::state::GraphView graph) -> ComposeResult<vui::state::ObservationBatch *> {
        for (auto &entry : observations) if (entry.graph.id() == graph.id()) return Ok(&entry.batch);
        auto batch = vui::state::ObservationBatch::begin(graph);
        if (batch.is_err()) return Err(state_error(batch.unwrap_err()));
        observations.push({graph, batch.unwrap()}); return Ok(&observations[observations.len() - usize(1)].batch);
    }
    void retire(rstd::rc::Rc<ComposeInstance> instance) {
        retired.push(instance.clone());
        retire_nodes(*instance.get());
    }
    void retire_nodes(const ComposeInstance &instance) {
        if (instance.id.is_some()) { edit.remove(*instance.id); return; }
        for (const auto &child : instance.children) retire_nodes(*child.get());
    }
    static auto dirty(const ComposeInstance &instance) -> bool {
        if (instance.scope && instance.scope->dirty) return true;
        for (const auto &mount : instance.mounts) if (!mount->attempted) return true;
        for (const auto &behavior : instance.attached) if (!behavior->attempted) return true;
        for (const auto &child : instance.children) if (dirty(*child.get())) return true;
        return false;
    }
    static void committed_roots(const ComposeInstance &instance, Vec<ComposeRoot> &roots) {
        if (instance.id.is_some()) roots.push({{}, instance.id});
        else for (const auto &child : instance.children) committed_roots(*child.get(), roots);
    }
    static auto same_roots(const Vec<ComposeRoot> &before, const Vec<ComposeRoot> &after) -> bool {
        if (before.len() != after.len()) return false;
        for (usize i{}; i < before.len(); ++i)
            if (after[i].existing.is_none() || before[i].existing != after[i].existing) return false;
        return true;
    }
    void children(UiNodeRef node, const Vec<ComposeRoot> &roots) {
        Vec<UiNodeRef> refs;
        for (auto root : roots) refs.push(root.prepared.is_some() ? *root.prepared : edit.existing(*root.existing));
        edit.children(node, rstd::move(refs));
    }
    auto reconcile(Element value, rstd::rc::Rc<ComposeInstance> previous, bool force, Index depth = 0)
        -> ComposeResult<rstd::rc::Rc<ComposeDraft>>;
};
class CompositionContext {
    ComposeBuild &build_;
    ScopeDraft &draft_;
    Vec<Key> groups_;
    struct Occurrence { CallIdentity identity; Serial next{}; };
    Vec<Occurrence> occurrences_;
    Vec<CallIdentity> groups_seen_;
    Option<ComposeError> failure_;
  public:
    class GroupGuard {
        CompositionContext *context_{};
        usize depth_{};
      public:
        GroupGuard(CompositionContext &context, usize depth) : context_(&context), depth_(depth) {}
        GroupGuard(const GroupGuard &) = delete;
        GroupGuard(GroupGuard &&other) : context_(rstd::exchange(other.context_, nullptr)), depth_(other.depth_) {}
        ~GroupGuard() { if (context_) context_->groups_.truncate(depth_); }
    };
    CompositionContext(ComposeBuild &build, ScopeDraft &draft) : build_(build), draft_(draft) {}
    CompositionContext(const CompositionContext &) = delete;
    auto session() -> UiSession & { return build_.session; }
    template <class F> void on_commit(F action) {
        build_.actions.push(Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(CommittedContext &)>>>::make(rstd::move(action)));
    }
    auto identity(Serial site) -> CallIdentity {
        auto value = CallIdentity(site);
        for (const auto &group : groups_) value.groups.push(Key(group));
        for (auto &entry : occurrences_)
            if (entry.identity.same_group(value)) { value.occurrence = entry.next++; return value; }
        occurrences_.push({CallIdentity(value), 1}); return value;
    }
    auto slot(Serial site) -> Key { return identity(site).memory_key(); }
    auto slot(Key key) -> Key {
        auto value = CallIdentity();
        for (const auto &group : groups_) value.groups.push(Key(group));
        return value.memory_key(Some(rstd::move(key)));
    }
    auto group(Key key, Serial site, rstd::source_location source = {}) -> GroupGuard {
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
    auto failure() const -> const Option<ComposeError> & { return failure_; }
    template <class T, class F> auto remember(Key key, F make) -> ComposeResult<Remembered<T>> {
        return remember<T>(rstd::move(key), {}, rstd::move(make));
    }
    template <class T, class F> auto remember(Key key, EffectKeys dependencies, F make) -> ComposeResult<Remembered<T>> {
        auto slot = draft_.memory<T>(MemoryKind::Remember, rstd::move(key), rstd::move(make), rstd::move(dependencies));
        if (slot.is_err()) return Err(slot.unwrap_err());
        return Ok(Remembered<T>(slot.unwrap().downgrade()));
    }
    template <class T> auto state(Key key, T initial) -> ComposeResult<State<T>> {
        auto slot = draft_.memory<vui::state::Cell<T>>(MemoryKind::State, rstd::move(key),
            [&]() -> ComposeResult<vui::state::Cell<T>> {
                auto observation = build_.observe(build_.local);
                if (observation.is_err()) return Err(observation.unwrap_err());
                auto cell = observation.unwrap()->cell(rstd::move(initial));
                if (cell.is_err()) return Err(state_error(cell.unwrap_err()));
                return Ok(cell.unwrap());
            });
        if (slot.is_err()) return Err(slot.unwrap_err());
        auto owned = slot.unwrap();
        auto &cell = static_cast<MemoryValue<vui::state::Cell<T>> &>(*owned.get()->payload.get()).value;
        return Ok(State<T>(owned.downgrade(), cell.value()));
    }
    auto node(Key key) -> ComposeResult<NodeRef> {
        auto slot = draft_.memory<rstd::rc::Rc<NodeLink>>(MemoryKind::Node, rstd::move(key), []() -> ComposeResult<rstd::rc::Rc<NodeLink>> {
            return Ok(rstd::rc::make_rc<NodeLink>());
        });
        if (slot.is_err()) return Err(slot.unwrap_err());
        auto owned = slot.unwrap();
        return Ok(NodeRef(static_cast<MemoryValue<rstd::rc::Rc<NodeLink>> &>(*owned.get()->payload.get()).value.clone()));
    }
    template <class T> auto read(vui::state::Value<T> value) -> ComposeResult<vui::state::ValueSnapshot<T>> {
        auto observation = build_.observe(value.graph());
        if (observation.is_err()) return Err(observation.unwrap_err());
        auto result = observation.unwrap()->read(value);
        if (result.is_err()) return Err(state_error(result.unwrap_err()));
        bool present{};
        for (const auto &entry : draft_.reads) present |= entry->id == value.id();
        if (!present) {
            rstd::rc::Rc<DependencyWatch> reused;
            for (const auto &entry : draft_.scope->reads) if (entry->id == value.id()) { reused = entry.clone(); break; }
            if (reused) draft_.reads.push(rstd::move(reused));
            else {
                auto scope = draft_.scope.downgrade(); auto signal = build_.signal.downgrade();
                auto watched = observation.unwrap()->watch(value, [scope, signal](vui::state::Invalidation) {
                    auto receiver = scope.upgrade(); auto owner = signal.upgrade();
                    if (receiver.is_some() && receiver->get()->alive && owner.is_some()) {
                        receiver->get()->dirty = true; owner->get()->invalidate();
                    }
                });
                if (watched.is_err()) return Err(state_error(watched.unwrap_err()));
                draft_.reads.push(rstd::rc::make_rc<DependencyWatch>(value.id(), watched.unwrap()));
            }
        }
        return Ok(result.unwrap());
    }
    template <class F> auto effect(Key key, EffectKeys keys, F start, rstd::source_location source = rstd::source_location::current()) -> ComposeResult<empty> {
        if (draft_.effects.len() >= usize(4096)) return Err(ComposeError(ComposeStage::Limit));
        for (const auto &slot : draft_.effects) if (slot->key == key) return Err(ComposeError(ComposeStage::Key));
        for (const auto &slot : draft_.scope->effects)
            if (slot->key == key && slot->keys == keys) { draft_.effects.push(slot.clone()); return Ok(empty{}); }
        draft_.effects.push(rstd::rc::make_rc<EffectSlot>(rstd::move(key), rstd::move(keys),
            rstd::rc::make_rc<EffectFunction>(rstd::move(start), source)));
        return Ok(empty{});
    }
};
namespace [[clang::annotate("vui.compose.runtime.v1")]] compiler {
inline thread_local CompositionContext *active_context{};
class Activation {
    CompositionContext *previous_;
  public:
    explicit Activation(CompositionContext &context) : previous_(active_context) { active_context = &context; }
    Activation(const Activation &) = delete;
    ~Activation() { active_context = previous_; }
};
inline auto current() -> CompositionContext * { return active_context; }
}
class PrepareContext {
    ComposeBuild &build_;
    ComposeDraft &draft_;
  public:
    PrepareContext(ComposeBuild &build, ComposeDraft &draft) : build_(build), draft_(draft) {}
    PrepareContext(const PrepareContext &) = delete;
    auto document() -> UiDocument & { return build_.session.document(); }
    auto edit() -> UiEditBatch & { return build_.edit; }
    auto previous_style() const -> Option<NodeStyle> {
        return draft_.fresh ? None<NodeStyle>() : Some(NodeStyle(draft_.instance->element.data().style));
    }
    void style(UiNodeRef id, NodeStyle value) {
        if (draft_.fresh) build_.edit.style(id, value);
        else build_.edit.style(id, NodeStylePatch::difference(draft_.instance->element.data().style, value));
    }
    auto signal() -> rstd::rc::Weak<ComposeSignal> { return build_.signal.downgrade(); }
    auto created() const -> bool { return draft_.fresh; }
    auto source() const -> rstd::source_location { return draft_.element.data().source; }
    auto target() -> NodeRef { return NodeRef(draft_.instance->link.clone()); }
    void owns_children() { draft_.owns_children = true; }
    template <class T, class F> auto runtime(F make) -> ComposeResult<rstd::rc::Rc<T>> {
        for (auto &payload : draft_.native)
            if (payload->type() == rstd::any::TypeId::of<T>())
                return Ok(static_cast<NativeValue<T> &>(*payload.get()).value.clone());
        auto made = make(); if (made.is_err()) return Err(made.unwrap_err());
        auto value = made.unwrap();
        draft_.native.push(rstd::rc::Rc<NativePayload>(new NativeValue<T>(value.clone()))); return Ok(rstd::move(value));
    }

    template <class F> void on_commit(F action) {
        build_.actions.push(Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(CommittedContext &)>>>::make(rstd::move(action)));
    }
    template <class F> void on_prepared(F prepare) {
        build_.preparers.push(Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(UiDocument::PreparedEdit &)>>>::make(rstd::move(prepare)));
    }
    template <class F> void on_mount(F start, rstd::source_location source = rstd::source_location::current()) {
        if (draft_.fresh) draft_.mounts.push(rstd::rc::make_rc<EffectSlot>(Key{draft_.mounts.len().to_primitive()}, EffectKeys{},
            rstd::rc::make_rc<EffectFunction>(rstd::move(start), source)));
    }
    template <class T> requires rstd::Impled<T, Behavior>
    void behavior(UiNodeRef node, NodeStateKey role = {}) {
        if (!draft_.fresh) return;
        build_.behaviors.push(Box<rstd::dyn<rstd::FnMut<Result<empty, InputError>(EventRouter &, EventRouter::BehaviorEdit &, const UiDocument::PreparedEdit &)>>>::make(
            [node, role](EventRouter &input, EventRouter::BehaviorEdit &edit, const UiDocument::PreparedEdit &document) {
                return input.prepare_behavior<T>(edit, document, node, role);
            }));
    }
};
inline auto compatible(const ElementData &a, const ElementData &b) -> bool {
    if (a.kind != b.kind) return false;
    if (a.kind == ElementKind::Native) return a.layout == b.layout;
    if (a.kind == ElementKind::Component) return a.component_type == b.component_type;
    if (a.kind == ElementKind::Custom) return a.adapter->type == b.adapter->type;
    return true;
}
inline auto identified(const ElementData &value) -> bool { return value.key.is_some() || value.identity.is_some(); }
inline auto same_identity(const ElementData &a, const ElementData &b) -> bool {
    if (a.identity.is_some() != b.identity.is_some() || a.key.is_some() != b.key.is_some()) return false;
    if (a.identity.is_some() && !a.identity->same_group(*b.identity)) return false;
    if (a.key.is_some()) return *a.key == *b.key;
    return a.identity.is_some() && a.identity->occurrence == b.identity->occurrence;
}
auto ComposeBuild::reconcile(Element value, rstd::rc::Rc<ComposeInstance> previous, bool force, Index depth)
    -> ComposeResult<rstd::rc::Rc<ComposeDraft>> {
    if (++nodes > usize(65536) || depth >= 128) return Err(ComposeError(ComposeStage::Limit));
    const auto &data = value.data();
    if (data.invalid) return Err(ComposeError(ComposeStage::Prepare, Some(UiError::InvalidValue)));
    if (previous && previous->id.is_some() && !session.document().valid(*previous->id))
        return Err(ComposeError(ComposeStage::External, Some(UiError::InvalidId)));
    if (previous && !compatible(data, previous->element.data())) { retire(previous); previous = {}; }
    auto plan = rstd::rc::make_rc<ComposeDraft>(); auto &draft = *plan.get();
    draft.element = value; draft.fresh = !previous;
    draft.instance = previous ? previous.clone() : rstd::rc::make_rc<ComposeInstance>();
    if (previous && !force && !dirty(*previous.get())) {
        draft.reused = true; committed_roots(*previous.get(), draft.roots); return Ok(rstd::move(plan));
    }
    for (const auto &payload : draft.instance->native) draft.native.push(payload.clone());
    draft.owns_children = draft.instance->owns_children;
    Vec<Element> children;
    if (data.kind == ElementKind::Component) {
        auto scope = draft.instance->scope ? draft.instance->scope.clone() : rstd::rc::make_rc<ScopeState>();
        if (force && !draft.fresh && !scope->dirty && data.parameters) {
            const auto &previous_parameters = draft.instance->element.data().parameters;
            ++comparisons;
            if (previous_parameters && data.parameters->type() == previous_parameters->type() &&
                data.parameters->equal(*previous_parameters.get())) { force = false; ++skipped; }
        }
        if (force || scope->dirty || draft.fresh) {
            ++evaluations; draft.scope = Some(Box<ScopeDraft>::make(scope));
            CompositionContext context(*this, **draft.scope);
            auto evaluate = data.view.clone();
            auto evaluated = [&]() -> ComposeResult<Element> {
                compiler::Activation activation(context);
                auto read = session.document().acquire_read();
                if (read.is_err()) return Err(ComposeError(ComposeStage::Evaluation, Some(read.unwrap_err())));
                auto guard = read.unwrap(); return evaluate.get()->invoke->operator()(context);
            }();
            if (context.failure().is_some()) return Err(ComposeError(*context.failure()));
            if (evaluated.is_err()) return Err(evaluated.unwrap_err());
            draft.output = Some(evaluated.unwrap()); force = true;
        } else if (draft.instance->output.is_some()) draft.output = Some(Element(*draft.instance->output));
        else return Err(ComposeError(ComposeStage::Type));
        auto modified = apply_modifiers(Element(*draft.output), data.modifiers);
        if (modified.is_err()) return Err(modified.unwrap_err());
        children.push(modified.unwrap());
        if (!data.modifiers.is_empty() || !draft.instance->element.data().modifiers.is_empty()) force = true;
    } else {
        for (const auto &child : data.children) children.push(Element(child));
        if (data.kind == ElementKind::Fragment && !data.modifiers.is_empty()) {
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
        if (old && !force && compatible(child, old->element.data()) && !dirty(*old.get())) {
            committed_roots(*old.get(), descendants);
            draft.reused_children.push(old.clone()); draft.committed_children.push(rstd::move(old)); continue;
        }
        auto built = reconcile(children[i], rstd::move(old), force, depth + 1);
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
    return Ok(rstd::move(plan));
}
class CompositionRuntime {
    struct RootTag {};
    UiId mount_;
    UiTreeOwner children_claim_;
    vui::state::Graph graph_;
    rstd::rc::Rc<ComposeInstance> root_;
    rstd::rc::Rc<ViewFunction> content_;
    rstd::source_location content_source_;
    Serial attempt_{};
    Option<CommitReport> last_;
    static void cleanup(const rstd::rc::Rc<ComposeInstance> &instance) {
        auto value = instance.clone();
        for (auto &child : value.get()->children) cleanup(child);
        for (usize i = value->attached.len(); i > usize(); --i) value.get()->attached[i - usize(1)].get()->close();
        value.get()->attached.clear();
        if (value->scope) {
            auto &scope = *value.get()->scope.get(); scope.alive = false;
            for (usize i = scope.effects.len(); i > usize(); --i) scope.effects[i - usize(1)].get()->lease = None();
            for (auto &slot : scope.memories) slot.get()->alive = false;
            scope.effects.clear(); scope.reads.clear(); scope.memories.clear();
        }
        for (usize i = value->mounts.len(); i > usize(); --i) value.get()->mounts[i - usize(1)].get()->lease = None();
        value.get()->mounts.clear(); value.get()->native.clear();
    }
    static void invalidate_links(const rstd::rc::Rc<ComposeInstance> &instance) {
        auto value = instance.clone(); value.get()->link.get()->active = false; value.get()->link.get()->alive = false;
        if (value->element.data().reference.is_some()) {
            auto link = value->element.data().reference->link(); link.get()->active = false; link.get()->alive = false;
        }
        for (const auto &child : value->children) invalidate_links(child);
    }
    static void native_roots(const ComposeInstance &instance, Vec<UiId> &result) {
        if (instance.id.is_some()) { result.push(UiId(*instance.id)); return; }
        for (const auto &child : instance.children) native_roots(*child.get(), result);
    }
    struct OldScope {
        rstd::rc::Rc<ScopeState> scope;
        Vec<rstd::rc::Rc<MemorySlot>> memories;
        Vec<rstd::rc::Rc<EffectSlot>> effects;
        Vec<rstd::rc::Rc<DependencyWatch>> reads;
    };
    static auto retained_references(const ComposeInstance &instance, UiDocument &document,
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
    static auto resolve(ComposeDraft &draft, UiDocument &document, const UiDocument::PreparedEdit &prepared,
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
    static void commit(ComposeDraft &draft, Vec<OldScope> &old) {
        if (draft.reused) return;
        auto &instance = *draft.instance.get();
        if (draft.scope.is_some()) {
            auto &scope = (**draft.scope).scope;
            old.push({scope.clone(), rstd::move(scope.get()->memories), rstd::move(scope.get()->effects), rstd::move(scope.get()->reads)});
            scope.get()->memories = rstd::move((**draft.scope).memories);
            scope.get()->effects = rstd::move((**draft.scope).effects);
            scope.get()->reads = rstd::move((**draft.scope).reads); scope.get()->dirty = false;
            for (auto &slot : scope.get()->memories) slot.get()->committed = true;
            instance.scope = scope.clone();
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
    void activate(ComposeDraft &draft, CommittedContext &context) {
        if (draft.reused || signal->closed) return;
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
            if (slot->attempted) continue;
            slot.get()->attempted = true;
            auto started = behavior.config->attach(*slot.get()->state.get(), attached);
            if (started.is_ok()) slot.get()->lease = Some(started.unwrap());
            else report(started.unwrap_err());
        }
        if (draft.scope.is_some())
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
    static auto locate(ComposeDraft &draft, UiNodeRef ref, ComposeError &error) -> bool {
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
    auto perform(UiSession &session) -> ComposeResult<CommitReport> {
        if (!session.document().valid(mount_)) {
            close(session);
            return Err(ComposeError(ComposeStage::External, Some(UiError::InvalidId)));
        }
        if (!content_) return Ok(CommitReport{session.document().revision(), attempt_});
        ComposeBuild build(session, graph_.view(), signal, children_claim_);
        auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Component;
        data.get()->component_type = rstd::any::TypeId::of<RootTag>(); data.get()->view = content_; data.get()->source = content_source_;
        auto built = build.reconcile(Element(rstd::move(data)), root_, !root_ || root_->scope->dirty);
        if (built.is_err()) {
            auto error = built.unwrap_err(); if (!error.source.line()) error.source = content_source_;
            return Err(rstd::move(error));
        }
        auto draft = built.unwrap();
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
        for (auto &observation : build.observations) observation.batch.publish();
        auto input_published = session.input().publish_behaviors(rstd::move(behaviors));
        if (input_published.is_err()) {
            ComposeError error(ComposeStage::Input); error.input = Some(input_published.unwrap_err()); signal.get()->report(rstd::move(error));
        }
        for (usize i = build.retired_behaviors.len(); i > usize(); --i) build.retired_behaviors[i - usize(1)].get()->close();
        build.retired_behaviors.clear();
        build.observations.clear();
        for (auto id : report.retired) {
            auto cancelled = session.input().cancel_subtree(id);
            if (cancelled.is_err()) { ComposeError error(ComposeStage::Input); error.input = Some(cancelled.unwrap_err()); signal.get()->report(rstd::move(error)); }
        }
        for (auto &instance : build.retired) cleanup(instance);
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
        for (auto &instance : build.retired) invalidate_links(instance);
        for (auto id : report.retired) if (session.document().valid(id)) session.document().destroy(id).unwrap();
        old_elements.clear();
        report.retirement = None();
        session.document().notify_edit_targets();
        CommittedContext context(session);
        for (auto &action : build.actions) {
            if (signal->closed) break;
            auto applied = (*action)(context); if (applied.is_err()) signal.get()->report(applied.unwrap_err());
        }
        activate(*draft.get(), context);
        return Ok(CommitReport{report.revision, attempt_, build.evaluations, report.created, report.updated, report.retired.len(), build.comparisons, build.skipped});
    }
  public:
    rstd::rc::Rc<ComposeSignal> signal;
    CompositionRuntime(UiDocument &document, UiId mount, UiTreeOwner claim)
        : mount_(mount), children_claim_(rstd::move(claim)), signal(rstd::rc::make_rc<ComposeSignal>()) {
        signal.get()->document = &document; signal.get()->lifetime = document.lifetime();
    }
    auto pending() const -> bool {
        return !signal->closed && (signal->pending || (signal->lifetime.alive() && !signal->document->valid(mount_)));
    }
    auto update(UiSession &session) -> Result<empty, UiError> {
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
    template <class F> void content(F evaluate, rstd::source_location source) {
        content_ = rstd::rc::make_rc<ViewFunction>(rstd::move(evaluate));
        content_source_ = source;
        signal.get()->source = source;
        if (root_ && root_->scope) root_.get()->scope.get()->dirty = true;
        signal.get()->invalidate();
    }
    void invalidate() {
        if (root_ && root_->scope) root_.get()->scope.get()->dirty = true;
        signal.get()->invalidate();
    }
    void retry_effects() {
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
    void close(UiSession &session) {
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
    auto last_result() const -> Option<ComposeResult<CommitReport>> {
        if (signal->error.is_some()) return Some(ComposeResult<CommitReport>(Err(ComposeError(*signal->error))));
        if (last_.is_some()) return Some(ComposeResult<CommitReport>(Ok(*last_)));
        return None();
    }
    auto take_diagnostics() -> ComposeDiagnostics {
        return rstd::exchange(signal.get()->diagnostics, ComposeDiagnostics{});
    }
};
class Composition {
    rstd::rc::Rc<CompositionRuntime> runtime_;
    Binding binding_;
    Composition(rstd::rc::Rc<CompositionRuntime> runtime, Binding binding)
        : runtime_(rstd::move(runtime)), binding_(rstd::move(binding)) {}
  public:
    static auto create(UiSession &session, UiId mount) -> ComposeResult<Composition> {
        auto &document = session.document();
        if (!document.valid(mount)) return Err(ComposeError(ComposeStage::Prepare, Some(UiError::InvalidId)));
        auto claim = document.claim_children(mount);
        if (claim.is_err()) return Err(ComposeError(ComposeStage::Prepare, Some(claim.unwrap_err())));
        auto runtime = rstd::rc::make_rc<CompositionRuntime>(document, mount, claim.unwrap());
        auto registered = session.updates().bind(runtime);
        if (registered.is_err()) return Err(ComposeError(ComposeStage::Prepare, Some(registered.unwrap_err())));
        return Ok(Composition(rstd::move(runtime), registered.unwrap()));
    }
    Composition(const Composition &) = delete;
    Composition(Composition &&) = default;
    auto operator=(Composition &&other) -> Composition & {
        if (this != &other) { close(); runtime_ = rstd::move(other.runtime_); binding_ = rstd::move(other.binding_); } return *this;
    }
    ~Composition() { close(); }
    void close() {
        if (runtime_) runtime_.get()->signal.get()->closed = true;
        binding_.disconnect();
    }
    template <class F> auto set_content(F evaluate, rstd::source_location source = rstd::source_location::current()) -> ComposeResult<empty> {
        if (!runtime_ || runtime_->signal->closed) return Err(ComposeError(ComposeStage::Closed));
        runtime_.get()->content(rstd::move(evaluate), source); return Ok(empty{});
    }
    auto set_content(Element value, rstd::source_location source = rstd::source_location::current()) -> ComposeResult<empty> {
        return set_content([value = rstd::move(value)](CompositionContext &) -> ComposeResult<Element> {
            return Ok(Element(value));
        }, source);
    }
    auto set_content(ComposeResult<Element> value, rstd::source_location source = rstd::source_location::current()) -> ComposeResult<empty> {
        if (value.is_err()) return Err(value.unwrap_err());
        return set_content(value.unwrap(), source);
    }
    void invalidate() { if (runtime_) runtime_.get()->invalidate(); }
    void retry_effects() { if (runtime_) runtime_.get()->retry_effects(); }
    auto last_result() const -> Option<ComposeResult<CommitReport>> { return runtime_ ? runtime_->last_result() : None<ComposeResult<CommitReport>>(); }
    auto take_diagnostics() -> ComposeDiagnostics { return runtime_ ? runtime_.get()->take_diagnostics() : ComposeDiagnostics{}; }
};
} // namespace vui::compose
