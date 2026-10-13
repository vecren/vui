export module vui:state;
import rstd;
using namespace rstd::prelude;

export namespace vui::state {
using Version = unsigned long long;
struct NodeId {
    Version graph{}, generation{};
    usize slot{};
    friend bool operator==(NodeId, NodeId) = default;
};
enum class StateError {
    Closed,
    SourceGone,
    WrongGraph,
    WrongThread,
    TypeMismatch,
    UndeclaredDependency,
    Cycle,
    Reentrant,
    Capacity,
    Evaluation
};
enum class InvalidationReason { Changed, SourceGone, Closed };
struct Invalidation {
    NodeId value;
    Version epoch;
    InvalidationReason reason;
};
struct GraphLimits {
    usize nodes{4096}, edges{16384}, observers{4096};
};
struct GraphStats {
    Version commits{}, evaluations{}, cache_hits{}, invalidations{}, notifications{};
    usize nodes{}, edges{}, observers{};
};
struct NodeOptions {
    String name;
};
struct StateDiagnostic {
    StateError error;
    NodeId value;
    Vec<NodeId> path;
    String name;
};
struct Payload {
    virtual ~Payload() = default;
    virtual auto type() const -> rstd::any::TypeId = 0;
    virtual auto address() const -> const void * = 0;
};
using SharedValue = rstd::rc::Rc<Payload>;
template <class T> struct StoredValue final : Payload {
    T value;
    explicit StoredValue(T data) : value(rstd::move(data)) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<T>(); }
    auto address() const -> const void * override { return &value; }
};
template <class T> auto store(T value) -> SharedValue {
    return SharedValue(new StoredValue<T>(rstd::move(value)));
}
template <class T> class ValueSnapshot {
    SharedValue value_;
    Version version_{}, epoch_{};
    friend class EvalContext;
    friend class ReadBatch;
    friend class GraphUpdate;
    ValueSnapshot(SharedValue value, Version version, Version epoch)
        : value_(rstd::move(value)), version_(version), epoch_(epoch) {}

  public:
    auto value() const -> const T & { return *static_cast<const T *>(value_->address()); }
    auto version() const -> Version { return version_; }
    auto epoch() const -> Version { return epoch_; }
};
class EvalContext;
struct GraphData;
template <class T> class Value;
template <class T> class Cell;
template <class T> class Computed;
class GraphView;
class ReadBatch;
class GraphUpdate;
class StateWriteGuard;
struct Dependency {
    NodeId id;
};
class Dependencies {
    Vec<Dependency> values_;

  public:
    template <class... T> static auto manual(const Value<T> &...values) -> Dependencies {
        Dependencies result;
        (result.values_.push(values.dependency()), ...);
        return result;
    }
    auto values() const -> slice<Dependency> { return values_.as_slice(); }
};
struct Observer {
    NodeId value;
    bool active{true}, pending{};
    InvalidationReason reason{InvalidationReason::Changed};
    Box<rstd::dyn<rstd::FnMut<void(Invalidation)>>> schedule;
    template <class F>
    Observer(NodeId id, F callback)
        : value(id),
          schedule(Box<rstd::dyn<rstd::FnMut<void(Invalidation)>>>::make(rstd::move(callback))) {}
};
class Subscription {
    rstd::rc::Rc<Observer> observer_;
    rstd::rc::Weak<GraphData> graph_;
    friend class GraphView;
    Subscription(rstd::rc::Rc<Observer> observer, rstd::rc::Weak<GraphData> graph)
        : observer_(rstd::move(observer)), graph_(rstd::move(graph)) {}

