module vui.controls;
import :scroll_view;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
namespace {
auto difference(Point a, Point b) -> Point { return {a.x - b.x, a.y - b.y}; }
auto sum(Point a, Point b) -> Point { return {a.x + b.x, a.y + b.y}; }

struct NestedConnection {
    rstd::rc::Weak<ScrollNestedReceiver> callback;
    BindingGuard guard;
};

auto nested_connection(UiDocument &document, UiId node) -> NestedConnection * {
    auto value = document.state<NestedConnection>(node, {rstd::any::TypeId::of<NestedConnection>(), 0});
    return value && value->guard.alive() ? value : nullptr;
}

auto map_delta(const Transform &transform, Point delta) -> Point {
    return {transform.a * delta.x + transform.c * delta.y, transform.b * delta.x + transform.d * delta.y};
}

auto same_linear(const Transform &a, const Transform &b) -> bool {
    return a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d;
}

auto transfer_delta(const InputSnapshot &snapshot, UiId from, UiId to, Point delta) -> Option<Point> {
    auto source = snapshot.content_to_surface(from);
    auto target = snapshot.content_to_surface(to);
    if (source.is_err() || target.is_err()) return None();
    auto inverse = target->inverse();
    if (!inverse) return None();
    const auto result = map_delta(*inverse, map_delta(*source, delta));
    return finite(result.x) && finite(result.y) ? Some(Point(result)) : None<Point>();
}
}

auto ScrollDeltaLease::write(Point offset, const ViewportMetrics &metrics) const -> Result<empty, UiError> {
    auto owner = owner_.upgrade();
    if (!owner || !owner->get()->owns_delta(gesture_))
        return Err(UiError::Cancelled);
    if (!owner->get()->write(offset, metrics)) return Err(owner->get()->error_.unwrap_or(UiError::PropertyOwned));
    return Ok(empty{});
}

auto ScrollDeltaLease::reached_boundary(Point remaining, const ViewportMetrics &metrics) const -> Result<empty, UiError> {
    auto owner = owner_.upgrade();
    if (!owner || !owner->get()->owns_delta(gesture_))
        return Ok(empty{});
    auto &interaction = *owner->get();
    if (!interaction.reached_boundary(remaining, metrics, interaction.pending_decay_time_))
        return Err(interaction.error_.unwrap_or(UiError::PropertyOwned));
    return Ok(empty{});
}

ScrollInteraction::~ScrollInteraction() {
    release_nested_drag();
    if (animation_) *animation_->status.get() = ScrollAnimationStatus::Cancelled;
}

auto ScrollInteraction::acquire() -> bool {
    if (lease_.active()) return true;
    PropertyKey key{viewport_,PresentationProperty::ViewportOffset};auto value=ui().take_over(slice<PropertyKey>::from_raw_parts(&key,usize(1)));
    if (value.is_err()) {error_=Some(value.unwrap_err());return false;}
    lease_=rstd::move(value).unwrap();return true;
}

auto ScrollInteraction::owns_delta(Serial gesture) -> bool {
    if (gesture_ != gesture || !lease_.active()) return false;
    if (mode_ != Mode::NestedDrag) return true;
    auto origin = nested_origin_->owner.upgrade();
    if (origin && (*origin)->mode_ == Mode::Drag && matches_origin(**origin) &&
        session_.input().pointer_available((*origin)->root_) &&
        ui().is_descendant(root_, (*origin)->root_).unwrap_or(false)) return true;
    stop(true, false, false);
    return false;
}

auto ScrollInteraction::local_event(const InputSnapshot &snapshot,const InputEvent &input) -> Option<InputEvent> {
    auto transform=snapshot.content_to_surface(root_);if (transform.is_err()) return None();
    auto inverse=transform->inverse();if (inverse.is_none()) return None();
    auto local=inverse->map(input.position);if (!finite(local.x) || !finite(local.y)) return None();
    bool reset = inverse_ && (uses_scroll_deltas() ? !same_linear(*inverse_, *inverse) : *inverse_ != *inverse);
    if (reset && (mode_==Mode::Drag || mode_==Mode::Pending)) {
        auto metrics=ui().viewport_metrics(viewport_);
        if (metrics.is_ok()) {
            origin_ = metrics->offset;
            anchor_ = local;
            last_local_ = uses_scroll_deltas() ? map_delta(*inverse, input.position) : local;
        }
        tracker_.remove({input.device,input.pointer});
        if (mode_==Mode::Pending) {
            pan_=PanRecognizer({style_.motion.threshold,style_.axes.horizontal && style_.axes.vertical ? PanAxis::Free : style_.axes.horizontal ? PanAxis::Horizontal : PanAxis::Vertical});
            auto down=input;down.type=EventType::PointerDown;down.position=local;
            InputContext context(snapshot,{}, {input.device,input.pointer});pan_.observe(context,{rstd::move(down),root_});
        }
    }
    inverse_=inverse;auto result=input;result.position=local;
    if (reset) result.history={};
    return Some(rstd::move(result));
}

auto ScrollInteraction::drag_value(Point position,const ViewportMetrics &metrics) const -> Point {
    auto raw=masked({origin_.x-(position.x-anchor_.x),origin_.y-(position.y-anchor_.y)});
    auto result=bounded(raw,metrics.maximum);
    if (style_.motion.movement==ScrollMovement::Elastic) {
        result.x += rubber(raw.x - result.x, metrics.viewport.width);
        result.y += rubber(raw.y - result.y, metrics.viewport.height);
    }
    return masked(result);
}

auto ScrollInteraction::rubber(Scalar delta, Scalar extent) const -> Scalar {
    if (extent <= 0) return 0;
    const auto amount = (1 - 1 / (1 + __builtin_fabs(delta) * .55 / extent)) * extent * style_.motion.rubber_scale;
    return delta < 0 ? -amount : amount;
}

auto ScrollInteraction::recover_pull(const ViewportMetrics &metrics) const -> Scalar {
    const auto extent = along(metrics.viewport);
    const auto capacity = extent * style_.motion.rubber_scale;
    if (capacity <= 0) return 0;
    const auto stretch = overscroll(metrics);
    const auto ratio = bounded(__builtin_fabs(stretch) / capacity, .99);
    return (stretch < 0 ? -1 : 1) * extent * ratio / (.55 * (1 - ratio));
}

auto ScrollInteraction::write_pull(Scalar pull, const ViewportMetrics &metrics) -> bool {
    edge_pull_ = pull;
    const auto extent = along(metrics.viewport);
    const auto offset = bounded(along(metrics.offset), along(metrics.maximum)) + rubber(pull, extent);
    return offset == along(metrics.offset) || write(main_axis(offset), metrics);
}

struct ScrollInteraction::Ancestor {
    rstd::rc::Rc<ScrollInteraction> owner;
    Point delta, remainder, consumed;
    ViewportMetrics metrics;
};

auto ScrollInteraction::drag_by(Point delta, const ViewportMetrics &before) -> bool {
    if (!finite(delta.x) || !finite(delta.y)) { error_ = Some(UiError::InvalidValue); return false; }
    const auto gesture = gesture_;
    if (nested_scroll_) delta = pre_nested(ScrollNestedPhase::PreScroll, delta);
    if (gesture != gesture_) return true;
    if (delta == Point{}) {
        if (nested_scroll_) {
            NestedRoute route{ScrollNestedPhase::PostScroll, root_, {}, {}};
            (void)ancestor({}, this, true, &route);
        }
        return true;
    }
    auto metrics = before;
    if (uses_elastic_deltas()) {
        const auto stretch = overscroll(metrics);
        if (stretch) {
            if (!edge_pull_ || (edge_pull_ > 0) != (stretch > 0)) edge_pull_ = recover_pull(metrics);
            const auto next = edge_pull_ + along(delta);
            if (!next || (next > 0) == (edge_pull_ > 0)) return write_pull(next, metrics);
            if (!write_pull(0, metrics)) return false;
            metrics.offset = bounded(metrics.offset, metrics.maximum);
            delta = main_axis(next);
        } else {
            edge_pull_ = 0;
        }
    }
    if (gesture != gesture_) return true;
    return consume_drag(delta, metrics);
}

