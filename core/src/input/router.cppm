export module vui:events;
import :input;
import :input.protocol;
import :interaction.arena;
import :input.scopes;
using namespace rstd::prelude;

export namespace vui {
struct ConnectionId {
    Serial owner{}, value{};
    friend auto operator==(const ConnectionId &, const ConnectionId &) -> bool = default;
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
    friend class EventRouter;
    EventContext(EventRouter &router, const RoutedEvent &event, bool &stopped, bool &prevented,
                 Point &remaining_scroll)
        : router_(router), event_(event), stopped_(stopped), prevented_(prevented),
          remaining_scroll_(remaining_scroll) {}

  public:
    auto document() -> UiDocument &;
    auto visual(UiId id) const -> const UiVisual *;
    auto scroll_remaining() const -> Point { return remaining_scroll_; }
    auto consume_scroll(Point amount) -> Result<empty, InputError> {
        auto valid = [](Scalar value, Scalar remaining) {
            return finite(value) && (remaining >= 0 ? value >= 0 && value <= remaining
                                                    : value <= 0 && value >= remaining);
        };
        if (event_.input.type != EventType::Wheel || !valid(amount.x, remaining_scroll_.x) ||
            !valid(amount.y, remaining_scroll_.y))
            return Err(InputError::InvalidEvent);
        remaining_scroll_.x -= amount.x;
        remaining_scroll_.y -= amount.y;
        return Ok(empty{});
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
    auto write_clipboard(ref<str>) -> Result<empty, ClipboardError>;
    template <class F> auto defer(F callback) -> Result<Serial, UiError> {
        return document().defer(rstd::move(callback));
    }
};

class EventRouter {
    friend class EventContext;
    struct BehaviorEntry {
        UiId target;
        explicit BehaviorEntry(UiId id) : target(id) {}
        Option<BindingGuard> binding;
        InputDelivery delivery{InputDelivery::EverySample};
        virtual ~BehaviorEntry() = default;
        virtual void invoke(EventContext &, const RoutedEvent &) = 0;
    };
    template <class T> struct NodeBehavior : BehaviorEntry {
        explicit NodeBehavior(UiId id) : BehaviorEntry(id) {}
        void invoke(EventContext &context, const RoutedEvent &event) override {
            if (auto state = context.document().template state<T>(this->target))
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
    };
    enum class CommandType { Capture, Release, Focus, RestartText };
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
    struct PointerObserver {
        UiId target;
        BindingGuard guard;
        bool available{};
        Box<rstd::dyn<rstd::FnMut<void(bool)>>> changed;
    };
    Vec<PointerObserver> pointer_observers_;
    bool availability_dirty_{};
    struct NodeRoutes {
        Vec<usize> behaviors, handlers;
    };
    Vec<NodeRoutes> node_routes_;
    bool routes_dirty_{true}, prune_requested_{true};
    usize every_sample_{}, motion_handlers_{};
    void index_routes() {
        if (!routes_dirty_)
            return;
        for (auto &row : node_routes_) {
            row.behaviors.clear();
            row.handlers.clear();
        }
        every_sample_ = {};
        motion_handlers_ = {};
        auto row = [&](UiId id) -> NodeRoutes & {
            while (node_routes_.len() <= usize(id.slot))
                node_routes_.push(NodeRoutes{});
            return node_routes_[usize(id.slot)];
        };
        for (usize i{}; i < behaviors_.len(); ++i) {
            const auto &entry = behaviors_[i];
            row(entry->target).behaviors.push(usize(i));
            if ((entry->binding.is_none() || entry->binding->alive()) &&
                entry->delivery == InputDelivery::EverySample)
                ++every_sample_;
        }
        for (usize i{}; i < handlers_.len(); ++i) {
            const auto &entry = handlers_[i];
            row(entry->target).handlers.push(usize(i));
            if (entry->active && (entry->binding.is_none() || entry->binding->alive()) &&
                (entry->type == EventType::PointerMove || entry->type == EventType::PointerEnter ||
                 entry->type == EventType::PointerLeave))
                ++motion_handlers_;
        }
        routes_dirty_ = false;
    }
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
    Option<UiDocument::RouteScope> scope_;
    Option<UiId> focus_;
    Serial focus_revision_{};
    bool sharing_{};
    Vec<UiId> shared_targets_;
    Vec<UiId> route_nodes_, owner_targets_;
    bool busy_{}, controls_{true}, window_focused_{true}, closed_{};
    Option<InputError> failure_;
    InputWork work_;
    Option<Box<rstd::dyn<rstd::FnMut<void(EventRouter &, const InputEvent &)>>>> navigation_;
    Option<Box<rstd::dyn<rstd::FnMut<NativeCaptureResult(PointerKind, bool)>>>> native_capture_;
    Option<Box<rstd::dyn<rstd::FnMut<NativeTextInputResult(Option<TextInputRequest>)>>>> native_text_input_;
    Option<TextInputRequest> text_request_;
    Option<UiId> text_client_;
    NativeTextInputResult text_result_{NativeTextInputResult::Unsupported};
    Option<Box<rstd::dyn<rstd::FnMut<Result<String, ClipboardError>()>>>> clipboard_read_;
    Option<Box<rstd::dyn<rstd::FnMut<Result<empty, ClipboardError>(ref<str>)>>>> clipboard_write_;
    static void path(const InputSnapshot &update, UiId id, Vec<UiId> &result) {
        result.clear();
        auto cursor = Some(id);
        while (cursor.is_some() && result.len() < usize(256)) {
            auto node = update.get(*cursor);
            if (!node) {
                result.clear();
                return;
            }
            result.push(UiId(*cursor));
            cursor = node->parent;
        }
        if (cursor.is_some() || result.is_empty() || update.root().is_none() ||
            result[result.len() - usize(1)] != *update.root()) {
            result.clear();
            return;
        }
        for (usize i{}; i < result.len() / usize(2); ++i)
            rstd::swap(result[i], result[result.len() - usize(1) - i]);
    }
    static auto path(const InputSnapshot &update, UiId id) -> Vec<UiId> {
        Vec<UiId> result;
        path(update, id, result);
        return result;
    }
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
        -> Point {
        for (usize i{}; i <= index; ++i) {
            auto inverse = update.get(route[i])->properties.transform.inverse();
            if (inverse.is_none())
                return {};
            point = inverse->map(point);
        }
        return point;
    }
    void refresh_contacts() {
        contacts_.clear();
        for (const auto &value : pointers_) {
            if (!value.buttons && !value.contact)
                continue;
            contacts_.push({{value.device, value.id},
                            value.sequence,
                            value.position,
                            value.kind,
                            value.buttons != 0 || value.contact});
        }
    }
    auto pointer(Index id, DeviceId device = 0) -> Pointer * {
        for (auto &value : pointers_)
            if (value.id == id && value.device == device)
                return &value;
        return nullptr;
    }
    void behavior(EventContext &context, const RoutedEvent &event, bool prevented) {
        auto type = event.input.type;
        if (sharing_ && event.phase == RoutePhase::Bubble)
            for (auto target : shared_targets_)
                if (target == event.current)
                    return;
        bool cleanup = type == EventType::PointerCancel || type == EventType::CaptureLost ||
                       type == EventType::TextInputCancelled ||
                       type == EventType::FocusLost || type == EventType::FocusGained ||
                       type == EventType::PointerUp || type == EventType::KeyUp ||
                       type == EventType::PointerEnter || type == EventType::PointerLeave;
        if (event.phase != RoutePhase::Capture && (!prevented || cleanup)) {
            auto row = node_routes(event.current);
            if (row)
                for (auto index : row->behaviors) {
                    auto &item = behaviors_[index];
                    ++work_.behaviors_examined;
                    if (item->target == event.current &&
                        (item->binding.is_none() || item->binding->alive())) {
                        item->invoke(context, event);
                        break;
                    }
                }
        }
    }
    void invoke(const InputSnapshot &update, slice<UiId> route, usize index, UiId target,
                const InputEvent &input, RoutePhase phase, Serial cutoff, bool &stopped,
                bool &prevented, Point &remaining) {
        ++work_.routes;
        RoutedEvent event{input,
                          target,
                          route[index],
                          phase,
                          local(update, route, index, input.position),
                          update.revision()};
        EventContext context(*this, event, stopped, prevented, remaining);
        bool cleanup = input.type == EventType::PointerCancel ||
                       input.type == EventType::CaptureLost || input.type == EventType::FocusLost ||
                       input.type == EventType::PointerLeave || input.type == EventType::TextInputCancelled;
        bool pointer_input = input.type != EventType::KeyDown && input.type != EventType::KeyUp &&
                             input.type != EventType::TextInput && input.type != EventType::TextEditing &&
                             input.type != EventType::TextInputCancelled &&
                             input.type != EventType::FocusGained &&
                             input.type != EventType::FocusLost;
        bool allowed = cleanup || scopes_.allows(update, event.current, pointer_input);
        auto row = node_routes(event.current);
        if (row)
            for (auto index : row->handlers) {
                auto *handler = &*handlers_[index];
                ++work_.handlers_examined;
                if (handler->active && (handler->binding.is_none() || handler->binding->alive()) &&
                    handler->id.value < cutoff && handler->target == event.current &&
                    handler->type == input.type && handler->phase == phase &&
                    (allowed || handler->access == ScopeAccess::Global))
                    handler->invoke(context, event);
            }
        if (allowed)
            behavior(context, event, prevented);
    }
    auto route(const InputSnapshot &update, UiId target, InputEvent input, bool direct = false)
        -> bool {
        path(update, target, route_nodes_);
        auto &nodes = route_nodes_;
        if (nodes.is_empty())
            return false;
        auto cutoff = next_handler_;
        bool stopped{}, prevented{};
        auto remaining = input.scroll_delta;
        auto last = nodes.len() - usize(1);
        if (!direct)
            for (usize i{}; i < last && !stopped; ++i)
                invoke(update, nodes.as_slice(), i, target, input, RoutePhase::Capture, cutoff,
                       stopped, prevented, remaining);
        if (!stopped)
            invoke(update, nodes.as_slice(), last, target, input, RoutePhase::Target, cutoff,
                   stopped, prevented, remaining);
        else if (input.type == EventType::KeyUp || input.type == EventType::PointerUp ||
                 input.type == EventType::PointerCancel) {
            bool suppressed = true;
            RoutedEvent event{input,
                              target,
                              target,
                              RoutePhase::Target,
                              local(update, nodes.as_slice(), last, input.position),
                              update.revision()};
            EventContext context(*this, event, stopped, suppressed, remaining);
            behavior(context, event, true);
        }
        if (!direct)
            for (auto i = last; i > usize() && !stopped; --i)
                invoke(update, nodes.as_slice(), i - usize(1), target, input, RoutePhase::Bubble,
                       cutoff, stopped, prevented, remaining);
        return prevented;
    }
    static auto event(EventType type, const Pointer &pointer) -> InputEvent {
        InputEvent result;
        result.type = type;
        result.pointer = pointer.id;
        result.position = pointer.position;
        result.pointer_kind = pointer.kind;
        result.device = pointer.device;
        result.sequence = pointer.sequence;
        result.sample_time = pointer.sample_time;
        return result;
    }
    void hover(Pointer &pointer, Option<UiId> target) {
        if (pointer.hover == target)
            return;
        auto old = pointer.hover.is_some() ? path(*update_, *pointer.hover) : Vec<UiId>{};
        auto next = target.is_some() ? path(*update_, *target) : Vec<UiId>{};
        usize common{};
        while (common < old.len() && common < next.len() && old[common] == next[common])
            ++common;
        pointer.hover = target;
        for (auto i = old.len(); i > common; --i)
            route(*update_, old[i - usize(1)], event(EventType::PointerLeave, pointer), true);
        for (auto i = common; i < next.len(); ++i)
            route(*update_, next[i], event(EventType::PointerEnter, pointer), true);
    }
    void capture_native(Pointer &pointer, bool enabled) {
        if (native_capture_.is_none()) {
            pointer.native_capture = NativeCaptureResult::Unsupported;
            return;
        }
        if (!enabled && pointer.kind == PointerKind::Mouse)
            for (const auto &other : pointers_)
                if (other.kind == PointerKind::Mouse && other.capture.is_some())
                    return;
        pointer.native_capture = (*native_capture_)->operator()(pointer.kind, enabled);
    }
    void release(Pointer &pointer, bool shared = true) {
        auto owners = shared ? arena_.owners({pointer.device, pointer.id}) : Vec<UiId>{};
        auto previous = pointer.capture.take();
        if (previous.is_some())
            capture_native(pointer, false);
        if (previous.is_some())
            route(*update_, *previous, event(EventType::CaptureLost, pointer), true);
        if (previous.is_some())
            for (auto target : owners)
                if (previous != Some(UiId(target)))
                    route(*update_, target, event(EventType::CaptureLost, pointer), true);
    }
    void cancel(Pointer &pointer, CancelReason reason = CancelReason::Explicit) {
        auto owners = arena_.owners({pointer.device, pointer.id});
        auto target = pointer.relinquished                ? None<UiId>()
                      : pointer.capture.is_some()         ? pointer.capture
                      : pointer.sequence_target.is_some() ? pointer.sequence_target
                                                          : pointer.hover;
        auto was_down = pointer.buttons != 0 || pointer.contact;
        pointer.buttons = 0;
        pointer.contact = false;
        refresh_contacts();
        sharing_ = owners.len() > usize(1);
        shared_targets_.clear();
        for (auto value : owners)
            shared_targets_.push(UiId(value));
        if (target.is_some() && (was_down || pointer.capture.is_some())) {
            auto cancelled = event(EventType::PointerCancel, pointer);
            cancelled.cancel_reason = reason;
            route(*update_, *target, cancelled);
            for (auto value : owners)
                if (target != Some(UiId(value)))
                    route(*update_, value, cancelled);
        }
        sharing_ = false;
        shared_targets_.clear();
        release(pointer);
        if (update_.is_some())
            arena_.end({pointer.device, pointer.id}, *update_, contacts_.as_slice(), reason);
        pointer.claimed = false;
        pointer.relinquished = false;
        pointer.sequence_target = None();
    }
    void change_focus(Option<UiId> target, CancelReason text_reason = CancelReason::FocusLost) {
        if (focus_ == target)
            return;
        if (focus_revision_ == ~Serial{})
            rstd::panic("vui focus revision exhausted");
        ++focus_revision_;
        cancel_text_input(text_reason);
        auto old = focus_;
        focus_ = target;
        if (old.is_some())
            route(*update_, *old, {EventType::FocusLost}, true);
        if (target.is_some())
            route(*update_, *target, {EventType::FocusGained}, true);
    }
    void cancel_text_input(CancelReason reason) {
        if (text_client_.is_none()) return;
        auto client = *text_client_;
        text_client_ = None(); text_request_ = None();
        text_result_ = native_text_input_.is_some() ? (*native_text_input_)->operator()(None<TextInputRequest>())
                                                  : NativeTextInputResult::Unsupported;
        auto controls = rstd::exchange(controls_, false);
        InputEvent event; event.type = EventType::TextInputCancelled; event.cancel_reason = reason;
        route(*update_, client, event, true);
        controls_ = controls;
    }
    void refresh_text_input(bool force = false) {
        Option<UiId> client;
        Option<Rect> area;
        const auto *node = update_.is_some() && focus_.is_some() ? update_->get(*focus_) : nullptr;
        if (window_focused_ && node && node->text_input_area.is_some() && eligible(*update_, *focus_, false)) {
            auto matrix = update_->local_to_surface(*focus_);
            if (matrix.is_ok() && matrix.unwrap().inverse().is_some()) {
                auto rect = *node->text_input_area;
                Point points[]{matrix.unwrap().map({rect.x, rect.y}), matrix.unwrap().map({rect.x + rect.width, rect.y}),
                    matrix.unwrap().map({rect.x, rect.y + rect.height}), matrix.unwrap().map({rect.x + rect.width, rect.y + rect.height})};
                Scalar left = points[0].x, top = points[0].y, right = left, bottom = top;
                for (auto point : points) {
                    left = rstd::cmp::min(left, point.x); top = rstd::cmp::min(top, point.y);
                    right = rstd::cmp::max(right, point.x); bottom = rstd::cmp::max(bottom, point.y);
                }
                Rect mapped{left, top, right - left, bottom - top};
                if (mapped.valid()) { client = focus_; area = Some(mapped); }
            }
        }
        if (client != text_client_) cancel_text_input(CancelReason::Explicit);
        if (client.is_none()) return;
        TextInputRequest request{text_request_.is_some() ? text_request_->session : new_session(), *area};
        if (!force && text_request_ == Some(request)) return;
        text_client_ = client; text_request_ = Some(request);
        text_result_ = native_text_input_.is_some() ? (*native_text_input_)->operator()(text_request_)
                                                  : NativeTextInputResult::Unsupported;
    }
    void apply_transfer(const OwnershipChange &change) {
        auto state = pointer(change.pointer.pointer, change.pointer.device);
        if (!state)
            return;
        if (change.additional) {
            route(*update_, change.target, event(EventType::CaptureGained, *state), true);
            return;
        }
        auto old = state->capture.is_some() ? state->capture : state->sequence_target;
        sharing_ = change.previous.len() > usize(1);
        shared_targets_.clear();
        for (auto target : change.previous)
            shared_targets_.push(UiId(target));
        if (old.is_some() && *old != change.target) {
            auto cancelled = event(EventType::PointerCancel, *state);
            cancelled.cancel_reason = CancelReason::Superseded;
            route(*update_, *old, cancelled);
        }
        for (auto target : change.previous)
            if (old != Some(UiId(target)) && target != change.target) {
                auto cancelled = event(EventType::PointerCancel, *state);
                cancelled.cancel_reason = CancelReason::Superseded;
                route(*update_, target, cancelled);
                route(*update_, target, event(EventType::CaptureLost, *state), true);
            }
        sharing_ = false;
        shared_targets_.clear();
        release(*state, false);
        state->capture = Some(UiId(change.target));
        state->claimed = true;
        state->relinquished = false;
        capture_native(*state, true);
        route(*update_, change.target, event(EventType::CaptureGained, *state), true);
    }
    void drain() {
        auto releasing = rstd::exchange(releases_, Vec<Vec<PointerKey>>{});
        for (const auto &pointers : releasing) {
            bool valid{true};
            for (auto key : pointers)
                valid &= arena_.has_stream(key) && pointer(key.pointer, key.device);
            if (!valid) {
                failure_ = Some(InputError::InvalidSequence);
                continue;
            }
            for (auto key : pointers) {
                auto state = pointer(key.pointer, key.device);
                auto owners = arena_.owners(key);
                sharing_ = owners.len() > usize(1);
                shared_targets_.clear();
                for (auto target : owners)
                    shared_targets_.push(UiId(target));
                auto cancelled = event(EventType::PointerCancel, *state);
                cancelled.cancel_reason = CancelReason::Explicit;
                if (owners.is_empty() && state->sequence_target.is_some())
                    route(*update_, *state->sequence_target, cancelled);
                for (auto target : owners)
                    route(*update_, target, cancelled);
                sharing_ = false;
                shared_targets_.clear();
                release(*state);
                arena_.release(key, *update_, contacts_.as_slice());
                state->claimed = true;
                state->relinquished = true;
            }
        }
        for (usize i{}; i < commands_.len(); ++i) {
            auto command = commands_[i];
            if (command.type == CommandType::Focus) {
                change_focus(command.target);
            } else if (command.type == CommandType::RestartText) {
                if (command.target == text_client_) {
                    text_request_->session = new_session();
                    text_result_ = native_text_input_.is_some() ? (*native_text_input_)->operator()(text_request_)
                                                              : NativeTextInputResult::Unsupported;
                    auto controls = rstd::exchange(controls_, false);
                    InputEvent event; event.type = EventType::TextInputCancelled; event.cancel_reason = CancelReason::Explicit;
                    route(*update_, *text_client_, event, true);
                    controls_ = controls;
                }
            } else if (auto *state = pointer(command.pointer, command.device)) {
                if (command.type == CommandType::Release) {
                    if (state->capture == command.target)
                        release(*state);
                } else if ((state->buttons || state->contact) && state->capture != command.target) {
                    release(*state);
                    state->capture = command.target;
                    capture_native(*state, true);
                    route(*update_, *command.target, event(EventType::CaptureGained, *state), true);
                }
            }
        }
        commands_.clear();
        auto pending = rstd::exchange(transfers_, Vec<Transfer>{});
        for (auto &transfer : pending) {
            auto changed = arena_.transfer(transfer.participant, transfer.pointers.as_slice(),
                                           *update_, contacts_.as_slice());
            if (changed.is_err()) {
                failure_ = Some(changed.unwrap_err());
                continue;
            }
            for (const auto &change : changed.unwrap())
                apply_transfer(change);
        }
    }
    void begin() {
        busy_ = true;
        scope_ = Some(document_.acquire_route().unwrap());
        failure_ = None();
    }
    auto end() -> Result<empty, InputError> {
        if (availability_dirty_) {
            availability_dirty_=false;
            for (auto &observer:pointer_observers_) {
                if (!observer.guard.alive()) continue;
                auto available=pointer_available(observer.target);
                if (available!=observer.available) {observer.available=available;observer.changed->operator()(available);}
                if (!document_.valid(observer.target)) observer.guard.disconnect();
            }
            pointer_observers_.retain([](const auto &observer){return observer.guard.alive();});
        }
        drain();
        refresh_text_input();
        scope_ = None();
        busy_ = false;
        if (prune_requested_) {
            handlers_.retain([&](const auto &handler) {
                if (!document_.valid(handler->target) && handler->owns_binding &&
                    handler->binding.is_some())
                    handler->binding->disconnect();
                return handler->active && document_.valid(handler->target) &&
                       (handler->binding.is_none() || handler->binding->alive());
            });
            behaviors_.retain([&](const auto &behavior) {
                if (!document_.valid(behavior->target) && behavior->binding.is_some())
                    behavior->binding->disconnect();
                return document_.valid(behavior->target) &&
                       (behavior->binding.is_none() || behavior->binding->alive());
            });
            prune_requested_ = false;
            routes_dirty_ = true;
        }
        return failure_.is_some() ? Result<empty, InputError>(Err(*failure_))
                                  : Result<empty, InputError>(Ok(empty{}));
    }
    auto command(Command value) -> Result<empty, InputError> {
        if (!busy_ && !document_.can_route())
            return Err(InputError::Reentrant);
        if (closed_)
            return Err(InputError::Unavailable);
        if (!update_.is_some() || !controls_ || !window_focused_)
            return Err(InputError::Unavailable);
        if (value.target.is_some() &&
            !eligible(*update_, *value.target,
                      value.type == CommandType::Capture || value.type == CommandType::Release,
                      true))
            return Err(InputError::InvalidId);
        if (value.type == CommandType::Capture || value.type == CommandType::Release) {
            auto state = pointer(value.pointer, value.device);
            if (!state ||
                (value.type == CommandType::Capture && !state->buttons && !state->contact))
                return Err(InputError::InvalidSequence);
        }
        if (commands_.len() >= usize(256)) {
            failure_ = Some(InputError::Limit);
            return Err(InputError::Limit);
        }
        commands_.push(rstd::move(value));
        if (!busy_) {
            begin();
            return end();
        }
        return Ok(empty{});
    }
    auto lost_receiver(UiId target) -> bool {
        auto row = node_routes(target);
        if (row)
            for (auto index : row->behaviors) {
                const auto &entry = behaviors_[index];
                if (entry->target == target && entry->binding.is_some() &&
                    !entry->binding->alive()) {
                    prune_requested_ = true;
                    return true;
                }
            }
        return false;
    }
    auto same_mount(const InputSnapshot &next, UiId id, bool pointer) const -> bool {
        auto before = update_->get(id), after = next.get(id);
        return before && after && before->mount_generation == after->mount_generation &&
               eligible(next, id, pointer);
    }

  public:
    explicit EventRouter(UiDocument &document) : document_(document) {}
    ~EventRouter() {
        if (native_text_input_.is_some() && text_request_.is_some())
            (*native_text_input_)->operator()(None<TextInputRequest>());
    }
    void begin_input_batch() {
        if (!busy_)
            prune_requested_ = true;
    }
    EventRouter(const EventRouter &) = delete;
    auto operator=(const EventRouter &) -> EventRouter & = delete;
    template <class T>
        requires rstd::Impled<T, vui::Behavior>
    auto attach_behavior(UiId target) -> Result<empty, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (busy_ || document_.routing())
            return Err(InputError::Reentrant);
        if (!document_.state<T>(target))
            return Err(InputError::InvalidId);
        routes_dirty_ = true;
        prune_requested_ = true;
        for (const auto &behavior : behaviors_)
            if (behavior->target == target)
                return Err(InputError::InvalidSequence);
        if (behaviors_.len() >= usize(65536))
            return Err(InputError::Limit);
        auto behavior = Box<NodeBehavior<T>>::make(target);
        behaviors_.push(Box<BehaviorEntry>::from_raw(
            rstd::move(behavior).into_raw().template cast<BehaviorEntry>()));
        return Ok(empty{});
    }
    template <class T>
        requires rstd::Impled<T, vui::Behavior>
    auto bind_behavior(UiId target, rstd::rc::Weak<T> receiver,
                       InputDelivery delivery = InputDelivery::EverySample)
        -> Result<Binding, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (busy_ || document_.routing())
            return Err(InputError::Reentrant);
        if (!document_.valid(target))
            return Err(InputError::InvalidId);
        for (const auto &entry : behaviors_)
            if (entry->target == target && (entry->binding.is_none() || entry->binding->alive()))
                return Err(InputError::InvalidSequence);
        if (behaviors_.len() >= usize(65536))
            return Err(InputError::Limit);
        routes_dirty_ = true;
        prune_requested_ = true;
        auto binding = document_.bindings().make(receiver);
        struct Entry : BehaviorEntry {
            rstd::rc::Weak<T> receiver;
            Entry(UiId id, rstd::rc::Weak<T> value, BindingGuard guard)
                : BehaviorEntry(id), receiver(rstd::move(value)) {
                this->binding = Some(BindingGuard(guard));
            }
            void invoke(EventContext &context, const RoutedEvent &event) override {
                auto value = receiver.upgrade();
                if (this->binding->alive() && value.is_some())
                    rstd::as<vui::Behavior>(*value->get()).handle(context, event);
            }
        };
        auto entry = Box<Entry>::make(target, rstd::move(receiver), binding.guard());
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
    auto disconnect(ConnectionId id) -> bool {
        for (auto &handler : handlers_)
            if (handler->id == id && handler->active) {
                handler->active = false;
                routes_dirty_ = true;
                prune_requested_ = true;
                if (!busy_)
                    handlers_.retain([](const auto &item) { return item->active; });
                return true;
            }
        return false;
    }
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
        native_text_input_ = Some(Box<rstd::dyn<rstd::FnMut<NativeTextInputResult(Option<TextInputRequest>)>>>::make(rstd::move(callback)));
        text_result_ = (*native_text_input_)->operator()(text_request_);
        return Ok(empty{});
    }
    auto clear_native_text_input() -> Result<empty, InputError> {
        if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
        if (native_text_input_.is_some()) (*native_text_input_)->operator()(None<TextInputRequest>());
        native_text_input_ = None(); text_result_ = NativeTextInputResult::Unsupported;
        return Ok(empty{});
    }
    auto text_input_request() const -> Option<TextInputRequest> { return text_request_; }
    auto text_input_client() const -> Option<UiId> { return text_client_; }
    auto text_input_result() const -> NativeTextInputResult { return text_result_; }
    auto restart_text_input(UiId client) -> Result<empty, InputError> {
        if (text_client_ != Some(client)) return Err(InputError::Unavailable);
        return command({CommandType::RestartText, 0, Some(client)});
    }
    template <class Read, class Write>
    auto set_clipboard(Read read, Write write) -> Result<empty, InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
        clipboard_read_ = Some(Box<rstd::dyn<rstd::FnMut<Result<String, ClipboardError>()>>>::make(rstd::move(read)));
        clipboard_write_ = Some(Box<rstd::dyn<rstd::FnMut<Result<empty, ClipboardError>(ref<str>)>>>::make(rstd::move(write)));
        return Ok(empty{});
    }
    auto clear_clipboard() -> Result<empty, InputError> {
        if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
        clipboard_read_ = None(); clipboard_write_ = None(); return Ok(empty{});
    }
    auto read_clipboard() -> Result<String, ClipboardError> {
        if (closed_) return Err(ClipboardError::Unavailable);
        if (clipboard_read_.is_none()) return Err(ClipboardError::Unsupported);
        return (*clipboard_read_)->operator()();
    }
    auto write_clipboard(ref<str> text) -> Result<empty, ClipboardError> {
        if (closed_) return Err(ClipboardError::Unavailable);
        if (clipboard_write_.is_none()) return Err(ClipboardError::Unsupported);
        return (*clipboard_write_)->operator()(text);
    }
    auto native_capture(Index id, DeviceId device = 0) const -> NativeCaptureResult {
        for (const auto &pointer : pointers_)
            if (pointer.id == id && pointer.device == device)
                return pointer.native_capture;
        return NativeCaptureResult::Unsupported;
    }
    template <class F> void set_navigation(F policy) {
        navigation_ =
            Some(Box<rstd::dyn<rstd::FnMut<void(EventRouter &, const InputEvent &)>>>::make(
                rstd::move(policy)));
    }
    auto pointer_available(UiId id) const -> bool {
        if (closed_ || !window_focused_ || update_.is_none() || !eligible(*update_,id,true)) return false;
        auto mount=document_.mount_state(id);auto visual=update_->get(id);
        if (mount.is_err() || !mount->mounted || !visual || visual->mount_generation!=mount->generation) return false;
        auto cursor=Some(id);
        for (unsigned depth{};cursor.is_some() && depth<256;++depth) {
            auto style=document_.resolved_style(*cursor);
            if (style.is_err() || !style->visible || !style->enabled || !style->hit_test_visible || style->transform.inverse().is_none()) return false;
            auto relation=document_.parent_relation(*cursor);if (relation.is_err()) return false;
            if (relation->is_none()) return cursor==update_->root();
            cursor=Some(UiId((**relation).parent));
        }
        return false;
    }
    template <class F> auto on_pointer_availability_changed(UiId target,F callback) -> Result<Binding,InputError> {
        if (closed_) return Err(InputError::Unavailable);
        if (busy_ || document_.routing()) return Err(InputError::Reentrant);
        if (!document_.valid(target)) return Err(InputError::InvalidId);
        pointer_observers_.retain([](const auto &observer){return observer.guard.alive();});
        if (pointer_observers_.len()>=usize(65536)) return Err(InputError::Limit);
        auto binding=document_.bindings().make();
        pointer_observers_.push({target,binding.guard(),pointer_available(target),Box<rstd::dyn<rstd::FnMut<void(bool)>>>::make(rstd::move(callback))});
        return Ok(rstd::move(binding));
    }
    auto focus_candidates() const -> Vec<UiId> {
        Vec<UiId> candidates;
        if (update_.is_some())
            for (const auto &visual : update_->visuals())
                if (eligible(*update_, visual.id, false))
                    candidates.push(UiId(visual.id));
        return candidates;
    }
    auto transfer_input(ParticipantId participant, slice<PointerKey> pointers)
        -> Result<empty, InputError> {
        if (closed_ || update_.is_none())
            return Err(InputError::Unavailable);
        if (!busy_ && !document_.can_route())
            return Err(InputError::Reentrant);
        if (transfers_.len() >= usize(256))
            return Err(InputError::Limit);
        Transfer transfer{participant, {}};
        for (auto key : pointers) {
            auto state = pointer(key.pointer, key.device);
            if (!state || (!state->buttons && !state->contact))
                return Err(InputError::InvalidSequence);
            transfer.pointers.push(PointerKey(key));
        }
        transfers_.push(rstd::move(transfer));
        if (!busy_) {
            begin();
            return end();
        }
        return Ok(empty{});
    }
    auto arena() -> InputArena & { return arena_; }
    auto release_input(slice<PointerKey> pointers) -> Result<empty, InputError> {
        if (closed_ || update_.is_none())
            return Err(InputError::Unavailable);
        if (!busy_ && !document_.can_route())
            return Err(InputError::Reentrant);
        if (releases_.len() >= usize(256))
            return Err(InputError::Limit);
        if (pointers.is_empty())
            return Err(InputError::InvalidSequence);
        Vec<PointerKey> pending;
        for (usize i{}; i < pointers.len(); ++i) {
            auto state = pointer(pointers[i].pointer, pointers[i].device);
            if (!state || (!state->buttons && !state->contact) || !arena_.has_stream(pointers[i]))
                return Err(InputError::InvalidSequence);
            for (usize j{}; j < i; ++j)
                if (pointers[i] == pointers[j])
                    return Err(InputError::InvalidSequence);
            pending.push(PointerKey(pointers[i]));
        }
        releases_.push(rstd::move(pending));
        if (!busy_) {
            begin();
            return end();
        }
        return Ok(empty{});
    }
    auto work() const -> InputWork { return work_; }
    auto contacts() const -> slice<PointerContact> { return contacts_.as_slice(); }
    auto focused() const -> Option<UiId> { return focus_; }
    auto focus_revision() const -> Serial { return focus_revision_; }
    auto captured(Index id, DeviceId device = 0) const -> Option<UiId> {
        for (const auto &state : pointers_)
            if (state.id == id && state.device == device)
                return state.capture;
        return None();
    }
    auto open_scope(UiId root, InputScopeOptions options, BindingGuard guard)
        -> Result<InputScopeRegistration, InputError> {
        return scopes_.open(root, options, guard);
    }
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
    auto pending_sync() const -> bool { return scopes_.pending(); }
    auto request_focus(Option<UiId> target) -> Result<empty, InputError> {
        return command({CommandType::Focus, 0, target});
    }
    auto synchronize(InputSnapshot update) -> Result<empty, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (busy_ || !document_.can_route())
            return Err(InputError::Reentrant);
        if (update.document() != document_.session() ||
            (update_.is_some() && update.revision() < update_->revision()))
            return Err(InputError::InvalidId);
        if (update_.is_none() || update_->layout_revision() != update.layout_revision())
            prune_requested_ = true;
        begin();
        controls_ = false;
        auto restore = scopes_.synchronize(update);
        arena_.set_scope_root(scopes_.pointer_root());
        if (update_.is_some()) {
            auto synchronized = arena_.synchronize(*update_, update, contacts_.as_slice());
            if (synchronized.is_err()) {
                failure_ = Some(synchronized.unwrap_err());
                return end();
            }
            for (auto &state : pointers_) {
                bool lost_capture =
                    state.capture.is_some() &&
                    (!same_mount(update, *state.capture, true) || lost_receiver(*state.capture) ||
                     (state.claimed && !state.relinquished &&
                      !arena_.ownership_alive({state.device, state.id})));
                if (state.claimed)
                    for (auto owner : arena_.owners({state.device, state.id}))
                        lost_capture |= !same_mount(update, owner, true);
                bool lost_hover = state.hover.is_some() && !same_mount(update, *state.hover, true);
                bool lost_sequence = state.capture.is_none() && state.sequence_target.is_some() &&
                                     !same_mount(update, *state.sequence_target, true);
                if (lost_capture || lost_sequence || (state.capture.is_none() && lost_hover)) {
                    bool outside = (state.capture.is_some() &&
                                    !scopes_.allows(update, *state.capture, true)) ||
                                   (state.sequence_target.is_some() &&
                                    !scopes_.allows(update, *state.sequence_target, true));
                    cancel(state, outside ? CancelReason::ScopeChanged
                                  : state.capture.is_some() && lost_receiver(*state.capture)
                                      ? CancelReason::ReceiverLost
                                      : CancelReason::Detached);
                }
                if (lost_hover)
                    hover(state, None());
            }
            if (focus_.is_some() && (!same_mount(update, *focus_, false) || lost_receiver(*focus_)))
                change_focus(None());
        }
        pointers_.retain([](const Pointer &pointer) {
            return pointer.kind != PointerKind::Touch || pointer.contact ||
                   pointer.capture.is_some();
        });
        refresh_contacts();
        if (prune_requested_)
            arena_.prune();
        update_ = Some(rstd::move(update));
        availability_dirty_=true;
        controls_ = true;
        if (window_focused_) {
            if (restore.is_some())
                change_focus(scopes_.restore_target(*update_, *restore));
            scopes_.resolve_focus(*update_, true,
                                  [&](Option<UiId> target) { change_focus(target); });
        } else
            scopes_.resolve_focus(*update_, false, [](Option<UiId>) {});
        if (window_focused_)
            for (auto &state : pointers_) {
                if (state.kind != PointerKind::Touch) {
                    auto hit = hit_at(*update_, state.position);
                    hover(state, hit.is_some() ? Some(*hit) : None<UiId>());
                }
            }
        return end();
    }
    auto can_coalesce(const InputEvent &a, const InputEvent &b) -> bool {
        if (a.type != EventType::PointerMove || b.type != a.type || a.pointer != b.pointer ||
            a.device != b.device || a.pointer_kind != b.pointer_kind ||
            a.coordinate_generation != b.coordinate_generation ||
            a.sample_time.clock != b.sample_time.clock ||
            b.sample_time.nanoseconds < a.sample_time.nanoseconds ||
            a.modifiers.shift != b.modifiers.shift || a.modifiers.control != b.modifiers.control ||
            a.modifiers.alt != b.modifiers.alt || a.modifiers.meta != b.modifiers.meta ||
            arena_.has_participants())
            return false;
        auto state = pointer(a.pointer, a.device);
        if (!state || state->capture.is_none() || (!state->buttons && !state->contact))
            return false;
        if (busy_)
            return false;
        auto row = node_routes(*state->capture);
        if (!row || every_sample_ != usize() || motion_handlers_ != usize())
            return false;
        for (auto index : row->behaviors) {
            const auto &entry = behaviors_[index];
            if (entry->target == *state->capture &&
                (entry->binding.is_none() || entry->binding->alive()))
                return true;
        }
        return false;
    }
    auto snapshot() const -> Option<InputSnapshot> {
        return update_.is_some() ? Some(InputSnapshot(*update_)) : None<InputSnapshot>();
    }
    auto dispatch(InputEvent input) -> Result<empty, InputError> {
        work_ = {};
        if (closed_)
            return Err(InputError::Unavailable);
        if (busy_ || !document_.can_route())
            return Err(InputError::Reentrant);
        if (update_.is_none())
            return Err(InputError::Unavailable);
        bool pointer_event =
            input.type == EventType::PointerMove || input.type == EventType::PointerDown ||
            input.type == EventType::PointerUp || input.type == EventType::PointerCancel;
        bool key_event = input.type == EventType::KeyDown || input.type == EventType::KeyUp;
        bool text_event = input.type == EventType::TextInput || input.type == EventType::TextEditing;
        bool wheel_event = input.type == EventType::Wheel;
        bool device_event = input.type == EventType::DeviceLost;
        bool window_event =
            input.type == EventType::WindowFocusLost || input.type == EventType::WindowFocusGained;
        if ((!pointer_event && !key_event && !text_event && !window_event && !wheel_event && !device_event) ||
            (text_event && (!input.text.present() ||
                (input.type == EventType::TextInput && input.text.selection().is_some()))) ||
            (!text_event && (input.text.present() || input.text_session != 0)) ||
            ((pointer_event || wheel_event) &&
             (!finite(input.position.x) || !finite(input.position.y))) ||
            (wheel_event &&
             (!finite(input.scroll_delta.x) || !finite(input.scroll_delta.y) ||
              static_cast<unsigned>(input.scroll_unit) > static_cast<unsigned>(ScrollUnit::Lines))))
            return Err(InputError::InvalidEvent);
        if (!window_focused_ && !window_event)
            return Ok(empty{});
        if (static_cast<unsigned>(input.button) > static_cast<unsigned>(PointerButton::Forward) ||
            static_cast<unsigned>(input.pointer_kind) > static_cast<unsigned>(PointerKind::Touch))
            return Err(InputError::InvalidEvent);
        unsigned mask = input.button == PointerButton::None
                            ? 0u
                            : 1u << (static_cast<unsigned>(input.button) - 1);
        if ((input.type == EventType::PointerDown || input.type == EventType::PointerUp) && !mask &&
            input.pointer_kind != PointerKind::Touch)
            return Err(InputError::InvalidEvent);
        Pointer *state{};
        if (pointer_event) {
            state = pointer(input.pointer, input.device);
            if (!state) {
                if (input.type == EventType::PointerUp)
                    return Ok(empty{});
                if (input.type == EventType::PointerCancel)
                    return Ok(empty{});
                if (pointers_.len() >= usize(32))
                    return Err(InputError::Limit);
                pointers_.push({input.pointer,
                                input.position,
                                input.pointer_kind,
                                0,
                                {},
                                {},
                                {},
                                input.device});
                state = pointer(input.pointer, input.device);
            }
            if (input.type == EventType::PointerDown &&
                ((state->buttons & mask) || (state->kind == PointerKind::Touch && state->contact)))
                return Err(InputError::InvalidSequence);
            if (input.type == EventType::PointerUp && !(state->buttons & mask) &&
                !(state->kind == PointerKind::Touch && state->contact))
                return Ok(empty{});
        }
        begin();
        if (input.type == EventType::WindowFocusLost) {
            availability_dirty_=true;
            controls_ = false;
            window_focused_ = false;
            for (auto &value : pointers_) {
                cancel(value, CancelReason::FocusLost);
                hover(value, None());
            }
            pointers_.clear();
            change_focus(None());
            controls_ = true;
        } else if (input.type == EventType::WindowFocusGained) {
            availability_dirty_=true;
            window_focused_ = true;
            refresh_text_input(true);
        } else if (device_event) {
            controls_ = false;
            for (auto &value : pointers_)
                if (value.device == input.device) {
                    cancel(value, CancelReason::DeviceLost);
                    hover(value, None());
                }
            pointers_.retain([&](const Pointer &value) { return value.device != input.device; });
            controls_ = true;
        } else if (wheel_event) {
            auto hit = hit_at(*update_, input.position);
            if (hit.is_some())
                route(*update_, *hit, input);
        } else if (text_event) {
            if (text_client_.is_some() && text_request_.is_some() &&
                (input.text_session == 0 || input.text_session == text_request_->session))
                route(*update_, *text_client_, input);
        } else if (key_event) {
            auto target = focus_.is_some() ? focus_ : scopes_.focus_root();
            bool prevented = target.is_some() && route(*update_, *target, input);
            if (!prevented && navigation_.is_some())
                (*navigation_)->operator()(*this, input);
        } else {
            state->position = input.position;
            state->sample_time = input.sample_time;
            if (input.type == EventType::PointerCancel) {
                controls_ = false;
                cancel(*state, input.cancel_reason == CancelReason::None ? CancelReason::Native
                                                                         : input.cancel_reason);
                hover(*state, None());
                controls_ = true;
            } else {
                auto hit = state->kind == PointerKind::Touch && input.type != EventType::PointerDown
                               ? None<UiId>()
                               : hit_at(*update_, input.position);
                if (state->kind != PointerKind::Touch)
                    hover(*state, hit.is_some() ? Some(*hit) : None<UiId>());
                bool starting =
                    input.type == EventType::PointerDown && !state->buttons && !state->contact;
                if (starting) {
                    if (next_sequence_ == ~Serial{}) {
                        failure_ = Some(InputError::Limit);
                        return end();
                    }
                    state->sequence = next_sequence_++;
                    state->sequence_target = hit.is_some() ? Some(UiId(*hit)) : None<UiId>();
                    state->claimed = false;
                    state->relinquished = hit.is_none() && scopes_.pointer_root().is_some();
                }
                if (input.type == EventType::PointerDown) {
                    if (state->kind == PointerKind::Touch)
                        state->contact = true;
                    else
                        state->buttons |= mask;
                }
                input.sequence = state->sequence;
                refresh_contacts();
                if ((state->buttons || state->contact) && !state->relinquished &&
                    (state->sequence_target.is_some() || arena_.has_global_participants())) {
                    auto claims = arena_.process({input, state->sequence_target.is_some()
                                                             ? *state->sequence_target
                                                             : UiId{}},
                                                 *update_, contacts_.as_slice());
                    auto observed = arena_.work();
                    work_.observations += observed.observations;
                    work_.claims += observed.claims;
                    if (claims.is_err()) {
                        failure_ = Some(claims.unwrap_err());
                        return end();
                    }
                    for (const auto &change : claims.unwrap()) {
                        if (starting) {
                            auto affected = pointer(change.pointer.pointer, change.pointer.device);
                            if (!affected)
                                continue;
                            if (change.additional) {
                                route(*update_, change.target,
                                      event(EventType::CaptureGained, *affected), true);
                                continue;
                            }
                            release(*affected, false);
                            affected->capture = Some(UiId(change.target));
                            affected->claimed = true;
                            capture_native(*affected, true);
                            route(*update_, change.target,
                                  event(EventType::CaptureGained, *affected), true);
                        } else
                            apply_transfer(change);
                    }
                }
                owner_targets_.clear();
                if (state->claimed)
                    arena_.owners({state->device, state->id}, owner_targets_);
                auto &owners = owner_targets_;
                if (!owners.is_empty()) {
                    sharing_ = owners.len() > usize(1);
                    shared_targets_.clear();
                    for (auto target : owners)
                        shared_targets_.push(UiId(target));
                    for (auto target : owners)
                        route(*update_, target, input);
                    sharing_ = false;
                    shared_targets_.clear();
                } else if (!state->relinquished) {
                    auto target = state->capture.is_some() ? state->capture
                                  : (state->kind == PointerKind::Touch || state->buttons)
                                      ? state->sequence_target
                                      : state->hover;
                    if (target.is_some())
                        route(*update_, *target, input);
                }
                drain();
                if (input.type == EventType::PointerUp) {
                    state->buttons &= ~mask;
                    if (state->kind == PointerKind::Touch)
                        state->contact = false;
                    if (!state->buttons && !state->contact) {
                        controls_ = false;
                        release(*state);
                        controls_ = true;
                        arena_.end({state->device, state->id}, *update_, contacts_.as_slice());
                        state->sequence_target = None();
                        state->claimed = false;
                    }
                }
            }
            if (state->kind == PointerKind::Touch &&
                (input.type == EventType::PointerUp || input.type == EventType::PointerCancel)) {
                hover(*state, None());
                pointers_.retain([&](const Pointer &value) {
                    return value.id != input.pointer || value.device != input.device;
                });
            }
        }
        refresh_contacts();
        return end();
    }
    auto close() -> Result<empty, InputError> {
        if (closed_)
            return Ok(empty{});
        if (busy_ || !document_.can_route())
            return Err(InputError::Reentrant);
        if (update_.is_some()) {
            begin();
            controls_ = false;
            window_focused_ = false;
            availability_dirty_=true;
            for (auto &pointer : pointers_) {
                cancel(pointer, CancelReason::Closed);
                hover(pointer, None());
            }
            pointers_.clear();
            change_focus(None(), CancelReason::Closed);
            auto result = end();
            if (result.is_err())
                return result;
        }
        closed_ = true;
        for (auto &observer:pointer_observers_) observer.guard.disconnect();
        pointer_observers_.clear();
        for (const auto &handler : handlers_)
            if (handler->owns_binding && handler->binding.is_some())
                handler->binding->disconnect();
        for (const auto &behavior : behaviors_)
            if (behavior->binding.is_some())
                behavior->binding->disconnect();
        handlers_.clear();
        behaviors_.clear();
        scopes_.close();
        arena_.close();
        contacts_.clear();
        pointers_.clear();
        update_ = None();
        return Ok(empty{});
    }
};
inline auto EventContext::document() -> UiDocument & { return router_.document_; }
inline auto EventContext::restart_text_input() -> Result<empty, InputError> { return router_.restart_text_input(event_.current); }
inline auto EventContext::read_clipboard() -> Result<String, ClipboardError> { return router_.read_clipboard(); }
inline auto EventContext::write_clipboard(ref<str> text) -> Result<empty, ClipboardError> { return router_.write_clipboard(text); }
inline auto EventContext::visual(UiId id) const -> const UiVisual * {
    return router_.update_->get(id);
}
inline auto EventContext::hit_within() const -> bool {
    auto hit = HitTest::target_at(*router_.update_, event_.input.position);
    if (hit.is_none())
        return false;
    auto cursor = hit;
    for (unsigned depth{}; cursor.is_some() && depth < 256; ++depth) {
        if (*cursor == event_.current)
            return true;
        cursor = router_.update_->get(*cursor)->parent;
    }
    return false;
}
inline auto EventContext::capture_pointer() -> Result<empty, InputError> {
    return router_.command({EventRouter::CommandType::Capture,
                            event_.input.pointer,
                            Some(UiId(event_.current)),
                            {},
                            event_.input.device});
}
inline auto EventContext::release_pointer() -> Result<empty, InputError> {
    return router_.command({EventRouter::CommandType::Release,
                            event_.input.pointer,
                            Some(UiId(event_.current)),
                            {},
                            event_.input.device});
}
inline auto EventContext::focus() -> Result<empty, InputError> {
    return router_.request_focus(Some(UiId(event_.current)));
}
} // namespace vui
