export module vui:events;
import :input;
using namespace rstd::prelude;

export namespace vui {
enum class EventType {
    PointerMove,
    PointerDown,
    PointerUp,
    PointerCancel,
    PointerEnter,
    PointerLeave,
    CaptureGained,
    CaptureLost,
    KeyDown,
    KeyUp,
    FocusGained,
    FocusLost,
    WindowFocusLost,
    WindowFocusGained,
    Activate,
    Wheel
};
enum class RoutePhase { Capture, Target, Bubble };
enum class PointerButton { None, Primary, Secondary, Middle };
enum class PointerKind { Mouse, Pen, Touch };
enum class Key {
    Unknown,
    Tab,
    Enter,
    Space,
    Escape,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown
};
enum class ScrollUnit { Pixels, Lines };
struct Modifiers {
    bool shift{}, control{}, alt{}, meta{};
};
struct InputEvent {
    EventType type{EventType::PointerMove};
    Index pointer{};
    Point position{};
    PointerButton button{};
    PointerKind pointer_kind{};
    Key key{};
    Modifiers modifiers{};
    bool repeat{};
    Point scroll_delta;
    ScrollUnit scroll_unit{ScrollUnit::Pixels};
    Option<InputStamp> stamp;
};
struct RoutedEvent {
    InputEvent input;
    UiId target, current;
    RoutePhase phase{};
    Point local;
    Serial ui_revision{};
};
enum class InputError { InvalidId, InvalidEvent, InvalidSequence, Unavailable, Reentrant, Limit };
struct ConnectionId {
    Serial owner{}, value{};
    friend auto operator==(const ConnectionId &, const ConnectionId &) -> bool = default;
};
class EventRouter;
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
    auto activate() -> Result<empty, InputError>;
    auto capture_pointer() -> Result<empty, InputError>;
    auto release_pointer() -> Result<empty, InputError>;
    auto focus() -> Result<empty, InputError>;
    template <class F> auto defer(F callback) -> Result<Serial, UiError> {
        return document().defer(rstd::move(callback));
    }
};

class EventRouter {
    friend class EventContext;
    struct Behavior {
        UiId target;
        explicit Behavior(UiId id) : target(id) {}
        virtual ~Behavior() = default;
        virtual void invoke(EventContext &, const RoutedEvent &) = 0;
    };
    template <class T> struct NodeBehavior : Behavior {
        explicit NodeBehavior(UiId id) : Behavior(id) {}
        void invoke(EventContext &context, const RoutedEvent &event) override {
            if (auto state = context.document().template state<T>(this->target))
                state->handle(context, event);
        }
    };
    struct Handler {
        ConnectionId id;
        UiId target;
        EventType type;
        RoutePhase phase;
        bool active{true};
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
        Option<UiId> hover, capture;
    };
    enum class CommandType { Capture, Release, Focus, Activate };
    struct Command {
        CommandType type;
        Index pointer{};
        Option<UiId> target;
        InputEvent input;
    };
    UiDocument &document_;
    Serial owner_{new_session()}, next_handler_{1};
    Vec<Box<Handler>> handlers_;
    Vec<Box<Behavior>> behaviors_;
    Vec<Pointer> pointers_;
    Vec<Command> commands_;
    Option<UiUpdate> update_;
    Option<UiDocument::RouteScope> scope_;
    Option<UiId> focus_;
    Serial focus_revision_{};
    bool busy_{}, controls_{true}, window_focused_{true}, closed_{};
    Option<InputError> failure_;