  public:
    Subscription() = default;
    Subscription(const Subscription &) = delete;
    Subscription(Subscription &&) = default;
    auto operator=(Subscription &&other) -> Subscription & {
        if (this != &other) {
            disconnect();
            observer_ = rstd::move(other.observer_);
            graph_ = rstd::move(other.graph_);
        }
        return *this;
    }
    ~Subscription() { disconnect(); }
    void disconnect();
    auto alive() const -> bool { return observer_ && observer_->active; }
};
struct Node {
    NodeId id;
    String name;
    rstd::any::TypeId type;
    SharedValue current;
    Option<StateError> error;
    Version version{1}, marked{};
    bool dirty{}, force{}, evaluated{}, retire{}, source{};
    Vec<NodeId> dependencies, consumers;
    Vec<Version> versions;
    Option<Box<rstd::dyn<rstd::FnMut<Result<SharedValue, StateError>(EvalContext &)>>>> evaluate;
    Box<rstd::dyn<rstd::Fn<bool(const SharedValue &, const SharedValue &)>>> equal;
    template <class Equal>
    Node(NodeId identity, rstd::any::TypeId value_type, Equal comparison, NodeOptions options)
        : id(identity), name(rstd::move(options.name)), type(value_type),
          equal(Box<rstd::dyn<rstd::Fn<bool(const SharedValue &, const SharedValue &)>>>::make(
              rstd::move(comparison))) {}
};
struct GraphData {
    struct Slot {
        Version generation{1};
        rstd::rc::Rc<Node> node;
    };
    rstd::thread::ThreadId thread{rstd::thread::current_id()};
    Version id{}, epoch{1}, propagation{};
    GraphLimits limits;
    GraphStats stats;
    Vec<Slot> slots;
    Vec<rstd::rc::Rc<Observer>> observers;
    Vec<StateDiagnostic> diagnostics;
    Vec<NodeId> evaluating;
    unsigned reads{};
    unsigned write_blocks{};
    bool notifying{}, publishing{}, cleaning{}, transaction{}, closed{}, closing{};
    explicit GraphData(GraphLimits value) : limits(value) {
        static rstd::sync::atomic::Atomic<rstd::u64> sequence{rstd::u64(1)};
        id = sequence.fetch_add(rstd::u64(1), rstd::sync::atomic::Ordering::Relaxed).to_primitive();
    }
    auto check() const -> Option<StateError> {
        if (thread != rstd::thread::current_id())
            return Some(StateError::WrongThread);
        if (closed)
            return Some(StateError::Closed);
        return None();
    }
    auto busy() const -> bool {
        return reads || notifying || publishing || cleaning || !evaluating.is_empty();
    }
    auto resolve(NodeId value) const -> rstd::rc::Rc<Node> {
        if (value.graph != id || value.slot >= slots.len())
            return {};
        const auto &slot = slots[value.slot];
        return slot.generation == value.generation ? slot.node : rstd::rc::Rc<Node>{};
    }
    auto validate(NodeId value) const -> Option<StateError> {
        if (value.graph != id)
            return Some(StateError::WrongGraph);
        if (!resolve(value))
            return Some(StateError::SourceGone);
        return None();
    }
    void report(StateError error, NodeId value) {
        auto node = resolve(value);
        StateDiagnostic diagnostic{
            error, value, {}, node ? String::make(node->name.as_str()) : String{}};
        for (auto dependency : evaluating)
            diagnostic.path.push(NodeId(dependency));
        if (diagnostics.len() == usize(256))
            diagnostics.remove(usize());
        diagnostics.push(rstd::move(diagnostic));
    }
    void dirty(NodeId value, InvalidationReason reason = InvalidationReason::Changed) {
        ++propagation;
        Vec<NodeId> pending;
        pending.push(NodeId(value));
        for (usize i{}; i < pending.len(); ++i) {
            auto key = pending[i];
            auto node = resolve(key);
            for (auto &observer : observers)
                if (observer.get()->active && observer.get()->value == key) {
                    observer.get()->pending = true;
                    observer.get()->reason = reason;
                }
            if (!node)
                continue;
            if (node.get()->marked == propagation)
                continue;
            node.get()->marked = propagation;
            node.get()->dirty = true;
            ++stats.invalidations;
            for (auto consumer : node.get()->consumers)
                pending.push(NodeId(consumer));
        }
    }
    void notify() {
        if (busy())
            return;
        notifying = true;
        auto batch = Vec<rstd::rc::Rc<Observer>>{};
        for (auto &observer : observers)
            if (observer.get()->active && observer.get()->pending)
                batch.push(observer.clone());
        for (auto &observer : batch) {
            if (!observer.get()->active || !observer.get()->pending)
                continue;
            observer.get()->pending = false;
            ++stats.notifications;
            observer.get()->schedule->operator()(
                {observer.get()->value, epoch, observer.get()->reason});
        }
        observers.retain([](const auto &observer) { return observer.get()->active; });
        stats.observers = observers.len();
        notifying = false;
        cleanup();
    }
    void retire(NodeId value) {
        auto node = resolve(value);
        if (!node || closed)
            return;
        node.get()->retire = true;
        if (!busy())
            cleanup();
    }
    void prune_observers() {
        if (busy() || closed)
            return;
        cleaning = true;
        bool remaining;
        do {
            observers.retain([](const auto &observer) { return observer->active; });
            remaining = false;
            for (auto &observer : observers)
                remaining |= !observer->active;
        } while (remaining);
        stats.observers = observers.len();
        cleaning = false;
        cleanup();
    }
    void cleanup() {
        if (busy())
            return;
        cleaning = true;
        bool changed = false;
        for (auto &slot : slots) {
            if (!slot.node || !slot.node.get()->retire)
                continue;
            auto node = slot.node;
            dirty(node.get()->id, InvalidationReason::SourceGone);
            for (auto id : node.get()->dependencies) {
                auto dependency = resolve(id);
                if (dependency)
                    dependency.get()->consumers.retain(
                        [&](NodeId consumer) { return consumer != node.get()->id; });
            }
            stats.edges -= node.get()->dependencies.len();
            --stats.nodes;
            slot.node = {};
            ++slot.generation;
            changed = true;
        }
        cleaning = false;
        if (changed) {
            ++epoch;
            notify();
        }
        if (closing) {
            closing = false;
            closed = true;
            ++epoch;
            notifying = true;
            for (auto &observer : observers)
                if (observer.get()->active) {
                    observer.get()->schedule->operator()(
                        {observer.get()->value, epoch, InvalidationReason::Closed});
                    observer.get()->active = false;
                }
            notifying = false;
            observers.clear();
            slots.clear();
            stats.nodes = stats.edges = stats.observers = usize();
        }
    }
    auto ensure(NodeId value) -> Result<empty, StateError>;
};
void Subscription::disconnect() {
    if (!observer_)
        return;
    observer_.get()->active = false;
    auto graph = graph_.upgrade();
    if (graph.is_some())
        graph->get()->prune_observers();
    observer_ = {};
}
class GraphView {
    rstd::rc::Weak<GraphData> data_;
    Option<rstd::thread::ThreadId> thread_;
    Version id_{};
    friend class Graph;
    template <class T> friend class Cell;
    template <class T> friend class Computed;
    friend auto begin_read(GraphView) -> Result<ReadBatch, StateError>;
    friend auto begin_update(GraphView) -> Result<GraphUpdate, StateError>;
    friend auto protect_writes(GraphView) -> Result<StateWriteGuard, StateError>;
    explicit GraphView(const rstd::rc::Rc<GraphData> &data)
        : data_(data.downgrade()), thread_(Some(rstd::thread::ThreadId(data.get()->thread))),
          id_(data.get()->id) {}
    auto open() const -> Result<rstd::rc::Rc<GraphData>, StateError> {
        if (thread_.is_some() && *thread_ != rstd::thread::current_id())
            return Err(StateError::WrongThread);
        auto data = data_.upgrade();
        if (data.is_none() || (*data).get()->closed)
            return Err(StateError::Closed);
        return Ok(rstd::move(*data));
    }

