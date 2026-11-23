export module vui:input.router;
import :input.hit_test;
import :input.protocol;
import :interaction.arena;
import :input.scopes;
import :scene.visual;
using namespace rstd::prelude;

export namespace vui {
struct ConnectionId {
    Serial owner{}, value{};
    friend auto operator==(const ConnectionId &, const ConnectionId &) -> bool = default;
};
struct InputAvailability {
    bool mounted{}, visible{}, enabled{}, window_focused{}, pointer{};
    Serial mount_generation{};
    auto operator==(const InputAvailability &) const -> bool = default;
};
class EventRouter;
class EventContext;
struct Behavior {
    template <class Self, class Delegate = void> struct Api {
        using Trait = Behavior;
        void handle(EventContext &context, const RoutedEvent &event) {
            rstd::trait_call<0>(this, context, event);
        }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::handle>;
};
class EventContext {
    EventRouter &router_;
    const RoutedEvent &event_;
    bool &stopped_;
    bool &prevented_;
    Point &remaining_scroll_;
    const Option<BindingGuard> *scroll_behavior_{};
    using ScrollCompletion = rstd::dyn<rstd::FnMut<Point()>>;
    auto defer_scroll_completion(Box<ScrollCompletion> completion) -> Result<empty, InputError>;
    friend class EventRouter;
    EventContext(EventRouter &router, const RoutedEvent &event, bool &stopped, bool &prevented,
                 Point &remaining_scroll)
        : router_(router), event_(event), stopped_(stopped), prevented_(prevented),
          remaining_scroll_(remaining_scroll) {}