auto ScrollInteraction::consume_drag(Point delta, ViewportMetrics metrics) -> bool {
    pending_delta_ = None();
    drag_delta_ = delta;
    auto result = resolve_delta(delta, metrics);
    if (result.is_err()) { error_ = Some(result.unwrap_err()); return false; }
    if (auto resolved = result.unwrap(); resolved) {
        if (resolved->after_layout) {
            pending_delta_ = rstd::move(resolved->after_layout);
            return true;
        }
        const auto moved = resolved->consumed;
        const Point offset{metrics.offset.x + moved.x, metrics.offset.y + moved.y};
        if (moved != Point{} && !write(offset, metrics)) return false;
        metrics.offset = offset;
        return reached_boundary(difference(delta, moved), metrics, 0);
    }
    const auto offset = bounded({metrics.offset.x + delta.x, metrics.offset.y + delta.y}, metrics.maximum);
    return offset == metrics.offset || write(offset, metrics);
}

void ScrollInteraction::track(InputEvent event,const ViewportMetrics &metrics,Point actual,bool measured) {
    Vec<PointerSample> history;
    for (auto sample : event.history) {
        const auto local = inverse_->map(sample.position);
        if (measured) {
            const auto vector = map_delta(*inverse_, sample.position);
            sample.position = {-vector.x, -vector.y};
        } else {
            sample.position = drag_value(local, metrics);
        }
        history.push(rstd::move(sample));
    }
    event.history = history.as_slice();
    event.position = measured ? Point{inverse_->tx - event.position.x, inverse_->ty - event.position.y} : actual;
    tracker_.add(event);
    tracked_offset_ = actual;
}

auto ScrollInteraction::write(Point value,const ViewportMetrics &metrics) -> bool {
    PresentationBatch batch;batch.viewport_offset(viewport_,value);auto result=ui().write_presentation(lease_,batch);
    if (result.is_err()) {error_=Some(result.unwrap_err());return false;}
    written_=value;if (value!=metrics.offset) changed_.emit({value,metrics.maximum}).unwrap();return true;
}

auto ScrollInteraction::resolve_delta(Point delta, const ViewportMetrics &metrics)
    -> Result<Option<ScrollDeltaResolution>, UiError> {
    if (!uses_scroll_deltas()) return Ok(None<ScrollDeltaResolution>());
    if (resolve_delta_)
        return (**resolve_delta_)(masked(delta), metrics, true, Some(ScrollDeltaLease(owner_, gesture_)));
    const auto target = bounded(masked({metrics.offset.x + delta.x, metrics.offset.y + delta.y}), metrics.maximum);
    return Ok(Some(ScrollDeltaResolution{difference(target, metrics.offset), {}}));
}

auto ScrollInteraction::matches_origin(const ScrollInteraction &origin) const -> bool {
    return nested_origin_ && nested_origin_->target == origin.root_ && nested_origin_->gesture == origin.gesture_;
}

auto ScrollInteraction::accepts_nested_drag(const ScrollInteraction &origin) const -> bool {
    return mode_ == Mode::Idle || (mode_ == Mode::Pending && pointer_ == origin.pointer_) ||
           (mode_ == Mode::NestedDrag && matches_origin(origin));
}

void ScrollInteraction::notify_nested(const InputSnapshot &snapshot, UiId node, NestedRoute &route) {
    auto connection = nested_connection(ui(), node);
    if (!connection || !session_.input().pointer_available(node)) return;
    auto callback = connection->callback.upgrade();
    if (!callback) return;
    auto available = transfer_delta(snapshot, root_, node, route.remaining);
    auto consumed = transfer_delta(snapshot, root_, node, route.consumed);
    if (!available || !consumed) return;
    const ScrollNestedOffer offer{route.source, *consumed, *available};
    const auto taken = (*callback->get()->invoke)(route.phase, offer);
    DeltaOffer valid(*available);
    if (valid.consume(taken).is_err()) return;
    auto returned = transfer_delta(snapshot, node, root_, valid.remaining);
    if (!returned) return;
    route.consumed = sum(route.consumed, difference(route.remaining, *returned));
    route.remaining = *returned;
}

auto ScrollInteraction::has_nested_connection() -> bool {
    auto cursor = root_;
    for (unsigned depth{}; depth < 256; ++depth) {
        auto relation = ui().parent_relation(cursor);
        if (relation.is_err() || !*relation) break;
        cursor = (**relation).parent;
        if (nested_connection(ui(), cursor)) return true;
    }
    return false;
}

auto ScrollInteraction::pre_nested(ScrollNestedPhase phase, Point amount) -> Point {
    auto snapshot = session_.input().snapshot();
    if (!snapshot) return amount;
    Vec<UiId> connections;
    auto cursor = root_;
    for (unsigned depth{}; depth < 256; ++depth) {
        auto relation = ui().parent_relation(cursor);
        if (relation.is_err() || !*relation) break;
        cursor = (**relation).parent;
        if (nested_connection(ui(), cursor)) connections.push(UiId(cursor));
    }
    NestedRoute route{phase, root_, {}, amount};
    const auto gesture = gesture_;
    for (auto i = connections.len(); i != usize() && gesture_ == gesture;)
        notify_nested(*snapshot, connections[--i], route);
    return route.remaining;
}

auto ScrollInteraction::ancestor(Point delta, const ScrollInteraction *drag, bool test_range, NestedRoute *route) -> Option<Ancestor> {
    if (delta == Point{} && !route) return None();
    const auto snapshot = session_.input().snapshot();
    if (!snapshot || !session_.input().pointer_available(root_)) return None();
    const auto gesture = gesture_;
    auto cursor = root_;
    for (unsigned depth{}; depth < 256; ++depth) {
        auto relation = ui().parent_relation(cursor);
        if (relation.is_err() || !*relation) break;
        cursor = (**relation).parent;
        if (route) {
            notify_nested(*snapshot, cursor, *route);
            if (gesture_ != gesture) return None();
            delta = route->remaining;
        }
        if (delta == Point{}) continue;
        auto state = ui().state<ScrollState>(cursor);
        if (!state || !state->interaction_ || !state->style_.user_scroll_enabled ||
            !state->style_.motion.enabled || !session_.input().pointer_available(cursor)) continue;
        auto parent = state->interaction_->clone();
        if (drag ? !parent->accepts_nested_drag(*drag)
                 : parent->mode_ != Mode::Idle || !parent->style_.motion.inertia ||
                   (!parent->style_.motion.fling && !parent->style_.motion.deceleration_rate))
            continue;
        auto mapped = transfer_delta(*snapshot, root_, cursor, delta);
        if (!mapped) continue;
        const auto projected = parent->masked(*mapped);
        if (__builtin_fabs(projected.x) + __builtin_fabs(projected.y) <= 1e-6) continue;
        auto metrics = ui().viewport_metrics(parent->viewport_);
        if (metrics.is_err()) continue;
        if (test_range) {
            if (parent->resolve_delta_) {
                auto resolved = (**parent->resolve_delta_)(projected, *metrics, false, {});
                if (resolved.is_err() || (*resolved && (*resolved)->consumed == Point{})) continue;
            } else {
                const auto offset = bounded(metrics->offset, metrics->maximum);
                if (bounded(sum(offset, projected), metrics->maximum) == offset) continue;
            }
        }
        const auto consumed = route ? transfer_delta(*snapshot, root_, cursor, route->consumed).unwrap_or(Point{}) : Point{};
        return Some(Ancestor{rstd::move(parent), projected, difference(*mapped, projected), consumed, *metrics});
    }
    return None();
}