  public:
    GraphView() = default;
    auto id() const -> Version { return id_; }
    auto epoch() const -> Result<Version, StateError> {
        auto data = open();
        if (data.is_err())
            return Err(data.unwrap_err());
        return Ok(data.unwrap()->epoch);
    }
    template <class T, class F>
    auto watch(const Value<T> &value, F callback) const -> Result<Subscription, StateError>;
};
class StateWriteGuard {
    rstd::rc::Rc<GraphData> data_;
    friend auto protect_writes(GraphView) -> Result<StateWriteGuard, StateError>;
    explicit StateWriteGuard(rstd::rc::Rc<GraphData> data) : data_(rstd::move(data)) {
        ++data_.get()->write_blocks;
    }

  public:
    StateWriteGuard(const StateWriteGuard &) = delete;
    StateWriteGuard(StateWriteGuard &&) = default;
    ~StateWriteGuard() {
        if (data_)
            --data_.get()->write_blocks;
    }
};
inline auto protect_writes(GraphView graph) -> Result<StateWriteGuard, StateError> {
    auto data = graph.open();
    if (data.is_err())
        return Err(data.unwrap_err());
    return Ok(StateWriteGuard(data.unwrap()));
}
template <class T> class Value {
    GraphView graph_;
    NodeId id_;
    friend class Graph;
    Value(GraphView graph, NodeId id) : graph_(rstd::move(graph)), id_(id) {}

  public:
    Value() = default;
    auto id() const -> NodeId { return id_; }
    auto graph() const -> GraphView { return graph_; }
    auto dependency() const -> Dependency { return {id_}; }
};
class EvalContext {
    GraphData &graph_;
    Node &node_;
    Option<StateError> fault_;
    friend struct GraphData;
    EvalContext(GraphData &graph, Node &node) : graph_(graph), node_(node) {}