  public:
    auto document() -> UiDocument &;
    auto visual(UiId id) const -> const UiVisual *;
    auto content_position() const -> Option<Point>;
    auto completed_content(UiId id, Serial source_id) const -> Result<Option<FrameContent>, FrameError>;
    auto completed_image(UiId id, Serial image_id) const -> Result<Option<FrameImage>, FrameError>;
    auto scroll_remaining() const -> Point { return remaining_scroll_; }
    auto consume_scroll(Point amount) -> Result<empty, InputError>;
    // The final wheel behavior at a node may settle its layout before reporting consumption.
    auto can_defer_scroll() const -> bool;
    template <class F> auto defer_scroll(F completion) -> Result<empty, InputError> {
        if (!can_defer_scroll()) return Err(InputError::Unavailable);
        return defer_scroll_completion(Box<ScrollCompletion>::make(rstd::move(completion)));
    }
    void stop_propagation() { stopped_ = true; }
    void prevent_default() { prevented_ = true; }
    auto default_prevented() const -> bool { return prevented_; }
    auto hit_within() const -> bool;
    auto capture_pointer() -> Result<empty, InputError>;
    auto release_pointer() -> Result<empty, InputError>;
    auto focus() -> Result<empty, InputError>;
    auto restart_text_input() -> Result<empty, InputError>;
    auto read_clipboard() -> Result<String, ClipboardError>;
    auto request_clipboard() -> Result<Serial, ClipboardError>;
    auto request_clipboard_write(ref<str>) -> Result<Serial, ClipboardError>;
    auto write_clipboard(ref<str>) -> Result<empty, ClipboardError>;
    template <class F> auto defer(F callback) -> Result<Serial, UiError> {
        return document().defer(rstd::move(callback));
    }
};

class EventRouter {
    friend class EventContext;
    friend class UiSession;
    friend class Semantics;
    struct BehaviorEntry {
        UiId target;
        NodeStateKey role;
        explicit BehaviorEntry(UiId id, NodeStateKey key = {}) : target(id), role(key) {}
        Option<BindingGuard> binding;
        InputDelivery delivery{InputDelivery::EverySample};
        virtual ~BehaviorEntry() = default;
        virtual void invoke(EventContext &, const RoutedEvent &) = 0;
    };
    template <class T> struct NodeBehavior : BehaviorEntry {
        NodeBehavior(UiId id, NodeStateKey key) : BehaviorEntry(id, key) {}
        void invoke(EventContext &context, const RoutedEvent &event) override {
            if (auto state = context.document().template state<T>(this->target, this->role))
                rstd::as<vui::Behavior>(*state).handle(context, event);
        }
    };
    struct Handler {
        ConnectionId id;
        UiId target;
        EventType type;
        RoutePhase phase;
        bool active{true}, owns_binding{};
        ScopeAccess access{ScopeAccess::Scoped};
        Option<BindingGuard> binding;
        Handler(ConnectionId id, UiId target, EventType type, RoutePhase phase)
            : id(id), target(target), type(type), phase(phase) {}
        virtual ~Handler() = default;
        virtual void invoke(EventContext &, const RoutedEvent &) = 0;
    };
    template <class F> struct Callback : Handler {
        F function;
        Callback(ConnectionId id, UiId target, EventType type, RoutePhase phase, F function)
            : Handler(id, target, type, phase), function(rstd::move(function)) {}
        void invoke(EventContext &context, const RoutedEvent &event) override {
            function(context, event);
        }
    };
    struct Pointer {
        Index id{};
        Point position{};
        PointerKind kind{};
        unsigned buttons{};
        Option<UiId> hover, capture, sequence_target;
        DeviceId device{};
        SequenceId sequence{};
        bool contact{}, claimed{}, relinquished{};
        SampleTime sample_time;
        NativeCaptureResult native_capture{NativeCaptureResult::Unsupported};
        bool inside_window{true};
        Vec<UiId> hover_path;
        bool hover_dirty{};
    };
    enum class CommandType { Capture, Release, Focus, RestartText, CancelSubtree };
    struct Command {
        CommandType type;
        Index pointer{};
        Option<UiId> target;
        InputEvent input;
        DeviceId device{};
    };
    UiDocument &document_;
    Serial owner_{new_session()}, next_handler_{1};
    Vec<Box<Handler>> handlers_;
    Vec<Box<BehaviorEntry>> behaviors_;
    template<class T> struct AvailabilityObserver {
        UiId target;
        BindingGuard guard;
        T available{};
        Box<rstd::dyn<rstd::FnMut<void(const T &)>>> changed;
    };
    Vec<AvailabilityObserver<bool>> pointer_observers_;
    Vec<AvailabilityObserver<InputAvailability>> input_observers_;
    bool availability_dirty_{};
    struct NodeRoutes {
        Vec<usize> behaviors, handlers;
    };
    Vec<NodeRoutes> node_routes_;
    bool routes_dirty_{true}, prune_requested_{true};
    usize every_sample_{}, motion_handlers_{};
    void index_routes();
    auto node_routes(UiId id) -> NodeRoutes * {
        index_routes();
        return usize(id.slot) < node_routes_.len() ? &node_routes_[usize(id.slot)] : nullptr;
    }
    Vec<Pointer> pointers_;
    Vec<PointerContact> contacts_;
    InputArena arena_{document_};
    InputScopes scopes_{document_};
    SequenceId next_sequence_{1};
    Vec<Command> commands_;
    struct Transfer {
        ParticipantId participant;
        Vec<PointerKey> pointers;
    };
    Vec<Transfer> transfers_;
    Vec<Vec<PointerKey>> releases_;
    Option<InputSnapshot> update_;
    Option<UiFrame> completed_frame_;
    auto completed_node(UiId id) const -> bool;
    Serial completed_frame_id_{};
    Option<UiDocument::RouteScope> scope_;
    Option<UiId> focus_;
    Serial focus_revision_{};
    bool sharing_{};
    Vec<UiId> shared_targets_;
    Vec<UiId> route_nodes_, owner_targets_;
    struct RouteState {
        InputEvent input;
        UiId target;
        Serial cutoff;
        Point remaining;
        bool stopped{}, prevented{};
    };
    struct WheelCompletion {
        Box<EventContext::ScrollCompletion> complete;
        Option<BindingGuard> guard;
    };
    struct DeferredWheel {
        InputSnapshot snapshot;
        Vec<UiId> nodes;
        usize cursor;
        RouteState route;
        WheelCompletion completion;
    };
    bool wheel_deferral_{};
    Option<WheelCompletion> wheel_completion_;
    Option<DeferredWheel> deferred_wheel_;
    bool busy_{}, controls_{true}, window_focused_{true}, closed_{};
    Option<InputError> failure_;
    InputWork work_;
    Option<Box<rstd::dyn<rstd::FnMut<void(EventRouter &, const InputEvent &)>>>> navigation_;
    struct FocusSearchProvider {
        UiId root;
        BindingGuard guard;
        Box<rstd::dyn<rstd::FnMut<bool(bool)>>> extend;
        Box<rstd::dyn<rstd::FnMut<bool()>>> release;
        Box<rstd::dyn<rstd::FnMut<bool(const InputSnapshot &, UiId)>>> accepts;
    };
    Vec<FocusSearchProvider> focus_providers_;
    struct FocusSearch {
        UiId origin, root;
        Serial revision, mount;
        bool backwards{}, selected{};
        Index probes{};
    };
    Option<FocusSearch> focus_search_;
    Option<Box<rstd::dyn<rstd::FnMut<NativeCaptureResult(PointerKind, bool)>>>> native_capture_;
    Option<Box<rstd::dyn<rstd::FnMut<NativeTextInputResult(Option<TextInputRequest>)>>>> native_text_input_;
    Option<Box<rstd::dyn<rstd::FnMut<NativeTextInputResult(Option<TextInputState>)>>>> native_text_state_;
    Option<TextInputRequest> text_request_;
    Option<UiId> text_client_;
    NativeTextInputResult text_result_{NativeTextInputResult::Unsupported};
    struct SurroundingEntry { UiId client; TextSurrounding value; TextChangeCause cause; };
    Vec<SurroundingEntry> surroundings_;
    Option<TextInputState> delivered_text_state_;
    Option<Box<rstd::dyn<rstd::FnMut<Result<String, ClipboardError>()>>>> clipboard_read_;
    Option<Box<rstd::dyn<rstd::FnMut<Result<empty, ClipboardError>(ref<str>)>>>> clipboard_write_;
    Option<Box<rstd::dyn<rstd::FnMut<Result<empty, ClipboardError>(Serial)>>>> clipboard_request_;
    Option<Box<rstd::dyn<rstd::FnMut<Result<empty, ClipboardError>(Serial, Serial, ref<str>)>>>> clipboard_write_request_;
    struct ClipboardPending { Serial id; UiId target; Serial focus_revision; bool write{}; };
    Vec<ClipboardPending> clipboard_pending_;
    static void path(const InputSnapshot &update, UiId id, Vec<UiId> &result);
    static auto path(const InputSnapshot &update, UiId id) -> Vec<UiId>;
    auto eligible(const InputSnapshot &update, UiId id, bool pointer,
                  bool require_focus = true) const -> bool {
        return input_eligible(update, id, pointer, require_focus) &&
               scopes_.allows(update, id, pointer);
    }
    auto hit_at(const InputSnapshot &snapshot, Point position) const -> Option<UiId> {
        auto target = HitTest::target_at(snapshot, position);
        return target.is_some() && scopes_.allows(snapshot, *target, true) ? target : None<UiId>();
    }
    static auto local(const InputSnapshot &update, slice<UiId> route, usize index, Point point)
        -> Point;
    void refresh_contacts();
    auto pointer(Index id, DeviceId device = 0) -> Pointer *;
    void behavior(EventContext &context, const RoutedEvent &event, bool prevented);
    void invoke(const InputSnapshot &update, slice<UiId> nodes, usize index, RoutePhase phase, RouteState &route);
    void bubble(const InputSnapshot &update, Vec<UiId> &nodes, usize cursor, RouteState &route);
    auto route(const InputSnapshot &update, UiId target, InputEvent input, bool direct = false)
        -> bool;
    auto dispatch_for_session(InputEvent input) -> Result<empty, InputError>;
    auto resume_wheel() -> Result<empty, InputError>;
    auto allows_semantics(const InputSnapshot &snapshot, UiId target) const -> bool;
    auto focus_neighbor(bool backwards, bool wrap) -> Option<UiId>;
    auto focus_provider(UiId root) -> FocusSearchProvider *;
    auto enclosing_focus_provider(UiId origin, Option<UiId> neighbor, Option<UiId> after = {}) -> FocusSearchProvider *;
    auto continue_focus_search() -> Result<bool, InputError>;
    auto release_focus_search() -> bool;
    auto focus_search_pending() const -> bool { return focus_search_ && !focus_search_->selected; }
    auto dispatch_semantics(UiId target, SemanticsAction action, Option<SemanticsPosition>) -> Result<bool, InputError>;
    static auto event(EventType type, const Pointer &pointer) -> InputEvent;
    void hover(Pointer &pointer, Option<UiId> target);
    void capture_native(Pointer &pointer, bool enabled);
    void release(Pointer &pointer, bool shared = true);
    void cancel(Pointer &pointer, CancelReason reason = CancelReason::Explicit);
    void change_focus(Option<UiId> target, CancelReason text_reason = CancelReason::FocusLost);
    void cancel_subtree_inputs(UiId root, bool current = false);
    auto current_text_state() const -> Option<TextInputState>;
    void notify_text_input(bool force);
    void cancel_text_input(CancelReason reason);
    void refresh_text_input(bool force = false);
    void apply_transfer(const OwnershipChange &change);
    void drain();
    void begin();
    void dispatch_availability(auto &observers,auto read) {
        for (auto &observer:observers) {
            if (!observer.guard.alive()) continue;
            auto available=read(observer.target);
            if (available!=observer.available) {observer.available=available;observer.changed->operator()(available);}
            if (!document_.valid(observer.target)) observer.guard.disconnect();
        }
        observers.retain([](const auto &observer){return observer.guard.alive();});
    }
    auto end() -> Result<empty, InputError>;
    auto command(Command value) -> Result<empty, InputError>;
    auto lost_receiver(UiId target) -> bool;
    auto same_mount(const InputSnapshot &next, UiId id, bool pointer) const -> bool;