auto ScrollInteraction::offer_drag(Point remaining, const ViewportMetrics &metrics) -> bool {
    auto source = nested_origin_ ? nested_origin_->owner.upgrade() : owner_.upgrade();
    if (!source || (*source)->mode_ != Mode::Drag || (nested_origin_ && !matches_origin(**source)))
        return true;
    const auto consumed = sum(nested_consumed_, difference(drag_delta_, remaining));
    remaining = sum(remaining, nested_remaining_);
    nested_remaining_ = {};
    nested_consumed_ = {};
    const auto gesture = gesture_;
    NestedRoute route{ScrollNestedPhase::PostScroll, (*source)->root_, consumed, remaining};
    auto next = nested_scroll_ || mode_ == Mode::NestedDrag ? ancestor(remaining, source->get(), true, &route) : None<Ancestor>();
    if (gesture_ != gesture || (*source)->mode_ != Mode::Drag) return true;
    if (next) {
        auto &parent = *next->owner.get();
        if (parent.mode_ != Mode::NestedDrag) {
            parent.stop(true, false, false);
            if (!parent.acquire()) return false;
            parent.mode_ = Mode::NestedDrag;
            parent.nested_origin_ = Some(NestedOrigin{(*source)->owner_, (*source)->root_, (*source)->gesture_});
            source->get()->nested_drag_members_.push(next->owner.downgrade());
            next->metrics.offset = bounded(next->metrics.offset, next->metrics.maximum);
            next->metrics.boundary = ViewportBoundary::Clamped;
        }
        parent.nested_remaining_ = next->remainder;
        parent.nested_consumed_ = next->consumed;
        return parent.consume_drag(next->delta, next->metrics);
    }
    remaining = route.remaining;
    if (!(*source)->uses_elastic_deltas()) return true;
    if (source->get() == this) return write_pull(along(remaining), metrics);
    auto snapshot = session_.input().snapshot();
    if (!snapshot) return true;
    auto returned = transfer_delta(*snapshot, root_, (*source)->root_, remaining);
    auto owner_metrics = ui().viewport_metrics((*source)->viewport_);
    if (!returned || owner_metrics.is_err()) return true;
    return source->get()->write_pull((*source)->along(*returned), *owner_metrics);
}

void ScrollInteraction::release_nested_drag() {
    auto members = rstd::exchange(nested_drag_members_, Vec<rstd::rc::Weak<ScrollInteraction>>{});
    for (const auto &member : members)
        if (auto owner = member.upgrade(); owner && (*owner)->matches_origin(*this))
            owner->get()->stop(true, false, false);
}

auto ScrollInteraction::rebase_offset(Point value) -> Result<empty, UiError> {
    if (!lease_.active()) return ui().set_viewport_offset(viewport_, value);
    PresentationBatch batch;
    batch.viewport_offset(viewport_, value);
    auto result = ui().write_presentation(lease_, batch);
    if (result.is_ok()) {
        written_ = value;
        if (mode_ == Mode::Programmatic) restart_animation(value, now());
        else rebase_ = true;
    }
    return result;
}

auto ScrollInteraction::animate_offset(Point value, Serial duration_ns) -> Result<ScrollAnimation, UiError> {
    if (!finite(value.x) || !finite(value.y) || !duration_ns) return Err(UiError::InvalidValue);
    auto metrics = ui().viewport_metrics(viewport_);
    if (metrics.is_err()) return Err(metrics.unwrap_err());

    stop();
    error_ = None();
    if (!acquire()) return Err(*error_);
    auto status = rstd::rc::make_rc<ScrollAnimationStatus>(ScrollAnimationStatus::Running);
    animation_ = Some(OffsetAnimation{status.clone(), bounded(metrics->offset, metrics->maximum),
                                       masked(value), now(), duration_ns});
    mode_ = Mode::Programmatic;
    written_ = animation_->from;
    rebase_ = false;
    wake();
    return Ok(ScrollAnimation(rstd::move(status)));
}

void ScrollInteraction::restart_animation(Point from, Serial time) {
    auto &animation = *animation_;
    const auto elapsed = time > animation.started ? time - animation.started : Serial{};
    if (elapsed >= animation.duration) return;
    animation.duration -= elapsed;
    animation.from = from;
    animation.started = time;
}

auto ScrollInteraction::retarget_offset(const ScrollAnimation &animation, Point value) -> Result<empty, UiError> {
    if (!finite(value.x) || !finite(value.y)) return Err(UiError::InvalidValue);
    if (mode_ != Mode::Programmatic || animation_->status.get() != animation.status_.get() || !lease_.active())
        return Err(UiError::Cancelled);
    const auto target = masked(value);
    if (target == animation_->to) return Ok(empty{});
    auto metrics = ui().viewport_metrics(viewport_);
    if (metrics.is_err()) return Err(metrics.unwrap_err());
    restart_animation(metrics->offset, now());
    animation_->to = target;
    return Ok(empty{});
}

auto ScrollInteraction::sample_animation(MotionContext &context, const ViewportMetrics &metrics)
    -> Result<MotionStatus, MotionError> {
    const auto time = context.now_ns();
    auto &animation = *animation_;
    const auto target = bounded(animation.to, metrics.maximum);
    const auto elapsed = time > animation.started ? time - animation.started : Serial{};
    const bool finished = elapsed >= animation.duration;
    auto value = target;
    if (!finished) {
        const auto progress = Scalar(elapsed) / Scalar(animation.duration);
        const auto eased = progress * progress * (3 - 2 * progress);
        value = bounded({animation.from.x + (target.x - animation.from.x) * eased,
                         animation.from.y + (target.y - animation.from.y) * eased}, metrics.maximum);
    }
    PresentationBatch batch;
    batch.viewport_offset(viewport_, value);
    if (auto written = context.write(lease_, batch); written.is_err()) {
        error_ = Some(UiError::PropertyOwned);
        stop(true, false, false);
        return Ok(MotionStatus::Idle);
    }
    written_ = value;
    if (animation.user_scroll && value != metrics.offset) {
        const auto gesture = gesture_;
        changed_.emit({value, metrics.maximum}).unwrap();
        if (gesture != gesture_) {
            if (mode_ != Mode::Animate && mode_ != Mode::Programmatic) return Ok(MotionStatus::Idle);
            context.request_tick(time + rstd::cmp::min(Serial(8'333'333), ~Serial{} - time)).unwrap();
            return Ok(MotionStatus::Running);
        }
    }
    if (finished) {
        *animation.status.get() = ScrollAnimationStatus::Finished;
        stop(true, false, false);
        return Ok(MotionStatus::Idle);
    }
    const auto remaining = animation.duration - elapsed;
    const auto step = remaining < Serial(8'333'333) ? remaining : Serial(8'333'333);
    context.request_tick(time + (step <= ~Serial{} - time ? step : ~Serial{} - time)).unwrap();
    return Ok(MotionStatus::Running);
}

auto ScrollInteraction::describe_semantics() const -> Result<empty, UiError> {
    auto actions = semantics_action_mask(SemanticsAction::Focus);
    if (style_.user_scroll_enabled)
        actions |= semantics_action_mask(SemanticsAction::ScrollForward) |
                   semantics_action_mask(SemanticsAction::ScrollBackward);
    return session_.semantics().set_default(root_, {.role = SemanticsRole::ScrollArea, .actions = actions});
}

void ScrollInteraction::begin_axis(Axis &axis,Scalar value,Scalar velocity,Scalar maximum,bool enabled,Serial started) {
    if (!enabled) {axis={};return;}
    auto target=bounded(value,maximum);
    if (style_.motion.movement==ScrollMovement::Elastic && value!=target) spring(axis,value,velocity,target,started);
    else if (style_.motion.inertia && (style_.motion.fling || style_.motion.deceleration_rate > 0) &&
             __builtin_fabs(velocity) >= 1) axis={Phase::Decay,{value,velocity},0,started};
    else axis={Phase::Rest,{target,0},target,started};
}

void ScrollInteraction::begin_motion(const ViewportMetrics &metrics, Point velocity, Serial started, bool transferred) {
    const auto settle_drag = mode_ == Mode::Drag;
    velocity_ = style_.motion.inertia ? masked(velocity) : Point{};
    const auto gesture = gesture_;
    const auto offer_fling = !transferred && nested_scroll_ && velocity_ != Point{};
    if (offer_fling) {
        velocity_ = masked(pre_nested(ScrollNestedPhase::PreFling, velocity_));
        if (gesture == gesture_ && velocity_ == Point{}) {
            NestedRoute route{ScrollNestedPhase::PostFling, root_, {}, {}};
            (void)ancestor({}, nullptr, true, &route);
        }
    }
    if (gesture != gesture_) return;
    fling_velocity_ = velocity_;
    post_fling_delivered_ = false;
    begin_axis(x_,metrics.offset.x,velocity_.x,metrics.maximum.x,style_.axes.horizontal,started);
    begin_axis(y_,metrics.offset.y,velocity_.y,metrics.maximum.y,style_.axes.vertical,started);
    if (x_.phase == Phase::Rest && y_.phase == Phase::Rest) {
        if (settle_drag && begin_snap(metrics, started)) wake();
        else if (gesture == gesture_) stop(true, false);
        return;
    }
    if (!acquire()) {stop(true,false);return;}mode_=Mode::Animate;rebase_=false;
    if (uses_elastic_deltas()) {
        const auto stretch = overscroll(metrics);
        if (stretch) {
            const auto extent = along(metrics.viewport);
            const auto pull = edge_pull_ ? edge_pull_ : recover_pull(metrics);
            const auto slope = extent > 0 ? 1 + __builtin_fabs(pull) * .55 / extent : 1;
            Axis axis;
            spring(axis, stretch, along(velocity_) * .55 * style_.motion.rubber_scale / (slope * slope), 0, started);
            edge_spring_ = Some(EdgeSpring{axis, stretch > 0});
        }
        edge_pull_ = 0;
    }
    decay_position_ = metrics.offset;
    wake();
}

