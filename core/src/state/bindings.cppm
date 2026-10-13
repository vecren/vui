export module vui:state.bindings;
import :state;
import :document;
using namespace rstd::prelude;

export namespace vui {
struct StateBindingError {
    Option<UiError> ui;
    Option<state::StateError> state;
};
struct StateBindingDiagnostic {
    state::NodeId source;
    UiId target;
    StateBindingError error;
};
struct StateBindingStats {
    Serial invalidations{}, reads{}, applied{}, skipped{}, cancelled{}, errors{};
    usize active{};
};
class StateUpdateContext {
    UiDocument &document_;
    BindingGuard guard_;

  public:
    StateUpdateContext(UiDocument &document, BindingGuard guard)
        : document_(document), guard_(rstd::move(guard)) {}
    auto document() -> UiDocument & { return document_; }
    auto guard() const -> BindingGuard { return guard_; }
};
struct StateBindingSlot;
struct StateBindingOwner {
    bool open{true}, draining{};
    Option<FrameWaker> waker;
    Vec<rstd::rc::Rc<StateBindingSlot>> slots;
    Vec<StateBindingDiagnostic> diagnostics;
    StateBindingStats stats;
    void wake() {
        if (open && waker.is_some())
            waker->request();
    }
    void report(StateBindingDiagnostic diagnostic) {
        ++stats.errors;
        if (diagnostics.len() >= usize(256))
            diagnostics.remove(usize());
        diagnostics.push(rstd::move(diagnostic));
    }
};
struct StateBindingSlot {
    BindingGuard lifetime;
    rstd::rc::Weak<StateBindingOwner> owner;
    bool pending{true}, cancelled{}, applied{};
    Serial generation{1};
    state::Version version{};
    Option<state::StateError> last_error;
    StateBindingSlot(BindingGuard guard, rstd::rc::Weak<StateBindingOwner> scope)
        : lifetime(rstd::move(guard)), owner(rstd::move(scope)) {}
    virtual ~StateBindingSlot() = default;
    virtual auto graph() const -> state::GraphView = 0;
    virtual auto source_id() const -> state::NodeId = 0;
    virtual auto target_id() const -> UiId { return {}; }
    virtual auto ready() const -> bool { return !cancelled && lifetime.alive(); }
    virtual auto property() const -> bool = 0;
    virtual auto prepare(state::ReadBatch &, UiValueBatch &) -> Result<bool, state::StateError> = 0;
    virtual auto complete(UiDocument &) -> Result<empty, UiError> = 0;
    virtual void discard() = 0;
    virtual void release() = 0;
    auto active() const -> bool { return !cancelled && lifetime.alive(); }
    void cancel() {
        if (cancelled)
            return;
        cancelled = true;
        pending = false;
        release();
        lifetime.disconnect();
        auto scope = owner.upgrade();
        if (scope.is_some())
            ++scope->get()->stats.cancelled;
    }
    void mark() {
        if (!active())
            return;
        auto scope = owner.upgrade();
        if (scope.is_none() || !(*scope)->open)
            return;
        pending = true;
        ++scope->get()->stats.invalidations;
        if (ready())
            scope->get()->wake();
    }
    void failure(state::StateError error) {
        pending = false;
        discard();
        if (last_error == Some(error))
            return;
        last_error = Some(error);
        auto scope = owner.upgrade();
        if (scope.is_some())
            scope->get()->report({source_id(), target_id(), {{}, Some(error)}});
    }
};
template <class T> struct TypedStateSlot : StateBindingSlot {
    state::Value<T> source;
    state::Subscription subscription;
    Option<state::ValueSnapshot<T>> prepared;
    TypedStateSlot(state::Value<T> value, BindingGuard guard,
                   rstd::rc::Weak<StateBindingOwner> scope)
        : StateBindingSlot(rstd::move(guard), rstd::move(scope)), source(rstd::move(value)) {}
    auto graph() const -> state::GraphView override { return source.graph(); }
    auto source_id() const -> state::NodeId override { return source.id(); }
    auto connect(const rstd::rc::Rc<StateBindingSlot> &self, state::Value<T> value)
        -> Result<empty, state::StateError> {
        auto next = value.graph().watch(value, [weak = self.downgrade()](state::Invalidation) {
            auto slot = weak.upgrade();
            if (slot.is_some())
                slot->get()->mark();
        });
        if (next.is_err())
            return Err(next.unwrap_err());
        subscription = next.unwrap();
        source = rstd::move(value);
        ++generation;
        applied = false;
        last_error = None();
        discard();
        mark();
        return Ok(empty{});
    }
    auto stage(state::ReadBatch &batch) -> Result<bool, state::StateError> {
        auto result = batch.read(source);
        if (result.is_err())
            return Err(result.unwrap_err());
        auto snapshot = result.unwrap();
        last_error = None();
        if (applied && snapshot.version() == version) {
            pending = false;
            return Ok(false);
        }
        prepared = Some(rstd::move(snapshot));
        return Ok(true);
    }
    void accepted() {
        if (prepared.is_some())
            version = prepared->version();
        prepared = None();
        pending = false;
        applied = true;
    }
    void discard() override { prepared = None(); }
};
template <class T> struct PropertyStateSlot final : TypedStateSlot<T> {
    UiPropertyTarget<T> target;
    UiValueWriter<T> writer;
    UiTargetSubscription target_watch;
    bool mounted{};
    Serial mount_generation{};
    PropertyStateSlot(state::Value<T> source, BindingGuard guard,
                      rstd::rc::Weak<StateBindingOwner> owner, UiPropertyTarget<T> destination,
                      UiValueWriter<T> output)
        : TypedStateSlot<T>(rstd::move(source), rstd::move(guard), rstd::move(owner)),
          target(rstd::move(destination)), writer(rstd::move(output)) {
        auto status = target.status();
        mounted = status.mounted;
        mount_generation = status.generation;
    }
    auto target_id() const -> UiId override { return target.node(); }
    auto ready() const -> bool override { return this->active() && target.status().mounted; }
    auto property() const -> bool override { return true; }
    void target_changed(UiTargetStatus status) {
        if (!status.valid ||
            (mounted && (!status.mounted || status.generation != mount_generation))) {
            this->cancel();
            return;
        }
        if (!mounted && status.mounted) {
            mounted = true;
            mount_generation = status.generation;
            this->mark();
        }
    }
    auto prepare(state::ReadBatch &reads, UiValueBatch &batch)
        -> Result<bool, state::StateError> override {
        auto staged = this->stage(reads);
        if (staged.is_err())
            return Err(staged.unwrap_err());
        if (staged.unwrap()) {
            batch.set(writer, this->prepared->value());
            return Ok(true);
        }
        return Ok(false);
    }
    auto complete(UiDocument &) -> Result<empty, UiError> override {
        this->accepted();
        return Ok(empty{});
    }
    void release() override {
        this->subscription.disconnect();
        target_watch.disconnect();
        writer.release();
        this->discard();
    }
};
template <class T> struct EffectStateSlot final : TypedStateSlot<T> {
    Box<rstd::dyn<rstd::FnMut<Result<empty, UiError>(StateUpdateContext &, const T &)>>> callback;
    template <class F>
    EffectStateSlot(state::Value<T> value, BindingGuard guard,
                    rstd::rc::Weak<StateBindingOwner> owner, F update)
        : TypedStateSlot<T>(rstd::move(value), rstd::move(guard), rstd::move(owner)),
          callback(
              Box<rstd::dyn<rstd::FnMut<Result<empty, UiError>(StateUpdateContext &, const T &)>>>::
                  make(rstd::move(update))) {}
    auto property() const -> bool override { return false; }
    auto prepare(state::ReadBatch &reads, UiValueBatch &)
        -> Result<bool, state::StateError> override {
        return this->stage(reads);
    }
    auto complete(UiDocument &document) -> Result<empty, UiError> override {
        auto protection = state::protect_writes(this->graph());
        if (protection.is_err())
            return Err(UiError::Cancelled);
        auto guard = protection.unwrap();
        StateUpdateContext context(document, this->lifetime);
        // Keep the callback's immutable input alive if it disconnects its own binding.
        auto snapshot = rstd::move(*this->prepared);
        this->prepared = None();
        auto result = callback->operator()(context, snapshot.value());
        if (!this->cancelled) {
            this->version = snapshot.version();
            this->pending = false;
            this->applied = true;
        }
        return result;
    }
    void release() override {
        this->subscription.disconnect();
        this->discard();
    }
};
template <class T> class StateBinding {
    Binding lifetime_;
    rstd::rc::Rc<StateBindingSlot> slot_;
    friend class UiStateBindings;
    StateBinding(Binding lifetime, rstd::rc::Rc<StateBindingSlot> slot)
        : lifetime_(rstd::move(lifetime)), slot_(rstd::move(slot)) {}

  public:
    StateBinding() = default;
    StateBinding(const StateBinding &) = delete;
    StateBinding(StateBinding &&) = default;
    auto operator=(StateBinding &&other) -> StateBinding & {
        if (this != &other) {
            disconnect();
            lifetime_ = rstd::move(other.lifetime_);
            slot_ = rstd::move(other.slot_);
        }
        return *this;
    }
    ~StateBinding() { disconnect(); }
    auto guard() const -> BindingGuard { return lifetime_.guard(); }
    auto alive() const -> bool { return lifetime_.alive() && slot_ && slot_->active(); }
    void disconnect() {
        if (slot_)
            slot_.get()->cancel();
        lifetime_.disconnect();
    }
    auto replace_source(state::Value<T> source) -> Result<empty, StateBindingError> {
        if (!alive())
            return Err(StateBindingError{Some(UiError::Cancelled), {}});
        auto result =
            static_cast<TypedStateSlot<T> *>(slot_.get())->connect(slot_, rstd::move(source));
        if (result.is_err())
            return Err(StateBindingError{{}, Some(result.unwrap_err())});
        return Ok(empty{});
    }
};
struct StateDrainResult {
    usize groups{};
    bool pending{};
};
class UiStateBindings {
    UiDocument &document_;
    BindingGuard document_lifetime_{document_.lifetime()};
    rstd::rc::Rc<StateBindingOwner> owner_{rstd::rc::make_rc<StateBindingOwner>()};
    auto available() -> Option<UiError> {
        if (!owner_->open || !document_lifetime_.alive())
            return Some(UiError::Cancelled);
        if (owner_->slots.len() >= usize(4096))
            return Some(UiError::Limit);
        return None();
    }
    void sweep() {
        for (auto &slot : owner_.get()->slots)
            if (!slot->active())
                slot.get()->cancel();
        owner_.get()->slots.retain([](const auto &slot) { return slot->active(); });
        owner_.get()->stats.active = owner_->slots.len();
    }