  public:
    class BehaviorEdit {
        struct Role { UiId target; NodeStateKey key; };
        struct Order { UiId target; Vec<NodeStateKey> roles; };
        Vec<Box<BehaviorEntry>> entries_;
        Vec<BindingGuard> bindings_;
        Vec<Role> retired_;
        Vec<Order> orders_;
        bool published_{};
        friend class EventRouter;
      public:
        BehaviorEdit() = default;
        BehaviorEdit(const BehaviorEdit &) = delete;
        BehaviorEdit(BehaviorEdit &&) = default;
        ~BehaviorEdit() { if (!published_) for (auto &binding : bindings_) binding.disconnect(); }
    };
    template <class T> requires rstd::Impled<T, vui::Behavior>
    auto prepare_behavior(BehaviorEdit &edit, const UiDocument::PreparedEdit &document, UiNodeRef ref, NodeStateKey role = {})
        -> Result<empty, InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || document_.routing()) return Err(InputError::Reentrant);
        auto target = document.id(ref);
        if (target.session != document_.session() || document.state_type(ref, role) != Some(rstd::any::TypeId::of<T>()))
            return Err(InputError::InvalidId);
        for (const auto &entry : behaviors_) if (entry->target == target && entry->role == role && (entry->binding.is_none() || entry->binding->alive())) return Err(InputError::InvalidSequence);
        for (const auto &entry : edit.entries_) if (entry->target == target && entry->role == role) return Err(InputError::InvalidSequence);
        if (behaviors_.len() + edit.entries_.len() >= usize(65536)) return Err(InputError::Limit);
        auto entry = Box<NodeBehavior<T>>::make(target, role);
        edit.entries_.push(Box<BehaviorEntry>::from_raw(rstd::move(entry).into_raw().template cast<BehaviorEntry>()));
        behaviors_.reserve(edit.entries_.len());
        return Ok(empty{});
    }
    template <class T> requires rstd::Impled<T, vui::Behavior>
    auto prepare_bind_behavior(BehaviorEdit &edit, const UiDocument::PreparedEdit &document, UiNodeRef ref,
                               rstd::rc::Weak<T> receiver, InputDelivery delivery = InputDelivery::EverySample, NodeStateKey role = {})
        -> Result<Binding, InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || document_.routing()) return Err(InputError::Reentrant);
        auto target = document.id(ref);
        if (target.session != document_.session() || receiver.expired()) return Err(InputError::InvalidId);
        for (const auto &entry : behaviors_) if (entry->target == target && entry->role == role &&
            (entry->binding.is_none() || entry->binding->alive())) return Err(InputError::InvalidSequence);
        for (const auto &entry : edit.entries_) if (entry->target == target && entry->role == role) return Err(InputError::InvalidSequence);
        if (behaviors_.len() + edit.entries_.len() >= usize(65536)) return Err(InputError::Limit);
        auto binding = document_.bindings().make(receiver);
        struct Entry : BehaviorEntry {
            rstd::rc::Weak<T> receiver;
            Entry(UiId id, rstd::rc::Weak<T> value, BindingGuard guard, NodeStateKey key, InputDelivery delivery)
                : BehaviorEntry(id, key), receiver(rstd::move(value)) { this->binding = Some(rstd::move(guard)); this->delivery = delivery; }
            void invoke(EventContext &context, const RoutedEvent &event) override {
                auto value = receiver.upgrade();
                if (this->binding->alive() && value.is_some()) rstd::as<vui::Behavior>(*value->get()).handle(context, event);
            }
        };
        auto entry = Box<Entry>::make(target, rstd::move(receiver), binding.guard(), role, delivery);
        edit.entries_.push(Box<BehaviorEntry>::from_raw(rstd::move(entry).into_raw().template cast<BehaviorEntry>()));
        edit.bindings_.push(binding.guard());
        behaviors_.reserve(edit.entries_.len());
        return Ok(rstd::move(binding));
    }
    auto retire_behavior(BehaviorEdit &edit, UiId target, NodeStateKey role) -> Result<empty, InputError>;
    auto prepare_behavior_order(BehaviorEdit &edit, UiId target, Vec<NodeStateKey> roles) -> Result<empty, InputError>;
    auto publish_behaviors(BehaviorEdit edit) -> Result<empty, InputError>;
    explicit EventRouter(UiDocument &document) : document_(document) {}
    ~EventRouter();
    void begin_input_batch() {
        if (!busy_)
            prune_requested_ = true;
    }
    EventRouter(const EventRouter &) = delete;
    auto operator=(const EventRouter &) -> EventRouter & = delete;
    template <class T>
        requires rstd::Impled<T, vui::Behavior>
    auto attach_behavior(UiId target, NodeStateKey role = {}) -> Result<empty, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (busy_ || document_.routing())
            return Err(InputError::Reentrant);
        if (!document_.state<T>(target, role))
            return Err(InputError::InvalidId);
        routes_dirty_ = true;
        prune_requested_ = true;
        for (const auto &behavior : behaviors_)
            if (behavior->target == target && behavior->role == role && (behavior->binding.is_none() || behavior->binding->alive()))
                return Err(InputError::InvalidSequence);
        if (behaviors_.len() >= usize(65536))
            return Err(InputError::Limit);
        auto behavior = Box<NodeBehavior<T>>::make(target, role);
        behaviors_.push(Box<BehaviorEntry>::from_raw(
            rstd::move(behavior).into_raw().template cast<BehaviorEntry>()));
        return Ok(empty{});
    }
    template <class T>
        requires rstd::Impled<T, vui::Behavior>
    auto bind_behavior(UiId target, rstd::rc::Weak<T> receiver,
                       InputDelivery delivery = InputDelivery::EverySample, NodeStateKey role = {})
        -> Result<Binding, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (busy_ || document_.routing())
            return Err(InputError::Reentrant);
        if (!document_.valid(target))
            return Err(InputError::InvalidId);
        for (const auto &entry : behaviors_)
            if (entry->target == target && entry->role == role && (entry->binding.is_none() || entry->binding->alive()))
                return Err(InputError::InvalidSequence);
        if (behaviors_.len() >= usize(65536))
            return Err(InputError::Limit);
        routes_dirty_ = true;
        prune_requested_ = true;
        auto binding = document_.bindings().make(receiver);
        struct Entry : BehaviorEntry {
            rstd::rc::Weak<T> receiver;
            Entry(UiId id, rstd::rc::Weak<T> value, BindingGuard guard, NodeStateKey role)
                : BehaviorEntry(id, role), receiver(rstd::move(value)) {
                this->binding = Some(BindingGuard(guard));
            }
            void invoke(EventContext &context, const RoutedEvent &event) override {
                auto value = receiver.upgrade();
                if (this->binding->alive() && value.is_some())
                    rstd::as<vui::Behavior>(*value->get()).handle(context, event);
            }
        };
        auto entry = Box<Entry>::make(target, rstd::move(receiver), binding.guard(), role);
        entry->delivery = delivery;
        behaviors_.push(Box<BehaviorEntry>::from_raw(
            rstd::move(entry).into_raw().template cast<BehaviorEntry>()));
        return Ok(rstd::move(binding));
    }
    template <class T, class F>
    auto bind_on(UiId target, EventType type, rstd::rc::Weak<T> receiver, F function,
                 RoutePhase phase = RoutePhase::Target, ScopeAccess access = ScopeAccess::Scoped)
        -> Result<Binding, InputError> {
        auto binding = document_.bindings().make(receiver);
        auto guard = binding.guard();
        auto result = on(
            target, type,
            [receiver = rstd::move(receiver), guard,
             function = rstd::move(function)](auto &context, const auto &event) mutable {
                auto value = receiver.upgrade();
                if (guard.alive() && value.is_some())
                    function(*value->get(), context, event);
            },
            phase, access);
        if (result.is_err())
            return Err(result.unwrap_err());
        for (auto &handler : handlers_)
            if (handler->id == result.unwrap()) {
                handler->binding = Some(BindingGuard(guard));
                handler->owns_binding = true;
            }
        return Ok(rstd::move(binding));
    }
    template <class F>
    auto on(UiId target, EventType type, F callback, RoutePhase phase = RoutePhase::Target,
            ScopeAccess access = ScopeAccess::Scoped) -> Result<ConnectionId, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (!document_.valid(target))
            return Err(InputError::InvalidId);
        if (next_handler_ == ~Serial{} || handlers_.len() >= usize(65536))
            return Err(InputError::Limit);
        routes_dirty_ = true;
        prune_requested_ = true;
        ConnectionId id{owner_, next_handler_++};
        auto handler = Box<Callback<F>>::make(id, target, type, phase, rstd::move(callback));
        handler->access = access;
        handlers_.push(
            Box<Handler>::from_raw(rstd::move(handler).into_raw().template cast<Handler>()));
        return Ok(id);
    }
    template <class F>
    auto on(UiId target, EventType type, BindingGuard guard, F callback,
            RoutePhase phase = RoutePhase::Target, ScopeAccess access = ScopeAccess::Scoped)
        -> Result<ConnectionId, InputError> {
        auto result = on(
            target, type,
            [guard, callback = rstd::move(callback)](auto &context, const auto &event) mutable {
                if (guard.alive())
                    callback(context, event);
            },
            phase, access);
        if (result.is_ok())
            for (auto &handler : handlers_)
                if (handler->id == result.unwrap())
                    handler->binding = Some(BindingGuard(guard));
        return result;
    }
    auto disconnect(ConnectionId id) -> bool;
    template <class F> void set_native_capture(F callback) {
        native_capture_ =
            Some(Box<rstd::dyn<rstd::FnMut<NativeCaptureResult(PointerKind, bool)>>>::make(
                rstd::move(callback)));
    }
    // Keep the host alive until this router is closed, destroyed, or explicitly unbound.
    template <class F> auto set_native_text_input(F callback) -> Result<empty, InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
        if (native_text_input_.is_some()) (*native_text_input_)->operator()(None<TextInputRequest>());
        if (native_text_state_.is_some()) (*native_text_state_)->operator()(None<TextInputState>());
        native_text_state_ = None(); delivered_text_state_ = None();
        native_text_input_ = Some(Box<rstd::dyn<rstd::FnMut<NativeTextInputResult(Option<TextInputRequest>)>>>::make(rstd::move(callback)));
        text_result_ = (*native_text_input_)->operator()(text_request_);
        return Ok(empty{});
    }
    template <class F> auto set_native_text_input_state(F callback) -> Result<empty, InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
        if (native_text_input_.is_some()) (*native_text_input_)->operator()(None<TextInputRequest>());
        if (native_text_state_.is_some()) (*native_text_state_)->operator()(None<TextInputState>());
        native_text_input_ = None(); delivered_text_state_ = None();
        native_text_state_ = Some(Box<rstd::dyn<rstd::FnMut<NativeTextInputResult(Option<TextInputState>)>>>::make(rstd::move(callback)));
        notify_text_input(true);
        return Ok(empty{});
    }
    auto clear_native_text_input() -> Result<empty, InputError>;
    auto text_input_request() const -> Option<TextInputRequest> { return text_request_; }
    auto text_input_state() const -> Option<TextInputState> { return current_text_state(); }
    auto text_input_client() const -> Option<UiId> { return text_client_; }
    auto text_input_result() const -> NativeTextInputResult { return text_result_; }
    auto set_text_input_surrounding(UiId client, TextSurrounding surrounding,
                                    TextChangeCause cause = TextChangeCause::Other) -> Result<empty, InputError>;
    auto restart_text_input(UiId client) -> Result<empty, InputError> {
        if (text_client_ != Some(client)) return Err(InputError::Unavailable);
        return command({CommandType::RestartText, 0, Some(client)});
    }
    template <class Read, class Write>
    auto set_clipboard(Read read, Write write) -> Result<empty, InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
        clipboard_request_ = None(); clipboard_write_request_ = None(); clipboard_pending_.clear();
        clipboard_read_ = Some(Box<rstd::dyn<rstd::FnMut<Result<String, ClipboardError>()>>>::make(rstd::move(read)));
        clipboard_write_ = Some(Box<rstd::dyn<rstd::FnMut<Result<empty, ClipboardError>(ref<str>)>>>::make(rstd::move(write)));
        return Ok(empty{});
    }
    template <class Request>
    auto set_clipboard_request(Request request) -> Result<empty, InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
        clipboard_pending_.retain([](const ClipboardPending &pending) { return pending.write; });
        clipboard_request_ = Some(Box<rstd::dyn<rstd::FnMut<Result<empty, ClipboardError>(Serial)>>>::make(rstd::move(request)));
        return Ok(empty{});
    }
    template <class Request>
    auto set_clipboard_write_request(Request request) -> Result<empty, InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
        clipboard_pending_.retain([](const ClipboardPending &pending) { return !pending.write; });
        clipboard_write_request_ = Some(Box<rstd::dyn<rstd::FnMut<Result<empty, ClipboardError>(Serial, Serial, ref<str>)>>>::make(rstd::move(request)));
        return Ok(empty{});
    }
    auto clear_clipboard() -> Result<empty, InputError>;
    auto read_clipboard() -> Result<String, ClipboardError>;
    auto request_clipboard(UiId target) -> Result<Serial, ClipboardError>;
    auto write_clipboard(ref<str> text) -> Result<empty, ClipboardError>;
    auto request_clipboard_write(UiId target, Serial activation, ref<str> text) -> Result<Serial, ClipboardError>;
    auto native_capture(Index id, DeviceId device = 0) const -> NativeCaptureResult;
    template <class F> void set_navigation(F policy) {
        navigation_ =
            Some(Box<rstd::dyn<rstd::FnMut<void(EventRouter &, const InputEvent &)>>>::make(
                rstd::move(policy)));
    }
    auto pointer_available(UiId id) const -> bool;
    auto input_availability(UiId id) -> InputAvailability;
    template <class F> auto on_input_availability_changed(UiId target,F callback) -> Result<Binding,InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || document_.routing()) return Err(InputError::Reentrant);
        if (!document_.valid(target)) return Err(InputError::InvalidId);
        input_observers_.retain([](const auto &observer){return observer.guard.alive();});
        if (input_observers_.len()>=usize(65536)) return Err(InputError::Limit);
        auto binding=document_.bindings().make();
        input_observers_.push({target,binding.guard(),input_availability(target),Box<rstd::dyn<rstd::FnMut<void(const InputAvailability &)>>>::make(rstd::move(callback))});
        return Ok(rstd::move(binding));
    }
    template <class F> auto on_pointer_availability_changed(UiId target,F callback) -> Result<Binding,InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || document_.routing()) return Err(InputError::Reentrant);
        if (!document_.valid(target)) return Err(InputError::InvalidId);
        pointer_observers_.retain([](const auto &observer){return observer.guard.alive();});
        if (pointer_observers_.len()>=usize(65536)) return Err(InputError::Limit);
        auto binding=document_.bindings().make();
        pointer_observers_.push({target,binding.guard(),pointer_available(target),Box<rstd::dyn<rstd::FnMut<void(const bool &)>>>::make([callback=rstd::move(callback)](const bool &value) mutable {callback(value);})});
        return Ok(rstd::move(binding));
    }
    auto focus_candidates() const -> Vec<UiId>;
    auto move_focus(bool backwards) -> Result<empty, InputError>;
    template <class Extend, class Release, class Accepts>
    auto on_focus_search(UiId root, Extend extend, Release release, Accepts accepts) -> Result<Binding, InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
        if (!document_.valid(root)) return Err(InputError::InvalidId);
        focus_providers_.retain([](const auto &provider) { return provider.guard.alive(); });
        if (focus_provider(root)) return Err(InputError::InvalidSequence);
        if (focus_providers_.len() >= usize(4096)) return Err(InputError::Limit);
        auto binding = document_.bindings().make();
        focus_providers_.push({
            root, binding.guard(),
            Box<rstd::dyn<rstd::FnMut<bool(bool)>>>::make(rstd::move(extend)),
            Box<rstd::dyn<rstd::FnMut<bool()>>>::make(rstd::move(release)),
            Box<rstd::dyn<rstd::FnMut<bool(const InputSnapshot &, UiId)>>>::make(rstd::move(accepts))
        });
        return Ok(rstd::move(binding));
    }
    auto transfer_input(ParticipantId participant, slice<PointerKey> pointers)
        -> Result<empty, InputError>;
    auto arena() -> InputArena & { return arena_; }
    auto release_input(slice<PointerKey> pointers) -> Result<empty, InputError>;
    auto work() const -> InputWork { return work_; }
    auto contacts() const -> slice<PointerContact> { return contacts_.as_slice(); }
    auto focused() const -> Option<UiId> { return focus_; }
    auto cancel_subtree(UiId root) -> Result<empty,InputError>;
    auto cancel_detached() -> Result<empty, InputError>;
    auto focus_revision() const -> Serial { return focus_revision_; }
    auto captured(Index id, DeviceId device = 0) const -> Option<UiId>;
    auto open_scope(UiId root, InputScopeOptions options, BindingGuard guard)
        -> Result<InputScopeRegistration, InputError> {
        return scopes_.open(root, options, guard);
    }
    auto set_scope_options(const InputScopeRegistration &scope, InputScopeOptions options)
        -> Result<empty, InputError> { return scopes_.set_options(scope, options); }
    auto focus_when_ready(UiId target, Option<ScopeId> scope, BindingGuard guard)
        -> Result<FocusRequestId, InputError> {
        return scopes_.request(target, scope, guard);
    }
    auto focus_when_ready(UiId target, ScopeId scope, BindingGuard guard)
        -> Result<FocusRequestId, InputError> {
        return scopes_.request(target, Some(scope), guard);
    }
    auto bookmark_focus() const -> FocusBookmark {
        return update_.is_some() ? scopes_.bookmark(*update_, focus_) : FocusBookmark{};
    }
    auto poll_scope_events() -> Vec<ScopeEvent> { return scopes_.poll_events(); }
    auto poll_focus_events() -> Vec<FocusRequestEvent> { return scopes_.poll_focus_events(); }
    auto scope_diagnostics() const -> ScopeDiagnostics { return scopes_.diagnostics(); }
    auto focus_request_result(FocusRequestId id) const -> Option<FocusRequestEvent> {
        return scopes_.focus_result(id);
    }
    auto pending_sync() const -> bool { return scopes_.pending() || !commands_.is_empty(); }
    auto request_focus(Option<UiId> target) -> Result<empty, InputError> {
        return command({CommandType::Focus, 0, target});
    }
    auto synchronize(InputSnapshot update) -> Result<empty, InputError>;
    auto can_coalesce(const InputEvent &a, const InputEvent &b) -> bool;
    auto snapshot() const -> Option<InputSnapshot> {
        return update_.is_some() ? Some(InputSnapshot(*update_)) : None<InputSnapshot>();
    }
    auto completed_content(UiId id, Serial source_id) const -> Result<Option<FrameContent>, FrameError>;
    auto completed_image(UiId id, Serial image_id) const -> Result<Option<FrameImage>, FrameError>;
    auto dispatch(InputEvent input) -> Result<empty, InputError>;
    auto close() -> Result<empty, InputError>;
};
} // namespace vui