auto ScrollInteraction::decay_sample(const Axis &axis, Serial elapsed) const -> ScalarMotion {
    if (axis.phase != Phase::Decay) return axis.initial;
    if (const auto curve = style_.motion.fling) return curve(axis.initial, style_.motion.deceleration_rate, elapsed);
    const auto drag = -__builtin_log(style_.motion.deceleration_rate);
    if (drag > 0) return Decay{axis.initial, drag}.sample(elapsed);
    return {axis.initial.value + axis.initial.velocity * Scalar(elapsed) / 1e9, axis.initial.velocity};
}

auto ScrollInteraction::decay_hit_time(const Axis &axis, Scalar edge, Serial elapsed) const -> Serial {
    if (style_.motion.fling) {
        Serial first{}, last = elapsed;
        const auto forward = axis.initial.velocity > 0;
        while (first < last && last - first > 1'000) {
            const auto mid = first + (last - first) / 2;
            const auto value = decay_sample(axis, mid).value;
            if (forward ? value < edge : value > edge) first = mid;
            else last = mid;
        }
        return last;
    }
    auto seconds = (edge - axis.initial.value) / axis.initial.velocity;
    const auto drag = -__builtin_log(style_.motion.deceleration_rate);
    if (drag > 0) seconds = -__builtin_log1p(-bounded(seconds * drag, 1)) / drag;
    return static_cast<Serial>(bounded(seconds * 1e9, Scalar(elapsed)));
}

auto ScrollInteraction::begin_snap(const ViewportMetrics &metrics, Serial time) -> bool {
    const auto &snap = style_.motion.snap;
    if (!snap.enabled()) return false;
    const auto from = bounded(metrics.offset, metrics.maximum);
    const ScrollSnapRequest request{metrics, from, fling_velocity_, snap.alignment};
    const auto gesture = gesture_;
    Option<Point> chosen;
    if (snap.target) chosen = snap.target(request);
    else if (snap_layout_ && snap.alignment != ScrollSnapAlignment::None) chosen = (**snap_layout_)(request);
    if (!chosen && !snap.target && snap.interval != Point{}) {
        const auto nearest = [](Scalar value, Scalar step) { return step > 0 ? __builtin_round(value / step) * step : value; };
        chosen = Some(Point{nearest(from.x, snap.interval.x), nearest(from.y, snap.interval.y)});
    }
    if (gesture != gesture_ || !chosen || !finite(chosen->x) || !finite(chosen->y)) return false;
    const auto target = bounded(masked(*chosen), metrics.maximum);
    if (target == from || !acquire()) return false;
    auto status = rstd::rc::make_rc<ScrollAnimationStatus>(ScrollAnimationStatus::Running);
    animation_ = Some(OffsetAnimation{rstd::move(status), from, target, time, snap.duration_ns, true});
    mode_ = Mode::Programmatic;
    velocity_ = {};
    written_ = metrics.offset;
    return true;
}

void ScrollInteraction::finish_fling() {
    if (!nested_scroll_ || post_fling_delivered_ || fling_velocity_ == Point{}) return;
    post_fling_delivered_ = true;
    NestedRoute route{ScrollNestedPhase::PostFling, root_, fling_velocity_, {}};
    (void)ancestor({}, nullptr, true, &route);
}

auto ScrollInteraction::sample_axis(Axis &axis,Scalar maximum,Serial time) -> ScalarMotion {
    auto elapsed=time>=axis.started ? time-axis.started : 0;
    if (axis.phase==Phase::Decay) {
        auto result = decay_sample(axis, elapsed);
        if (!finite(result.value) || !finite(result.velocity)) {
            error_ = Some(UiError::InvalidValue);
            axis = {Phase::Rest, {bounded(axis.initial.value, maximum), 0}, 0, time};
            return axis.initial;
        }
        if (result.value<0 || result.value>maximum) {
            auto target=bounded(result.value,maximum);
            if (style_.motion.movement==ScrollMovement::Clamped) {axis={Phase::Rest,{target,0},target,time};return axis.initial;}
            const auto hit = decay_hit_time(axis, target, elapsed);
            const auto speed = decay_sample(axis, hit).velocity;
            spring(axis,target,speed,target,axis.started+hit);elapsed=time-axis.started;
        } else {
            if (__builtin_fabs(result.velocity)<1) {axis={Phase::Rest,{result.value,0},result.value,time};result.velocity=0;}
            return result;
        }
    }
    if (axis.phase==Phase::Spring) {
        auto omega=2/style_.motion.elasticity;Spring value{axis.initial,axis.target,omega*omega,2*omega,1};
        auto result=value.sample(elapsed);
        if (value.settled(result,.05,1)) {axis={Phase::Rest,{axis.target,0},axis.target,time};return axis.initial;}
        return result;
    }
    return axis.initial;
}

auto ScrollInteraction::sample_edge_spring(const ViewportMetrics &metrics, Serial time) -> bool {
    auto &edge = *edge_spring_;
    const auto offset = along(metrics.offset);
    const auto base = edge.trailing ? along(metrics.maximum) : 0;
    if (edge.trailing ? offset < base - 1e-6 : offset > base + 1e-6) {
        edge.axis.phase = Phase::Rest;
        return true;
    }
    const auto sample = sample_axis(edge.axis, 0, time);
    const auto stretch = edge.trailing ? sample.value : -sample.value;
    if (stretch < 0) edge.axis.phase = Phase::Rest;
    const auto capacity = .99 * along(metrics.viewport) * style_.motion.rubber_scale;
    const auto position = base + (edge.trailing ? 1 : -1) * bounded(stretch, capacity);
    velocity_ = edge.axis.phase == Phase::Rest ? Point{} : main_axis(sample.velocity);
    return position == offset || write(main_axis(position), metrics);
}

auto ScrollInteraction::boundary_hit(const Axis &axis, Scalar edge, Serial time) const -> BoundaryHit {
    if (axis.phase != Phase::Decay) return {0, time};
    const auto elapsed = time >= axis.started ? time - axis.started : 0;
    const auto hit = decay_hit_time(axis, edge, elapsed);
    return {decay_sample(axis, hit).velocity, axis.started + hit};
}

auto ScrollInteraction::start_boundary_spring(Scalar remaining, const ViewportMetrics &metrics, BoundaryHit hit, Serial time) -> bool {
    Axis axis;
    spring(axis, 0, hit.velocity, 0, hit.time);
    edge_spring_ = Some(EdgeSpring{axis, remaining > 0});
    return sample_edge_spring(metrics, time);
}