  public:
    auto fault() const -> Option<StateError> { return fault_; }
    template <class T> auto read(const Value<T> &value) -> Result<ValueSnapshot<T>, StateError> {
        bool declared = false;
        for (auto id : node_.dependencies)
            if (id == value.id())
                declared = true;
        if (!declared) {
            fault_ = Some(value.id().graph == graph_.id ? StateError::UndeclaredDependency
                                                        : StateError::WrongGraph);
            return Err(*fault_);
        }
        auto ensured = graph_.ensure(value.id());
        if (ensured.is_err()) {
            fault_ = Some(ensured.unwrap_err());
            return Err(*fault_);
        }
        auto node = graph_.resolve(value.id());
        if (node.get()->type != rstd::any::TypeId::of<T>()) {
            fault_ = Some(StateError::TypeMismatch);
            return Err(*fault_);
        }
        return Ok(ValueSnapshot<T>(node.get()->current, node.get()->version, graph_.epoch));
    }
};
auto GraphData::ensure(NodeId value) -> Result<empty, StateError> {
    struct StackGuard {
        Vec<NodeId> &stack;
        usize size;
        ~StackGuard() {
            while (stack.len() > size)
                stack.pop();
        }
    } guard{evaluating, evaluating.len()};
    if (auto error = validate(value); error.is_some())
        return Err(*error);
    auto target = resolve(value);
    if (target.get()->source)
        return Ok(empty{});
    for (auto active : evaluating)
        if (active == value) {
            report(StateError::Cycle, value);
            return Err(StateError::Cycle);
        }
    if (!target.get()->dirty && target.get()->evaluated) {
        ++stats.cache_hits;
        return target.get()->error.is_some() ? Result<empty, StateError>(Err(*target.get()->error))
                                             : Result<empty, StateError>(Ok(empty{}));
    }
    // Manual dependencies are fixed; materialize them in a bounded work stack before evaluating.
    struct Work {
        NodeId id;
        bool ready;
    };
    Vec<Work> work;
    work.push({value, false});
    while (!work.is_empty()) {
        auto item = work.pop().unwrap();
        auto node = resolve(item.id);
        if (!node || node.get()->source || (!node.get()->dirty && node.get()->evaluated))
            continue;
        if (!item.ready) {
            for (auto active : evaluating)
                if (active == item.id) {
                    report(StateError::Cycle, item.id);
                    return Err(StateError::Cycle);
                }
            evaluating.push(NodeId(item.id));
            work.push({item.id, true});
            for (auto dependency : node.get()->dependencies) {
                auto upstream = resolve(dependency);
                if (upstream && !upstream.get()->source &&
                    (upstream.get()->dirty || !upstream.get()->evaluated))
                    work.push({dependency, false});
            }
            continue;
        }
        evaluating.pop();
        Vec<Version> versions;
        Option<StateError> error;
        for (auto dependency : node.get()->dependencies) {
            auto upstream = resolve(dependency);
            if (!upstream) {
                versions.push(Version{});
                error = Some(StateError::SourceGone);
            } else {
                versions.push(Version(upstream.get()->version));
                if (upstream.get()->error.is_some())
                    error = upstream.get()->error;
            }
        }
        bool changed = node.get()->force || !node.get()->evaluated ||
                       versions.len() != node.get()->versions.len();
        for (usize i{}; !changed && i < versions.len(); ++i)
            changed = versions[i] != node.get()->versions[i];
        if (!changed) {
            node.get()->dirty = false;
            ++stats.cache_hits;
            continue;
        }
        SharedValue next;
        evaluating.push(NodeId(node.get()->id));
        if (error.is_none()) {
            EvalContext context(*this, *node.get());
            ++stats.evaluations;
            auto result = (*node.get()->evaluate)->operator()(context);
            if (context.fault().is_some())
                error = context.fault();
            else if (result.is_err())
                error = Some(result.unwrap_err());
            else
                next = result.unwrap();
        }
        bool different =
            node.get()->evaluated &&
            (error != node.get()->error ||
             (error.is_none() &&
              (!node.get()->current || !node.get()->equal->operator()(node.get()->current, next))));
        if (different)
            ++node.get()->version;
        if (error.is_some() && (!node.get()->evaluated || error != node.get()->error))
            report(*error, node.get()->id);
        evaluating.pop();
        if (error.is_none())
            node.get()->current = rstd::move(next);
        node.get()->error = error;
        node.get()->versions = rstd::move(versions);
        node.get()->dirty = node.get()->force = false;
        node.get()->evaluated = true;
    }
    return target.get()->error.is_some() ? Result<empty, StateError>(Err(*target.get()->error))
                                         : Result<empty, StateError>(Ok(empty{}));
}
class ReadBatch {
    rstd::rc::Rc<GraphData> data_;
    Version epoch_{};
    friend auto begin_read(GraphView) -> Result<ReadBatch, StateError>;
    explicit ReadBatch(rstd::rc::Rc<GraphData> data)
        : data_(rstd::move(data)), epoch_(data_.get()->epoch) {
        ++data_.get()->reads;
    }

