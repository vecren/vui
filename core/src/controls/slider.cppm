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
    auto valid() const -> bool;
    auto ratio(Scalar value) const -> Scalar {return (value-minimum)/(maximum-minimum);}
    auto bounded(Scalar value) const -> Scalar;
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
    auto geometry(UiId id,Point local) const -> Option<Geometry>;
    void write(Scalar value){value=options_.bounded(value);if(value==value_)return;value_=value;changed_.emit(value).unwrap();}
    void drag(const Geometry &geometry);
  public:
    SliderState(UiSession &session,SliderOptions options):session_(session),options_(options),value_(options.minimum){}
    auto connect(UiId id) -> Result<empty,ControlError>;
    auto parts(UiDocument &ui,SliderParts value) -> Result<empty,UiError>;
    auto set(Scalar value) -> Result<empty,UiError> {
        if(!finite(value) || value<options_.minimum || value>options_.maximum)return Err(UiError::InvalidValue);
        value_=options_.bounded(value);return Ok(empty{});
    }
    auto status() const -> SliderStatus {return {value_,options_.ratio(value_),pointer_.is_some()};}
    auto on_changed(UiDocument &ui,auto callback) -> Result<Binding,ControlError> {
        auto connected=changed_.connect(ui.bindings(),rstd::move(callback));if(connected.is_err())return Err(ControlError{Some(UiError::Limit),{}});return Ok(rstd::move(connected).unwrap());
    }
    void handle(EventContext &context,const RoutedEvent &event);
};
// Existing layouts and paint remain owned by the caller; status exposes the normalized value.
class Slider {
    static auto attach(UiSession &session,UiId id,SliderOptions options={}) -> Result<empty,ControlError>;
  public:
    static auto create(UiSession &session,NodeStyle style={},SliderOptions options={}) -> Result<UiId,ControlError>;
    static auto set_parts(UiDocument &ui,UiId id,SliderParts parts) -> Result<empty,UiError> {auto state=ui.state<SliderState>(id);return state ? state->parts(ui,parts) : Result<empty,UiError>(Err(UiError::InvalidId));}
    static auto set_value(UiDocument &ui,UiId id,Scalar value) -> Result<empty,UiError> {auto state=ui.state<SliderState>(id);return state ? state->set(value) : Result<empty,UiError>(Err(UiError::InvalidId));}
    static auto status(UiDocument &ui,UiId id) -> Result<SliderStatus,UiError> {auto state=ui.state<SliderState>(id);return state ? Result<SliderStatus,UiError>(Ok(state->status())) : Result<SliderStatus,UiError>(Err(UiError::InvalidId));}
    static auto on_changed(UiDocument &ui,UiId id,auto callback) -> Result<Binding,ControlError> {auto state=ui.state<SliderState>(id);return state ? state->on_changed(ui,rstd::move(callback)) : Result<Binding,ControlError>(Err(ControlError{Some(UiError::InvalidId),{}}));}
};
}