auto ScrollInteraction::handoff_fling(Point velocity, Point remaining, const ViewportMetrics &metrics,
                                      Serial hit, Serial time, bool complete) -> bool {
    if (!nested_scroll_ || (!ancestor(velocity, nullptr) && !has_nested_connection())) return false;
    auto queued = ui().defer([weak = owner_, gesture = gesture_, remaining, metrics, hit, time, velocity, complete](UiDocument &doc)
            -> Result<empty, UiError> {
        auto live = weak.upgrade();
        if (!live || (*live)->gesture_ != gesture || (*live)->mode_ != Mode::Animate || !(*live)->lease_.active())
            return Ok(empty{});
        auto &source = *live->get();
        source.handoff_pending_ = false;
        if (!source.session_.input().pointer_available(source.root_)) {
            source.stop(true, false, false);
            return Ok(empty{});
        }
        source.post_fling_delivered_ = true;
        NestedRoute route{ScrollNestedPhase::PostFling, source.root_, difference(source.fling_velocity_, velocity), velocity};
        auto next = source.ancestor(velocity, nullptr, true, &route);
        UiId basis = source.root_;
        for (unsigned depth{}; next && depth < 256 && source.gesture_ == gesture; ++depth) {
            auto &target = *next->owner.get();
            basis = target.root_;
            auto available = next->delta;
            if (target.style_.motion.movement == ScrollMovement::Elastic) {
                auto boundary = doc.set_viewport_boundary(target.viewport_, ViewportBoundary::Unclamped);
                if (boundary.is_err()) return boundary;
                target.boundary_owned_ = true;
            }
            target.nested_scroll_ = target.has_nested_connection() || target.ancestor(available, nullptr, false).is_some();
            target.begin_motion(next->metrics, available, hit, true);
            const bool accepted = target.mode_ == Mode::Animate;
            route.remaining = accepted ? next->remainder : sum(available, next->remainder);
            route.consumed = accepted ? sum(next->consumed, available) : next->consumed;
            next = target.ancestor(route.remaining, nullptr, true, &route);
        }
        if (source.gesture_ != gesture || !complete) return Ok(empty{});
        if (source.uses_elastic_deltas() && route.remaining != Point{}) {
            auto snapshot = source.session_.input().snapshot();
            auto available = snapshot ? transfer_delta(*snapshot, basis, source.root_, route.remaining) : None<Point>();
            if (available && __builtin_fabs(source.along(*available)) >= 1 &&
                source.start_boundary_spring(source.along(remaining), metrics, {source.along(*available), hit}, time))
                return Ok(empty{});
        }
        if (source.begin_snap(metrics, time)) source.wake();
        else if (source.gesture_ == gesture) source.stop(true, false, false);
        return Ok(empty{});
    });
    handoff_pending_ = queued.is_ok();
    return handoff_pending_;
}

auto ScrollInteraction::reached_boundary(Point remaining, const ViewportMetrics &metrics, Serial time) -> bool {
    if (mode_ == Mode::Drag || mode_ == Mode::NestedDrag) {
        if (!nested_scroll_ && mode_ != Mode::NestedDrag)
            return !uses_elastic_deltas() || remaining == Point{} || write_pull(along(remaining), metrics);
        return offer_drag(remaining, metrics);
    }
    if (mode_ != Mode::Animate || remaining == Point{}) return true;
    if (single_axis()) {
        const auto delta = along(remaining);
        if (__builtin_fabs(delta) <= 1e-6) return true;
        const auto &axis = style_.axes.horizontal ? x_ : y_;
        const auto hit = boundary_hit(axis, along(decay_position_) - delta, time);
        if (handoff_fling(main_axis(hit.velocity), remaining, metrics, hit.time, time, true)) return true;
        if (uses_elastic_deltas()) return start_boundary_spring(delta, metrics, hit, time);
        return true;
    }
    Point velocity;
    Serial hit = time;
    auto cut_axis = [&](Axis &axis, Scalar remainder, Scalar &position, Scalar offset, Scalar &available) {
        if (axis.phase != Phase::Decay || __builtin_fabs(remainder) <= 1e-6) return;
        const auto boundary = boundary_hit(axis, position - remainder, time);
        available = boundary.velocity;
        hit = rstd::cmp::min(hit, boundary.time);
        axis = {Phase::Rest, {offset, 0}, offset, time};
        position = offset;
    };
    cut_axis(x_, remaining.x, decay_position_.x, metrics.offset.x, velocity.x);
    cut_axis(y_, remaining.y, decay_position_.y, metrics.offset.y, velocity.y);
    if (velocity != Point{})
        (void)handoff_fling(velocity, remaining, metrics, hit, time, x_.phase == Phase::Rest && y_.phase == Phase::Rest);
    return true;
}

auto ScrollInteraction::sample_scroll_deltas(MotionContext &context, const ViewportMetrics &metrics)
    -> Result<MotionStatus, MotionError> {
    const auto time = context.now_ns();
    const auto tick = [&] {
        context.request_tick(time + rstd::cmp::min(Serial(8'333'333), ~Serial{} - time)).unwrap();
        return Ok(MotionStatus::Running);
    };
    const auto finish = [&] {
        const auto gesture = gesture_;
        finish_fling();
        if (gesture != gesture_) return Ok(MotionStatus::Idle);
        auto current = ui().viewport_metrics(viewport_);
        if (!error_ && current.is_ok() && begin_snap(*current, time)) return tick();
        if (gesture == gesture_) stop(true, false, false);
        return Ok(MotionStatus::Idle);
    };
    if (handoff_pending_) return tick();
    if (edge_spring_) {
        if (!sample_edge_spring(metrics, time) || edge_spring_->axis.phase == Phase::Rest) return finish();
        return tick();
    }
    if (pending_delta_) {
        const auto progress = (**pending_delta_)();
        if (!progress.settled) return tick();
        pending_delta_ = None();
        if (progress.reached_boundary) return finish();
        decay_position_.x -= pending_decay_delta_.x - progress.consumed.x;
        decay_position_.y -= pending_decay_delta_.y - progress.consumed.y;
    }
    const auto x = decay_sample(x_, time >= x_.started ? time - x_.started : 0);
    const auto y = decay_sample(y_, time >= y_.started ? time - y_.started : 0);
    if (!finite(x.value) || !finite(x.velocity) || !finite(y.value) || !finite(y.velocity)) {
        error_ = Some(UiError::InvalidValue);
        return finish();
    }
    const auto value = masked({x.value, y.value});
    velocity_ = masked({x.velocity, y.velocity});
    const Point delta{value.x - decay_position_.x, value.y - decay_position_.y};
    decay_position_ = value;
    auto resolution = resolve_delta(delta, metrics);
    if (resolution.is_err()) {
        error_ = Some(resolution.unwrap_err());
        return finish();
    }
    if (auto resolved = resolution.unwrap(); resolved) {
        if (resolved->after_layout) {
            pending_delta_ = rstd::move(resolved->after_layout);
            pending_decay_delta_ = delta;
            pending_decay_time_ = time;
            return tick();
        }
        const auto moved = resolved->consumed;
        const Point offset{metrics.offset.x + moved.x, metrics.offset.y + moved.y};
        if (moved != Point{} && !write(offset, metrics)) return finish();
        if (moved != delta) {
            auto at_edge = metrics;
            at_edge.offset = offset;
            if (!reached_boundary(difference(delta, moved), at_edge, time)) return finish();
            if (edge_spring_ || handoff_pending_) return tick();
            if (single_axis() || (x_.phase == Phase::Rest && y_.phase == Phase::Rest)) return finish();
        }
    } else {
        const auto offset = bounded({metrics.offset.x + delta.x, metrics.offset.y + delta.y}, metrics.maximum);
        if (offset != metrics.offset && !write(offset, metrics)) return finish();
    }
    if (__builtin_fabs(x.velocity) < 1 && __builtin_fabs(y.velocity) < 1) return finish();
    return tick();
}

void ScrollInteraction::press(EventContext &,const RoutedEvent &event) {
    if (!style_.user_scroll_enabled || (!style_.motion.enabled && mode_ != Mode::Programmatic) ||
        (event.input.pointer_kind != PointerKind::Touch && event.input.button != PointerButton::Primary) ||
        pointer_.is_some()) return;
    stop(false, false);
    if (!style_.motion.enabled) return;
    pointer_=Some(PointerKey{event.input.device,event.input.pointer});mode_=Mode::Pending;error_=None();
}

void ScrollInteraction::pending_up(EventContext &,const RoutedEvent &event) {
    if (mode_!=Mode::Pending || pointer_!=Some(PointerKey{event.input.device,event.input.pointer})) return;
    pointer_=None();auto metrics=ui().viewport_metrics(viewport_);if (metrics.is_ok()) begin_motion(*metrics,{},now());else stop(true,false);
}

