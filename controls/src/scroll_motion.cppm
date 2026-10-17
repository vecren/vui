module vui.controls:scroll_motion;
import vui;
import :types;
import :scroll_types;
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
    auto acquire() -> bool {
        if (lease_.active()) return true;
        PropertyKey key{viewport_,PresentationProperty::ViewportOffset};auto value=ui().take_over(slice<PropertyKey>::from_raw_parts(&key,usize(1)));
        if (value.is_err()) {error_=Some(value.unwrap_err());return false;}
        lease_=rstd::move(value).unwrap();return true;
    }
    auto local_event(const InputSnapshot &snapshot,const InputEvent &input) -> Option<InputEvent> {
        auto transform=snapshot.local_to_surface(root_);if (transform.is_err()) return None();
        auto inverse=transform->inverse();if (inverse.is_none()) return None();
        auto local=inverse->map(input.position);if (!finite(local.x) || !finite(local.y)) return None();
        bool reset=inverse_.is_some() && *inverse_!=*inverse;
        if (reset && (mode_==Mode::Drag || mode_==Mode::Pending)) {
            auto metrics=ui().viewport_metrics(viewport_);
            if (metrics.is_ok()) {origin_=metrics->offset;anchor_=local;}
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
    auto drag_value(Point position,const ViewportMetrics &metrics) const -> Point {
        auto raw=masked({origin_.x-(position.x-anchor_.x),origin_.y-(position.y-anchor_.y)});
        auto result=bounded(raw,metrics.maximum);
        if (style_.motion.movement==ScrollMovement::Elastic) {
            auto rubber=[&](Scalar delta,Scalar extent) {
                if (extent<=0) return Scalar(0);
                auto amount=(1-1/(1+__builtin_fabs(delta)*.55/extent))*extent*style_.motion.rubber_scale;
                return delta<0 ? -amount : amount;
            };
            result.x+=rubber(raw.x-result.x,metrics.viewport.width);result.y+=rubber(raw.y-result.y,metrics.viewport.height);
        }
        return masked(result);
    }
    void track(InputEvent event,const ViewportMetrics &metrics,Point actual) {
        Vec<PointerSample> history;
        for (auto sample:event.history) {sample.position=drag_value(inverse_->map(sample.position),metrics);history.push(rstd::move(sample));}
        event.history=history.as_slice();event.position=actual;tracker_.add(event);
    }
    auto write(Point value,const ViewportMetrics &metrics) -> bool {
        PresentationBatch batch;batch.viewport_offset(viewport_,value);auto result=ui().write_presentation(lease_,batch);
        if (result.is_err()) {error_=Some(result.unwrap_err());return false;}
        written_=value;if (value!=metrics.offset) changed_.emit({value,metrics.maximum}).unwrap();return true;
    }
    void spring(Axis &axis,Scalar value,Scalar velocity,Scalar target,Serial started) {
        axis={Phase::Spring,{value,velocity},target,started};
    }
    void begin_axis(Axis &axis,Scalar value,Scalar velocity,Scalar maximum,bool enabled,Serial started) {
        if (!enabled) {axis={};return;}
        auto target=bounded(value,maximum);
        if (style_.motion.movement==ScrollMovement::Elastic && value!=target) spring(axis,value,velocity,target,started);
        else if (style_.motion.inertia && style_.motion.deceleration_rate>0 && __builtin_fabs(velocity)>=1) axis={Phase::Decay,{value,velocity},0,started};
        else axis={Phase::Rest,{target,0},target,started};
    }
    void begin_motion(const ViewportMetrics &metrics,Point velocity,Serial started) {
        velocity_=style_.motion.inertia ? masked(velocity) : Point{};begin_axis(x_,metrics.offset.x,velocity_.x,metrics.maximum.x,style_.axes.horizontal,started);
        begin_axis(y_,metrics.offset.y,velocity_.y,metrics.maximum.y,style_.axes.vertical,started);
        if (x_.phase==Phase::Rest && y_.phase==Phase::Rest) {stop(true,false);return;}
        if (!acquire()) {stop(true,false);return;}mode_=Mode::Animate;rebase_=false;wake();
    }
    auto sample_axis(Axis &axis,Scalar maximum,Serial time) -> ScalarMotion {
        auto elapsed=time>=axis.started ? time-axis.started : 0;
        if (axis.phase==Phase::Decay) {
            auto drag=-__builtin_log(style_.motion.deceleration_rate);
            auto result=drag>0 ? Decay{axis.initial,drag}.sample(elapsed) : ScalarMotion{axis.initial.value+axis.initial.velocity*Scalar(elapsed)/1e9,axis.initial.velocity};
            if (result.value<0 || result.value>maximum) {
                auto target=bounded(result.value,maximum);
                if (style_.motion.movement==ScrollMovement::Clamped) {axis={Phase::Rest,{target,0},target,time};return axis.initial;}
                auto seconds=drag>0 ? -__builtin_log(1-(target-axis.initial.value)*drag/axis.initial.velocity)/drag : (target-axis.initial.value)/axis.initial.velocity;
                auto hit=static_cast<Serial>(bounded(seconds*1e9,Scalar(elapsed)));
                auto speed=drag>0 ? axis.initial.velocity*__builtin_exp(-drag*Scalar(hit)/1e9) : axis.initial.velocity;
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
    void press(EventContext &,const RoutedEvent &event) {
        if (!style_.motion.enabled || (event.input.pointer_kind!=PointerKind::Touch && event.input.button!=PointerButton::Primary) || pointer_.is_some()) return;
        stop(false,false);pointer_=Some(PointerKey{event.input.device,event.input.pointer});mode_=Mode::Pending;error_=None();
    }
    void pending_up(EventContext &,const RoutedEvent &event) {
        if (mode_!=Mode::Pending || pointer_!=Some(PointerKey{event.input.device,event.input.pointer})) return;
        pointer_=None();auto metrics=ui().viewport_metrics(viewport_);if (metrics.is_ok()) begin_motion(*metrics,{},now());else stop(true,false);
    }
  public:
    ScrollInteraction(UiSession &session,UiId root,UiId viewport,ScrollStyle &style,Signal<void(UserScroll)> &changed)
        :session_(session),root_(root),viewport_(viewport),style_(style),changed_(changed) {configure();}
    void configure() {
        pan_=PanRecognizer({style_.motion.threshold,style_.axes.horizontal && style_.axes.vertical ? PanAxis::Free : style_.axes.horizontal ? PanAxis::Horizontal : PanAxis::Vertical});
        inverse_=None();
    }
    static auto make(UiSession &session,UiId root,UiId viewport,ScrollStyle &style,Signal<void(UserScroll)> &changed) -> Result<rstd::rc::Rc<ScrollInteraction>,ControlError> {
        auto owner=rstd::rc::make_rc<ScrollInteraction>(session,root,viewport,style,changed);auto self=owner.downgrade();
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
            if (value.geometry_known_ && (value.viewport_size_!=metrics.viewport || value.maximum_!=metrics.maximum ||
                (value.mode_==Mode::Animate && value.written_!=metrics.offset))) value.rebase_=true;
            value.viewport_size_=metrics.viewport;value.maximum_=metrics.maximum;value.geometry_known_=true;
        }).unwrap();return Ok(rstd::move(owner));
    }
    void stop(bool clamp=true,bool release=true,bool request=true) {
        bool owned=mode_!=Mode::Idle || lease_.active() || boundary_owned_;auto pointer=pointer_;pointer_=None();mode_=Mode::Idle;velocity_={};
        if (lease_.active()) ui().release_presentation(lease_);else lease_.revoke();
        if (clamp && owned && ui().valid(viewport_)) {ui().set_viewport_boundary(viewport_,ViewportBoundary::Clamped);boundary_owned_=false;}
        if (release && pointer.is_some()) {auto key=*pointer;session_.input().release_input(slice<PointerKey>::from_raw_parts(&key,usize(1)));}
        if (request && owned) wake();
    }
    auto status() const -> ScrollStatus {return {mode_==Mode::Drag,mode_==Mode::Animate,velocity_,error_};}
    auto observe(InputContext &context,const InputPacket &packet) -> InputDecision {
        if (!style_.motion.enabled || (!style_.axes.horizontal && !style_.axes.vertical) ||
            (packet.input.pointer_kind!=PointerKind::Touch && packet.input.type==EventType::PointerDown && packet.input.button!=PointerButton::Primary) ||
            (pointer_.is_some() && *pointer_!=PointerKey{packet.input.device,packet.input.pointer})) return InputDecision::reject();
        auto local=local_event(context.snapshot(),packet.input);if (local.is_none()) return InputDecision::reject();
        return pan_.observe(context,{rstd::move(local).unwrap(),packet.target});
    }
    void cancel(InputContext &context,CancelReason reason) {
        pan_.cancel(context,reason);tracker_.remove(context.pointer());
        if (pointer_==Some(PointerKey(context.pointer()))) stop(true,false);
    }
    auto handle(EventContext &context,const RoutedEvent &event) -> bool {
        if (event.input.type==EventType::Wheel && mode_==Mode::Drag) {context.prevent_default();return true;}
        if (event.input.type==EventType::CaptureGained && event.target==root_ && style_.motion.enabled) {
            auto snapshot=session_.input().snapshot();auto metrics=ui().viewport_metrics(viewport_);
            if (snapshot.is_none() || metrics.is_err()) return true;
            auto local=local_event(*snapshot,event.input);if (local.is_none() || !acquire()) return true;
            pointer_=Some(PointerKey{event.input.device,event.input.pointer});mode_=Mode::Drag;anchor_=last_local_=local->position;origin_=metrics->offset;rebase_=false;
            tracker_.remove(*pointer_);
            if (style_.motion.movement==ScrollMovement::Elastic) {ui().set_viewport_boundary(viewport_,ViewportBoundary::Unclamped).unwrap();boundary_owned_=true;}
            context.focus();return true;
        }
        auto key=PointerKey{event.input.device,event.input.pointer};if (pointer_!=Some(PointerKey(key))) return false;
        if (event.input.type==EventType::PointerCancel || event.input.type==EventType::CaptureLost) {stop(true,false);return true;}
        if (mode_!=Mode::Drag || (event.input.type!=EventType::PointerMove && event.input.type!=EventType::PointerUp)) return false;
        auto snapshot=session_.input().snapshot();auto metrics=ui().viewport_metrics(viewport_);if (snapshot.is_none() || metrics.is_err()) return true;
        auto local=local_event(*snapshot,event.input);if (local.is_none()) {error_=Some(UiError::InvalidValue);stop(true,false);return true;}
        if (rebase_) {origin_=metrics->offset;anchor_=last_local_;rebase_=false;tracker_.remove(key);}
        if (event.input.type==EventType::PointerMove) {
            auto value=drag_value(local->position,*metrics);if (!write(value,*metrics)) {stop(true,false);return true;}
            track(rstd::move(local).unwrap(),*metrics,value);last_local_=inverse_->map(event.input.position);
        } else {
            track(rstd::move(local).unwrap(),*metrics,metrics->offset);auto velocity=tracker_.velocity(key);tracker_.remove(key);pointer_=None();
            begin_motion(*metrics,velocity.is_some() ? *velocity : Point{},now());
        }
        context.prevent_default();return true;
    }
    auto sample(MotionContext &context,FrameTiming) -> Result<MotionStatus,MotionError> {
        if (mode_==Mode::Idle) return Ok(MotionStatus::Idle);
        if (!session_.input().pointer_available(root_)) {stop(true,false,false);return Ok(MotionStatus::Idle);}
        if (mode_!=Mode::Animate) return Ok(MotionStatus::Idle);
        if (!lease_.active()) {error_=Some(UiError::PropertyOwned);stop(true,false,false);return Ok(MotionStatus::Idle);}
        auto metrics=ui().viewport_metrics(viewport_);
        if (metrics.is_err()) {context.request_tick(context.now_ns()+8'333'333).unwrap();return Ok(MotionStatus::Running);}
        if (rebase_) {
            begin_axis(x_,metrics->offset.x,velocity_.x,metrics->maximum.x,style_.axes.horizontal,context.now_ns());
            begin_axis(y_,metrics->offset.y,velocity_.y,metrics->maximum.y,style_.axes.vertical,context.now_ns());rebase_=false;
        }
        auto x=sample_axis(x_,metrics->maximum.x,context.now_ns()),y=sample_axis(y_,metrics->maximum.y,context.now_ns());velocity_={x.velocity,y.velocity};
        PresentationBatch batch;Point value{x.value,y.value};batch.viewport_offset(viewport_,value);auto written=context.write(lease_,batch);
        if (written.is_err()) {error_=Some(UiError::PropertyOwned);stop(true,false,false);return Ok(MotionStatus::Idle);}
        written_=value;if (value!=metrics->offset) changed_.emit({value,metrics->maximum}).unwrap();
        if (x_.phase==Phase::Rest && y_.phase==Phase::Rest) {stop(true,false,false);return Ok(MotionStatus::Idle);}
        context.request_tick(context.now_ns()+8'333'333).unwrap();return Ok(MotionStatus::Running);
    }
};
} // namespace vui
