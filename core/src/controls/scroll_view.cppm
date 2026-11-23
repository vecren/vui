export module vui.controls:scroll_view;
import vui;
export import :scroll_types;
import :types;
using namespace rstd::prelude;
namespace vui {
class ScrollInteraction {
    enum class Mode { Idle, Pending, Drag, Animate } mode_{Mode::Idle};
    enum class Phase { Rest, Decay, Spring };
    struct Axis { Phase phase{Phase::Rest};ScalarMotion initial;Scalar target{};Serial started{}; } x_,y_;
    UiSession &session_;
    UiId root_,viewport_;
    ScrollStyle &style_;
    Signal<void(UserScroll)> &changed_;
    ParticipantRegistration participant_;
    MotionRegistration motion_;
    Binding press_,up_,metrics_,availability_;
    PanRecognizer pan_;
    VelocityTracker tracker_;
    PresentationLease lease_;
    Option<PointerKey> pointer_;
    Option<Transform> inverse_;
    Point anchor_,origin_,last_local_,velocity_,written_;
    Size viewport_size_;
    Point maximum_;
    bool geometry_known_{},rebase_{},boundary_owned_{};
    Option<UiError> error_;
    auto ui() -> UiDocument & {return session_.document();}
    static auto bounded(Scalar value,Scalar maximum) -> Scalar {return value<0 ? 0 : value>maximum ? maximum : value;}
    static auto bounded(Point value,Point maximum) -> Point {return {bounded(value.x,maximum.x),bounded(value.y,maximum.y)};}
    auto masked(Point value) const -> Point {return {style_.axes.horizontal ? value.x : 0,style_.axes.vertical ? value.y : 0};}
    auto now() -> Serial {auto value=ui().animation_sample_time();return value.is_some() ? *value : 0;}
    void wake() {if (motion_.binding.alive()) session_.motion().request(motion_.id);}
    auto acquire() -> bool;
    auto local_event(const InputSnapshot &snapshot,const InputEvent &input) -> Option<InputEvent>;
    auto drag_value(Point position,const ViewportMetrics &metrics) const -> Point;
    void track(InputEvent event,const ViewportMetrics &metrics,Point actual);
    auto write(Point value,const ViewportMetrics &metrics) -> bool;
    void spring(Axis &axis,Scalar value,Scalar velocity,Scalar target,Serial started) {
        axis={Phase::Spring,{value,velocity},target,started};
    }
    void begin_axis(Axis &axis,Scalar value,Scalar velocity,Scalar maximum,bool enabled,Serial started);
    void begin_motion(const ViewportMetrics &metrics,Point velocity,Serial started);
    auto sample_axis(Axis &axis,Scalar maximum,Serial time) -> ScalarMotion;
    void press(EventContext &,const RoutedEvent &event);
    void pending_up(EventContext &,const RoutedEvent &event);
  public:
    ScrollInteraction(UiSession &session,UiId root,UiId viewport,ScrollStyle &style,Signal<void(UserScroll)> &changed)
        :session_(session),root_(root),viewport_(viewport),style_(style),changed_(changed) {configure();}
    void configure() {
        pan_=PanRecognizer({style_.motion.threshold,style_.axes.horizontal && style_.axes.vertical ? PanAxis::Free : style_.axes.horizontal ? PanAxis::Horizontal : PanAxis::Vertical});
        inverse_=None();
    }
    static auto make(UiSession &session,UiId root,UiId viewport,ScrollStyle &style,Signal<void(UserScroll)> &changed) -> Result<rstd::rc::Rc<ScrollInteraction>,ControlError>;
    void stop(bool clamp=true,bool release=true,bool request=true);
    auto rebase_offset(Point value) -> Result<empty, UiError>;
    auto describe_semantics() const -> Result<empty, UiError>;
    auto status() const -> ScrollStatus {return {mode_==Mode::Drag,mode_==Mode::Animate,velocity_,error_};}
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
    UiId viewport_;
    ScrollStyle style_;
    Signal<void(UserScroll)> user_scrolled_;
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