auto ScrollInteraction::make(UiSession &session, UiId root, UiId viewport, ScrollStyle &style,
                             Signal<void(UserScroll)> &changed, Option<Box<ScrollDeltaResolver>> &resolve_delta,
                             Option<Box<ScrollSnapLayout>> &snap_layout)
    -> Result<rstd::rc::Rc<ScrollInteraction>, ControlError> {
    auto owner = rstd::rc::make_rc<ScrollInteraction>(session, root, viewport, style, changed, resolve_delta, snap_layout);
    auto self = owner.downgrade();
    owner.get()->owner_ = self;
    auto participant=session.input().arena().bind(root,self);if (participant.is_err()) return Err(ControlError{{},Some(participant.unwrap_err())});
    owner.get()->participant_=rstd::move(participant).unwrap();
    auto motion=session.motion().bind(self);if (motion.is_err()) return Err(ControlError{Some(UiError::Limit),{}});owner.get()->motion_=rstd::move(motion).unwrap();
    auto press=session.input().bind_on(root,EventType::PointerDown,self,[](auto &owner,auto &context,const auto &event){owner.press(context,event);},RoutePhase::Capture);
    if (press.is_err()) return Err(ControlError{{},Some(press.unwrap_err())});owner.get()->press_=rstd::move(press).unwrap();
    auto up=session.input().bind_on(root,EventType::PointerUp,self,[](auto &owner,auto &context,const auto &event){owner.pending_up(context,event);},RoutePhase::Capture);
    if (up.is_err()) return Err(ControlError{{},Some(up.unwrap_err())});owner.get()->up_=rstd::move(up).unwrap();
    auto availability=session.input().on_pointer_availability_changed(root,[self](bool available){
        if (available) return;auto value=self.upgrade();if (value.is_some()) value->get()->stop(true,false);
    });
    if (availability.is_err()) return Err(ControlError{{},Some(availability.unwrap_err())});owner.get()->availability_=rstd::move(availability).unwrap();
    owner.get()->metrics_=session.document().on_viewport_changed(viewport,[self](ViewportMetrics metrics){
        auto owner=self.upgrade();if (owner.is_none()) return;auto &value=*owner->get();
        if (value.mode_ == Mode::Programmatic) {
            if (value.written_ != metrics.offset) {
                value.restart_animation(metrics.offset, value.now());
                value.written_ = metrics.offset;
            }
        } else if (value.geometry_known_ &&
                   (value.viewport_size_ != metrics.viewport || value.maximum_ != metrics.maximum ||
                    (value.mode_ == Mode::Animate && value.written_ != metrics.offset))) {
            value.rebase_ = true;
        }
        value.viewport_size_=metrics.viewport;value.maximum_=metrics.maximum;value.geometry_known_=true;
    }).unwrap();return Ok(rstd::move(owner));
}

void ScrollInteraction::stop(bool clamp,bool release,bool request) {
    release_nested_drag();
    bool owned=mode_!=Mode::Idle || lease_.active() || boundary_owned_;auto pointer=pointer_;pointer_=None();mode_=Mode::Idle;velocity_={};
    if (lease_.active()) ui().release_presentation(lease_);else lease_.revoke();
    pending_delta_ = None();
    edge_spring_ = None();
    edge_pull_ = 0;
    nested_origin_ = None();
    drag_delta_ = {};
    nested_remaining_ = {};
    nested_consumed_ = {};
    fling_velocity_ = {};
    post_fling_delivered_ = false;
    nested_scroll_ = false;
    handoff_pending_ = false;
    ++gesture_;
    if (clamp && owned && ui().valid(viewport_)) {ui().set_viewport_boundary(viewport_,ViewportBoundary::Clamped);boundary_owned_=false;}
    if (release && pointer.is_some()) {auto key=*pointer;session_.input().release_input(slice<PointerKey>::from_raw_parts(&key,usize(1)));}
    if (animation_) {
        if (*animation_->status == ScrollAnimationStatus::Running)
            *animation_->status.get() = ScrollAnimationStatus::Cancelled;
        animation_ = None();
        if (ui().valid(viewport_)) ui().invalidate_viewport_observation(viewport_);
    }
    if (request && owned) wake();
}

auto ScrollInteraction::observe(InputContext &context,const InputPacket &packet) -> InputDecision {
    if (!style_.user_scroll_enabled || !style_.motion.enabled || (!style_.axes.horizontal && !style_.axes.vertical) ||
        (packet.input.pointer_kind!=PointerKind::Touch && packet.input.type==EventType::PointerDown && packet.input.button!=PointerButton::Primary) ||
        (pointer_.is_some() && *pointer_!=PointerKey{packet.input.device,packet.input.pointer})) return InputDecision::reject();
    auto local=local_event(context.snapshot(),packet.input);if (local.is_none()) return InputDecision::reject();
    return pan_.observe(context,{rstd::move(local).unwrap(),packet.target});
}

auto ScrollInteraction::handle(EventContext &context,const RoutedEvent &event) -> bool {
    if (event.input.type==EventType::Wheel && (mode_==Mode::Drag || mode_==Mode::NestedDrag)) {context.prevent_default();return true;}
    if (event.input.type==EventType::CaptureGained && event.target==root_ && style_.motion.enabled && style_.user_scroll_enabled) {
        auto snapshot=session_.input().snapshot();auto metrics=ui().viewport_metrics(viewport_);
        if (snapshot.is_none() || metrics.is_err()) return true;
        auto local=local_event(*snapshot,event.input);if (local.is_none()) return true;
        if (mode_ == Mode::Programmatic) stop(false, false);
        if (!acquire()) return true;
        pointer_ = Some(PointerKey{event.input.device, event.input.pointer});
        nested_scroll_ = has_nested_connection() || ancestor(masked({1, 1}), this, false).is_some();
        mode_ = Mode::Drag;
        anchor_ = local->position;
        last_local_ = uses_scroll_deltas() ? map_delta(*inverse_, event.input.position) : local->position;
        origin_ = metrics->offset;
        rebase_ = false;
        tracked_offset_ = metrics->offset;
        tracker_.remove(*pointer_);
        if (style_.motion.movement==ScrollMovement::Elastic) {ui().set_viewport_boundary(viewport_,ViewportBoundary::Unclamped).unwrap();boundary_owned_=true;}
        context.focus();return true;
    }
    auto key=PointerKey{event.input.device,event.input.pointer};if (pointer_!=Some(PointerKey(key))) return false;
    if (event.input.type==EventType::PointerCancel || event.input.type==EventType::CaptureLost) {stop(true,false);return true;}
    if (mode_!=Mode::Drag || (event.input.type!=EventType::PointerMove && event.input.type!=EventType::PointerUp)) return false;
    auto snapshot=session_.input().snapshot();auto metrics=ui().viewport_metrics(viewport_);if (snapshot.is_none() || metrics.is_err()) return true;
    auto local=local_event(*snapshot,event.input);if (local.is_none()) {error_=Some(UiError::InvalidValue);stop(true,false);return true;}
    const bool measured = uses_scroll_deltas();
    if (rebase_) {
        if (!measured)
            tracker_.translate(key, {metrics->offset.x - tracked_offset_.x, metrics->offset.y - tracked_offset_.y});
        origin_ = tracked_offset_ = metrics->offset;
        anchor_ = last_local_;
        rebase_ = false;
    }
    if (event.input.type==EventType::PointerMove) {
        const auto gesture = gesture_;
        auto value = metrics->offset;
        if (measured) {
            const auto delta = masked(difference(last_local_, map_delta(*inverse_, event.input.position)));
            if (!drag_by(delta, *metrics)) { stop(true, false); return true; }
        } else {
            value = drag_value(local->position, *metrics);
            if (value != metrics->offset && !write(value, *metrics)) { stop(true, false); return true; }
        }
        if (gesture != gesture_) return true;
        track(rstd::move(local).unwrap(), *metrics, value, measured);
        last_local_ = measured ? map_delta(*inverse_, event.input.position) : inverse_->map(event.input.position);
    } else {
        release_nested_drag();
        pending_delta_ = None();
        track(rstd::move(local).unwrap(), *metrics, metrics->offset, measured);auto velocity=tracker_.velocity(key);tracker_.remove(key);pointer_=None();
        begin_motion(*metrics,velocity.is_some() ? *velocity : Point{},now());
    }
    context.prevent_default();return true;
}

