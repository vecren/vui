export module vui.controls:scrollbar;
import vui;
import :types;
import :scroll_view;
using namespace rstd::prelude;
export namespace vui {
enum class ScrollbarAxis { Horizontal, Vertical };
enum class ScrollbarPageStep { Viewport, HandleFraction };
struct ScrollbarOptions {
    ScrollbarAxis axis{ScrollbarAxis::Horizontal};
    Scalar minimum_handle{8}, line_step{.1};
    Insets handle_insets{};
    ScrollbarPageStep page_step{ScrollbarPageStep::Viewport};
    bool repeat_track{};
    Scalar track_quantum{};
    bool reverse_scroll{};
    auto valid() const -> bool;
};
struct ScrollbarValue {
    Scalar position{}, size{1};
    auto valid() const -> bool {return finite(position) && position>=0 && position<=1 && finite(size) && size>=0 && size<=1;}
    auto operator==(const ScrollbarValue &) const -> bool = default;
};
struct ScrollbarStatus {ScrollbarValue value;bool dragging{},repeating{};};
struct ScrollbarLayout {
    ScrollbarOptions options;
    ScrollbarValue value;
    auto length(Scalar track) const -> Scalar {auto desired=track*value.size;return desired<options.minimum_handle ? (options.minimum_handle<track ? options.minimum_handle : track) : desired;}
    auto measure(LayoutMeasureContext &context,LayoutConstraints constraints) -> Result<Size,UiError> {return context.measure_child(usize(),constraints);}
    auto bounds(Size size) const -> Rect;
    auto arrange(LayoutArrangeContext &context,Size size) -> Result<empty,UiError> {
        auto rect=bounds(size);
        return context.place_child(usize(),{rect,{rect.width,rect.height},{}});
    }
};
class ScrollbarState {
    struct Repeat {
        UiSession &session;UiId id;
        auto sample(MotionContext &context,FrameTiming) -> Result<MotionStatus,MotionError> {
            auto state=session.document().state<ScrollbarState>(id);return state ? state->sample_repeat(id,context) : Result<MotionStatus,MotionError>(Ok(MotionStatus::Finished));
        }
    };
    UiSession &session_;
    UiId handle_,scroll_;
    ScrollbarOptions options_;
    ScrollbarValue value_;
    Option<PointerKey> pointer_;
    Scalar grab_{};
    bool layout_pending_{};
    bool repeating_{};
    Point track_position_;
    Serial press_generation_{},repeat_sampled_{};
    rstd::rc::Rc<Repeat> repeat_;
    MotionRegistration motion_;
    Signal<void(Scalar)> changed_;
    Binding lifetime_,metrics_,availability_;
    Serial generation_{};
    static auto bounded(Scalar value) -> Scalar {return value<0 ? 0 : value>1 ? 1 : value;}
    auto vertical() const -> bool {return options_.axis==ScrollbarAxis::Vertical;}
    auto page() const -> Scalar {return options_.page_step==ScrollbarPageStep::HandleFraction ? value_.size : value_.size>0 && value_.size<1 ? value_.size/(1-value_.size) : options_.line_step;}
    void wake() {if (motion_.binding.alive()) session_.motion().request(motion_.id);}
    void clear_press(bool request=true) {pointer_=None();repeating_=false;++press_generation_;if (request) wake();}
    void write(UiDocument &ui,UiId id,Scalar value);
    void page_at(UiId id,Point local,Size size);
    auto sample_repeat(UiId id,MotionContext &context) -> Result<MotionStatus,MotionError>;
  public:
    ScrollbarState(UiSession &session,UiId handle,ScrollbarOptions options):session_(session),handle_(handle),options_(options),lifetime_(session.document().bindings().make()) {}
    auto connect(UiId id) -> Result<empty,ControlError>;
    auto thumb() const -> UiId {return handle_;}
    auto status() const -> ScrollbarStatus {return {value_,pointer_.is_some() && !repeating_,repeating_};}
    auto set_value(UiDocument &ui,UiId id,ScrollbarValue value) -> Result<empty,UiError> {return scroll_.valid() ? Result<empty,UiError>(Err(UiError::InvalidValue)) : set(ui,id,value);}
    auto set(UiDocument &ui,UiId id,ScrollbarValue value) -> Result<empty,UiError>;
    auto on_changed(UiDocument &ui,auto callback) -> Result<Binding,ControlError> {
        auto binding=changed_.connect(ui.bindings(),rstd::move(callback));if (binding.is_err()) return Err(ControlError{Some(UiError::Limit),{}});return Ok(rstd::move(binding).unwrap());
    }
    auto sync(UiDocument &ui,UiId id,const ViewportMetrics &metrics) -> Result<empty,UiError>;
    auto bind_scroll(UiDocument &ui,UiId id,UiId scroll) -> Result<empty,UiError>;
    void unbind_scroll() {metrics_.disconnect();scroll_={};++generation_;}
    void handle(EventContext &context,const RoutedEvent &event);
};
class Scrollbar {
  public:
    static auto create(UiSession &session,UiId handle,NodeStyle style={},ScrollbarOptions options={}) -> Result<UiId,ControlError>;
    // A bound ScrollView owns the displayed value; change its offset instead.
    static auto set_value(UiDocument &ui,UiId id,ScrollbarValue value) -> Result<empty,UiError> {auto state=ui.state<ScrollbarState>(id);return state ? state->set_value(ui,id,value) : Result<empty,UiError>(Err(UiError::InvalidId));}
    static auto status(UiDocument &ui,UiId id) -> Result<ScrollbarStatus,UiError> {auto state=ui.state<ScrollbarState>(id);return state ? Result<ScrollbarStatus,UiError>(Ok(state->status())) : Result<ScrollbarStatus,UiError>(Err(UiError::InvalidId));}
    static auto handle(UiDocument &ui,UiId id) -> Result<UiId,UiError> {auto state=ui.state<ScrollbarState>(id);return state ? Result<UiId,UiError>(Ok(state->thumb())) : Result<UiId,UiError>(Err(UiError::InvalidId));}
    static auto on_changed(UiDocument &ui,UiId id,auto callback) -> Result<Binding,ControlError> {auto state=ui.state<ScrollbarState>(id);return state ? state->on_changed(ui,rstd::move(callback)) : Result<Binding,ControlError>(Err(ControlError{Some(UiError::InvalidId),{}}));}
    static auto bind_scroll_view(UiDocument &ui,UiId id,UiId scroll) -> Result<empty,UiError> {auto state=ui.state<ScrollbarState>(id);return state ? state->bind_scroll(ui,id,scroll) : Result<empty,UiError>(Err(UiError::InvalidId));}
    static auto unbind_scroll_view(UiDocument &ui,UiId id) -> Result<empty,UiError> {auto state=ui.state<ScrollbarState>(id);if (!state) return Err(UiError::InvalidId);state->unbind_scroll();return Ok(empty{});}
};
} // namespace vui
