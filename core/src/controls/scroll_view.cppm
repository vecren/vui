export module vui.controls:scroll_view;
import vui;
export import :scroll_types;
import :types;
using namespace rstd::prelude;
namespace vui {
class ScrollInteraction;
}

export namespace vui {
enum class ScrollAnimationStatus { Running, Finished, Cancelled };

class ScrollAnimation {
    friend class ScrollInteraction;
    rstd::rc::Rc<ScrollAnimationStatus> status_;
    explicit ScrollAnimation(rstd::rc::Rc<ScrollAnimationStatus> status) : status_(rstd::move(status)) {}

  public:
    auto status() const -> ScrollAnimationStatus { return status_ ? *status_ : ScrollAnimationStatus::Cancelled; }
};

class ScrollDeltaLease {
    friend class ScrollInteraction;
    rstd::rc::Weak<ScrollInteraction> owner_;
    Serial gesture_{};
    ScrollDeltaLease(rstd::rc::Weak<ScrollInteraction> owner, Serial gesture)
        : owner_(rstd::move(owner)), gesture_(gesture) {}

  public:
    auto write(Point offset, const ViewportMetrics &metrics) const -> Result<empty, UiError>;
    // A deferred request reports a remainder here once layout confirms its boundary.
    auto reached_boundary(Point remaining, const ViewportMetrics &metrics) const -> Result<empty, UiError>;
};

struct ScrollDeltaProgress {
    Point consumed;
    bool settled{}, reached_boundary{};
};

using ScrollDeltaCompletion = rstd::dyn<rstd::FnMut<ScrollDeltaProgress()>>;

struct ScrollDeltaResolution {
    Point consumed;
    Option<Box<ScrollDeltaCompletion>> after_layout;
};

// Deltas use viewport pixels. Deferred writers keep the initiating gesture's presentation lease.
using ScrollDeltaResolver = rstd::dyn<rstd::FnMut<Result<Option<ScrollDeltaResolution>, UiError>(
    Point, ViewportMetrics, bool, Option<ScrollDeltaLease>)>>;
}