auto ScrollInteraction::sample(MotionContext &context,FrameTiming) -> Result<MotionStatus,MotionError> {
    if (mode_==Mode::Idle) return Ok(MotionStatus::Idle);
    if (!session_.input().pointer_available(root_)) {stop(true,false,false);return Ok(MotionStatus::Idle);}
    if (mode_!=Mode::Animate && mode_!=Mode::Programmatic) return Ok(MotionStatus::Idle);
    if (!lease_.active()) {error_=Some(UiError::PropertyOwned);stop(true,false,false);return Ok(MotionStatus::Idle);}
    auto metrics=ui().viewport_metrics(viewport_);
    if (metrics.is_err()) {context.request_tick(context.now_ns()+8'333'333).unwrap();return Ok(MotionStatus::Running);}
    if (mode_ == Mode::Programmatic) return sample_animation(context, *metrics);
    if (uses_scroll_deltas()) return sample_scroll_deltas(context, *metrics);
    if (rebase_) {
        begin_axis(x_,metrics->offset.x,velocity_.x,metrics->maximum.x,style_.axes.horizontal,context.now_ns());
        begin_axis(y_,metrics->offset.y,velocity_.y,metrics->maximum.y,style_.axes.vertical,context.now_ns());rebase_=false;
    }
    auto x=sample_axis(x_,metrics->maximum.x,context.now_ns()),y=sample_axis(y_,metrics->maximum.y,context.now_ns());velocity_={x.velocity,y.velocity};
    PresentationBatch batch;Point value{x.value,y.value};batch.viewport_offset(viewport_,value);auto written=context.write(lease_,batch);
    if (written.is_err()) {error_=Some(UiError::PropertyOwned);stop(true,false,false);return Ok(MotionStatus::Idle);}
    written_=value;if (value!=metrics->offset) changed_.emit({value,metrics->maximum}).unwrap();
    if (x_.phase == Phase::Rest && y_.phase == Phase::Rest) {
        const auto gesture = gesture_;
        auto current = *metrics;
        current.offset = value;
        if (error_ || !begin_snap(current, context.now_ns())) {
            if (gesture == gesture_) stop(true, false, false);
            return Ok(MotionStatus::Idle);
        }
    }
    context.request_tick(context.now_ns()+8'333'333).unwrap();return Ok(MotionStatus::Running);
}

auto ScrollState::shift(Scalar value, Scalar amount, Scalar step, Scalar maximum) -> Scalar {
    if (amount > 0)
        return amount >= (maximum - value) / step ? maximum : value + amount * step;
    return amount <= -value / step ? 0 : value + amount * step;
}

auto ScrollState::consumed(Scalar change, Scalar step, Scalar amount) -> Scalar {
    auto result = change / step;
    return amount > 0 ? (result > amount ? amount : result)
                      : (result < amount ? amount : result);
}

auto ScrollState::set_style(UiDocument &ui, ScrollStyle style) -> Result<empty, UiError> {
    if (!style.valid())
        return Err(UiError::InvalidValue);
    stop();
    auto result = ui.set_viewport_axes(viewport_, style.axes);
    if (result.is_ok()) {
        style_ = style;
        if (interaction_.is_some()) {
            interaction_->get()->configure();
            return (*interaction_)->describe_semantics();
        }
    }
    return result;
}

auto ScrollState::finish_configuration() -> Result<empty, UiError> {
    stop();
    if (interaction_.is_none()) return Ok(empty{});
    interaction_->get()->configure();
    return (*interaction_)->describe_semantics();
}

auto ScrollState::connect(UiSession &session, UiId root) -> Result<empty, ControlError> {
    auto value = ScrollInteraction::make(session, root, viewport_, style_, user_scrolled_, resolve_delta_, snap_layout_);
    if (value.is_err()) return Err(value.unwrap_err());
    auto described = (*value)->describe_semantics();
    if (described.is_err()) return Err(ControlError{Some(described.unwrap_err()), {}});
    interaction_ = Some(rstd::move(value).unwrap());
    return Ok(empty{});
}

void ScrollState::handle(EventContext &context, const RoutedEvent &event) {
    if (interaction_.is_some() && interaction_->get()->handle(context,event)) return;
    if (!style_.user_scroll_enabled) return;
    const bool semantic = event.input.type == EventType::SemanticAction && event.target == event.current &&
        (event.input.semantic_action == SemanticsAction::ScrollForward || event.input.semantic_action == SemanticsAction::ScrollBackward);
    if (semantic && context.default_prevented()) return;
    if (semantic || event.input.type==EventType::Wheel || (event.input.type==EventType::KeyDown && event.target==event.current)) stop();
    auto visual = context.visual(viewport_);
    if (!visual || visual->viewport.is_none())
        return;
    auto actual=context.document().viewport_metrics(viewport_);
    auto metrics=actual.is_ok() ? *actual : *visual->viewport;
    auto next = metrics.offset;
    if (event.input.type == EventType::PointerDown &&
        event.input.button == PointerButton::Primary) {
        if (context.focus().is_ok())
            context.prevent_default();
    } else if (event.input.type == EventType::Wheel) {
        auto remaining = context.scroll_remaining();
        bool horizontal=style_.wheel_mode==ScrollWheelMode::SingleAxis && style_.axes.horizontal && !style_.axes.vertical && __builtin_fabs(remaining.y)>__builtin_fabs(remaining.x);
        bool vertical=style_.wheel_mode==ScrollWheelMode::SingleAxis && style_.axes.vertical && !style_.axes.horizontal && __builtin_fabs(remaining.x)>__builtin_fabs(remaining.y);
        auto amount=horizontal ? Point{remaining.y,0} : vertical ? Point{0,remaining.x} : remaining;
        auto step = event.input.scroll_unit == ScrollUnit::Lines ? style_.line_step : Scalar(1);
        auto direction = style_.wheel_direction;
        auto to_input = [direction, amount, step, horizontal, vertical](Point delta) {
            Point used{consumed(delta.x * direction.x, step, amount.x),
                       consumed(delta.y * direction.y, step, amount.y)};
            return horizontal ? Point{0, used.x} : vertical ? Point{used.y, 0} : used;
        };
        const Point delta{style_.axes.horizontal ? amount.x * direction.x * step : 0,
                          style_.axes.vertical ? amount.y * direction.y * step : 0};
        if (resolve_delta_) {
            auto result = (**resolve_delta_)(delta, metrics, context.can_defer_scroll(), {});
            if (result.is_err()) return;
            if (auto resolved = result.unwrap(); resolved) {
                if (resolved->after_layout) {
                    auto *document = &context.document();
                    auto id = event.current;
                    auto deferred = context.defer_scroll([document, id, to_input,
                            complete = resolved->after_layout.unwrap()]() mutable -> Point {
                        const auto moved = (*complete)().consumed;
                        if (moved != Point{})
                            if (auto state = document->state<ScrollState>(id)) {
                                auto settled = document->viewport_metrics(state->viewport_);
                                if (settled.is_ok()) state->user_scrolled_.emit({settled->offset, settled->maximum}).unwrap();
                            }
                        return to_input(moved);
                    });
                    if (deferred.is_ok()) return;
                } else {
                    next = {next.x + resolved->consumed.x, next.y + resolved->consumed.y};
                    if (next == metrics.offset || context.document().set_viewport_offset(viewport_, next).is_err()) return;
                    context.consume_scroll(to_input(resolved->consumed));
                    if (context.scroll_remaining() == Point{}) context.prevent_default();
                    user_scrolled_.emit({next, metrics.maximum}).unwrap();
                    return;
                }
            }
        }
        next = {shift(next.x, amount.x * direction.x, step, metrics.maximum.x),
                shift(next.y, amount.y * direction.y, step, metrics.maximum.y)};
        if (next == metrics.offset)
            return;
        if (context.document().set_viewport_offset(viewport_, next).is_err())
            return;
        context.consume_scroll(to_input({next.x - metrics.offset.x, next.y - metrics.offset.y}));
        if (context.scroll_remaining() == Point{})
            context.prevent_default();
        user_scrolled_.emit({next, metrics.maximum}).unwrap();
    } else if (semantic || (event.input.type == EventType::KeyDown && event.target == event.current)) {
        auto page = [&](Scalar direction) {
            if (style_.axes.horizontal && (!style_.axes.vertical || (semantic && metrics.maximum.y <= 0)))
                next.x = shift(next.x, direction * metrics.viewport.width, 1, metrics.maximum.x);
            else next.y = shift(next.y, direction * metrics.viewport.height, 1, metrics.maximum.y);
        };
        if (semantic) {
            const auto direction = event.input.semantic_action == SemanticsAction::ScrollForward ? 1 : -1;
            page(style_.reverse_semantics ? -direction : direction);
            if (next == metrics.offset) return;
        } else switch (event.input.key) {
        case Key::Left:
            next.x = shift(next.x, -1, style_.line_step, metrics.maximum.x);
            break;
        case Key::Right:
            next.x = shift(next.x, 1, style_.line_step, metrics.maximum.x);
            break;
        case Key::Up:
            next.y = shift(next.y, -1, style_.line_step, metrics.maximum.y);
            break;
        case Key::Down:
            next.y = shift(next.y, 1, style_.line_step, metrics.maximum.y);
            break;
        case Key::PageUp:
            page(-1);
            break;
        case Key::PageDown:
            page(1);
            break;
        case Key::Home:
            next = {};
            break;
        case Key::End:
            next = metrics.maximum;
            break;
        default:
            return;
        }
        if (context.document().set_viewport_offset(viewport_, next).is_ok()) {
            context.prevent_default();
            if (semantic) context.stop_propagation();
            if (next != metrics.offset) user_scrolled_.emit({next, metrics.maximum}).unwrap();
        }
    }
}

