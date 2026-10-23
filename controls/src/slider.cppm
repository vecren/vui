export module vui.controls:slider;
import vui;
import :types;

export namespace vui {
using namespace rstd::prelude;
enum class SliderAxis { Horizontal, Vertical };
struct SliderOptions {
    Scalar minimum{}, maximum{1}, step{}, keyboard_step{.1};
    SliderAxis axis{SliderAxis::Horizontal};
    bool reversed{};
    auto valid() const -> bool {
        return finite(minimum) && finite(maximum) && maximum>minimum && finite(maximum-minimum) &&
            finite(step) && step>=0 && step<=maximum-minimum && finite(keyboard_step) && keyboard_step>0 &&
            (axis==SliderAxis::Horizontal || axis==SliderAxis::Vertical);
    }
    auto ratio(Scalar value) const -> Scalar {return (value-minimum)/(maximum-minimum);}
    auto bounded(Scalar value) const -> Scalar {
        value=value<minimum ? minimum : value>maximum ? maximum : value;
        if(step>0 && value!=maximum)value=minimum+__builtin_roundeven((value-minimum)/step)*step;
        return value<minimum ? minimum : value>maximum ? maximum : value;
    }
};
struct SliderParts {UiId range,handle;};
struct SliderStatus {Scalar value{},ratio{};bool dragging{};};
class SliderState {
    UiSession &session_;
    SliderOptions options_;
    SliderParts parts_;
    Scalar value_,grab_{};
    Option<PointerKey> pointer_;
    Binding availability_;
    Signal<void(Scalar)> changed_;
    struct Geometry {Point point;Scalar length{},handle_length{};Rect handle;};
    auto geometry(UiId id,Point local) const -> Option<Geometry> {
        auto snapshot=session_.input().snapshot();if(snapshot.is_none())return None();
        auto range=parts_.range.valid() ? parts_.range : id;auto node=snapshot->get(range);if(!node)return None();
        auto mapped=snapshot->map_point(id,range,local);if(mapped.is_err())return None();
        Geometry result{*mapped,options_.axis==SliderAxis::Horizontal ? node->layout_bounds.width : node->layout_bounds.height};
        if(parts_.handle.valid()){
            auto thumb=snapshot->get(parts_.handle);if(!thumb)return None();
            const Point corners[]{{0,0},{thumb->layout_bounds.width,0},{0,thumb->layout_bounds.height},{thumb->layout_bounds.width,thumb->layout_bounds.height}};
            Scalar left{},top{},right{},bottom{};bool first=true;
            for(auto corner:corners){auto point=snapshot->map_point(parts_.handle,range,corner);if(point.is_err())return None();
                if(first){left=right=point->x;top=bottom=point->y;first=false;}
                else{left=rstd::cmp::min(left,point->x);right=rstd::cmp::max(right,point->x);top=rstd::cmp::min(top,point->y);bottom=rstd::cmp::max(bottom,point->y);}}
            result.handle={left,top,right-left,bottom-top};result.handle_length=options_.axis==SliderAxis::Horizontal ? right-left : bottom-top;
        }
        return Some(result);
    }
    void write(Scalar value){value=options_.bounded(value);if(value==value_)return;value_=value;changed_.emit(value).unwrap();}
    void drag(const Geometry &geometry){
        if(geometry.length<=0)return;
        auto coordinate=options_.axis==SliderAxis::Horizontal ? geometry.point.x : geometry.point.y;
        auto ratio=(coordinate-grab_*geometry.handle_length)/geometry.length;
        if(options_.reversed)ratio=1-ratio;
        write(options_.minimum+ratio*(options_.maximum-options_.minimum));
    }
  public:
    SliderState(UiSession &session,SliderOptions options):session_(session),options_(options),value_(options.minimum){}
    auto connect(UiId id) -> Result<empty,ControlError> {
        auto connected=session_.input().on_pointer_availability_changed(id,[&session=session_,id](bool available){if(!available)if(auto state=session.document().state<SliderState>(id))state->pointer_=None();});
        if(connected.is_err())return Err(ControlError{{},Some(connected.unwrap_err())});availability_=rstd::move(connected).unwrap();return Ok(empty{});
    }
    auto parts(UiDocument &ui,SliderParts value) -> Result<empty,UiError> {
        if(pointer_.is_some())return Err(UiError::InvalidValue);
        if((value.range.valid() && !ui.valid(value.range)) || (value.handle.valid() && !ui.valid(value.handle)))return Err(UiError::InvalidId);
        parts_=value;return Ok(empty{});
    }
    auto set(Scalar value) -> Result<empty,UiError> {
        if(!finite(value) || value<options_.minimum || value>options_.maximum)return Err(UiError::InvalidValue);
        value_=options_.bounded(value);return Ok(empty{});
    }
    auto status() const -> SliderStatus {return {value_,options_.ratio(value_),pointer_.is_some()};}
    auto on_changed(UiDocument &ui,auto callback) -> Result<Binding,ControlError> {
        auto connected=changed_.connect(ui.bindings(),rstd::move(callback));if(connected.is_err())return Err(ControlError{Some(UiError::Limit),{}});return Ok(rstd::move(connected).unwrap());
    }
    void handle(EventContext &context,const RoutedEvent &event){
        const auto &input=event.input;PointerKey pointer{input.device,input.pointer};
        if(input.type==EventType::CaptureLost || input.type==EventType::PointerCancel){if(pointer_==Some(pointer))pointer_=None();return;}
        if(input.type==EventType::KeyDown){
            if(event.target!=event.current || input.modifiers.alt || input.modifiers.control || input.modifiers.meta)return;
            auto increment=options_.step>0 ? options_.step : options_.keyboard_step;auto next=value_;
            auto vertical=options_.axis==SliderAxis::Vertical;
            if(input.key==Key::Home)next=options_.minimum;else if(input.key==Key::End)next=options_.maximum;
            else if(input.key==(vertical ? Key::Up : Key::Left))next+=options_.reversed ? increment : -increment;
            else if(input.key==(vertical ? Key::Down : Key::Right))next+=options_.reversed ? -increment : increment;else return;
            write(next);context.prevent_default();context.stop_propagation();return;
        }
        if(input.type!=EventType::PointerDown && input.type!=EventType::PointerMove && input.type!=EventType::PointerUp)return;
        if(input.type!=EventType::PointerDown && pointer_!=Some(pointer))return;
        if(input.type!=EventType::PointerMove && input.pointer_kind!=PointerKind::Touch && input.button!=PointerButton::Primary)return;
        if(input.type==EventType::PointerDown && pointer_.is_some())return;
        auto shape=geometry(event.current,event.local);
        if(shape.is_none()){if(pointer_.is_some()){pointer_=None();context.release_pointer();}return;}
        if(input.type==EventType::PointerDown){
            if(shape->length<=0 || context.capture_pointer().is_err())return;
            pointer_=Some(pointer);context.focus();grab_=0;
            if(parts_.handle.valid() && shape->handle.contains(shape->point) && shape->handle_length>0){
                auto coordinate=options_.axis==SliderAxis::Horizontal ? shape->point.x : shape->point.y;
                auto start=options_.axis==SliderAxis::Horizontal ? shape->handle.x : shape->handle.y;
                grab_=(coordinate-start)/shape->handle_length-.5;
            }else drag(*shape);
        }else{drag(*shape);if(input.type==EventType::PointerUp){pointer_=None();context.release_pointer();}}
        context.prevent_default();context.stop_propagation();
    }
};
// Existing layouts and paint remain owned by the caller; status exposes the normalized value.
class Slider {
    static auto attach(UiSession &session,UiId id,SliderOptions options={}) -> Result<empty,ControlError> {
        auto &ui=session.document();if(!options.valid())return Err(ControlError{Some(UiError::InvalidValue),{}});
        if(!ui.valid(id) || ui.state<SliderState>(id))return Err(ControlError{Some(UiError::InvalidId),{}});
        auto attached=ui.attach_state(id,SliderState(session,options));if(attached.is_err())return Err(ControlError{Some(attached.unwrap_err()),{}});
        auto behavior=session.input().attach_behavior<SliderState>(id);if(behavior.is_err())return Err(ControlError{{},Some(behavior.unwrap_err())});return ui.state<SliderState>(id)->connect(id);
    }
  public:
    static auto create(UiSession &session,NodeStyle style={},SliderOptions options={}) -> Result<UiId,ControlError> {
        if(!options.valid())return Err(ControlError{Some(UiError::InvalidValue),{}});style.focusable=true;
        auto created=session.document().create<Stack>(style);if(created.is_err())return Err(ControlError{Some(created.unwrap_err()),{}});
        auto attached=attach(session,*created,options);if(attached.is_err()){session.document().destroy(*created);return Err(attached.unwrap_err());}return Ok(*created);
    }
    static auto set_parts(UiDocument &ui,UiId id,SliderParts parts) -> Result<empty,UiError> {auto state=ui.state<SliderState>(id);return state ? state->parts(ui,parts) : Result<empty,UiError>(Err(UiError::InvalidId));}
    static auto set_value(UiDocument &ui,UiId id,Scalar value) -> Result<empty,UiError> {auto state=ui.state<SliderState>(id);return state ? state->set(value) : Result<empty,UiError>(Err(UiError::InvalidId));}
    static auto status(UiDocument &ui,UiId id) -> Result<SliderStatus,UiError> {auto state=ui.state<SliderState>(id);return state ? Result<SliderStatus,UiError>(Ok(state->status())) : Result<SliderStatus,UiError>(Err(UiError::InvalidId));}
    static auto on_changed(UiDocument &ui,UiId id,auto callback) -> Result<Binding,ControlError> {auto state=ui.state<SliderState>(id);return state ? state->on_changed(ui,rstd::move(callback)) : Result<Binding,ControlError>(Err(ControlError{Some(UiError::InvalidId),{}}));}
};
}