  public:
    explicit UiStateBindings(UiDocument &document) : document_(document) {}
    UiStateBindings(const UiStateBindings &) = delete;
    ~UiStateBindings() { close(); }
    template <class T>
    auto bind(UiPropertyTarget<T> target, state::Value<T> value)
        -> Result<StateBinding<T>, StateBindingError> {
        if (auto error = available(); error.is_some())
            return Err(StateBindingError{error, {}});
        auto claimed = document_.claim_value_writer(target);
        if (claimed.is_err())
            return Err(StateBindingError{Some(claimed.unwrap_err()), {}});
        auto lifetime = document_.bindings().make();
        rstd::rc::Rc<StateBindingSlot> concrete(new PropertyStateSlot<T>(
            value, lifetime.guard(), owner_.downgrade(), target, claimed.unwrap()));
        auto typed = static_cast<PropertyStateSlot<T> *>(concrete.get());
        auto connected = typed->connect(concrete, rstd::move(value));
        if (connected.is_err())
            return Err(StateBindingError{{}, Some(connected.unwrap_err())});
        auto observed =
            document_.observe_target(target, [weak = concrete.downgrade()](UiTargetStatus status) {
                auto slot = weak.upgrade();
                if (slot.is_some())
                    static_cast<PropertyStateSlot<T> *>(slot->get())->target_changed(status);
            });
        if (observed.is_err())
            return Err(StateBindingError{Some(observed.unwrap_err()), {}});
        typed->target_watch = observed.unwrap();
        owner_.get()->slots.push(concrete.clone());
        owner_.get()->stats.active = owner_->slots.len();
        return Ok(StateBinding<T>(rstd::move(lifetime), rstd::move(concrete)));
    }
    template <class T, class F>
    auto watch(state::Value<T> value, F callback) -> Result<StateBinding<T>, StateBindingError> {
        if (auto error = available(); error.is_some())
            return Err(StateBindingError{error, {}});
        auto lifetime = document_.bindings().make();
        rstd::rc::Rc<StateBindingSlot> concrete(new EffectStateSlot<T>(
            value, lifetime.guard(), owner_.downgrade(), rstd::move(callback)));
        auto connected =
            static_cast<TypedStateSlot<T> *>(concrete.get())->connect(concrete, rstd::move(value));
        if (connected.is_err())
            return Err(StateBindingError{{}, Some(connected.unwrap_err())});
        owner_.get()->slots.push(concrete.clone());
        owner_.get()->stats.active = owner_->slots.len();
        return Ok(StateBinding<T>(rstd::move(lifetime), rstd::move(concrete)));
    }
    auto pending() const -> bool {
        if (!owner_->open || !document_lifetime_.alive())
            return false;
        for (auto &slot : owner_->slots)
            if (slot->active() && slot->pending && slot->ready())
                return true;
        return false;
    }
    auto drain(bool strict = false, usize group_budget = usize(16))
        -> Result<StateDrainResult, UiError> {
        if (!owner_->open || !document_lifetime_.alive())
            return Err(UiError::Cancelled);
        if (owner_->draining)
            return Err(UiError::Routing);
        owner_.get()->draining = true;
        struct Guard {
            StateBindingOwner &owner;
            ~Guard() { owner.draining = false; }
        } guard{*owner_.get()};
        sweep();
        Vec<state::GraphView> groups;
        for (auto &slot : owner_->slots)
            if (slot->pending && slot->ready()) {
                bool found = false;
                for (auto group : groups)
                    found |= group.id() == slot->graph().id();
                if (!found)
                    groups.push(slot->graph());
            }
        StateDrainResult result;
        for (auto group : groups) {
            if (!strict && result.groups >= group_budget)
                break;
            ++result.groups;
            struct Prepared {
                rstd::rc::Rc<StateBindingSlot> slot;
                Serial generation;
            };
            Vec<rstd::rc::Rc<StateBindingSlot>> active, properties;
            Vec<Prepared> staged;
            for (auto &slot : owner_->slots)
                if (slot->active() && slot->pending && slot->ready() &&
                    slot->graph().id() == group.id())
                    active.push(slot.clone());
            auto started = state::begin_read(group);
            if (started.is_err()) {
                for (auto &slot : active)
                    slot.get()->failure(started.unwrap_err());
                continue;
            }
            auto reads = started.unwrap();
            auto epoch = reads.epoch();
            UiValueBatch batch;
            for (auto &slot : active) {
                ++owner_.get()->stats.reads;
                auto value = slot.get()->prepare(reads, batch);
                if (value.is_err())
                    slot.get()->failure(value.unwrap_err());
                else if (!value.unwrap())
                    ++owner_.get()->stats.skipped;
                else {
                    staged.push({slot.clone(), slot->generation});
                    if (slot->property())
                        properties.push(slot.clone());
                }
            }
            reads.finish();
            auto current = group.epoch();
            if (current.is_err() || current.unwrap() != epoch) {
                for (auto &item : staged)
                    item.slot.get()->discard();
                continue;
            }
            auto applied = document_.apply_values(rstd::move(batch));
            if (applied.is_err()) {
                auto error = applied.unwrap_err();
                if (error.index < properties.len()) {
                    auto failed = properties[error.index];
                    owner_.get()->report(
                        {failed->source_id(), failed->target_id(), {Some(error.error), {}}});
                    failed.get()->cancel();
                }
                for (auto &item : staged)
                    item.slot.get()->discard();
                continue;
            }
            owner_.get()->stats.applied += applied.unwrap().to_primitive();
            for (auto &item : staged)
                if (item.slot->active() && item.slot->generation == item.generation) {
                    auto slot = item.slot;
                    auto completed = slot.get()->complete(document_);
                    if (completed.is_err()) {
                        owner_.get()->report({slot->source_id(),
                                              slot->target_id(),
                                              {Some(completed.unwrap_err()), {}}});
                        slot.get()->cancel();
                    }
                }
        }
        sweep();
        result.pending = pending();
        if (result.pending)
            owner_.get()->wake();
        return Ok(result);
    }
    void set_waker(Option<FrameWaker> waker) {
        owner_.get()->waker = rstd::move(waker);
        if (pending())
            owner_.get()->wake();
    }
    auto stats() const -> StateBindingStats { return owner_->stats; }
    auto take_diagnostics() -> Vec<StateBindingDiagnostic> {
        return rstd::exchange(owner_.get()->diagnostics, {});
    }
    void close() {
        if (!owner_->open)
            return;
        owner_.get()->open = false;
        owner_.get()->waker = None();
        for (auto &slot : owner_.get()->slots)
            slot.get()->cancel();
        owner_.get()->slots.clear();
        owner_.get()->stats.active = usize();
    }
};
} // namespace vui