auto ScrollState::rebase_offset(UiDocument &ui, Point value) -> Result<empty, UiError> {
    return interaction_.is_some() ? interaction_->get()->rebase_offset(value)
                                  : ui.set_viewport_offset(viewport_, value);
}

auto ScrollView::rebase_offset(UiDocument &ui, UiId id, Point value) -> Result<empty, UiError> {
    auto state = ui.state<ScrollState>(id);
    return state ? state->rebase_offset(ui, value)
                 : Result<empty, UiError>(Err(UiError::InvalidId));
}

auto ScrollView::animate_offset(UiDocument &ui, UiId id, Point value, Serial duration_ns)
    -> Result<ScrollAnimation, UiError> {
    auto state = ui.state<ScrollState>(id);
    if (!state || !state->interaction_) return Err(UiError::InvalidId);
    return state->interaction_->get()->animate_offset(value, duration_ns);
}

auto ScrollView::retarget_offset(UiDocument &ui, UiId id, const ScrollAnimation &animation, Point value)
    -> Result<empty, UiError> {
    auto state = ui.state<ScrollState>(id);
    if (!state || !state->interaction_) return Err(UiError::InvalidId);
    return state->interaction_->get()->retarget_offset(animation, value);
}

auto ScrollView::prepare(UiDocument &ui, UiEditBatch &edit, Option<UiId> previous,
                    NodeStyle style, ScrollStyle scroll, Option<NodeStyle> before) -> Result<Prepared, UiError> {
    if (!scroll.valid()) return Err(UiError::InvalidValue);
    auto state = previous.is_some() ? ui.state<ScrollState>(*previous) : nullptr;
    if (previous.is_some() && !state) return Err(UiError::InvalidId);
    style.focusable = true;
    auto node = previous.is_some() ? edit.existing(*previous) : edit.create(LayoutKind::Stack, style);
    if (before.is_some()) { before->focusable = true; edit.style(node, NodeStylePatch::difference(*before, style)); }
    else edit.style(node, style);
    bool changed = state && state->style() != scroll;
    if (changed) {
        struct Patch final : UiStatePatch {
            ScrollStyle style;
            explicit Patch(ScrollStyle value) : style(value) {}
            auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<ScrollState>(); }
            void publish(UiNodeState &target) override { static_cast<UiStoredState<ScrollState> &>(target).value.configure_prepared(style); }
        };
        auto patch = Box<Patch>::make(scroll);
        edit.patch(node, Box<UiStatePatch>::from_raw(rstd::move(patch).into_raw().template cast<UiStatePatch>()));
    }
    return Ok(Prepared{node, previous.is_none(), changed});
}

auto ScrollView::prepare_state(UiDocument::PreparedEdit &edit, Prepared prepared, ScrollStyle scroll) -> Result<empty, UiError> {
    auto children = edit.children(prepared.node);
    if (children.len() != usize(1)) return Err(UiError::MissingChild);
    if (prepared.created) {
        auto stored = edit.state(prepared.node, ScrollState(children[usize()], scroll));
        if (stored.is_err()) return stored;
    }
    if (prepared.created || prepared.changed) return edit.viewport_axes(children[usize()], scroll.axes);
    return Ok(empty{});
}

auto ScrollView::create(UiSession &session, UiId content, NodeStyle style, ScrollStyle scroll)
    -> Result<UiId, ControlError> {
    auto &ui = session.document();
    if (!scroll.valid())
        return Err(ControlError{Some(UiError::InvalidValue), {}});
    if (!ui.valid(content))
        return Err(ControlError{Some(UiError::InvalidId), {}});
    style.focusable = true;
    auto root = ui.create<Stack>(style);
    if (root.is_err())
        return Err(ControlError{Some(root.unwrap_err()), {}});
    auto id = root.unwrap();
    auto failure = [&](UiError error) -> Result<UiId, ControlError> {
        ui.destroy(id);
        return Err(ControlError{Some(error), {}});
    };
    auto viewport = ui.append<Viewport>(id);
    if (viewport.is_err())
        return failure(viewport.unwrap_err());
    auto view = viewport.unwrap();
    ui.set_viewport_axes(view, scroll.axes).unwrap();
    auto stored = ui.attach_state(id, ScrollState(view, scroll));
    if (stored.is_err())
        return failure(stored.unwrap_err());
    auto behavior = session.input().attach_behavior<ScrollState>(id);
    if (behavior.is_err()) {
        ui.destroy(id);
        return Err(ControlError{{}, Some(behavior.unwrap_err())});
    }
    auto attached = ui.attach(view, content);
    if (attached.is_err())
        return failure(attached.unwrap_err());
    auto connected=ui.state<ScrollState>(id)->connect(session,id);
    if (connected.is_err()) {ui.destroy(id);return Err(connected.unwrap_err());}
    return Ok(id);
}

auto ScrollView::set_offset(UiDocument &ui, UiId id, Point value) -> Result<empty, UiError> {
    auto state = ui.state<ScrollState>(id);
    if (state) state->stop();
    return state ? ui.set_viewport_offset(state->viewport(), value)
                 : Result<empty, UiError>(Err(UiError::InvalidId));
}

auto ScrollView::set_delta_resolver(UiDocument &ui, UiId id, Option<Box<ScrollDeltaResolver>> resolver)
    -> Result<empty, UiError> {
    auto state = ui.state<ScrollState>(id);
    if (!state) return Err(UiError::InvalidId);
    state->resolve_delta_ = rstd::move(resolver);
    return Ok(empty{});
}

auto ScrollView::set_snap_layout(UiDocument &ui, UiId id, Option<Box<ScrollSnapLayout>> layout)
    -> Result<empty, UiError> {
    auto state = ui.state<ScrollState>(id);
    if (!state) return Err(UiError::InvalidId);
    state->snap_layout_ = rstd::move(layout);
    return Ok(empty{});
}

auto ScrollView::bind_nested_scroll(UiDocument &ui, UiId id, Box<ScrollNestedCallback> callback)
    -> Result<ScrollNestedBinding, UiError> {
    const NodeStateKey key{rstd::any::TypeId::of<NestedConnection>(), 0};
    auto previous = ui.state<NestedConnection>(id, key);
    if (previous && previous->guard.alive()) return Err(UiError::PropertyOwned);
    auto owner = rstd::rc::make_rc<ScrollNestedReceiver>(rstd::move(callback));
    auto binding = ui.bindings().make();
    NestedConnection entry{owner.downgrade(), binding.guard()};
    if (previous) *previous = rstd::move(entry);
    else {
        auto attached = ui.attach_state(id, rstd::move(entry), key);
        if (attached.is_err()) return Err(attached.unwrap_err());
    }
    return Ok(ScrollNestedBinding(rstd::move(owner), rstd::move(binding)));
}

auto ScrollView::metrics(UiDocument &ui, UiId id) -> Result<ViewportMetrics, UiError> {
    auto state = ui.state<ScrollState>(id);
    return state ? ui.viewport_metrics(state->viewport())
                 : Result<ViewportMetrics, UiError>(Err(UiError::InvalidId));
}

auto ScrollView::content(UiDocument &ui, UiId id) -> Result<Option<UiId>, UiError> {
    auto state = ui.state<ScrollState>(id);
    return state ? ui.viewport_content(state->viewport())
                 : Result<Option<UiId>, UiError>(Err(UiError::InvalidId));
}

auto ScrollView::attach_content(UiDocument &ui, UiId id, UiId content) -> Result<empty, UiError> {
    auto state = ui.state<ScrollState>(id);
    return state ? ui.attach(state->viewport(), content)
                 : Result<empty, UiError>(Err(UiError::InvalidId));
}

auto ScrollView::set_style(UiDocument &ui, UiId id, ScrollStyle style) -> Result<empty, UiError> {
    auto state = ui.state<ScrollState>(id);
    return state ? state->set_style(ui, style)
                 : Result<empty, UiError>(Err(UiError::InvalidId));
}
} // namespace vui