  public:
    ReadBatch() = default;
    ReadBatch(const ReadBatch &) = delete;
    ReadBatch(ReadBatch &&) = default;
    ~ReadBatch() { finish(); }
    void finish() {
        if (data_) {
            auto data = rstd::exchange(data_, {});
            --data.get()->reads;
            data.get()->cleanup();
        }
    }
    auto epoch() const -> Version { return epoch_; }
    template <class T> auto read(const Value<T> &value) -> Result<ValueSnapshot<T>, StateError> {
        if (!data_)
            return Err(StateError::Closed);
        if (auto error = data_.get()->check(); error.is_some())
            return Err(*error);
        if (!data_.get()->evaluating.is_empty())
            return Err(StateError::Reentrant);
        auto result = data_.get()->ensure(value.id());
        if (result.is_err())
            return Err(result.unwrap_err());
        auto node = data_.get()->resolve(value.id());
        if (node.get()->type != rstd::any::TypeId::of<T>())
            return Err(StateError::TypeMismatch);
        return Ok(ValueSnapshot<T>(node.get()->current, node.get()->version, epoch_));
    }
};
inline auto begin_read(GraphView graph) -> Result<ReadBatch, StateError> {
    auto opened = graph.open();
    if (opened.is_err())
        return Err(opened.unwrap_err());
    auto data = opened.unwrap();
    if (data.get()->notifying || data.get()->publishing || !data.get()->evaluating.is_empty() ||
        data.get()->reads)
        return Err(StateError::Reentrant);
    return Ok(ReadBatch(rstd::move(data)));
}
template <class T> auto read(const Value<T> &value) -> Result<ValueSnapshot<T>, StateError> {
    auto batch = begin_read(value.graph());
    if (batch.is_err())
        return Err(batch.unwrap_err());
    return batch.unwrap().read(value);
}
template <class T, class F>
auto GraphView::watch(const Value<T> &value, F callback) const -> Result<Subscription, StateError> {
    auto opened = open();
    if (opened.is_err())
        return Err(opened.unwrap_err());
    auto data = opened.unwrap();
    if (data.get()->busy())
        return Err(StateError::Reentrant);
    data.get()->prune_observers();
    if (auto error = data.get()->check(); error.is_some())
        return Err(*error);
    if (auto error = data.get()->validate(value.id()); error.is_some())
        return Err(*error);
    if (data.get()->observers.len() >= data.get()->limits.observers)
        return Err(StateError::Capacity);
    auto observer = rstd::rc::make_rc<Observer>(value.id(), rstd::move(callback));
    data.get()->observers.push(observer.clone());
    data.get()->stats.observers = data.get()->observers.len();
    return Ok(Subscription(rstd::move(observer), data.downgrade()));
}
class GraphUpdate;
template <class T> class Cell {
    Value<T> value_;
    bool owner_{true};
    void release() {
        if (owner_) {
            auto data = value_.graph().open();
            if (data.is_ok())
                data.unwrap().get()->retire(value_.id());
            owner_ = false;
        }
    }
    friend class Graph;
    template <class U> friend class Computed;
    explicit Cell(Value<T> value) : value_(rstd::move(value)) {}

  public:
    Cell(const Cell &) = delete;
    Cell(Cell &&other) : value_(other.value_), owner_(rstd::exchange(other.owner_, false)) {}
    auto operator=(Cell &&other) -> Cell & {
        if (this != &other) {
            release();
            value_ = other.value_;
            owner_ = rstd::exchange(other.owner_, false);
        }
        return *this;
    }
    ~Cell() { release(); }
    auto value() const -> Value<T> { return value_; }
    auto owns() const -> bool { return owner_; }
    auto set(T value) -> Result<empty, StateError>;
};
template <class T> class Computed {
    Cell<T> owner_;
    friend class Graph;
    explicit Computed(Value<T> value) : owner_(rstd::move(value)) {}

  public:
    Computed(const Computed &) = delete;
    Computed(Computed &&) = default;
    auto operator=(Computed &&) -> Computed & = default;
    auto value() const -> Value<T> { return owner_.value(); }
    auto invalidate() -> Result<empty, StateError> {
        auto opened = value().graph().open();
        if (opened.is_err())
            return Err(opened.unwrap_err());
        auto data = opened.unwrap();
        if (data.get()->busy() || data.get()->transaction || data.get()->write_blocks)
            return Err(StateError::Reentrant);
        auto node = data.get()->resolve(value().id());
        if (!node)
            return Err(StateError::SourceGone);
        node.get()->force = true;
        ++data.get()->epoch;
        data.get()->dirty(node.get()->id);
        data.get()->notify();
        return Ok(empty{});
    }
};
struct CommitResult {
    Version epoch;
    usize changed;
};
class GraphUpdate {
    struct Write {
        NodeId id;
        SharedValue value;
    };
    rstd::rc::Rc<GraphData> data_;
    Vec<Write> writes_;
    friend auto begin_update(GraphView) -> Result<GraphUpdate, StateError>;
    explicit GraphUpdate(rstd::rc::Rc<GraphData> data) : data_(rstd::move(data)) {
        data_.get()->transaction = true;
    }

