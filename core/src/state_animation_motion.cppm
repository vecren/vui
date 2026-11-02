export module vui:state_animation_motion;
import :session;
import :state_animation;
using namespace rstd::prelude;
export namespace vui {
struct StateAnimationMotionError {Option<UiError> ui;Option<InputError> input;Option<StateAnimationError> animation;};
struct StateAnimationMotionStatus {bool active{};StateAnimationStatus playback;Option<StateAnimationMotionError> error;};
template<class Projection> class StateAnimationMotion {
    UiSession &session_;UiDocument &ui_;BindingGuard lifetime_;UiId root_;Projection projection_;
    StateAnimationPlayer player_;Vec<PropertyKey> keys_;Vec<PropertyWrite> originals_;PresentationLease lease_;
    MotionRegistration motion_;Vec<Binding> availability_;InputAvailability source_;Vec<Serial> mounts_;
    Signal<void(const StateAnimationEvent &)> events_;Signal<void()> activated_;
    bool active_{},dirty_{},sampling_{};Option<StateAnimationMotionError> error_;
    Serial interval_{16'666'667};
    auto now() const -> Serial {return ui_.animation_sample_time().unwrap_or(Serial(0));}
    void committed(slice<PropertyWrite> writes) {
        if constexpr(requires {projection_.committed(ui_,writes);})projection_.committed(ui_,writes);
    }
    auto restore() -> Result<empty,UiError> {
        active_=false;
        if (lease_.active()) {
            auto result=ui_.release_presentation(lease_,LeaseEndPolicy::RestoreBase);if (result.is_err()) return result;
            committed(lease_.originals());
        } else if (dirty_) {
            PresentationBatch batch;for (auto value:originals_) if (ui_.valid(value.key.target)) batch.append(PropertyWrite(value));
            auto result=ui_.set_presentation(batch);if (result.is_err()) return result;
            committed(batch.writes());
        }
        dirty_=false;return Ok(empty{});
    }
    auto fail(StateAnimationMotionError error) -> Result<empty,StateAnimationMotionError> {
        error_=Some(error);player_.poll_events();return Err(error);
    }
    auto synchronize() -> Result<empty,StateAnimationMotionError> {
        if (error_.is_some()) return Err(*error_);
        auto current=session_.input().input_availability(root_);
        bool mounted=current.mount_generation!=source_.mount_generation;source_=current;
        if (!ui_.valid(root_)) return fail({Some(UiError::InvalidId),{}, {}});
        bool available=current.mounted && current.visible && current.window_focused;
        for (usize i{};i<keys_.len();++i) {
            if (!ui_.valid(keys_[i].target)) return fail({Some(UiError::InvalidId),{}, {}});
            auto target=session_.input().input_availability(keys_[i].target);available&=target.mounted;
            mounted|=target.mount_generation!=mounts_[i];mounts_[i]=target.mount_generation;
        }
        if (mounted || !available) {
            auto restored=restore();if (restored.is_err()) return fail({Some(restored.unwrap_err()),{}, {}});
            player_.poll_events();auto stopped=player_.stop(now());if (stopped.is_err()) return fail({{},{},Some(stopped.unwrap_err())});
        }
        if (!available) return Ok(empty{});
        if (active_ && !lease_.active()) return fail({Some(UiError::PropertyOwned),{}, {}});
        if (!active_) {
            auto acquired=ui_.take_over(keys_.as_slice(),TakeoverPolicy::RejectOccupied);
            if (acquired.is_err()) return fail({Some(acquired.unwrap_err()),{}, {}});
            lease_=rstd::move(acquired).unwrap();originals_=Vec<PropertyWrite>::from(lease_.current());
            auto started=player_.restart(now());if (started.is_err()) return fail({{},{},Some(started.unwrap_err())});active_=true;
            if (activated_.emit().is_err()) return fail({Some(UiError::Limit),{}, {}});
        }
        return Ok(empty{});
    }
    auto project() -> Result<empty,StateAnimationMotionError> {
        if (!active_ || !projection_.ready()) return Ok(empty{});
        PresentationBatch batch;auto projected=projection_.project(player_.values(),batch);
        if (projected.is_err()) return fail({Some(projected.unwrap_err()),{}, {}});
        auto written=ui_.write_presentation(lease_,batch,Some(motion_.binding.guard()));
        if (written.is_err()) return fail({Some(written.unwrap_err()),{}, {}});dirty_=true;committed(batch.writes());return Ok(empty{});
    }
    auto dispatch_events() -> Result<empty,StateAnimationMotionError> {
        unsigned count{};
        for (;;) {
            auto pending=player_.poll_events();if (pending.is_empty()) return Ok(empty{});
            for (const auto &event:pending) if (++count>4096 || events_.emit(event).is_err()) return fail({Some(UiError::Limit),{}, {}});
        }
    }
    void availability_changed() {auto result=synchronize();if (result.is_ok() && !active_) dispatch_events();wake();}
  public:
    StateAnimationMotion(UiSession &session,UiId root,StateAnimationProgram program,Vec<PropertyKey> keys,Projection projection)
        :session_(session),ui_(session.document()),lifetime_(ui_.lifetime()),root_(root),projection_(rstd::move(projection)),
         player_(rstd::move(program)),keys_(rstd::move(keys)),source_(session.input().input_availability(root)) {
        for (auto key:keys_) mounts_.push(Serial(session.input().input_availability(key.target).mount_generation));
    }
    ~StateAnimationMotion() {for (auto &binding:availability_) binding.disconnect();if (lifetime_.alive()) restore();motion_.binding.disconnect();}
    static auto create(UiSession &session,UiId root,StateAnimationProgram program,Vec<PropertyKey> keys,Projection projection)
        -> Result<rstd::rc::Rc<StateAnimationMotion>,StateAnimationMotionError> {
        if (!session.document().valid(root)) return Err(StateAnimationMotionError{Some(UiError::InvalidId),{}, {}});
        if (keys.is_empty() || keys.len()>usize(256)) return Err(StateAnimationMotionError{Some(UiError::InvalidValue),{}, {}});
        for (usize i{};i<keys.len();++i) {
            auto key=keys[i];if (!session.document().valid(key.target)) return Err(StateAnimationMotionError{Some(UiError::InvalidId),{}, {}});
            if (unsigned(key.property)>=unsigned(PresentationProperty::Count)) return Err(StateAnimationMotionError{Some(UiError::InvalidValue),{}, {}});
            for (usize j{};j<i;++j) if (keys[j]==key) return Err(StateAnimationMotionError{Some(UiError::InvalidValue),{}, {}});
        }
        auto owner=rstd::rc::make_rc<StateAnimationMotion>(session,root,rstd::move(program),rstd::move(keys),rstd::move(projection));auto self=owner.downgrade();
        auto registration=session.motion().bind(self);if (registration.is_err()) return Err(StateAnimationMotionError{Some(UiError::Limit),{}, {}});owner.get()->motion_=rstd::move(registration).unwrap();
        Vec<UiId> observed;observed.push(UiId(root));for (auto key:owner.get()->keys_) {
            bool found{};for (auto target:observed) found|=target==key.target;if (!found) observed.push(UiId(key.target));
        }
        for (auto target:observed) {
            auto binding=session.input().on_input_availability_changed(target,[self](const InputAvailability &) {auto owner=self.upgrade();if (owner.is_some()) owner->get()->availability_changed();});
            if (binding.is_err()) return Err(StateAnimationMotionError{{},Some(binding.unwrap_err()),{}});owner.get()->availability_.push(rstd::move(binding).unwrap());
        }
        auto ready=owner.get()->synchronize();if (ready.is_err()) return Err(ready.unwrap_err());return Ok(rstd::move(owner));
    }
    auto projection() -> Projection & {return projection_;}
    auto player() -> StateAnimationPlayer & {return player_;}
    auto status() const -> StateAnimationMotionStatus {return {active_,player_.status(),error_};}
    void wake() {if (!sampling_ && motion_.binding.alive()) session_.motion().request(motion_.id);}
    auto refresh() -> Result<empty,StateAnimationMotionError> {auto available=synchronize();return available.is_err() ? available:project();}
    auto retry() -> Result<empty,StateAnimationMotionError> {
        auto restored=restore();if (restored.is_err()) return fail({Some(restored.unwrap_err()),{}, {}});error_=None();auto result=synchronize();wake();return result;
    }
    auto set_frame_interval(Serial value) -> Result<empty,UiError> {
        if (!value) return Err(UiError::InvalidValue);interval_=value;wake();return Ok(empty{});
    }
    auto on_activated(auto callback) -> Result<Binding,SignalError> {return activated_.connect(ui_.bindings(),rstd::move(callback));}
    auto on_event(auto callback) -> Result<Binding,SignalError> {return events_.connect(ui_.bindings(),rstd::move(callback));}
    auto sample(MotionContext &context,FrameTiming) -> Result<MotionStatus,MotionError> {
        sampling_=true;struct Guard {bool &flag;~Guard(){flag=false;}} guard{sampling_};
        auto ready=synchronize();if (ready.is_err() || !active_) return Ok(MotionStatus::Idle);
        auto advanced=player_.advance(context.now_ns());if (advanced.is_err()) {fail({{},{},Some(advanced.unwrap_err())});return Ok(MotionStatus::Idle);}
        auto written=project();if (written.is_err()) return Ok(MotionStatus::Idle);
        if (!projection_.ready()) return Ok(MotionStatus::Idle);
        auto delivered=dispatch_events();if (delivered.is_err()) return Ok(MotionStatus::Idle);
        auto next=player_.next_tick(interval_);if (next.is_err()) {fail({{},{},Some(next.unwrap_err())});return Ok(MotionStatus::Idle);}
        if (next->is_none()) return Ok(MotionStatus::Idle);
        context.request_tick(**next).unwrap();return Ok(MotionStatus::Running);
    }
};
} // namespace vui
