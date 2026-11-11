export module vui:state_animation;
import :curves;
using namespace rstd::prelude;
export namespace vui {
enum class StateAnimationError { InvalidValue, InvalidState, InvalidParameter, Timing, Limit };
enum class StateAnimationParameterKind { Float, Int, Bool, Trigger };
enum class StateAnimationConditionMode { If, IfNot, Greater, Less, Equal, NotEqual };
enum class StateAnimationInterruption { None, Current, Next, CurrentThenNext, NextThenCurrent };
struct StateAnimationParameter {
    StateAnimationParameterKind kind{StateAnimationParameterKind::Float};Scalar initial{};
};
struct StateAnimationCondition {
    Index parameter{};StateAnimationConditionMode mode{StateAnimationConditionMode::If};Scalar threshold{};
};
struct StateAnimationTransition {
    Index target{};Vec<StateAnimationCondition> conditions;
    Scalar duration{},offset{},exit_time{};
    bool fixed_duration{true},has_exit_time{};
    StateAnimationInterruption interruption{StateAnimationInterruption::None};
    bool ordered{true},self_transition{true};
};
struct StateAnimationCurve { Option<Scalar> constant;Vec<ScalarCurveKey> keys; };
struct StateAnimationTrack { Index channel{};StateAnimationCurve curve; };
struct StateAnimationMarker {Scalar time{};Index id{};};
enum class StateAnimationClipSampling { Mathematical,NormalizedFloat32Ceil };
struct StateAnimationClip {
    Scalar start{},stop{};Vec<StateAnimationTrack> tracks;
    ScalarCurveArithmetic arithmetic{};StateAnimationClipSampling sampling{};
    Vec<StateAnimationMarker> markers;
};
struct StateAnimationState {
    StateAnimationClip clip;Scalar speed{1};bool loop{},write_defaults{true};
    Vec<StateAnimationTransition> transitions;
};
struct StateAnimationDefinition {
    Vec<Scalar> defaults;Vec<StateAnimationParameter> parameters;Vec<StateAnimationState> states;
    Vec<StateAnimationTransition> transitions;Index initial{};
};
enum class StateAnimationSampleSide { After,Before };
struct StateAnimationTime {Scalar now{},started{},offset{};StateAnimationSampleSide side{};};
class StateAnimationProgram {
    struct Data {StateAnimationDefinition definition;explicit Data(StateAnimationDefinition value):definition(rstd::move(value)) {}};
    rstd::sync::Arc<Data> data_;
    explicit StateAnimationProgram(StateAnimationDefinition value):data_(rstd::sync::Arc<Data>::make(rstd::move(value))) {}
    static auto parameter_value(StateAnimationParameterKind kind,Scalar value) -> bool {
        if (!finite(value)) return false;
        switch (kind) {
        case StateAnimationParameterKind::Float:return true;
        case StateAnimationParameterKind::Int:return __builtin_floor(value)==value && __builtin_fabs(value)<=9'007'199'254'740'991.0;
        case StateAnimationParameterKind::Bool:
        case StateAnimationParameterKind::Trigger:return value==0 || value==1;
        }
        return false;
    }
    auto period(Index index) const -> Scalar {const auto &s=definition().states[usize(index)];return (s.clip.stop-s.clip.start)/s.speed;}
    auto position(Index index,Scalar elapsed,Scalar offset) const -> Scalar {auto duration=period(index);return duration>0 ? offset+elapsed/duration:Scalar(1);}
    friend class StateAnimationPlayer;
  public:
    StateAnimationProgram(const StateAnimationProgram &other):data_(other.data_.clone()) {}
    StateAnimationProgram(StateAnimationProgram &&)=default;
    auto operator=(const StateAnimationProgram &other) -> StateAnimationProgram & {if (this!=&other) data_=other.data_.clone();return *this;}
    auto operator=(StateAnimationProgram &&) -> StateAnimationProgram & =default;
    auto definition() const -> const StateAnimationDefinition & {return data_->definition;}
    // Sampling a state does not evaluate transitions or produce playback events.
    auto sample_state(Index index,StateAnimationTime time,slice<Scalar> inherited={}) const -> Result<Vec<Scalar>,StateAnimationError> {
        if (usize(index)>=definition().states.len()) return Err(StateAnimationError::InvalidState);
        if (!finite(time.now) || !finite(time.started) || time.started<0 || time.now<time.started || !finite(time.offset) || time.offset<0 || unsigned(time.side)>1 ||
            (!inherited.is_empty() && inherited.len()!=definition().defaults.len()))
            return Err(StateAnimationError::InvalidValue);
        const auto &s=definition().states[usize(index)];const auto &clip=s.clip;
        auto baseline=s.write_defaults || inherited.is_empty() ? definition().defaults.as_slice():inherited;
        for (auto value:baseline) if (!finite(value)) return Err(StateAnimationError::InvalidValue);
        auto values=Vec<Scalar>::from(baseline);auto p=position(index,time.now-time.started,time.offset);
        if (!finite(p)) return Err(StateAnimationError::InvalidValue);
        auto duration=period(index);
        if (s.loop && duration>0) {
            auto cycle=__builtin_floor(p);bool edge{};
            for (unsigned i{};i<2;++i) {
                auto ordinal=cycle+Scalar(i);
                if (ordinal>0 && time.now==time.started+(ordinal-time.offset)*duration) edge=true;
            }
            p=edge ? (time.side==StateAnimationSampleSide::Before ? Scalar(1):Scalar(0)) : p-cycle;
        } else p=duration>0 && time.now==time.started+(1-time.offset)*duration ? Scalar(1):__builtin_fmax(0,__builtin_fmin(1,p));
        auto sample=clip.start+(clip.stop-clip.start)*p;
        if (clip.sampling==StateAnimationClipSampling::NormalizedFloat32Ceil) {
            auto normalized=float(p);
            if (Scalar(normalized)<p) normalized=__builtin_bit_cast(float,__builtin_bit_cast(__UINT32_TYPE__,normalized)+static_cast<__UINT32_TYPE__>(1));
            volatile float rounded=float(Scalar(normalized)*clip.stop);sample=Scalar(rounded);
        }
        for (const auto &track:clip.tracks) {
            auto value=track.curve.constant.is_some() ? *track.curve.constant : ScalarCurve{clip.start,clip.stop,1,track.curve.keys.as_slice()}.sample(sample-clip.start,clip.arithmetic);
            if (!finite(value)) return Err(StateAnimationError::InvalidValue);values[usize(track.channel)]=value;
        }
        return Ok(rstd::move(values));
    }
    auto sample_state(Index index,Scalar elapsed,Scalar offset=0,slice<Scalar> inherited={},StateAnimationSampleSide side=StateAnimationSampleSide::After) const -> Result<Vec<Scalar>,StateAnimationError> {
        return sample_state(index,StateAnimationTime{elapsed,0,offset,side},inherited);
    }
    auto next_state_boundary(Index index,StateAnimationTime begin,Scalar end) const -> Result<Option<Scalar>,StateAnimationError> {
        if (usize(index)>=definition().states.len()) return Err(StateAnimationError::InvalidState);
        if (!finite(begin.now) || !finite(begin.started) || begin.started<0 || begin.now<begin.started || !finite(begin.offset) || begin.offset<0 || !finite(end) || end<begin.now)
            return Err(StateAnimationError::InvalidValue);
        auto duration=period(index);if (!duration) return Ok(Option<Scalar>{});
        auto p=position(index,begin.now-begin.started,begin.offset);if (!finite(p)) return Err(StateAnimationError::InvalidValue);
        const auto &state=definition().states[usize(index)];auto cycle=state.loop ? __builtin_floor(p)+1:Scalar(1);
        for (unsigned attempt{};attempt<2;++attempt) {
            auto time=begin.started+(cycle-begin.offset)*duration;
            if (!finite(time)) return Err(StateAnimationError::Limit);
            if (time>begin.now) return time<end ? Ok(Some(time)):Ok(Option<Scalar>{});
            if (!state.loop) return Ok(Option<Scalar>{});
            if (cycle+1<=cycle) return Err(StateAnimationError::Limit);cycle+=1;
        }
        return Err(StateAnimationError::Limit);
    }
    static auto create(StateAnimationDefinition value) -> Result<StateAnimationProgram,StateAnimationError> {
        if (value.states.is_empty() || usize(value.initial)>=value.states.len()) return Err(StateAnimationError::InvalidState);
        if (value.states.len()>usize(1024) || value.defaults.len()>usize(4096) || value.parameters.len()>usize(256)) return Err(StateAnimationError::Limit);
        for (auto v:value.defaults) if (!finite(v)) return Err(StateAnimationError::InvalidValue);
        for (auto p:value.parameters) if (!parameter_value(p.kind,p.initial)) return Err(StateAnimationError::InvalidParameter);
        usize transition_count{},key_count{},marker_count{};
        auto transitions=[&](slice<StateAnimationTransition> entries) -> Result<empty,StateAnimationError> {
            transition_count+=entries.len();if (transition_count>usize(4096)) return Err(StateAnimationError::Limit);
            for (const auto &t:entries) {
                if (usize(t.target)>=value.states.len()) return Err(StateAnimationError::InvalidState);
                if (!finite(t.duration) || t.duration<0 || !finite(t.offset) || t.offset<0 || !finite(t.exit_time) || t.exit_time<0 ||
                    unsigned(t.interruption)>unsigned(StateAnimationInterruption::NextThenCurrent)) return Err(StateAnimationError::InvalidValue);
                if (t.conditions.len()>usize(256)) return Err(StateAnimationError::Limit);
                for (auto c:t.conditions) {
                    if (usize(c.parameter)>=value.parameters.len() || !finite(c.threshold) || unsigned(c.mode)>unsigned(StateAnimationConditionMode::NotEqual)) return Err(StateAnimationError::InvalidParameter);
                    auto kind=value.parameters[usize(c.parameter)].kind;
                    bool boolean=c.mode==StateAnimationConditionMode::If || c.mode==StateAnimationConditionMode::IfNot;
                    if ((kind==StateAnimationParameterKind::Trigger && c.mode!=StateAnimationConditionMode::If) ||
                        (kind==StateAnimationParameterKind::Bool && !boolean) ||
                        ((kind==StateAnimationParameterKind::Float || kind==StateAnimationParameterKind::Int) && boolean)) return Err(StateAnimationError::InvalidParameter);
                }
            }
            return Ok(empty{});
        };
        auto any=transitions(value.transitions.as_slice());if (any.is_err()) return Err(any.unwrap_err());
        for (const auto &state:value.states) {
            const auto &clip=state.clip;
            if (!finite(clip.start) || !finite(clip.stop) || clip.start<0 || clip.stop<clip.start || !finite(state.speed) || state.speed<=0 ||
                !finite((clip.stop-clip.start)/state.speed)) return Err(StateAnimationError::InvalidValue);
            if (unsigned(clip.arithmetic)>unsigned(ScalarCurveArithmetic::Float32Horner) || unsigned(clip.sampling)>unsigned(StateAnimationClipSampling::NormalizedFloat32Ceil)) return Err(StateAnimationError::InvalidValue);
            if (clip.sampling!=StateAnimationClipSampling::Mathematical && (Scalar(float(clip.start))!=clip.start || Scalar(float(clip.stop))!=clip.stop)) return Err(StateAnimationError::InvalidValue);
            if (clip.sampling==StateAnimationClipSampling::NormalizedFloat32Ceil && (clip.start!=0 || clip.stop<=0)) return Err(StateAnimationError::InvalidValue);
            marker_count+=clip.markers.len();if(marker_count>usize(4096))return Err(StateAnimationError::Limit);
            auto previous=clip.start;for(auto marker:clip.markers){if(!finite(marker.time) || marker.time<previous || marker.time>clip.stop)return Err(StateAnimationError::InvalidValue);previous=marker.time;}
            auto t=transitions(state.transitions.as_slice());if (t.is_err()) return Err(t.unwrap_err());
            if (clip.tracks.len()>value.defaults.len()) return Err(StateAnimationError::InvalidValue);
            Vec<Index> channels;
            for (const auto &track:clip.tracks) {
                if (usize(track.channel)>=value.defaults.len()) return Err(StateAnimationError::InvalidValue);
                for (auto channel:channels) if (channel==track.channel) return Err(StateAnimationError::InvalidValue);
                channels.push(Index(track.channel));key_count+=track.curve.keys.len();
                if (key_count>usize(1'048'576)) return Err(StateAnimationError::Limit);
                if (track.curve.constant.is_some()) {
                    if (!finite(*track.curve.constant) || !track.curve.keys.is_empty()) return Err(StateAnimationError::InvalidValue);
                    if (clip.arithmetic==ScalarCurveArithmetic::Float32Horner && Scalar(float(*track.curve.constant))!=*track.curve.constant) return Err(StateAnimationError::InvalidValue);
                } else if (track.curve.keys.is_empty() || !ScalarCurve{clip.start,clip.stop,1,track.curve.keys.as_slice()}.valid(clip.arithmetic)) return Err(StateAnimationError::InvalidValue);
            }
        }
        return Ok(StateAnimationProgram(rstd::move(value)));
    }
};
enum class StateAnimationEventKind { Entered, TransitionFinished, Completed, Interrupted, Exited, Stopped, Marker };
struct StateAnimationEvent {StateAnimationEventKind kind;Serial owner{},run{},time_ns{};Index state{},marker{},marker_index{};};
struct StateAnimationStatus {
    bool running{};Index state{};Serial run{};Option<Index> target;Option<Serial> target_run;
};
class StateAnimationPlayer {
    struct Cursor {Index state{};Serial run{},started{};Scalar offset{};Vec<Scalar> inherited;bool completed{};Option<Serial> evaluated,marker_seen;};
    struct Origin {bool any{};Index state{},index{};};
    struct Blend {
        Cursor next;Origin origin;Scalar duration{};Serial started{};
        StateAnimationInterruption interruption{StateAnimationInterruption::None};bool ordered{},frozen{};
        Vec<Scalar> pose;
    };
    enum class Mode {Ready,Running,Stopped};
    StateAnimationProgram program_;
    Serial owner_{},next_run_{1};Mode mode_{Mode::Ready};Cursor current_;Option<Blend> blend_;
    Vec<Scalar> parameters_,values_;Vec<StateAnimationEvent> events_;Option<Serial> sampled_;
    struct Snapshot {};
    StateAnimationPlayer(StateAnimationProgram program,Snapshot):program_(rstd::move(program)) {}
    static auto copy_cursor(const Cursor &value) -> Cursor {
        return {value.state,value.run,value.started,value.offset,Vec<Scalar>::from(value.inherited.as_slice()),value.completed,value.evaluated,value.marker_seen};
    }
    auto copy() const -> StateAnimationPlayer {
        StateAnimationPlayer result(StateAnimationProgram(program_),Snapshot{});
        result.owner_=owner_;result.next_run_=next_run_;result.mode_=mode_;result.current_=copy_cursor(current_);result.sampled_=sampled_;
        result.parameters_=Vec<Scalar>::from(parameters_.as_slice());result.values_=Vec<Scalar>::from(values_.as_slice());result.events_=Vec<StateAnimationEvent>::from(events_.as_slice());
        if (blend_.is_some()) {const auto &b=*blend_;result.blend_=Some(Blend{copy_cursor(b.next),b.origin,b.duration,b.started,b.interruption,b.ordered,b.frozen,Vec<Scalar>::from(b.pose.as_slice())});}
        return result;
    }
    auto definition() const -> const StateAnimationDefinition & {return program_.definition();}
    auto state(const Cursor &cursor) const -> const StateAnimationState & {return definition().states[usize(cursor.state)];}
    auto period(const Cursor &cursor) const -> Scalar {return program_.period(cursor.state);}
    auto position(const Cursor &cursor,Serial time) const -> Scalar {
        return program_.position(cursor.state,Scalar(time>=cursor.started ? time-cursor.started : 0)/1e9,cursor.offset);
    }
    auto values_at(const Cursor &cursor,Serial time) const -> Result<Vec<Scalar>,StateAnimationError> {
        return program_.sample_state(cursor.state,Scalar(time>=cursor.started ? time-cursor.started : 0)/1e9,cursor.offset,cursor.inherited.as_slice());
    }
    auto emit(StateAnimationEventKind kind,const Cursor &cursor,Serial time) -> Result<empty,StateAnimationError> {
        if (events_.len()>=usize(4096)) return Err(StateAnimationError::Limit);
        events_.push({kind,owner_,cursor.run,time,cursor.state});return Ok(empty{});
    }
    auto marker_fraction(const Cursor &cursor,StateAnimationMarker marker) const -> Scalar {
        const auto &clip=state(cursor).clip;
        return clip.stop>clip.start ? (marker.time-clip.start)/(clip.stop-clip.start):Scalar(0);
    }
    auto marker_time(const Cursor &cursor,Scalar point) const -> Result<Option<Serial>,StateAnimationError> {
        auto delta=(point-cursor.offset)*period(cursor)*1e9;
        if (!finite(point) || !finite(delta) || delta>=Scalar(~Serial{}-cursor.started)) return Err(StateAnimationError::Limit);
        return delta<0 ? Ok(None<Serial>()) : Ok(Some(cursor.started+Serial(__builtin_ceil(delta))));
    }
    auto markers(Cursor &cursor,Serial time) -> Result<empty,StateAnimationError> {
        const auto &s=state(cursor);auto duration=period(cursor);const auto &clip=s.clip;
        for(usize index{};index<clip.markers.len();++index){auto marker=clip.markers[index];
            auto fraction=marker_fraction(cursor,marker);
            auto from=cursor.marker_seen.is_some() ? position(cursor,*cursor.marker_seen):cursor.offset;
            auto cycle=s.loop && duration>0 ? __builtin_fmax(0,__builtin_floor(from-fraction)):Scalar(0);
            for(;;){
                auto deadline=marker_time(cursor,cycle+fraction);if(deadline.is_err())return Err(deadline.unwrap_err());
                if(deadline->is_some()){
                    auto when=**deadline;if(when>time)break;
                    if(cursor.marker_seen.is_none() || when>*cursor.marker_seen){
                        if(events_.len()>=usize(4096))return Err(StateAnimationError::Limit);
                        events_.push({StateAnimationEventKind::Marker,owner_,cursor.run,when,cursor.state,marker.id,Index(index.to_primitive())});
                    }
                }
                if(!s.loop || duration==0)break;
                if(cycle+1<=cycle)return Err(StateAnimationError::Limit);cycle+=1;
            }
        }
        cursor.marker_seen=Some(time);return Ok(empty{});
    }
    auto collect_markers(Serial time) -> Result<empty,StateAnimationError> {
        auto first=events_.len();auto source_end=time;
        if(blend_.is_some()){
            auto ns=__builtin_ceil(blend_->duration*1e9);
            if(!finite(ns) || ns>=Scalar(~Serial{}-blend_->started))return Err(StateAnimationError::Limit);
            auto end=blend_->started+Serial(ns);if(end<source_end)source_end=end;
        }
        if(!(blend_.is_some() && blend_->frozen)){auto result=markers(current_,source_end);if(result.is_err())return result;}
        if(blend_.is_some()){auto result=markers(blend_->next,time);if(result.is_err())return result;}
        auto before=[&](const StateAnimationEvent &a,const StateAnimationEvent &b){
            if(a.time_ns!=b.time_ns)return a.time_ns<b.time_ns;
            if(a.run!=b.run)return false;
            const auto &clip=definition().states[usize(a.state)].clip;
            return clip.stop>clip.start && clip.markers[usize(a.marker_index)].time==clip.stop && clip.markers[usize(b.marker_index)].time==clip.start;
        };
        for(auto i=first;i<events_.len();++i)for(auto j=i;j>first && before(events_[j],events_[j-usize(1)]);--j){auto item=events_[j];events_[j]=events_[j-usize(1)];events_[j-usize(1)]=item;}
        return Ok(empty{});
    }
    auto cursor(Index index,Serial time,Scalar offset) -> Result<Cursor,StateAnimationError> {
        if (next_run_==~Serial{}) return Err(StateAnimationError::Limit);
        return Ok(Cursor{index,next_run_++,time,offset,Vec<Scalar>::from(values_.as_slice()),false});
    }
    auto complete(Cursor &cursor,Serial time) -> Result<empty,StateAnimationError> {
        if (!cursor.completed && !state(cursor).loop && position(cursor,time)>=1) {
            cursor.completed=true;return emit(StateAnimationEventKind::Completed,cursor,time);
        }
        return Ok(empty{});
    }
    auto render(Serial time) -> Result<empty,StateAnimationError> {
        auto from=blend_.is_some() && blend_->frozen ? Result<Vec<Scalar>,StateAnimationError>(Ok(Vec<Scalar>::from(blend_->pose.as_slice()))) : values_at(current_,time);
        if (from.is_err()) return Err(from.unwrap_err());
        auto values=rstd::move(from).unwrap();
        if (blend_.is_some()) {
            auto target=values_at(blend_->next,time);if (target.is_err()) return Err(target.unwrap_err());
            auto t=__builtin_fmin(1,Scalar(time-blend_->started)/1e9/blend_->duration);
            for (usize i{};i<values.len();++i) {
                auto a=values[i],b=(*target)[i];auto value=t>=1 ? b : (1-t)*a+t*b;
                if (!finite(value)) return Err(StateAnimationError::InvalidValue);values[i]=value;
            }
        }
        values_=rstd::move(values);return Ok(empty{});
    }
    auto condition(const StateAnimationCondition &c) const -> bool {
        auto value=parameters_[usize(c.parameter)];
        switch (c.mode) {
        case StateAnimationConditionMode::If:return value!=0;
        case StateAnimationConditionMode::IfNot:return value==0;
        case StateAnimationConditionMode::Greater:return value>c.threshold;
        case StateAnimationConditionMode::Less:return value<c.threshold;
        case StateAnimationConditionMode::Equal:return value==c.threshold;
        case StateAnimationConditionMode::NotEqual:return value!=c.threshold;
        }
        return false;
    }
    auto eligible(const StateAnimationTransition &t,const Cursor &source,Serial time) const -> bool {
        if (!t.self_transition && (t.target==current_.state || (blend_.is_some() && t.target==blend_->next.state))) return false;
        for (auto c:t.conditions) if (!condition(c)) return false;
        if (!t.has_exit_time) return true;
        auto now=position(source,time);
        auto before=source.evaluated.is_some() ? position(source,*source.evaluated):source.offset-1e-12;
        if (state(source).loop && t.exit_time<1 && period(source)>0)
            return __builtin_floor(now-t.exit_time)>__builtin_floor(before-t.exit_time);
        return before<t.exit_time && now>=t.exit_time;
    }
    auto choose(Serial time) const -> Option<Origin> {
        auto any=definition().transitions.as_slice();
        for (usize i{};i<any.len();++i) {
            if (blend_.is_some() && blend_->ordered && blend_->origin.any && i>usize(blend_->origin.index)) break;
            if (eligible(any[i],current_,time)) return Some(Origin{true,current_.state,Index(i.to_primitive())});
        }
        auto candidates=[&](const Cursor &cursor) -> Option<Origin> {
            auto entries=state(cursor).transitions.as_slice();
            for (usize i{};i<entries.len();++i) {
                if (blend_.is_some() && blend_->ordered && !blend_->origin.any && cursor.state==blend_->origin.state && i>=usize(blend_->origin.index)) break;
                if (eligible(entries[i],cursor,time)) return Some(Origin{false,cursor.state,Index(i.to_primitive())});
            }
            return None();
        };
        if (blend_.is_none()) return candidates(current_);
        if (blend_->ordered && blend_->origin.any) return None();
        auto source=blend_->interruption;
        if (source==StateAnimationInterruption::Current || source==StateAnimationInterruption::CurrentThenNext) {
            auto value=candidates(current_);if (value.is_some()) return value;
        }
        if (source==StateAnimationInterruption::Next || source==StateAnimationInterruption::NextThenCurrent || source==StateAnimationInterruption::CurrentThenNext) {
            auto value=candidates(blend_->next);if (value.is_some()) return value;
        }
        if (source==StateAnimationInterruption::NextThenCurrent) return candidates(current_);
        return None();
    }
    auto transition(const Origin &origin) const -> const StateAnimationTransition & {
        return origin.any ? definition().transitions[usize(origin.index)] : definition().states[usize(origin.state)].transitions[usize(origin.index)];
    }
    auto begin(Origin origin,Serial time) -> Result<empty,StateAnimationError> {
        const auto &t=transition(origin);auto source=(!origin.any && blend_.is_some() && origin.state==blend_->next.state) ? &blend_->next:&current_;
        auto duration=t.duration*(t.fixed_duration ? Scalar(1):period(*source));
        if (!finite(duration)) return Err(StateAnimationError::InvalidValue);
        for (auto c:t.conditions) if (definition().parameters[usize(c.parameter)].kind==StateAnimationParameterKind::Trigger) parameters_[usize(c.parameter)]=0;
        auto next=cursor(t.target,time,t.offset);if (next.is_err()) return Err(next.unwrap_err());
        bool frozen=blend_.is_some();Vec<Scalar> pose;
        if (frozen) {
            auto interrupted=emit(StateAnimationEventKind::Interrupted,blend_->next,time);if (interrupted.is_err()) return interrupted;
            pose=Vec<Scalar>::from(values_.as_slice());current_=rstd::move(blend_->next);blend_=None();
        }
        auto entered=emit(StateAnimationEventKind::Entered,*next,time);if (entered.is_err()) return entered;
        if (duration==0) {
            auto exited=emit(StateAnimationEventKind::Exited,current_,time);if (exited.is_err()) return exited;
            current_=rstd::move(next).unwrap();return emit(StateAnimationEventKind::TransitionFinished,current_,time);
        }
        blend_=Some(Blend{rstd::move(next).unwrap(),origin,duration,time,t.interruption,t.ordered,frozen,rstd::move(pose)});
        return Ok(empty{});
    }
    auto advance_impl(Serial time) -> Result<empty,StateAnimationError> {
        if (sampled_.is_some() && time<*sampled_) return Err(StateAnimationError::Timing);
        if (mode_==Mode::Stopped) {sampled_=Some(time);return Ok(empty{});}
        if (mode_==Mode::Ready) {
            auto first=cursor(definition().initial,time,0);if (first.is_err()) return Err(first.unwrap_err());current_=rstd::move(first).unwrap();mode_=Mode::Running;
            auto entered=emit(StateAnimationEventKind::Entered,current_,time);if (entered.is_err()) return entered;
        }
        auto rendered=render(time);if (rendered.is_err()) return rendered;
        auto marked=collect_markers(time);if(marked.is_err())return marked;
        if (blend_.is_some() && Scalar(time-blend_->started)/1e9>=blend_->duration) {
            auto exited=emit(StateAnimationEventKind::Exited,current_,time);if (exited.is_err()) return exited;
            current_=rstd::move(blend_->next);blend_=None();
            auto finished=emit(StateAnimationEventKind::TransitionFinished,current_,time);if (finished.is_err()) return finished;
        }
        auto selected=choose(time);
        if (selected.is_some()) {
            auto started=begin(*selected,time);if (started.is_err()) return started;
            rendered=render(time);if (rendered.is_err()) return rendered;
            marked=collect_markers(time);if(marked.is_err())return marked;
        }
        auto completed=complete(current_,time);if (completed.is_err()) return completed;
        if (blend_.is_some()) {completed=complete(blend_->next,time);if (completed.is_err()) return completed;}
        current_.evaluated=Some(time);if (blend_.is_some()) blend_->next.evaluated=Some(time);
        sampled_=Some(time);return Ok(empty{});
    }
    auto dynamic(const Cursor &cursor) const -> bool {
        const auto &s=state(cursor);if ((!s.loop && cursor.completed) || s.clip.stop==s.clip.start) return false;
        for (const auto &track:s.clip.tracks) if (track.curve.constant.is_none()) {
            auto keys=track.curve.keys.as_slice();auto first=keys[usize()].d;
            for (auto key:keys) if (key.a!=0 || key.b!=0 || key.c!=0 || key.d!=first) return true;
        }
        return false;
    }
  public:
    explicit StateAnimationPlayer(StateAnimationProgram program):program_(rstd::move(program)),owner_(new_session()) {
        values_=Vec<Scalar>::from(definition().defaults.as_slice());current_.state=definition().initial;
        for (auto p:definition().parameters) parameters_.push(Scalar(p.initial));
    }
    StateAnimationPlayer(const StateAnimationPlayer &)=delete;
    StateAnimationPlayer(StateAnimationPlayer &&)=default;
    auto operator=(StateAnimationPlayer &&) -> StateAnimationPlayer & =default;
    auto values() const -> slice<Scalar> {return values_.as_slice();}
    auto owner() const -> Serial {return owner_;}
    auto status() const -> StateAnimationStatus {
        return {mode_==Mode::Running,current_.state,current_.run,blend_.is_some() ? Some(Index(blend_->next.state)):None<Index>(),blend_.is_some() ? Some(Serial(blend_->next.run)):None<Serial>()};
    }
    auto parameter(Index index) const -> Result<Scalar,StateAnimationError> {
        return usize(index)<parameters_.len() ? Result<Scalar,StateAnimationError>(Ok(parameters_[usize(index)])):Result<Scalar,StateAnimationError>(Err(StateAnimationError::InvalidParameter));
    }
    auto set_parameter(Index index,Scalar value) -> Result<empty,StateAnimationError> {
        if (usize(index)>=parameters_.len() || !StateAnimationProgram::parameter_value(definition().parameters[usize(index)].kind,value)) return Err(StateAnimationError::InvalidParameter);
        parameters_[usize(index)]=value;return Ok(empty{});
    }
    auto trigger(Index index) -> Result<empty,StateAnimationError> {
        if (usize(index)>=parameters_.len() || definition().parameters[usize(index)].kind!=StateAnimationParameterKind::Trigger) return Err(StateAnimationError::InvalidParameter);
        parameters_[usize(index)]=1;return Ok(empty{});
    }
    auto reset_trigger(Index index) -> Result<empty,StateAnimationError> {
        if (usize(index)>=parameters_.len() || definition().parameters[usize(index)].kind!=StateAnimationParameterKind::Trigger) return Err(StateAnimationError::InvalidParameter);
        parameters_[usize(index)]=0;return Ok(empty{});
    }
    auto advance(Serial time) -> Result<empty,StateAnimationError> {
        auto work=copy();auto result=work.advance_impl(time);if (result.is_ok()) *this=rstd::move(work);return result;
    }
    auto play(Index index,Serial time,Scalar offset=0) -> Result<Serial,StateAnimationError> {
        if (usize(index)>=definition().states.len()) return Err(StateAnimationError::InvalidState);
        if (!finite(offset) || offset<0) return Err(StateAnimationError::InvalidValue);
        auto work=copy();if (work.sampled_.is_some() && time<*work.sampled_) return Err(StateAnimationError::Timing);
        if (work.mode_==Mode::Running) {auto rendered=work.render(time);if (rendered.is_err()) return Err(rendered.unwrap_err());auto marked=work.collect_markers(time);if(marked.is_err())return Err(marked.unwrap_err());auto interrupted=work.emit(StateAnimationEventKind::Interrupted,work.blend_.is_some() ? work.blend_->next:work.current_,time);if (interrupted.is_err()) return Err(interrupted.unwrap_err());}
        auto value=work.cursor(index,time,offset);if (value.is_err()) return Err(value.unwrap_err());work.current_=rstd::move(value).unwrap();work.blend_=None();work.mode_=Mode::Running;
        auto entered=work.emit(StateAnimationEventKind::Entered,work.current_,time);if (entered.is_err()) return Err(entered.unwrap_err());
        auto marked=work.collect_markers(time);if(marked.is_err())return Err(marked.unwrap_err());
        auto rendered=work.render(time);if (rendered.is_err()) return Err(rendered.unwrap_err());auto completed=work.complete(work.current_,time);if (completed.is_err()) return Err(completed.unwrap_err());
        work.sampled_=Some(time);auto run=work.current_.run;*this=rstd::move(work);return Ok(run);
    }
    auto stop(Serial time) -> Result<empty,StateAnimationError> {
        if (sampled_.is_some() && time<*sampled_) return Err(StateAnimationError::Timing);
        if (mode_!=Mode::Running) {mode_=Mode::Stopped;sampled_=Some(time);return Ok(empty{});}
        auto work=copy();auto rendered=work.render(time);if (rendered.is_err()) return rendered;
        auto marked=work.collect_markers(time);if(marked.is_err())return marked;
        auto stopped=work.emit(StateAnimationEventKind::Stopped,work.blend_.is_some() ? work.blend_->next:work.current_,time);if (stopped.is_err()) return stopped;
        if (work.blend_.is_some()) work.current_=rstd::move(work.blend_->next);
        work.blend_=None();work.mode_=Mode::Stopped;work.sampled_=Some(time);*this=rstd::move(work);return Ok(empty{});
    }
    auto restart(Serial time) -> Result<empty,StateAnimationError> {
        if (sampled_.is_some() && time<*sampled_) return Err(StateAnimationError::Timing);
        auto work=copy();auto stopped=work.stop(time);if (stopped.is_err()) return stopped;
        work.mode_=Mode::Ready;work.blend_=None();work.values_=Vec<Scalar>::from(definition().defaults.as_slice());
        for (usize i{};i<work.parameters_.len();++i) work.parameters_[i]=definition().parameters[i].initial;
        auto result=work.advance_impl(time);if (result.is_ok()) *this=rstd::move(work);return result;
    }
    auto poll_events() -> Vec<StateAnimationEvent> {return rstd::exchange(events_,Vec<StateAnimationEvent>{});}
    auto next_tick(Serial interval) const -> Result<Option<Serial>,StateAnimationError> {
        if (!interval) return Err(StateAnimationError::InvalidValue);
        if (mode_!=Mode::Running || sampled_.is_none()) return Ok(None<Serial>());
        auto now=*sampled_;Option<Scalar> seconds;Option<Serial> marker_tick;
        auto add=[&](Scalar value) {if (finite(value) && value>0 && (seconds.is_none() || value<*seconds)) seconds=Some(value);};
        auto inspect=[&](const Cursor &cursor,bool sample_markers) -> Result<empty,StateAnimationError> {
            auto p=position(cursor,now),duration=period(cursor);const auto &s=state(cursor);
            if(sample_markers && duration>0)for(auto marker:s.clip.markers){
                auto fraction=marker_fraction(cursor,marker);
                auto cycle=s.loop ? __builtin_fmax(0,__builtin_floor(p-fraction)):Scalar(0);
                for(;;){
                    auto deadline=marker_time(cursor,cycle+fraction);if(deadline.is_err())return Err(deadline.unwrap_err());
                    if(deadline->is_some() && **deadline>now){
                        if(marker_tick.is_none() || **deadline<*marker_tick)marker_tick=Some(**deadline);
                        break;
                    }
                    if(!s.loop)break;
                    if(cycle+1<=cycle)return Err(StateAnimationError::Limit);cycle+=1;
                }
            }
            if (!s.loop && !cursor.completed && duration>0) add((1-p)*duration);
            auto exits=[&](slice<StateAnimationTransition> entries) {
                for (const auto &t:entries) if (t.has_exit_time && duration>0) {
                    bool ready=true;for (auto c:t.conditions) ready&=condition(c);if (!ready) continue;
                    auto next=t.exit_time;
                    if (s.loop && next<1) next+=__builtin_floor(p-next)+1;
                    add((next-p)*duration);
                }
            };
            exits(definition().transitions.as_slice());exits(s.transitions.as_slice());
            return Ok(empty{});
        };
        bool source_active=!(blend_.is_some() && blend_->frozen);
        bool moving=source_active && dynamic(current_);auto inspected=inspect(current_,source_active);if(inspected.is_err())return Err(inspected.unwrap_err());
        if (blend_.is_some()) {
            add(blend_->duration-Scalar(now-blend_->started)/1e9);moving|=dynamic(blend_->next);inspected=inspect(blend_->next,true);if(inspected.is_err())return Err(inspected.unwrap_err());
            auto destination=values_at(blend_->next,now);if (destination.is_err()) return Err(destination.unwrap_err());
            for (usize i{};i<values_.len();++i) moving|=values_[i]!=(*destination)[i];
        }
        if (moving) add(Scalar(interval)/1e9);
        if (choose(now).is_some()) add(Scalar(interval)/1e9);
        if (seconds.is_none()) return Ok(marker_tick);
        auto ns=__builtin_ceil(*seconds*1e9);
        if (!finite(ns) || ns>=Scalar(~Serial{}-now)) {
            if(marker_tick.is_some())return Ok(marker_tick);
            return Err(StateAnimationError::Limit);
        }
        auto delta=Serial(ns);if (!delta) delta=1;
        auto tick=now+delta;return Ok(Some(marker_tick.is_some() && *marker_tick<tick ? *marker_tick:tick));
    }
};
} // namespace vui