    static auto path(const UiUpdate &update, UiId id) -> Vec<UiId> {
        Vec<UiId> result;
        auto cursor = Some(id);
        while (cursor.is_some() && result.len() < usize(256)) {
            auto node = update.get(*cursor);
            if (!node)
                return {};
            result.push(UiId(*cursor));
            cursor = node->parent;
        }
        if (cursor.is_some() || result.is_empty() || update.root().is_none() ||
            result[result.len() - usize(1)] != *update.root())
            return {};
        for (usize i{}; i < result.len() / usize(2); ++i)
            rstd::swap(result[i], result[result.len() - usize(1) - i]);
        return result;
    }
    static auto eligible(const UiUpdate &update, UiId id, bool pointer, bool require_focus = true)
        -> bool {
        auto route = path(update, id);
        if (route.is_empty())
            return false;
        for (auto ancestor : route) {
            auto node = update.get(ancestor);
            if (!node->enabled || !node->properties.visible ||
                (pointer &&
                 (!node->hit_test_visible || node->properties.transform.inverse().is_none())))
                return false;
        }
        return pointer || !require_focus || update.get(id)->focusable;
    }
    static auto local(const UiUpdate &update, slice<UiId> route, usize index, Point point)
        -> Point {
        for (usize i{}; i <= index; ++i) {
            auto inverse = update.get(route[i])->properties.transform.inverse();
            if (inverse.is_none())
                return {};
            point = inverse->map(point);
        }
        return point;
    }
    auto pointer(Index id) -> Pointer * {
        for (auto &value : pointers_)
            if (value.id == id)
                return &value;
        return nullptr;
    }
    void behavior(EventContext &context, const RoutedEvent &event, bool prevented) {
        const auto type = event.input.type;
        bool cleanup = type == EventType::PointerCancel || type == EventType::CaptureLost ||
                       type == EventType::FocusLost || type == EventType::FocusGained ||
                       type == EventType::PointerUp || type == EventType::KeyUp ||
                       type == EventType::PointerEnter || type == EventType::PointerLeave;
        if (event.phase != RoutePhase::Capture && (!prevented || cleanup))
            for (auto &item : behaviors_)
                if (item->target == event.current) {
                    item->invoke(context, event);
                    break;
                }
    }
    void invoke(const UiUpdate &update, slice<UiId> route, usize index, UiId target,
                const InputEvent &input, RoutePhase phase, Serial cutoff, bool &stopped,
                bool &prevented, Point &remaining) {
        RoutedEvent event{input,
                          target,
                          route[index],
                          phase,
                          local(update, route, index, input.position),
                          update.revision()};
        EventContext context(*this, event, stopped, prevented, remaining);
        for (usize i{}; i < handlers_.len(); ++i) {
            auto *handler = &*handlers_[i];
            if (handler->active && handler->id.value < cutoff && handler->target == event.current &&
                handler->type == input.type && handler->phase == phase)
                handler->invoke(context, event);
        }
        behavior(context, event, prevented);
    }
    auto route(const UiUpdate &update, UiId target, InputEvent input, bool direct = false) -> bool {
        auto nodes = path(update, target);
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
    void release(Pointer &pointer) {
        auto previous = pointer.capture.take();
        if (previous.is_some())
            route(*update_, *previous, event(EventType::CaptureLost, pointer), true);
    }
    void cancel(Pointer &pointer) {
        auto target = pointer.capture.is_some() ? pointer.capture : pointer.hover;
        auto was_down = pointer.buttons != 0;
        pointer.buttons = 0;
        if (target.is_some() && (was_down || pointer.capture.is_some()))
            route(*update_, *target, event(EventType::PointerCancel, pointer));
        release(pointer);
    }
    void change_focus(Option<UiId> target) {
        if (focus_ == target)
            return;
        if (focus_revision_ == ~Serial{})
            rstd::panic("vui focus revision exhausted");
        ++focus_revision_;
        auto old = focus_;
        focus_ = target;
        if (old.is_some())
            route(*update_, *old, {EventType::FocusLost}, true);
        if (target.is_some())
            route(*update_, *target, {EventType::FocusGained}, true);
    }
    void drain() {
        for (usize i{}; i < commands_.len(); ++i) {
            auto command = commands_[i];
            if (command.type == CommandType::Focus) {
                change_focus(command.target);
            } else if (command.type == CommandType::Activate) {
                auto input = command.input;
                input.type = EventType::Activate;
                route(*update_, *command.target, input);
            } else if (auto *state = pointer(command.pointer)) {
                if (command.type == CommandType::Release) {
                    if (state->capture == command.target)
                        release(*state);
                } else if (state->buttons && state->capture != command.target) {
                    release(*state);
                    state->capture = command.target;
                    route(*update_, *command.target, event(EventType::CaptureGained, *state), true);
                }
            }
        }
        commands_.clear();
    }
    void begin() {
        busy_ = true;
        scope_ = Some(document_.acquire_route().unwrap());
        failure_ = None();
    }
    auto end() -> Result<empty, InputError> {
        drain();
        scope_ = None();
        busy_ = false;
        handlers_.retain([&](const auto &handler) {
            return handler->active && document_.valid(handler->target);
        });
        behaviors_.retain([&](const auto &behavior) { return document_.valid(behavior->target); });
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
                      value.type != CommandType::Activate))
            return Err(InputError::InvalidId);
        if (value.type == CommandType::Capture || value.type == CommandType::Release) {
            auto state = pointer(value.pointer);
            if (!state || (value.type == CommandType::Capture && !state->buttons))
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
    auto same_mount(const UiUpdate &next, UiId id, bool pointer) const -> bool {
        auto before = update_->get(id), after = next.get(id);
        return before && after && before->mount_generation == after->mount_generation &&
               eligible(next, id, pointer);
    }

  public:
    explicit EventRouter(UiDocument &document) : document_(document) {}
    EventRouter(const EventRouter &) = delete;
    auto operator=(const EventRouter &) -> EventRouter & = delete;
    template <class T> auto attach_behavior(UiId target) -> Result<empty, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (busy_ || document_.routing())
            return Err(InputError::Reentrant);
        if (!document_.state<T>(target))
            return Err(InputError::InvalidId);
        behaviors_.retain([&](const auto &item) { return document_.valid(item->target); });
        for (const auto &behavior : behaviors_)
            if (behavior->target == target)
                return Err(InputError::InvalidSequence);
        if (behaviors_.len() >= usize(65536))
            return Err(InputError::Limit);
        auto behavior = Box<NodeBehavior<T>>::make(target);
        behaviors_.push(
            Box<Behavior>::from_raw(rstd::move(behavior).into_raw().template cast<Behavior>()));
        return Ok(empty{});
    }
    template <class F>
    auto on(UiId target, EventType type, F callback, RoutePhase phase = RoutePhase::Target)
        -> Result<ConnectionId, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (!document_.valid(target))
            return Err(InputError::InvalidId);
        if (next_handler_ == ~Serial{} || handlers_.len() >= usize(65536))
            return Err(InputError::Limit);
        ConnectionId id{owner_, next_handler_++};
        auto handler = Box<Callback<F>>::make(id, target, type, phase, rstd::move(callback));
        handlers_.push(
            Box<Handler>::from_raw(rstd::move(handler).into_raw().template cast<Handler>()));
        return Ok(id);
    }
    auto disconnect(ConnectionId id) -> bool {
        for (auto &handler : handlers_)
            if (handler->id == id && handler->active) {
                handler->active = false;
                if (!busy_)
                    handlers_.retain([](const auto &item) { return item->active; });
                return true;
            }
        return false;
    }
    auto focused() const -> Option<UiId> { return focus_; }
    auto focus_revision() const -> Serial { return focus_revision_; }
    auto captured(Index id) const -> Option<UiId> {
        for (const auto &state : pointers_)
            if (state.id == id)
                return state.capture;
        return None();
    }
    auto request_focus(Option<UiId> target) -> Result<empty, InputError> {
        return command({CommandType::Focus, 0, target});
    }
    auto synchronize(UiUpdate update) -> Result<empty, InputError> {
        if (closed_)
            return Err(InputError::Unavailable);
        if (busy_ || !document_.can_route())
            return Err(InputError::Reentrant);
        if (update.document() != document_.session() ||
            (update_.is_some() && update.revision() < update_->revision()))
            return Err(InputError::InvalidId);
        begin();
        controls_ = false;
        if (update_.is_some()) {
            for (auto &state : pointers_) {
                bool lost_capture =
                    state.capture.is_some() && !same_mount(update, *state.capture, true);
                bool lost_hover = state.hover.is_some() && !same_mount(update, *state.hover, true);
                if (lost_capture || (state.capture.is_none() && lost_hover))
                    cancel(state);
                if (lost_hover)
                    hover(state, None());
            }
            if (focus_.is_some() && !same_mount(update, *focus_, false))
                change_focus(None());
        }
        update_ = Some(rstd::move(update));
        controls_ = true;
        if (window_focused_)
            for (auto &state : pointers_) {
                auto hit = HitTest::at(*update_, state.position);
                hover(state, hit.is_some() ? Some(hit->target) : None<UiId>());
            }
        return end();
    }
    auto dispatch(InputEvent input) -> Result<empty, InputError> {
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
        bool wheel_event = input.type == EventType::Wheel;
        bool window_event =
            input.type == EventType::WindowFocusLost || input.type == EventType::WindowFocusGained;
        if ((!pointer_event && !key_event && !window_event && !wheel_event) ||
            ((pointer_event || wheel_event) &&
             (!finite(input.position.x) || !finite(input.position.y))) ||
            (wheel_event &&
             (!finite(input.scroll_delta.x) || !finite(input.scroll_delta.y) ||
              static_cast<unsigned>(input.scroll_unit) > static_cast<unsigned>(ScrollUnit::Lines))))
            return Err(InputError::InvalidEvent);
        if (!window_focused_ && !window_event)
            return Ok(empty{});
        if (static_cast<unsigned>(input.button) > static_cast<unsigned>(PointerButton::Middle) ||
            static_cast<unsigned>(input.pointer_kind) > static_cast<unsigned>(PointerKind::Touch))
            return Err(InputError::InvalidEvent);
        unsigned mask = input.button == PointerButton::None
                            ? 0u
                            : 1u << (static_cast<unsigned>(input.button) - 1);
        if ((input.type == EventType::PointerDown || input.type == EventType::PointerUp) && !mask)
            return Err(InputError::InvalidEvent);
        Pointer *state{};
        if (pointer_event) {
            state = pointer(input.pointer);
            if (!state) {
                if (input.type == EventType::PointerUp)
                    return Ok(empty{});
                if (input.type == EventType::PointerCancel)
                    return Ok(empty{});
                if (pointers_.len() >= usize(32))
                    return Err(InputError::Limit);
                pointers_.push({input.pointer, input.position, input.pointer_kind, 0, {}, {}});
                state = pointer(input.pointer);
            }
            if (input.type == EventType::PointerDown && (state->buttons & mask))
                return Err(InputError::InvalidSequence);
            if (input.type == EventType::PointerUp && !(state->buttons & mask))
                return Ok(empty{});
        }
        begin();
        if (input.type == EventType::WindowFocusLost) {
            controls_ = false;
            window_focused_ = false;
            for (auto &value : pointers_) {
                cancel(value);
                hover(value, None());
            }
            pointers_.clear();
            change_focus(None());
            controls_ = true;
        } else if (input.type == EventType::WindowFocusGained) {
            window_focused_ = true;
        } else if (wheel_event) {
            auto hit = HitTest::at(*update_, input.position);
            if (hit.is_some())
                route(*update_, hit->target, input);
        } else if (key_event) {
            bool prevented = focus_.is_some() && route(*update_, *focus_, input);
            if (!prevented && input.type == EventType::KeyDown && input.key == Key::Tab &&
                !input.repeat) {
                Vec<UiId> candidates;
                for (const auto &visual : update_->visuals())
                    if (eligible(*update_, visual.id, false))
                        candidates.push(UiId(visual.id));
                if (!candidates.is_empty()) {
                    usize index{};
                    while (index < candidates.len() &&
                           (focus_.is_none() || candidates[index] != *focus_))
                        ++index;
                    if (input.modifiers.shift)
                        index = index == usize() || index == candidates.len()
                                    ? candidates.len() - usize(1)
                                    : index - usize(1);
                    else
                        index = index == candidates.len() ? usize()
                                                          : (index + usize(1)) % candidates.len();
                    command({CommandType::Focus, 0, Some(UiId(candidates[index]))});
                }
            }
        } else {
            state->position = input.position;
            if (input.type == EventType::PointerCancel) {
                controls_ = false;
                cancel(*state);
                hover(*state, None());
                controls_ = true;
            } else {
                auto hit = HitTest::at(*update_, input.position);
                hover(*state, hit.is_some() ? Some(hit->target) : None<UiId>());
                if (input.type == EventType::PointerDown)
                    state->buttons |= mask;
                auto target = state->capture.is_some() ? state->capture : state->hover;
                if (target.is_some())
                    route(*update_, *target, input);
                drain();
                if (input.type == EventType::PointerUp) {
                    state->buttons &= ~mask;
                    if (!state->buttons) {
                        controls_ = false;
                        release(*state);
                        controls_ = true;
                    }
                }
            }
            if (state->kind == PointerKind::Touch &&
                (input.type == EventType::PointerUp || input.type == EventType::PointerCancel)) {
                hover(*state, None());
                pointers_.retain([&](const Pointer &value) { return value.id != input.pointer; });
            }
        }
        return end();
    }
    auto close() -> Result<empty, InputError> {
        if (closed_)
            return Ok(empty{});
        if (busy_ || !document_.can_route())
            return Err(InputError::Reentrant);
        if (update_.is_some()) {
            auto result = dispatch({EventType::WindowFocusLost});
            if (result.is_err())
                return result;
        }
        closed_ = true;
        handlers_.clear();
        behaviors_.clear();
        pointers_.clear();
        update_ = None();
        return Ok(empty{});
    }
};
inline auto EventContext::document() -> UiDocument & { return router_.document_; }
inline auto EventContext::visual(UiId id) const -> const UiVisual * {
    return router_.update_->get(id);
}
inline auto EventContext::hit_within() const -> bool {
    auto hit = HitTest::at(*router_.update_, event_.input.position);
    if (hit.is_none())
        return false;
    for (auto id : hit->path)
        if (id == event_.current)
            return true;
    return false;
}
inline auto EventContext::activate() -> Result<empty, InputError> {
    return router_.command(
        {EventRouter::CommandType::Activate, 0, Some(UiId(event_.current)), event_.input});
}
inline auto EventContext::capture_pointer() -> Result<empty, InputError> {
    return router_.command(
        {EventRouter::CommandType::Capture, event_.input.pointer, Some(UiId(event_.current))});
}
inline auto EventContext::release_pointer() -> Result<empty, InputError> {
    return router_.command(
        {EventRouter::CommandType::Release, event_.input.pointer, Some(UiId(event_.current))});
}
inline auto EventContext::focus() -> Result<empty, InputError> {
    return router_.request_focus(Some(UiId(event_.current)));
}
} // namespace vui