  public:
    GraphUpdate(const GraphUpdate &) = delete;
    GraphUpdate(GraphUpdate &&) = default;
    ~GraphUpdate() {
        if (data_)
            data_.get()->transaction = false;
    }
    template <class T> auto set(const Cell<T> &cell, T value) -> Result<empty, StateError> {
        if (!data_)
            return Err(StateError::Closed);
        if (auto error = data_.get()->check(); error.is_some())
            return Err(*error);
        if (data_.get()->busy() || data_.get()->write_blocks)
            return Err(StateError::Reentrant);
        if (!cell.owns())
            return Err(StateError::SourceGone);
        if (auto error = data_.get()->validate(cell.value().id()); error.is_some())
            return Err(*error);
        auto node = data_.get()->resolve(cell.value().id());
        if (!node.get()->source)
            return Err(StateError::TypeMismatch);
        for (auto &write : writes_)
            if (write.id == node.get()->id) {
                write.value = store(rstd::move(value));
                return Ok(empty{});
            }
        writes_.push({node.get()->id, store(rstd::move(value))});
        return Ok(empty{});
    }
    template <class T> auto read(const Cell<T> &cell) -> Result<ValueSnapshot<T>, StateError> {
        if (!data_)
            return Err(StateError::Closed);
        if (auto error = data_.get()->check(); error.is_some())
            return Err(*error);
        if (auto error = data_.get()->validate(cell.value().id()); error.is_some())
            return Err(*error);
        auto node = data_.get()->resolve(cell.value().id());
        for (auto &write : writes_)
            if (write.id == node.get()->id)
                return Ok(ValueSnapshot<T>(write.value, node.get()->version, data_.get()->epoch));
        return Ok(ValueSnapshot<T>(node.get()->current, node.get()->version, data_.get()->epoch));
    }
    auto commit() -> Result<CommitResult, StateError> {
        if (!data_)
            return Err(StateError::Closed);
        auto data = data_;
        if (auto error = data.get()->check(); error.is_some())
            return Err(*error);
        if (data.get()->busy() || data.get()->write_blocks)
            return Err(StateError::Reentrant);
        for (auto &write : writes_)
            if (auto error = data.get()->validate(write.id); error.is_some()) {
                data.get()->transaction = false;
                data_ = {};
                return Err(*error);
            }
        data.get()->publishing = true;
        Vec<Write> changed;
        for (auto &write : writes_) {
            auto node = data.get()->resolve(write.id);
            if (!node.get()->equal->operator()(node.get()->current, write.value))
                changed.push(rstd::move(write));
        }
        if (!changed.is_empty()) {
            ++data.get()->epoch;
            ++data.get()->stats.commits;
            for (auto &write : changed) {
                auto node = data.get()->resolve(write.id);
                node.get()->current = rstd::move(write.value);
                ++node.get()->version;
            }
            for (auto &write : changed) {
                auto node = data.get()->resolve(write.id);
                node.get()->dirty = false;
                data.get()->dirty(write.id);
                node.get()->dirty = false;
            }
        }
        data.get()->publishing = false;
        data.get()->transaction = false;
        data_ = {};
        data.get()->notify();
        return Ok(CommitResult{data.get()->epoch, changed.len()});
    }
};
inline auto begin_update(GraphView graph) -> Result<GraphUpdate, StateError> {
    auto opened = graph.open();
    if (opened.is_err())
        return Err(opened.unwrap_err());
    auto data = opened.unwrap();
    if (data.get()->busy() || data.get()->transaction || data.get()->write_blocks)
        return Err(StateError::Reentrant);
    return Ok(GraphUpdate(rstd::move(data)));
}
template <class T> auto Cell<T>::set(T value) -> Result<empty, StateError> {
    auto started = begin_update(value_.graph());
    if (started.is_err())
        return Err(started.unwrap_err());
    auto update = started.unwrap();
    auto written = update.set(*this, rstd::move(value));
    if (written.is_err())
        return written;
    auto committed = update.commit();
    return committed.is_err() ? Result<empty, StateError>(Err(committed.unwrap_err()))
                              : Result<empty, StateError>(Ok(empty{}));
}
struct ValueEqual {
    template <class T> auto operator()(const T &a, const T &b) const -> bool {
        if constexpr (requires { a.as_str(); })
            return a.as_str() == b.as_str();
        else
            return a == b;
    }
};
class Graph {
    rstd::rc::Rc<GraphData> data_;
    template <class T, class Equal>
    auto allocate(Equal comparison, NodeOptions options) -> Result<rstd::rc::Rc<Node>, StateError> {
        if (auto error = data_.get()->check(); error.is_some())
            return Err(*error);
        if (data_.get()->busy() || data_.get()->transaction || data_.get()->write_blocks)
            return Err(StateError::Reentrant);
        if (data_.get()->stats.nodes >= data_.get()->limits.nodes)
            return Err(StateError::Capacity);
        usize slot{};
        while (slot < data_.get()->slots.len() && data_.get()->slots[slot].node)
            ++slot;
        if (slot == data_.get()->slots.len())
            data_.get()->slots.push({});
        NodeId id{data_.get()->id, data_.get()->slots[slot].generation, slot};
        auto equal = [comparison = rstd::move(comparison)](const SharedValue &a,
                                                           const SharedValue &b) {
            return comparison(*static_cast<const T *>(a->address()),
                              *static_cast<const T *>(b->address()));
        };
        auto node = rstd::rc::make_rc<Node>(id, rstd::any::TypeId::of<T>(), rstd::move(equal),
                                            rstd::move(options));
        data_.get()->slots[slot].node = node;
        ++data_.get()->stats.nodes;
        return Ok(rstd::move(node));
    }