namespace vui {
class ScrollInteraction {
    friend class ScrollDeltaLease;
    enum class Mode { Idle, Pending, Drag, NestedDrag, Animate, Programmatic } mode_{Mode::Idle};
    enum class Phase { Rest, Decay, Spring };
    struct Axis { Phase phase{Phase::Rest};ScalarMotion initial;Scalar target{};Serial started{}; } x_,y_;
    struct OffsetAnimation {
        rstd::rc::Rc<ScrollAnimationStatus> status;
        Point from, to;
        Serial started, duration;
    };
    UiSession &session_;
    UiId root_,viewport_;
    ScrollStyle &style_;
    Signal<void(UserScroll)> &changed_;
    Option<Box<ScrollDeltaResolver>> &resolve_delta_;
    rstd::rc::Weak<ScrollInteraction> owner_;
    Serial gesture_{};
    ParticipantRegistration participant_;
    MotionRegistration motion_;
    Binding press_,up_,metrics_,availability_;
    PanRecognizer pan_;
    VelocityTracker tracker_;
    PresentationLease lease_;
    Option<PointerKey> pointer_;
    Option<Transform> inverse_;
    Point anchor_,origin_,last_local_,velocity_,written_,tracked_offset_;
    Size viewport_size_;
    Point maximum_;
    bool geometry_known_{},rebase_{},boundary_owned_{};
    Option<UiError> error_;
    Option<OffsetAnimation> animation_;
    Option<Box<ScrollDeltaCompletion>> pending_delta_;
    Point decay_position_;
    Point pending_decay_delta_;
    Serial pending_decay_time_{};
    struct EdgeSpring {
        Axis axis;
        bool trailing;
    };
    Option<EdgeSpring> edge_spring_;
    Scalar edge_pull_{};
    struct NestedOrigin {
        rstd::rc::Weak<ScrollInteraction> owner;
        UiId target;
        Serial gesture;
    };
    struct Ancestor;
    struct BoundaryHit {
        Scalar velocity;
        Serial time;
    };
    Option<NestedOrigin> nested_origin_;
    Vec<rstd::rc::Weak<ScrollInteraction>> nested_drag_members_;
    bool nested_scroll_{}, handoff_pending_{};
    auto ui() -> UiDocument & {return session_.document();}
    static auto bounded(Scalar value,Scalar maximum) -> Scalar {return value<0 ? 0 : value>maximum ? maximum : value;}
    static auto bounded(Point value,Point maximum) -> Point {return {bounded(value.x,maximum.x),bounded(value.y,maximum.y)};}
    auto masked(Point value) const -> Point {return {style_.axes.horizontal ? value.x : 0,style_.axes.vertical ? value.y : 0};}
    auto now() -> Serial {auto value=ui().animation_sample_time();return value.is_some() ? *value : 0;}
    void wake() {if (motion_.binding.alive()) session_.motion().request(motion_.id);}
    auto acquire() -> bool;
    auto owns_delta(Serial gesture) -> bool;
    auto local_event(const InputSnapshot &snapshot,const InputEvent &input) -> Option<InputEvent>;
    auto drag_value(Point position,const ViewportMetrics &metrics) const -> Point;
    void track(InputEvent event,const ViewportMetrics &metrics,Point actual,bool measured);
    auto write(Point value,const ViewportMetrics &metrics) -> bool;
    auto single_axis() const -> bool { return style_.axes.horizontal != style_.axes.vertical; }
    auto uses_scroll_deltas() const -> bool {
        return (resolve_delta_ || nested_scroll_ || mode_ == Mode::NestedDrag) &&
               (style_.motion.movement == ScrollMovement::Clamped || single_axis());
    }
    auto uses_elastic_deltas() const -> bool { return uses_scroll_deltas() && style_.motion.movement == ScrollMovement::Elastic; }
    auto along(Point value) const -> Scalar { return style_.axes.horizontal ? value.x : value.y; }
    auto along(Size value) const -> Scalar { return style_.axes.horizontal ? value.width : value.height; }
    auto main_axis(Scalar value) const -> Point { return style_.axes.horizontal ? Point{value, 0} : Point{0, value}; }
    auto overscroll(const ViewportMetrics &metrics) const -> Scalar {
        const auto offset = along(metrics.offset);
        return offset - bounded(offset, along(metrics.maximum));
    }
    auto rubber(Scalar delta, Scalar extent) const -> Scalar;
    auto recover_pull(const ViewportMetrics &metrics) const -> Scalar;
    auto write_pull(Scalar pull, const ViewportMetrics &metrics) -> bool;
    auto drag_by(Point delta, const ViewportMetrics &metrics) -> bool;
    auto consume_drag(Point delta, ViewportMetrics metrics) -> bool;
    auto ancestor(Point delta, const ScrollInteraction *drag, bool test_range = true) -> Option<Ancestor>;
    auto accepts_nested_drag(const ScrollInteraction &origin) const -> bool;
    auto matches_origin(const ScrollInteraction &origin) const -> bool;
    auto offer_drag(Point remaining, const ViewportMetrics &metrics) -> bool;
    void release_nested_drag();
    auto boundary_hit(Point remaining, Serial time) const -> BoundaryHit;
    auto start_boundary_spring(Scalar remaining, const ViewportMetrics &metrics, BoundaryHit hit, Serial time) -> bool;
    auto handoff_fling(Point remaining, const ViewportMetrics &metrics, BoundaryHit hit, Serial time) -> bool;
    auto reached_boundary(Point remaining, const ViewportMetrics &metrics, Serial time) -> bool;
    auto sample_edge_spring(const ViewportMetrics &metrics, Serial time) -> bool;
    auto resolve_delta(Point delta, const ViewportMetrics &metrics) -> Result<Option<ScrollDeltaResolution>, UiError>;
    void spring(Axis &axis,Scalar value,Scalar velocity,Scalar target,Serial started) {
        axis={Phase::Spring,{value,velocity},target,started};
    }
    void begin_axis(Axis &axis,Scalar value,Scalar velocity,Scalar maximum,bool enabled,Serial started);
    void begin_motion(const ViewportMetrics &metrics,Point velocity,Serial started);
    auto sample_axis(Axis &axis,Scalar maximum,Serial time) -> ScalarMotion;
    auto decay_sample(const Axis &axis, Serial elapsed) const -> ScalarMotion;
    auto sample_scroll_deltas(MotionContext &context, const ViewportMetrics &metrics)
        -> Result<MotionStatus, MotionError>;
    void restart_animation(Point from, Serial time);
    auto sample_animation(MotionContext &context, const ViewportMetrics &metrics) -> Result<MotionStatus, MotionError>;
    void press(EventContext &,const RoutedEvent &event);
    void pending_up(EventContext &,const RoutedEvent &event);
  public:
    ScrollInteraction(UiSession &session, UiId root, UiId viewport, ScrollStyle &style,
                      Signal<void(UserScroll)> &changed, Option<Box<ScrollDeltaResolver>> &resolve_delta)
        : session_(session), root_(root), viewport_(viewport), style_(style), changed_(changed),
          resolve_delta_(resolve_delta) { configure(); }
    ~ScrollInteraction();
    void configure() {
        pan_=PanRecognizer({style_.motion.threshold,style_.axes.horizontal && style_.axes.vertical ? PanAxis::Free : style_.axes.horizontal ? PanAxis::Horizontal : PanAxis::Vertical});
        inverse_=None();
    }
    static auto make(UiSession &session, UiId root, UiId viewport, ScrollStyle &style,
                     Signal<void(UserScroll)> &changed, Option<Box<ScrollDeltaResolver>> &resolve_delta)
        -> Result<rstd::rc::Rc<ScrollInteraction>, ControlError>;
    void stop(bool clamp=true,bool release=true,bool request=true);
    auto rebase_offset(Point value) -> Result<empty, UiError>;
    auto animate_offset(Point value, Serial duration_ns) -> Result<ScrollAnimation, UiError>;
    auto retarget_offset(const ScrollAnimation &animation, Point value) -> Result<empty, UiError>;
    auto describe_semantics() const -> Result<empty, UiError>;
    auto status() const -> ScrollStatus {return {mode_==Mode::Drag || mode_==Mode::NestedDrag,mode_==Mode::Animate || mode_==Mode::Programmatic,velocity_,error_};}
    auto observe(InputContext &context,const InputPacket &packet) -> InputDecision;
    void cancel(InputContext &context,CancelReason reason) {
        pan_.cancel(context,reason);tracker_.remove(context.pointer());
        if (pointer_==Some(PointerKey(context.pointer()))) stop(true,false);
    }
    auto handle(EventContext &context,const RoutedEvent &event) -> bool;
    auto sample(MotionContext &context,FrameTiming) -> Result<MotionStatus,MotionError>;
};
} // namespace vui

