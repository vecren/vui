export module vui.compose:runtime;
import :element;
import :environment;
import :saveable;
import :types;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui::compose {
enum class MemoryKind { Remember, State, SavedState, Node };
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
class ComposeBuild;
struct SavedPathFrame {
    const SavedPathFrame *parent;
    const ElementData *element;
    usize position;
};
struct SavedScopeCursor {
    const SavedItemSource *item{};
    const SavedItemValues *restore{};
    const SavedPathFrame *path{};
    auto address(Key key) const -> SavedAddress;
};
struct SavedStateSlot {
    SavedAddress address;
    const MemorySlot *slot;
    ComposeResult<rstd::rc::Rc<MemoryPayload>> (*capture)(ComposeBuild &, const MemorySlot &);
};
using CommitAction = Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(CommittedContext &)>>>;
struct ScopeState {
    bool dirty{true}, alive{true}, precomposed{};
    Vec<rstd::rc::Rc<MemorySlot>> memories;
    Vec<SavedStateSlot> saved_slots;
    Vec<rstd::rc::Rc<EffectSlot>> effects;
    Vec<rstd::rc::Rc<DependencyWatch>> reads;
    Vec<LocalRead> local_reads;
    Vec<rstd::rc::Rc<CommitAction>> deferred_actions;
};
struct ScopeDraft {
    rstd::rc::Rc<ScopeState> scope;
    Vec<rstd::rc::Rc<MemorySlot>> memories;
    Vec<SavedStateSlot> saved_slots;
    Vec<rstd::rc::Rc<EffectSlot>> effects;
    Vec<rstd::rc::Rc<DependencyWatch>> reads;
    Vec<LocalRead> local_reads;
    Vec<rstd::rc::Rc<CommitAction>> deferred_actions;
    bool precompose{};
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
    Option<Binding> modifier;
    Option<EffectLease> lease;
    bool attempted{};
    AttachedBehaviorSlot(Key name, rstd::rc::Rc<AttachedBehaviorState> value, rstd::rc::Rc<AttachedBehaviorConfig> descriptor)
        : key(rstd::move(name)), state(rstd::move(value)), config(rstd::move(descriptor)) {}
    void close() {
        if (input.is_some()) input->disconnect();
        if (modifier.is_some()) modifier->disconnect();
        lease = None();
    }
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
    rstd::rc::Rc<SavedItemActivation> saved_activation;
    rstd::rc::Rc<SavedItemValues> saved_preview;
    Vec<rstd::rc::Rc<NativePayload>> native;
    Vec<rstd::rc::Rc<ComposeInstance>> children;
    Vec<rstd::rc::Rc<EffectSlot>> mounts;
    Vec<rstd::rc::Rc<AttachedBehaviorSlot>> attached;
    bool owns_children{};
    bool detached{}, native_active{};
    rstd::rc::Rc<NodeLink> link{rstd::rc::make_rc<NodeLink>()};
};
struct ComposeRoot {
    Option<UiNodeRef> prepared;
    Option<UiId> existing;
};
struct MovableEntry {
    rstd::rc::Rc<MovableIdentity> identity;
    rstd::rc::Rc<ComposeInstance> instance;
};
struct MovableClaim {
    rstd::rc::Rc<MovableIdentity> identity;
    rstd::rc::Rc<ComposeInstance> instance;
    Vec<ComposeRoot> before, after;
};
struct ComposeDraft {
    Element element;
    Option<Element> output;
    rstd::rc::Rc<ComposeInstance> instance;
    Option<Box<ScopeDraft>> scope;
    rstd::rc::Rc<SavedItemValues> saved_preview;
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
    CompositionEnvironment precompose_environment;
    bool fresh{}, reused{}, owns_children{}, promoted{}, precompose_failed{}, precompose_reserved{};
};
class ComposeBuild {
    struct Observation {
        vui::state::GraphView graph;
        vui::state::ObservationBatch batch;
    };
    struct ItemSave {
        rstd::rc::Rc<SaveableStateHolder> holder;
        rstd::rc::Rc<SavedItemActivation> activation;
        rstd::rc::Rc<SavedItemValues> values;
    };
    Vec<ItemSave> saved_items_;
    auto capture_item_state(const ComposeInstance &instance) -> ComposeResult<empty>;
  public:
    UiSession &session;
    const UiTreeOwner &tree_owner;
    vui::state::GraphView local;
    rstd::rc::Rc<ComposeSignal> signal;
    UiEditBatch edit;
    Vec<Observation> observations;
    Vec<rstd::rc::Rc<ComposeInstance>> retired;
    Vec<rstd::rc::Rc<ComposeInstance>> parked;
    Vec<MovableEntry> movable;
    Vec<MovableClaim> claims;
    Vec<rstd::rc::Rc<AttachedBehaviorSlot>> retired_behaviors;
    Vec<Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(CommittedContext &)>>>> actions;
    Vec<Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(UiDocument::PreparedEdit &)>>>> preparers;
    Vec<Box<rstd::dyn<rstd::FnMut<Result<empty, InputError>(EventRouter &, EventRouter::BehaviorEdit &, const UiDocument::PreparedEdit &)>>>> behaviors;
    Serial evaluations{}, comparisons{}, skipped{};
    usize nodes{};
    usize &precompose_budget;
    bool speculative{};
    ComposeBuild(UiSession &ui, vui::state::GraphView graph, rstd::rc::Rc<ComposeSignal> source, const UiTreeOwner &owner,
                 usize &budget)
        : session(ui), tree_owner(owner), local(graph), signal(rstd::move(source)), edit(owner.access()),
          precompose_budget(budget) {}
    auto observe(vui::state::GraphView graph) -> ComposeResult<vui::state::ObservationBatch *>;
    void retire(rstd::rc::Rc<ComposeInstance> instance) {
        retired.push(instance.clone());
    }
    void index_movable(const rstd::rc::Rc<ComposeInstance> &);
    auto previous_movable(const rstd::rc::Rc<MovableIdentity> &) const -> rstd::rc::Rc<ComposeInstance>;
    auto claim_movable(rstd::rc::Rc<MovableIdentity>, rstd::rc::Rc<ComposeInstance>) -> ComposeResult<usize>;
    auto claim_reused(const rstd::rc::Rc<ComposeInstance> &, bool root_claimed = false) -> ComposeResult<empty>;
    auto retains(const rstd::rc::Rc<ComposeInstance> &) const -> bool;
    auto preserve_moves() -> ComposeResult<empty>;
    auto retired_roots() const -> Vec<UiId>;
    auto stage_saved_items(const ComposeDraft &draft) -> ComposeResult<empty>;
    void apply_saved_items(ComposeDraft &draft);
    static auto dirty(const ComposeInstance &instance, const CompositionEnvironment &environment) -> bool;
    static void committed_roots(const ComposeInstance &instance, Vec<ComposeRoot> &roots) {
        if (instance.element.data().precompose) return;
        if (instance.id.is_some()) roots.push({{}, instance.id});
        else for (const auto &child : instance.children) committed_roots(*child.get(), roots);
    }
    static auto same_roots(const Vec<ComposeRoot> &before, const Vec<ComposeRoot> &after) -> bool;
    void children(UiNodeRef node, const Vec<ComposeRoot> &roots);
    auto reconcile(Element value, rstd::rc::Rc<ComposeInstance> previous, bool force, Index depth = 0,
                   CompositionEnvironment environment = {}, SavedScopeCursor saved = {})
        -> ComposeResult<rstd::rc::Rc<ComposeDraft>>;
};
class CompositionContext {
    ComposeBuild &build_;
    ScopeDraft &draft_;
    CompositionEnvironment environment_;
    SavedScopeCursor saved_;
    Vec<Key> groups_;
    struct Occurrence { CallIdentity identity; Serial next{}; };
    Vec<Occurrence> occurrences_;
    Vec<CallIdentity> groups_seen_;
    Option<ComposeError> failure_;

    template <bool Save, class T> auto open_state(Key key, T initial) -> ComposeResult<State<T>> {
        Option<SavedAddress> address;
        if constexpr (Save)
            if (saved_.item) address = Some(saved_.address(Key(key)));
        auto slot = draft_.memory<vui::state::Cell<T>>(address ? MemoryKind::SavedState : MemoryKind::State,
            rstd::move(key), [&]() -> ComposeResult<vui::state::Cell<T>> {
                if constexpr (Save) {
                    if (address && saved_.restore)
                        if (auto restored = saved_.restore->find(*address)) {
                            using Snapshot = vui::state::ValueSnapshot<T>;
                            if (restored->type() != rstd::any::TypeId::of<Snapshot>())
                                return Err(ComposeError(ComposeStage::Type));
                            const auto &value = static_cast<const MemoryValue<Snapshot> &>(*restored).value.value();
                            if constexpr (rstd::mtp::same_as<T, String>) initial = value.clone();
                            else initial = value;
                        }
                }
                auto observation = build_.observe(build_.local);
                if (observation.is_err()) return Err(observation.unwrap_err());
                auto cell = observation.unwrap()->cell(rstd::move(initial));
                if (cell.is_err()) return Err(state_error(cell.unwrap_err()));
                return Ok(cell.unwrap());
            });
        if (slot.is_err()) return Err(slot.unwrap_err());
        auto owned = slot.unwrap();
        auto &cell = static_cast<MemoryValue<vui::state::Cell<T>> &>(*owned.get()->payload.get()).value;
        if constexpr (Save) {
            if (address) draft_.saved_slots.push({address.unwrap(), owned.get(),
                [](ComposeBuild &build, const MemorySlot &source) -> ComposeResult<rstd::rc::Rc<MemoryPayload>> {
                    const auto &cell = static_cast<const MemoryValue<vui::state::Cell<T>> &>(*source.payload).value;
                    auto value = cell.value();
                    auto observation = build.observe(value.graph());
                    if (observation.is_err()) return Err(observation.unwrap_err());
                    auto snapshot = observation.unwrap()->read(value);
                    if (snapshot.is_err()) return Err(state_error(snapshot.unwrap_err()));
                    return Ok(rstd::rc::Rc<MemoryPayload>(new MemoryValue<vui::state::ValueSnapshot<T>>(snapshot.unwrap())));
                }});
        }
        return Ok(State<T>(owned.downgrade(), cell.value()));
    }
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
    CompositionContext(ComposeBuild &build, ScopeDraft &draft, CompositionEnvironment environment = {},
                       SavedScopeCursor saved = {})
        : build_(build), draft_(draft), environment_(rstd::move(environment)), saved_(saved) {}
    CompositionContext(const CompositionContext &) = delete;
    auto session() -> UiSession & { return build_.session; }
    template <class F> void on_commit(F action) {
        auto callback = CommitAction::make(rstd::move(action));
        if (draft_.precompose) draft_.deferred_actions.push(rstd::rc::make_rc<CommitAction>(rstd::move(callback)));
        else build_.actions.push(rstd::move(callback));
    }
    auto identity(Serial site) -> CallIdentity;
    auto slot(Serial site) -> Key { return identity(site).memory_key(); }
    auto slot(Key key) -> Key;
    auto group(Key key, Serial site, rstd::source_location source = {}) -> GroupGuard;
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
        return open_state<false>(rstd::move(key), rstd::move(initial));
    }
    template <SaveableStateValue T> auto saved_state(Key key, T initial) -> ComposeResult<State<T>> {
        return open_state<true>(rstd::move(key), rstd::move(initial));
    }
    auto node(Key key) -> ComposeResult<NodeRef>;
    template <class T> auto read(const CompositionLocal<T> &local) -> ComposeResult<LocalSnapshot<T>> {
        auto fallback = local.fallback();
        auto value = environment_.resolve(fallback);
        if (value.value_->type() != rstd::any::TypeId::of<T>()) return Err(ComposeError(ComposeStage::Type));
        bool present{};
        for (const auto &entry : draft_.local_reads) present |= entry.fallback.identity() == fallback.identity();
        if (!present) {
            if (draft_.local_reads.len() >= usize(4096)) return Err(ComposeError(ComposeStage::Limit));
            draft_.local_reads.push({rstd::move(fallback), LocalBinding(value)});
        }
        return Ok(LocalSnapshot<T>(value.value_.clone()));
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
                        receiver->get()->dirty = true;
                        if (!receiver->get()->precomposed) owner->get()->invalidate();
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
            if (slot->key == key && slot->keys == keys && !draft_.scope->precomposed) {
                draft_.effects.push(slot.clone()); return Ok(empty{});
            }
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
    auto session() -> UiSession & { return build_.session; }
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
    auto first_activation() const -> bool { return !draft_.instance->native_active; }
    auto activating() const -> bool { return draft_.fresh || draft_.promoted; }
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
        if (!build_.speculative) build_.actions.push(CommitAction::make(rstd::move(action)));
    }
    template <class F> void on_prepared(F prepare) {
        build_.preparers.push(Box<rstd::dyn<rstd::FnMut<ComposeResult<empty>(UiDocument::PreparedEdit &)>>>::make(rstd::move(prepare)));
    }
    template <class F> void on_mount(F start, rstd::source_location source = rstd::source_location::current()) {
        if (activating()) draft_.mounts.push(rstd::rc::make_rc<EffectSlot>(Key{draft_.mounts.len().to_primitive()}, EffectKeys{},
            rstd::rc::make_rc<EffectFunction>(rstd::move(start), source)));
    }
    template <class T> requires rstd::Impled<T, Behavior>
    void behavior(UiNodeRef node, NodeStateKey role = {}) {
        if (build_.speculative || !first_activation()) return;
        build_.behaviors.push(Box<rstd::dyn<rstd::FnMut<Result<empty, InputError>(EventRouter &, EventRouter::BehaviorEdit &, const UiDocument::PreparedEdit &)>>>::make(
            [node, role](EventRouter &input, EventRouter::BehaviorEdit &edit, const UiDocument::PreparedEdit &document) {
                return input.prepare_behavior<T>(edit, document, node, role);
            }));
    }
};
auto compatible(const ElementData &a, const ElementData &b) -> bool;
inline auto identified(const ElementData &value) -> bool {
    return value.kind == ElementKind::Movable || value.key.is_some() || value.identity.is_some();
}
auto same_identity(const ElementData &a, const ElementData &b) -> bool;

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
    static void cleanup(const rstd::rc::Rc<ComposeInstance> &instance, const ComposeBuild *retained = nullptr);
    static void invalidate_links(const rstd::rc::Rc<ComposeInstance> &instance, const ComposeBuild *retained = nullptr);
    static void native_roots(const ComposeInstance &instance, Vec<UiId> &result, bool covered = false) {
        if (instance.element.data().precompose) covered = false;
        if (!covered && instance.id) {
            result.push(UiId(*instance.id));
            covered = true;
        }
        for (const auto &child : instance.children) native_roots(*child.get(), result, covered);
    }
    struct OldScope {
        rstd::rc::Rc<ScopeState> scope;
        Vec<rstd::rc::Rc<MemorySlot>> memories;
        Vec<rstd::rc::Rc<EffectSlot>> effects;
        Vec<rstd::rc::Rc<DependencyWatch>> reads;
    };
    static auto retained_references(const ComposeInstance &instance, UiDocument &document,
                                    Vec<NodeLink *> &references) -> ComposeResult<empty>;
    static auto resolve(ComposeDraft &draft, UiDocument &document, const UiDocument::PreparedEdit &prepared,
                        Vec<Element> &old_elements, Vec<NodeLink *> &references, usize &scopes) -> ComposeResult<empty>;
    static void commit(ComposeDraft &draft, Vec<OldScope> &old, bool speculative = false);
    static void park(const rstd::rc::Rc<ComposeInstance> &instance, const ComposeBuild &retained);
    void prepare_native(ComposeDraft &draft, UiSession &session, UiCommitReport &report, usize &budget);
    void activate(ComposeDraft &draft, CommittedContext &context);
    static auto locate(ComposeDraft &draft, UiNodeRef ref, ComposeError &error) -> bool;
    auto perform(UiSession &session) -> ComposeResult<CommitReport>;
  public:
    rstd::rc::Rc<ComposeSignal> signal;
    CompositionRuntime(UiDocument &document, UiId mount, UiTreeOwner claim)
        : mount_(mount), children_claim_(rstd::move(claim)), signal(rstd::rc::make_rc<ComposeSignal>()) {
        signal.get()->document = &document; signal.get()->lifetime = document.lifetime();
    }
    auto pending() const -> bool {
        return !signal->closed && (signal->pending || (signal->lifetime.alive() && !signal->document->valid(mount_)));
    }
    auto update(UiSession &session) -> Result<empty, UiError>;
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
    void retry_effects();
    void close(UiSession &session);
    auto last_result() const -> Option<ComposeResult<CommitReport>>;
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
    static auto create(UiSession &session, UiId mount) -> ComposeResult<Composition>;
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
    auto set_content(Element value, rstd::source_location source = rstd::source_location::current()) -> ComposeResult<empty>;
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