  public:
    explicit Graph(GraphLimits limits = {}) : data_(rstd::rc::make_rc<GraphData>(limits)) {}
    Graph(const Graph &) = delete;
    ~Graph() {
        if (!data_.get()->closed) {
            data_.get()->closing = true;
            data_.get()->cleanup();
        }
    }
    auto view() const -> GraphView { return GraphView(data_); }
    auto epoch() const -> Version { return data_.get()->epoch; }
    auto stats() const -> GraphStats { return data_.get()->stats; }
    auto take_diagnostics() -> Vec<StateDiagnostic> {
        return rstd::exchange(data_.get()->diagnostics, {});
    }
    template <class T, class Equal = ValueEqual>
    auto cell(T initial, Equal equal = {}, NodeOptions options = {})
        -> Result<Cell<T>, StateError> {
        auto allocated = allocate<T>(rstd::move(equal), rstd::move(options));
        if (allocated.is_err())
            return Err(allocated.unwrap_err());
        auto node = allocated.unwrap();
        node.get()->source = node.get()->evaluated = true;
        node.get()->current = store(rstd::move(initial));
        return Ok(Cell<T>(Value<T>(view(), node.get()->id)));
    }
    template <class T, class F, class Equal = ValueEqual>
    auto computed(Dependencies dependencies, F evaluate, Equal equal = {}, NodeOptions options = {})
        -> Result<Computed<T>, StateError> {
        if (auto error = data_.get()->check(); error.is_some())
            return Err(*error);
        Vec<NodeId> keys;
        for (auto &dependency : dependencies.values()) {
            if (auto error = data_.get()->validate(dependency.id); error.is_some())
                return Err(*error);
            bool present = false;
            for (auto key : keys)
                present |= key == dependency.id;
            if (!present)
                keys.push(NodeId(dependency.id));
        }
        if (data_.get()->stats.edges + keys.len() > data_.get()->limits.edges)
            return Err(StateError::Capacity);
        auto allocated = allocate<T>(rstd::move(equal), rstd::move(options));
        if (allocated.is_err())
            return Err(allocated.unwrap_err());
        auto node = allocated.unwrap();
        node.get()->dirty = true;
        node.get()->dependencies = rstd::move(keys);
        for (auto key : node.get()->dependencies)
            data_.get()->resolve(key).get()->consumers.push(NodeId(node.get()->id));
        data_.get()->stats.edges += node.get()->dependencies.len();
        node.get()->evaluate =
            Some(Box<rstd::dyn<rstd::FnMut<Result<SharedValue, StateError>(EvalContext &)>>>::make(
                [evaluate = rstd::move(evaluate)](
                    EvalContext &context) mutable -> Result<SharedValue, StateError> {
                    auto result = evaluate(context);
                    if (result.is_err())
                        return Err(result.unwrap_err());
                    return Ok(store<T>(rstd::move(result.unwrap())));
                }));
        return Ok(Computed<T>(Value<T>(view(), node.get()->id)));
    }
    auto begin_update() -> Result<GraphUpdate, StateError> {
        if (auto error = data_.get()->check(); error.is_some())
            return Err(*error);
        return state::begin_update(view());
    }
    auto begin_read() -> Result<ReadBatch, StateError> {
        if (auto error = data_.get()->check(); error.is_some())
            return Err(*error);
        return state::begin_read(view());
    }
    template <class T> auto read(const Value<T> &value) -> Result<ValueSnapshot<T>, StateError> {
        auto batch = begin_read();
        if (batch.is_err())
            return Err(batch.unwrap_err());
        return batch.unwrap().read(value);
    }
    template <class T, class F>
    auto watch(const Value<T> &value, F callback) -> Result<Subscription, StateError> {
        if (auto error = data_.get()->check(); error.is_some())
            return Err(*error);
        return view().watch(value, rstd::move(callback));
    }
    auto close() -> Result<empty, StateError> {
        if (data_.get()->closed)
            return Ok(empty{});
        if (auto error = data_.get()->check(); error.is_some())
            return Err(*error);
        if (data_.get()->busy())
            return Err(StateError::Reentrant);
        data_.get()->closing = true;
        data_.get()->cleanup();
        return Ok(empty{});
    }
};
} // namespace vui::state