export namespace vui {
class ScrollState {
    friend class ScrollView;
    friend class ScrollInteraction;
    UiId viewport_;
    ScrollStyle style_;
    Signal<void(UserScroll)> user_scrolled_;
    Option<Box<ScrollDeltaResolver>> resolve_delta_;
    Option<rstd::rc::Rc<ScrollInteraction>> interaction_;
    static auto shift(Scalar value, Scalar amount, Scalar step, Scalar maximum) -> Scalar;
    static auto consumed(Scalar change, Scalar step, Scalar amount) -> Scalar;

  public:
    ScrollState(UiId viewport, ScrollStyle style) : viewport_(viewport), style_(style) {}
    auto style() const -> ScrollStyle { return style_; }
    void configure_prepared(ScrollStyle style) { style_ = style; }
    auto finish_configuration() -> Result<empty, UiError>;
    auto viewport() const -> UiId { return viewport_; }
    auto connect(UiSession &session, UiId root) -> Result<empty, ControlError>;
    void stop() {if (interaction_.is_some()) interaction_->get()->stop();}
    auto status() const -> ScrollStatus {return interaction_.is_some() ? interaction_->get()->status() : ScrollStatus{};}
    auto on_user_scrolled(UiDocument &ui, auto callback) -> Result<Binding, UiError> {
        auto connected = user_scrolled_.connect(ui.bindings(), rstd::move(callback));
        return connected.is_ok() ? Result<Binding, UiError>(Ok(rstd::move(connected).unwrap())) : Result<Binding, UiError>(Err(UiError::Limit));
    }
    auto set_style(UiDocument &ui, ScrollStyle style) -> Result<empty, UiError>;
    auto rebase_offset(UiDocument &ui, Point value) -> Result<empty, UiError>;
    void handle(EventContext &context, const RoutedEvent &event);
};
class ScrollView {
  public:
    struct Prepared {
        UiNodeRef node;
        bool created{}, changed{};
    };
    static auto prepare(UiDocument &ui, UiEditBatch &edit, Option<UiId> previous,
                        NodeStyle style, ScrollStyle scroll, Option<NodeStyle> before = {}) -> Result<Prepared, UiError>;
    static auto prepare_state(UiDocument::PreparedEdit &edit, Prepared prepared, ScrollStyle scroll) -> Result<empty, UiError>;
    static auto connect(UiSession &session, UiId id) -> Result<empty, ControlError> {
        auto state = session.document().state<ScrollState>(id);
        return state ? state->connect(session, id) : Result<empty, ControlError>(Err(ControlError{Some(UiError::InvalidId), {}}));
    }
    static auto set_delta_resolver(UiDocument &ui, UiId id, Option<Box<ScrollDeltaResolver>> resolver)
        -> Result<empty, UiError>;
    static auto finish_configuration(UiDocument &ui, UiId id) -> Result<empty, UiError> {
        auto state = ui.state<ScrollState>(id);
        return state ? state->finish_configuration() : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    static auto on_metrics_changed(UiDocument &ui, UiId id, auto callback) -> Result<Binding, UiError> {
        auto state = ui.state<ScrollState>(id);
        return state ? ui.on_viewport_changed(state->viewport(), rstd::move(callback)) : Result<Binding, UiError>(Err(UiError::InvalidId));
    }
    // Programmatic offsets and layout clamping do not emit this input notification.
    static auto on_user_scrolled(UiDocument &ui, UiId id, auto callback) -> Result<Binding, UiError> {
        auto state = ui.state<ScrollState>(id);
        return state ? state->on_user_scrolled(ui, rstd::move(callback)) : Result<Binding, UiError>(Err(UiError::InvalidId));
    }
    static auto create(UiSession &session, UiId content, NodeStyle style, ScrollStyle scroll = {})
        -> Result<UiId, ControlError>;
    static auto set_offset(UiDocument &ui, UiId id, Point value) -> Result<empty, UiError>;
    static auto rebase_offset(UiDocument &ui, UiId id, Point value) -> Result<empty, UiError>;
    static auto animate_offset(UiDocument &ui, UiId id, Point value, Serial duration_ns)
        -> Result<ScrollAnimation, UiError>;
    static auto retarget_offset(UiDocument &ui, UiId id, const ScrollAnimation &animation, Point value)
        -> Result<empty, UiError>;
    static auto metrics(UiDocument &ui, UiId id) -> Result<ViewportMetrics, UiError>;
    static auto status(UiDocument &ui,UiId id) -> Result<ScrollStatus,UiError> {
        auto state=ui.state<ScrollState>(id);return state ? Result<ScrollStatus,UiError>(Ok(state->status())) : Result<ScrollStatus,UiError>(Err(UiError::InvalidId));
    }
    static auto stop(UiDocument &ui,UiId id) -> Result<empty,UiError> {
        auto state=ui.state<ScrollState>(id);if (!state) return Err(UiError::InvalidId);state->stop();return Ok(empty{});
    }
    static auto content(UiDocument &ui, UiId id) -> Result<Option<UiId>, UiError>;
    static auto attach_content(UiDocument &ui, UiId id, UiId content) -> Result<empty, UiError>;
    static auto set_style(UiDocument &ui, UiId id, ScrollStyle style) -> Result<empty, UiError>;
};
} // namespace vui
