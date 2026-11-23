module vui;
import :input.router;
using namespace rstd::prelude;

namespace vui {
namespace {
auto valid_scroll_amount(Point amount, Point remaining) -> bool {
    auto valid = [](Scalar value, Scalar available) {
        return finite(value) && (available >= 0 ? value >= 0 && value <= available
                                                : value <= 0 && value >= available);
    };
    return valid(amount.x, remaining.x) && valid(amount.y, remaining.y);
}
}

auto EventContext::consume_scroll(Point amount) -> Result<empty, InputError> {
    if (event_.input.type != EventType::Wheel || !valid_scroll_amount(amount, remaining_scroll_))
        return Err(InputError::InvalidEvent);
    remaining_scroll_.x -= amount.x;
    remaining_scroll_.y -= amount.y;
    return Ok(empty{});
}

auto EventContext::can_defer_scroll() const -> bool {
    return scroll_behavior_ && !router_.wheel_completion_ && !default_prevented() && remaining_scroll_ != Point{};
}

auto EventContext::defer_scroll_completion(Box<ScrollCompletion> completion) -> Result<empty, InputError> {
    auto guard = *scroll_behavior_ ? Some(BindingGuard(**scroll_behavior_)) : None<BindingGuard>();
    router_.wheel_completion_ = Some(EventRouter::WheelCompletion{rstd::move(completion), rstd::move(guard)});
    return Ok(empty{});
}

void EventRouter::index_routes() {
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

void EventRouter::path(const InputSnapshot &update, UiId id, Vec<UiId> &result) {
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

auto EventRouter::path(const InputSnapshot &update, UiId id) -> Vec<UiId> {
    Vec<UiId> result;
    path(update, id, result);
    return result;
}

auto EventRouter::local(const InputSnapshot &update, slice<UiId> route, usize index, Point point)
    -> Point {
    for (usize i{}; i <= index; ++i) {
        auto inverse = update.get(route[i])->properties.transform.inverse();
        if (inverse.is_none())
            return {};
        point = inverse->map(point);
    }
    return point;
}

void EventRouter::refresh_contacts() {
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

auto EventRouter::pointer(Index id, DeviceId device) -> Pointer * {
    for (auto &value : pointers_)
        if (value.id == id && value.device == device)
            return &value;
    return nullptr;
}

void EventRouter::behavior(EventContext &context, const RoutedEvent &event, bool prevented) {
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
        if (row) {
            const BehaviorEntry *last = nullptr;
            if (type == EventType::Wheel && wheel_deferral_)
                for (auto index : row->behaviors) {
                    auto &item = behaviors_[index];
                    if (item->target == event.current && (item->binding.is_none() || item->binding->alive()))
                        last = item.get();
                }
            for (auto index : row->behaviors) {
                auto &item = behaviors_[index];
                ++work_.behaviors_examined;
                if (item->target == event.current &&
                    (item->binding.is_none() || item->binding->alive())) {
                    if (context.default_prevented() && !cleanup) break;
                    if (item.get() == last) context.scroll_behavior_ = &item->binding;
                    item->invoke(context, event);
                    context.scroll_behavior_ = nullptr;
                }
            }
        }
    }
}

void EventRouter::invoke(const InputSnapshot &update, slice<UiId> nodes, usize index,
                         RoutePhase phase, RouteState &route) {
    ++work_.routes;
    const auto &input = route.input;
    RoutedEvent event{input,
                      route.target,
                      nodes[index],
                      phase,
                      local(update, nodes, index, input.position),
                      update.revision()};
    EventContext context(*this, event, route.stopped, route.prevented, route.remaining);
    bool cleanup = input.type == EventType::PointerCancel ||
                   input.type == EventType::CaptureLost || input.type == EventType::FocusLost ||
                   input.type == EventType::PointerLeave || input.type == EventType::TextInputCancelled;
    bool pointer_input = input.type != EventType::KeyDown && input.type != EventType::KeyUp &&
                         input.type != EventType::TextInput && input.type != EventType::TextEditing &&
                         input.type != EventType::TextTransaction &&
                         input.type != EventType::TextInputCancelled &&
                         input.type != EventType::ClipboardResult &&
                         input.type != EventType::ClipboardWriteResult &&
                         input.type != EventType::FocusGained &&
                         input.type != EventType::FocusLost;
    bool allowed = cleanup || scopes_.allows(update, event.current, pointer_input);
    auto row = node_routes(event.current);
    if (row)
        for (auto index : row->handlers) {
            auto *handler = &*handlers_[index];
            ++work_.handlers_examined;
            if (handler->active && (handler->binding.is_none() || handler->binding->alive()) &&
                handler->id.value < route.cutoff && handler->target == event.current &&
                handler->type == input.type && handler->phase == phase &&
                (allowed || handler->access == ScopeAccess::Global))
                handler->invoke(context, event);
        }
    if (allowed)
        behavior(context, event, route.prevented);
}

void EventRouter::bubble(const InputSnapshot &update, Vec<UiId> &nodes, usize cursor, RouteState &route) {
    while (cursor > usize() && !route.stopped && !wheel_completion_)
        invoke(update, nodes.as_slice(), --cursor, RoutePhase::Bubble, route);
    if (wheel_completion_) {
        auto completion = wheel_completion_.unwrap();
        wheel_completion_ = None();
        deferred_wheel_ = Some(DeferredWheel{InputSnapshot(update), rstd::move(nodes), cursor,
                                             rstd::move(route), rstd::move(completion)});
    }
}

auto EventRouter::route(const InputSnapshot &update, UiId target, InputEvent input, bool direct)
    -> bool {
    path(update, target, route_nodes_);
    auto &nodes = route_nodes_;
    if (nodes.is_empty())
        return false;
    RouteState route{input, target, next_handler_, input.scroll_delta};
    auto last = nodes.len() - usize(1);
    if (!direct)
        for (usize i{}; i < last && !route.stopped; ++i)
            invoke(update, nodes.as_slice(), i, RoutePhase::Capture, route);
    if (!route.stopped)
        invoke(update, nodes.as_slice(), last, RoutePhase::Target, route);
    else if (input.type == EventType::KeyUp || input.type == EventType::PointerUp ||
             input.type == EventType::PointerCancel) {
        bool suppressed = true;
        RoutedEvent event{input,
                          target,
                          target,
                          RoutePhase::Target,
                          local(update, nodes.as_slice(), last, input.position),
                          update.revision()};
        EventContext context(*this, event, route.stopped, suppressed, route.remaining);
        behavior(context, event, true);
    }
    if (!direct)
        bubble(update, nodes, last, route);
    return route.prevented;
}

auto EventRouter::dispatch_for_session(InputEvent input) -> Result<empty, InputError> {
    wheel_deferral_ = input.type == EventType::Wheel;
    auto result = dispatch(rstd::move(input));
    wheel_deferral_ = false;
    return result;
}

auto EventRouter::resume_wheel() -> Result<empty, InputError> {
    auto deferred = deferred_wheel_.unwrap();
    deferred_wheel_ = None();
    const auto &owner = deferred.nodes[deferred.cursor];
    if (!update_ || !window_focused_ || !eligible(*update_, owner, true) ||
        (deferred.completion.guard && !deferred.completion.guard->alive()))
        return Ok(empty{});
    for (usize i{}; i <= deferred.cursor; ++i) {
        const auto id = deferred.nodes[i];
        const auto *before = deferred.snapshot.get(id);
        const auto *current = update_->get(id);
        if (!current || current->mount_generation != before->mount_generation || current->parent != before->parent)
            return Ok(empty{});
    }
    begin();
    auto &route = deferred.route;
    const auto consumed = deferred.completion.complete->operator()();
    if (valid_scroll_amount(consumed, route.remaining)) {
        route.remaining.x -= consumed.x;
        route.remaining.y -= consumed.y;
        route.prevented |= route.remaining == Point{};
        wheel_deferral_ = true;
        bubble(deferred.snapshot, deferred.nodes, deferred.cursor, route);
        wheel_deferral_ = false;
    } else {
        failure_ = Some(InputError::InvalidEvent);
    }
    return end();
}

auto EventRouter::event(EventType type, const Pointer &pointer) -> InputEvent {
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

void EventRouter::hover(Pointer &pointer, Option<UiId> target) {
    if (pointer.hover == target && !pointer.hover_dirty)
        return;
    auto old = rstd::move(pointer.hover_path);
    if (old.is_empty() && pointer.hover.is_some()) old = path(*update_, *pointer.hover);
    auto next = target.is_some() ? path(*update_, *target) : Vec<UiId>{};
    pointer.hover = target;
    pointer.hover_dirty = false;
    for (auto id : next) pointer.hover_path.push(UiId(id));
    auto contains = [](const Vec<UiId> &nodes, UiId id) {
        for (auto value : nodes) if (value == id) return true;
        return false;
    };
    for (auto i = old.len(); i > usize(); --i)
        if (!contains(next, old[i - usize(1)]))
            route(*update_, old[i - usize(1)], event(EventType::PointerLeave, pointer), true);
    for (auto id : next)
        if (!contains(old, id)) route(*update_, id, event(EventType::PointerEnter, pointer), true);
}

void EventRouter::capture_native(Pointer &pointer, bool enabled) {
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

void EventRouter::release(Pointer &pointer, bool shared) {
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

void EventRouter::cancel(Pointer &pointer, CancelReason reason) {
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

void EventRouter::change_focus(Option<UiId> target, CancelReason text_reason) {
    if (focus_ == target)
        return;
    if (focus_revision_ == ~Serial{})
        rstd::panic("vui focus revision exhausted");
    ++focus_revision_;
    clipboard_pending_.clear();
    cancel_text_input(text_reason);
    auto old = focus_;
    focus_ = target;
    if (old.is_some())
        route(*update_, *old, {EventType::FocusLost}, true);
    if (target.is_some())
        route(*update_, *target, {EventType::FocusGained}, true);
}

void EventRouter::cancel_subtree_inputs(UiId root, bool current) {
    if (!update_.is_some() || !update_->get(root)) return;
    auto affected = [&](UiId target) {
        if (!current) return input_descendant(*update_, root, target);
        auto descendant = document_.is_descendant(root, target);
        return descendant.is_ok() && descendant.unwrap();
    };
    for (auto &pointer : pointers_) {
        bool cancel_pointer = (pointer.capture && affected(*pointer.capture)) ||
                              (pointer.sequence_target && affected(*pointer.sequence_target));
        for (auto owner : arena_.owners({pointer.device, pointer.id})) cancel_pointer |= affected(owner);
        if (cancel_pointer) cancel(pointer, CancelReason::Explicit);
    }
    if (focus_ && affected(*focus_)) change_focus({}, CancelReason::Explicit);
    auto cancelled = arena_.cancel_subtree(root, *update_, contacts_.as_slice(), current);
    if (cancelled.is_err()) failure_ = Some(cancelled.unwrap_err());
}

auto EventRouter::current_text_state() const -> Option<TextInputState> {
    if (text_request_.is_none() || text_client_.is_none()) return None();
    for (const auto &entry : surroundings_)
        if (entry.client == *text_client_)
            return Some(TextInputState{*text_request_, TextSurrounding(entry.value), entry.cause});
    return Some(TextInputState{*text_request_});
}

void EventRouter::notify_text_input(bool force) {
    if (native_text_state_.is_some()) {
        auto state = current_text_state();
        if (!force && ((state.is_none() && delivered_text_state_.is_none()) ||
            (state.is_some() && delivered_text_state_.is_some() && *state == *delivered_text_state_))) return;
        delivered_text_state_ = state.is_some() ? Some(TextInputState(*state)) : None<TextInputState>();
        text_result_ = (*native_text_state_)->operator()(rstd::move(state));
    } else {
        delivered_text_state_ = None();
        text_result_ = native_text_input_.is_some() ? (*native_text_input_)->operator()(text_request_)
                                                   : NativeTextInputResult::Unsupported;
    }
}

void EventRouter::cancel_text_input(CancelReason reason) {
    if (text_client_.is_none()) return;
    auto client = *text_client_;
    text_client_ = None(); text_request_ = None();
    notify_text_input(true);
    auto controls = rstd::exchange(controls_, false);
    InputEvent event; event.type = EventType::TextInputCancelled; event.cancel_reason = reason;
    route(*update_, client, event, true);
    controls_ = controls;
}

void EventRouter::refresh_text_input(bool force) {
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
    if (!force && text_request_ == Some(request) && native_text_state_.is_none()) return;
    text_client_ = client; text_request_ = Some(request);
    notify_text_input(force);
}

void EventRouter::apply_transfer(const OwnershipChange &change) {
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

void EventRouter::drain() {
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
        if (command.type == CommandType::CancelSubtree) {
            cancel_subtree_inputs(*command.target);
        } else if (command.type == CommandType::Focus) {
            change_focus(command.target);
        } else if (command.type == CommandType::RestartText) {
            if (command.target == text_client_) {
                text_request_->session = new_session();
                notify_text_input(true);
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

void EventRouter::begin() {
    busy_ = true;
    scope_ = Some(document_.acquire_route().unwrap());
    failure_ = None();
}

auto EventRouter::end() -> Result<empty, InputError> {
    if (availability_dirty_) {
        availability_dirty_=false;
        dispatch_availability(pointer_observers_,[this](UiId id){return pointer_available(id);});
        dispatch_availability(input_observers_,[this](UiId id){return input_availability(id);});
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
        surroundings_.retain([&](const auto &entry) { return document_.valid(entry.client); });
        prune_requested_ = false;
        routes_dirty_ = true;
    }
    return failure_.is_some() ? Result<empty, InputError>(Err(*failure_))
                              : Result<empty, InputError>(Ok(empty{}));
}

auto EventRouter::command(Command value) -> Result<empty, InputError> {
    if (!busy_ && !document_.can_route() && value.type!=CommandType::CancelSubtree)
        return Err(InputError::Reentrant);
    if (closed_)
        return Err(InputError::Unavailable);
    if (!update_.is_some() || (value.type!=CommandType::CancelSubtree && (!controls_ || !window_focused_)))
        return Err(InputError::Unavailable);
    if(value.type==CommandType::CancelSubtree && (!value.target || !update_->get(*value.target)))return Err(InputError::InvalidId);
    if (value.type!=CommandType::CancelSubtree && value.target.is_some() &&
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
    if (!busy_ && document_.can_route()) {
        begin();
        return end();
    }
    return Ok(empty{});
}

auto EventRouter::lost_receiver(UiId target) -> bool {
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

auto EventRouter::same_mount(const InputSnapshot &next, UiId id, bool pointer) const -> bool {
    auto before = update_->get(id), after = next.get(id);
    return before && after && before->mount_generation == after->mount_generation &&
           eligible(next, id, pointer);
}

auto EventRouter::retire_behavior(BehaviorEdit &edit, UiId target, NodeStateKey role) -> Result<empty, InputError> {
    if (closed_) return Err(InputError::Unavailable);
    if (busy_ || document_.routing()) return Err(InputError::Reentrant);
    if (!document_.valid(target)) return Err(InputError::InvalidId);
    for (const auto &entry : edit.retired_) if (entry.target == target && entry.key == role) return Ok(empty{});
    edit.retired_.push({target, role}); return Ok(empty{});
}

auto EventRouter::prepare_behavior_order(BehaviorEdit &edit, UiId target, Vec<NodeStateKey> roles) -> Result<empty, InputError> {
    if (closed_) return Err(InputError::Unavailable);
    if (busy_ || document_.routing()) return Err(InputError::Reentrant);
    if (target.session != document_.session()) return Err(InputError::InvalidId);
    for (usize i{}; i < roles.len(); ++i) {
        for (usize j{}; j < i; ++j) if (roles[i] == roles[j]) return Err(InputError::InvalidSequence);
        bool found{};
        for (const auto &entry : edit.entries_) found |= entry->target == target && entry->role == roles[i];
        for (const auto &entry : behaviors_) found |= entry->target == target && entry->role == roles[i] &&
            (entry->binding.is_none() || entry->binding->alive());
        for (const auto &entry : edit.retired_) if (entry.target == target && entry.key == roles[i]) found = false;
        if (!found) return Err(InputError::InvalidId);
    }
    edit.orders_.push({target, rstd::move(roles)}); return Ok(empty{});
}

auto EventRouter::publish_behaviors(BehaviorEdit edit) -> Result<empty, InputError> {
    if (closed_) return Err(InputError::Unavailable);
    if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
    for (const auto &entry : edit.entries_) if (!document_.valid(entry->target)) return Err(InputError::InvalidId);
    for (const auto &entry : edit.retired_) if (!document_.valid(entry.target)) return Err(InputError::InvalidId);
    for (const auto &entry : edit.orders_) if (!document_.valid(entry.target)) return Err(InputError::InvalidId);
    Option<InputError> failure;
    if (!edit.retired_.is_empty() && update_.is_some()) {
        index_routes(); begin();
        for (usize i{}; i < edit.retired_.len(); ++i) {
            bool previous{};
            for (usize j{}; j < i; ++j) previous |= edit.retired_[j].target == edit.retired_[i].target;
            if (!previous) cancel_subtree_inputs(edit.retired_[i].target, true);
        }
        auto cancelled = end(); if (cancelled.is_err()) failure = Some(cancelled.unwrap_err());
    }
    for (const auto &role : edit.retired_)
        behaviors_.retain([&](const auto &entry) {
            if (entry->target != role.target || entry->role != role.key) return true;
            if (entry->binding.is_some()) entry->binding->disconnect(); return false;
        });
    for (auto &entry : edit.entries_) behaviors_.push(rstd::move(entry));
    for (const auto &order : edit.orders_) {
        usize cursor{};
        auto selected = [&](const BehaviorEntry &entry) {
            if (entry.target != order.target) return false;
            for (auto role : order.roles) if (entry.role == role) return true;
            return false;
        };
        for (auto role : order.roles) {
            while (cursor < behaviors_.len() && !selected(*behaviors_[cursor])) ++cursor;
            auto desired = cursor;
            while (desired < behaviors_.len() && !(behaviors_[desired]->target == order.target && behaviors_[desired]->role == role)) ++desired;
            if (cursor < behaviors_.len() && desired < behaviors_.len() && cursor != desired) {
                auto previous = rstd::move(behaviors_[cursor]); behaviors_[cursor] = rstd::move(behaviors_[desired]); behaviors_[desired] = rstd::move(previous);
            }
            ++cursor;
        }
    }
    routes_dirty_ = true; prune_requested_ = true;
    edit.published_ = true;
    return failure.is_some() ? Result<empty, InputError>(Err(*failure)) : Result<empty, InputError>(Ok(empty{}));
}

EventRouter::~EventRouter() {
    if (native_text_input_.is_some() && text_request_.is_some())
        (*native_text_input_)->operator()(None<TextInputRequest>());
    if (native_text_state_.is_some() && text_request_.is_some())
        (*native_text_state_)->operator()(None<TextInputState>());
}

auto EventRouter::disconnect(ConnectionId id) -> bool {
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

auto EventRouter::clear_native_text_input() -> Result<empty, InputError> {
    if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
    if (native_text_input_.is_some()) (*native_text_input_)->operator()(None<TextInputRequest>());
    if (native_text_state_.is_some()) (*native_text_state_)->operator()(None<TextInputState>());
    native_text_input_ = None(); native_text_state_ = None(); delivered_text_state_ = None();
    text_result_ = NativeTextInputResult::Unsupported;
    return Ok(empty{});
}

auto EventRouter::set_text_input_surrounding(UiId client, TextSurrounding surrounding,
                                TextChangeCause cause) -> Result<empty, InputError> {
    if (closed_) return Err(InputError::Unavailable);
    if (!document_.valid(client)) return Err(InputError::InvalidId);
    if (!surrounding.valid() || unsigned(cause) > unsigned(TextChangeCause::Other))
        return Err(InputError::InvalidEvent);
    SurroundingEntry *found{};
    for (auto &entry : surroundings_) if (entry.client == client) { found = &entry; break; }
    if (found && found->value == surrounding) return Ok(empty{});
    if (!surrounding.present()) surroundings_.retain([&](const auto &entry) { return entry.client != client; });
    else if (found) { found->value = rstd::move(surrounding); found->cause = cause; }
    else surroundings_.push({client, rstd::move(surrounding), cause});
    if (!busy_ && document_.can_route()) { begin(); return end(); }
    document_.bindings().wake();
    return Ok(empty{});
}

auto EventRouter::clear_clipboard() -> Result<empty, InputError> {
    if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
    clipboard_read_ = None(); clipboard_write_ = None(); clipboard_request_ = None(); clipboard_write_request_ = None();
    clipboard_pending_.clear(); return Ok(empty{});
}

auto EventRouter::read_clipboard() -> Result<String, ClipboardError> {
    if (closed_) return Err(ClipboardError::Unavailable);
    if (clipboard_read_.is_none()) return Err(ClipboardError::Unsupported);
    return (*clipboard_read_)->operator()();
}

auto EventRouter::request_clipboard(UiId target) -> Result<Serial, ClipboardError> {
    if (closed_ || !window_focused_ || focus_ != Some(target)) return Err(ClipboardError::Unavailable);
    if (clipboard_request_.is_none()) return Err(ClipboardError::Unsupported);
    if (clipboard_pending_.len() >= usize(32)) return Err(ClipboardError::Limit);
    auto id = new_session();
    clipboard_pending_.push({id, target, focus_revision_});
    auto result = (*clipboard_request_)->operator()(id);
    if (result.is_err()) {
        clipboard_pending_.retain([&](const ClipboardPending &pending) { return pending.id != id; });
        return Err(result.unwrap_err());
    }
    return Ok(id);
}

auto EventRouter::write_clipboard(ref<str> text) -> Result<empty, ClipboardError> {
    if (closed_) return Err(ClipboardError::Unavailable);
    if (clipboard_write_.is_none()) return Err(ClipboardError::Unsupported);
    return (*clipboard_write_)->operator()(text);
}

auto EventRouter::request_clipboard_write(UiId target, Serial activation, ref<str> text) -> Result<Serial, ClipboardError> {
    if (closed_ || !window_focused_ || focus_ != Some(target)) return Err(ClipboardError::Unavailable);
    if (clipboard_write_request_.is_none()) return Err(ClipboardError::Unsupported);
    if (clipboard_pending_.len() >= usize(32)) return Err(ClipboardError::Limit);
    auto id = new_session();
    clipboard_pending_.push({id, target, focus_revision_, true});
    auto result = (*clipboard_write_request_)->operator()(id, activation, text);
    if (result.is_err()) {
        clipboard_pending_.retain([&](const ClipboardPending &pending) { return pending.id != id; });
        return Err(result.unwrap_err());
    }
    return Ok(id);
}

auto EventRouter::native_capture(Index id, DeviceId device) const -> NativeCaptureResult {
    for (const auto &pointer : pointers_)
        if (pointer.id == id && pointer.device == device)
            return pointer.native_capture;
    return NativeCaptureResult::Unsupported;
}

auto EventRouter::pointer_available(UiId id) const -> bool {
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

auto EventRouter::input_availability(UiId id) -> InputAvailability {
    auto mount=document_.mount_state(id);if (mount.is_err()) return {};
    InputAvailability result{mount->mounted,true,true,window_focused_,pointer_available(id),mount->generation};
    auto cursor=Some(id);
    for (unsigned depth{};cursor.is_some() && depth<256;++depth) {
        auto style=document_.resolved_style(*cursor);if (style.is_err()) return {};
        result.visible&=style->visible;result.enabled&=style->enabled;
        auto relation=document_.parent_relation(*cursor);if (relation.is_err()) return {};
        if (relation->is_none()) return result;
        cursor=Some(UiId((**relation).parent));
    }
    return {};
}

auto EventRouter::focus_candidates() const -> Vec<UiId> {
    Vec<UiId> candidates;
    if (update_.is_some())
        for (const auto &visual : update_->visuals())
            if (eligible(*update_, visual.id, false))
                candidates.push(UiId(visual.id));
    return candidates;
}

auto EventRouter::focus_neighbor(bool backwards, bool wrap) -> Option<UiId> {
    if (!update_) return None();
    auto candidates = focus_candidates();
    Option<UiId> first, last;
    bool found{};
    for (const auto id : candidates) {
        if (focus_ == Some(id)) {
            found = true;
            if (backwards && last) return last;
            continue;
        }
        bool accepted = true;
        for (auto &provider : focus_providers_.as_mut_slice().as_mut_ref()) {
            if (!provider.guard.alive() || !input_descendant(*update_, provider.root, id)) continue;
            if (!(*provider.accepts)(*update_, id)) { accepted = false; break; }
        }
        if (!accepted) continue;
        if (found && !backwards) return Some(UiId(id));
        if (!first) first = Some(UiId(id));
        last = Some(UiId(id));
    }
    if (!found || wrap) {
        auto target = backwards ? last : first;
        return target ? target : focus_;
    }
    return None();
}

auto EventRouter::focus_provider(UiId root) -> FocusSearchProvider * {
    for (auto &provider : focus_providers_.as_mut_slice().as_mut_ref())
        if (provider.root == root && provider.guard.alive()) return &provider;
    return nullptr;
}

auto EventRouter::enclosing_focus_provider(UiId origin, Option<UiId> neighbor, Option<UiId> after)
    -> FocusSearchProvider * {
    auto route = path(*update_, origin);
    for (auto i = route.len(); i > usize();) {
        const auto id = route[--i];
        if (after) {
            if (*after == id) after = None();
            continue;
        }
        if (neighbor && id != *neighbor && input_descendant(*update_, id, *neighbor)) break;
        if (auto provider = focus_provider(id)) return provider;
    }
    return nullptr;
}

auto EventRouter::release_focus_search() -> bool {
    if (!focus_search_) return false;
    const auto root = focus_search_->root;
    focus_search_ = None();
    auto provider = focus_provider(root);
    return provider && (*provider->release)();
}

auto EventRouter::move_focus(bool backwards) -> Result<empty, InputError> {
    if (!update_) return Err(InputError::Unavailable);
    release_focus_search();
    const auto neighbor = focus_neighbor(backwards, false);
    if (focus_) {
        if (auto provider = enclosing_focus_provider(*focus_, neighbor)) {
            const auto *root = update_->get(provider->root);
            focus_search_ = Some(FocusSearch{*focus_, provider->root, focus_revision_, root->mount_generation, backwards});
            document_.bindings().wake();
            return Ok(empty{});
        }
    }
    const auto target = neighbor ? neighbor : focus_neighbor(backwards, true);
    return target ? request_focus(target) : Result<empty, InputError>(Ok(empty{}));
}

auto EventRouter::continue_focus_search() -> Result<bool, InputError> {
    if (!focus_search_pending()) return Ok(false);
    auto search = *focus_search_;
    auto provider = focus_provider(search.root);
    const auto *root = update_->get(search.root);
    if (!provider || !root || root->mount_generation != search.mount || focus_revision_ != search.revision ||
        focus_ != Some(search.origin) || !eligible(*update_, search.origin, false) ||
        !input_descendant(*update_, search.root, search.origin))
        return Ok(release_focus_search());
    const auto neighbor = focus_neighbor(search.backwards, false);
    if (neighbor && *neighbor != search.root && input_descendant(*update_, search.root, *neighbor)) {
        focus_search_->selected = true;
        auto focused = request_focus(neighbor);
        if (focused.is_err()) return Err(focused.unwrap_err());
        return Ok(true);
    }
    if (search.probes < 12 && (*provider->extend)(search.backwards)) {
        ++focus_search_->probes;
        return Ok(true);
    }
    auto enclosing = enclosing_focus_provider(search.origin, neighbor, Some(search.root));
    const auto released = release_focus_search();
    if (enclosing && search.probes < 12) {
        search.root = enclosing->root;
        search.mount = update_->get(enclosing->root)->mount_generation;
        ++search.probes;
        focus_search_ = Some(search);
        return Ok(true);
    }
    const auto fallback = focus_neighbor(search.backwards, true);
    if (!fallback || focus_ == fallback) return Ok(released);
    auto focused = request_focus(fallback);
    if (focused.is_err()) return Err(focused.unwrap_err());
    return Ok(true);
}

auto EventRouter::transfer_input(ParticipantId participant, slice<PointerKey> pointers)
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

auto EventRouter::release_input(slice<PointerKey> pointers) -> Result<empty, InputError> {
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

auto EventRouter::cancel_subtree(UiId root) -> Result<empty,InputError> {
    if(closed_)return Err(InputError::Unavailable);
    if(!document_.valid(root))return Err(InputError::InvalidId);
    if(!update_ || !update_->get(root))return Ok(empty{});
    return command({CommandType::CancelSubtree,0,Some(root)});
}

auto EventRouter::cancel_detached() -> Result<empty, InputError> {
    if (closed_) return Err(InputError::Unavailable);
    if (busy_ || !document_.can_route()) return Err(InputError::Reentrant);
    if (update_.is_none()) return Ok(empty{});
    index_routes();
    begin();
    auto detached = [&](UiId id) { return !input_current_mount(document_, *update_, id); };
    for (auto &pointer : pointers_) {
        bool lost_capture = pointer.capture.is_some() && detached(*pointer.capture);
        if (pointer.claimed) for (auto owner : arena_.owners({pointer.device, pointer.id}))
            lost_capture |= detached(owner);
        auto lost_sequence = pointer.capture.is_none() && pointer.sequence_target.is_some() && detached(*pointer.sequence_target);
        auto lost_hover = pointer.hover.is_some() && detached(*pointer.hover);
        if (lost_capture || lost_sequence || (pointer.capture.is_none() && lost_hover)) cancel(pointer, CancelReason::Detached);
        if (lost_hover) hover(pointer, None());
        else if (pointer.hover.is_some()) {
            for (auto i = pointer.hover_path.len(); i > usize(); --i) {
                auto id = pointer.hover_path[i - usize(1)];
                if (!detached(id)) continue;
                route(*update_, id, event(EventType::PointerLeave, pointer), true);
                pointer.hover_path.remove(i - usize(1));
                pointer.hover_dirty = true;
            }
        }
    }
    auto cancelled = arena_.cancel_detached(*update_, contacts_.as_slice());
    if (cancelled.is_err()) failure_ = Some(cancelled.unwrap_err());
    if (focus_.is_some() && detached(*focus_)) change_focus(None(), CancelReason::Detached);
    if (text_client_.is_some() && detached(*text_client_)) cancel_text_input(CancelReason::Detached);
    availability_dirty_ = true;
    return end();
}

auto EventRouter::captured(Index id, DeviceId device) const -> Option<UiId> {
    for (const auto &state : pointers_)
        if (state.id == id && state.device == device)
            return state.capture;
    return None();
}

auto EventRouter::synchronize(InputSnapshot update) -> Result<empty, InputError> {
    if (closed_)
        return Err(InputError::Unavailable);
    if (busy_ || !document_.can_route())
        return Err(InputError::Reentrant);
    if (update.document() != document_.session() ||
        (update_.is_some() && update.revision() < update_->revision()))
        return Err(InputError::InvalidId);
    auto route_changed = update_.is_none() || update_->layout_revision() != update.layout_revision();
    if (route_changed)
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
            if (state.kind != PointerKind::Touch && state.inside_window) {
                state.hover_dirty |= route_changed;
                auto hit = hit_at(*update_, state.position);
                hover(state, hit.is_some() ? Some(*hit) : None<UiId>());
            }
        }
    return end();
}

auto EventRouter::can_coalesce(const InputEvent &a, const InputEvent &b) -> bool {
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

auto EventRouter::completed_node(UiId id) const -> bool {
    auto current = update_ ? update_->get(id) : nullptr;
    auto completed = completed_frame_ ? completed_frame_->update().get(id) : nullptr;
    return current && completed && current->mount_generation == completed->mount_generation;
}

auto EventRouter::completed_content(UiId id, Serial source_id) const -> Result<Option<FrameContent>, FrameError> {
    if (!completed_node(id)) return Ok(None<FrameContent>());
    return completed_frame_->content(id, source_id);
}

auto EventRouter::completed_image(UiId id, Serial image_id) const -> Result<Option<FrameImage>, FrameError> {
    if (!completed_node(id)) return Ok(None<FrameImage>());
    return completed_frame_->image(id, image_id);
}

auto EventRouter::allows_semantics(const InputSnapshot &snapshot, UiId target) const -> bool {
    // A scope that contains focus can be used by a non-modal control, such as a menu.
    return !closed_ && scopes_.allows(snapshot, target, true);
}

auto EventRouter::dispatch_semantics(UiId target, SemanticsAction action, Option<SemanticsPosition> position)
    -> Result<bool, InputError> {
    if (busy_ || !document_.can_route())
        return Err(InputError::Reentrant);
    if (closed_ || !controls_ || !window_focused_ || update_.is_none())
        return Err(InputError::Unavailable);
    bool focus = action == SemanticsAction::Focus;
    if (!allows_semantics(*update_, target) || !input_eligible(*update_, target, false, focus))
        return Ok(false);
    if (focus) {
        auto result = request_focus(Some(target));
        if (result.is_err()) return Err(result.unwrap_err());
        return Ok(focus_ == Some(target));
    }
    begin();
    InputEvent input{.type = EventType::SemanticAction, .semantic_action = action, .semantic_position = position};
    auto handled = route(*update_, target, input, true);
    auto result = end();
    if (result.is_err()) return Err(result.unwrap_err());
    return Ok(handled);
}

auto EventRouter::dispatch(InputEvent input) -> Result<empty, InputError> {
    work_ = {};
    if (closed_)
        return Err(InputError::Unavailable);
    if (busy_ || !document_.can_route())
        return Err(InputError::Reentrant);
    if (update_.is_none())
        return Err(InputError::Unavailable);
    bool pointer_enter = input.type == EventType::WindowPointerEnter;
    bool pointer_leave = input.type == EventType::WindowPointerLeave;
    bool pointer_event = pointer_enter ||
        input.type == EventType::PointerMove || input.type == EventType::PointerDown ||
        input.type == EventType::PointerUp || input.type == EventType::PointerCancel;
    bool key_event = input.type == EventType::KeyDown || input.type == EventType::KeyUp;
    bool transaction_event = input.type == EventType::TextTransaction;
    bool clipboard_event = input.type == EventType::ClipboardResult;
    bool clipboard_write_event = input.type == EventType::ClipboardWriteResult;
    bool text_event = input.type == EventType::TextInput || input.type == EventType::TextEditing || transaction_event;
    bool wheel_event = input.type == EventType::Wheel;
    bool device_event = input.type == EventType::DeviceLost;
    bool reset_event = input.type == EventType::InputReset;
    bool window_event =
        input.type == EventType::WindowFocusLost || input.type == EventType::WindowFocusGained;
    if ((!pointer_event && !pointer_leave && !key_event && !text_event && !clipboard_event && !clipboard_write_event && !window_event && !wheel_event && !device_event && !reset_event) ||
        (text_event && !transaction_event && (!input.text.present() ||
            (input.type == EventType::TextInput && input.text.selection().is_some()))) ||
        (transaction_event && (!input.transaction.get() || input.text.present())) ||
        (!transaction_event && input.transaction.get()) ||
        (clipboard_event && (!input.clipboard_request || input.text_session ||
            (input.clipboard_error.is_some() == input.text.present()) || input.text.selection().is_some() ||
            (input.clipboard_error.is_some() && unsigned(*input.clipboard_error) > unsigned(ClipboardError::Limit)))) ||
        (clipboard_write_event && (!input.clipboard_request || input.text.present() || input.text_session ||
            (input.clipboard_error.is_some() && unsigned(*input.clipboard_error) > unsigned(ClipboardError::Limit)))) ||
        (!clipboard_event && !clipboard_write_event && (input.clipboard_request || input.clipboard_error.is_some())) ||
        (!text_event && !clipboard_event && (input.text.present() || input.text_session != 0)) ||
        ((pointer_event || wheel_event) &&
         (!finite(input.position.x) || !finite(input.position.y))) ||
        (wheel_event &&
         (!finite(input.scroll_delta.x) || !finite(input.scroll_delta.y) ||
          static_cast<unsigned>(input.scroll_unit) > static_cast<unsigned>(ScrollUnit::Lines))))
        return Err(InputError::InvalidEvent);
    if (!window_focused_ && !window_event && !reset_event)
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
        if (pointer_enter) {
            state->inside_window = true;
            input.type = EventType::PointerMove;
        }
    }
    begin();
    if (pointer_leave) {
        auto value = pointer(input.pointer, input.device);
        if (value) {
            value->inside_window = false;
            hover(*value, None());
        }
    } else if (reset_event) {
        availability_dirty_=true;
        controls_ = false;
        for (auto &value : pointers_) {
            cancel(value, CancelReason::InputLost);
            hover(value, None());
        }
        pointers_.clear();
        change_focus(None(), CancelReason::InputLost);
        if (input.window_focused.is_some()) window_focused_ = *input.window_focused;
        controls_ = true;
    } else if (input.type == EventType::WindowFocusLost) {
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
    } else if (clipboard_event || clipboard_write_event) {
        Option<UiId> target;
        for (const auto &pending : clipboard_pending_)
            if (pending.id == input.clipboard_request && pending.write == clipboard_write_event && pending.focus_revision == focus_revision_ &&
                focus_ == Some(pending.target)) { target = Some(UiId(pending.target)); break; }
        if (target.is_some()) clipboard_pending_.retain([&](const ClipboardPending &pending) { return pending.id != input.clipboard_request; });
        if (target.is_some()) route(*update_, *target, input, true);
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
            auto hit = (state->kind == PointerKind::Touch && input.type != EventType::PointerDown) ||
                               !state->inside_window
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

auto EventRouter::close() -> Result<empty, InputError> {
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
    release_focus_search();
    for (auto &provider : focus_providers_) provider.guard.disconnect();
    focus_providers_.clear();
    for (auto &observer:pointer_observers_) observer.guard.disconnect();
    pointer_observers_.clear();
    for (auto &observer:input_observers_) observer.guard.disconnect();
    input_observers_.clear();
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
    surroundings_.clear();
    clipboard_pending_.clear();
    delivered_text_state_ = None();
    update_ = None();
    completed_frame_ = None();
    return Ok(empty{});
}

auto EventContext::document() -> UiDocument & { return router_.document_; }
auto EventContext::restart_text_input() -> Result<empty, InputError> { return router_.restart_text_input(event_.current); }
auto EventContext::read_clipboard() -> Result<String, ClipboardError> { return router_.read_clipboard(); }
auto EventContext::request_clipboard() -> Result<Serial, ClipboardError> { return router_.request_clipboard(event_.current); }
auto EventContext::request_clipboard_write(ref<str> text) -> Result<Serial, ClipboardError> {
    return router_.request_clipboard_write(event_.current, event_.input.activation, text);
}
auto EventContext::write_clipboard(ref<str> text) -> Result<empty, ClipboardError> { return router_.write_clipboard(text); }
auto EventContext::visual(UiId id) const -> const UiVisual * {
    return router_.update_->get(id);
}
auto EventContext::content_position() const -> Option<Point> {
    auto node = visual(event_.current);
    return node ? node->content_position(event_.local) : None<Point>();
}
auto EventContext::completed_content(UiId id, Serial source_id) const -> Result<Option<FrameContent>, FrameError> {
    return router_.completed_content(id, source_id);
}

auto EventContext::completed_image(UiId id, Serial image_id) const -> Result<Option<FrameImage>, FrameError> {
    return router_.completed_image(id, image_id);
}
auto EventContext::hit_within() const -> bool {
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
auto EventContext::capture_pointer() -> Result<empty, InputError> {
    return router_.command({EventRouter::CommandType::Capture,
                            event_.input.pointer,
                            Some(UiId(event_.current)),
                            {},
                            event_.input.device});
}
auto EventContext::release_pointer() -> Result<empty, InputError> {
    return router_.command({EventRouter::CommandType::Release,
                            event_.input.pointer,
                            Some(UiId(event_.current)),
                            {},
                            event_.input.device});
}
auto EventContext::focus() -> Result<empty, InputError> {
    return router_.request_focus(Some(UiId(event_.current)));
}
} // namespace vui
