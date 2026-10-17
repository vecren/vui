export module vui.controls:scrollbar;
import vui;
import :types;
import :scroll_view;
using namespace rstd::prelude;
export namespace vui {
enum class ScrollbarAxis { Horizontal, Vertical };
struct ScrollbarOptions {
    ScrollbarAxis axis{ScrollbarAxis::Horizontal};
    Scalar minimum_handle{8}, line_step{.1};
    auto valid() const -> bool {return (axis==ScrollbarAxis::Horizontal || axis==ScrollbarAxis::Vertical) && finite(minimum_handle) && minimum_handle>=0 && minimum_handle<=1048576 && finite(line_step) && line_step>0 && line_step<=1;}
};
struct ScrollbarValue {
    Scalar position{}, size{1};
    auto valid() const -> bool {return finite(position) && position>=0 && position<=1 && finite(size) && size>=0 && size<=1;}
    auto operator==(const ScrollbarValue &) const -> bool = default;
};
struct ScrollbarStatus {ScrollbarValue value;bool dragging{};};
struct ScrollbarLayout {
    ScrollbarOptions options;
    ScrollbarValue value;
    auto length(Scalar track) const -> Scalar {auto desired=track*value.size;return desired<options.minimum_handle ? (options.minimum_handle<track ? options.minimum_handle : track) : desired;}
    auto measure(LayoutMeasureContext &context,LayoutConstraints constraints) -> Result<Size,UiError> {return context.measure_child(usize(),constraints);}
    auto arrange(LayoutArrangeContext &context,Size size) -> Result<empty,UiError> {
        bool vertical=options.axis==ScrollbarAxis::Vertical;auto track=vertical ? size.height : size.width;auto handle=length(track);auto start=(track-handle)*value.position;
        Rect rect=vertical ? Rect{0,start,size.width,handle} : Rect{start,0,handle,size.height};
        return context.place_child(usize(),{rect,{rect.width,rect.height},{}});
    }
};
class ScrollbarState {
    UiId handle_,scroll_;
    ScrollbarOptions options_;
    ScrollbarValue value_;
    Option<PointerKey> pointer_;
    Scalar grab_{};
    bool layout_pending_{};
    Signal<void(Scalar)> changed_;
    Binding lifetime_,metrics_;
    Serial generation_{};
    static auto bounded(Scalar value) -> Scalar {return value<0 ? 0 : value>1 ? 1 : value;}
    auto vertical() const -> bool {return options_.axis==ScrollbarAxis::Vertical;}
    auto page() const -> Scalar {return value_.size>0 && value_.size<1 ? value_.size/(1-value_.size) : options_.line_step;}
    void write(EventContext &context,UiId id,Scalar value) {
        value=bounded(value);if (value_.size==1) value=0;if (value==value_.position) return;
        auto next=value_;next.position=value;set(context.document(),id,next).unwrap();
        if (scroll_.valid()) {
            auto metrics=ScrollView::metrics(context.document(),scroll_);
            if (metrics.is_ok()) {auto offset=metrics->offset;if (vertical()) offset.y=value*metrics->maximum.y;else offset.x=value*metrics->maximum.x;ScrollView::set_offset(context.document(),scroll_,offset).unwrap();}
            else unbind_scroll();
        }
        changed_.emit(value).unwrap();
    }
  public:
    ScrollbarState(UiDocument &ui,UiId handle,ScrollbarOptions options):handle_(handle),options_(options),lifetime_(ui.bindings().make()) {}
    auto thumb() const -> UiId {return handle_;}
    auto status() const -> ScrollbarStatus {return {value_,pointer_.is_some()};}
    auto set_value(UiDocument &ui,UiId id,ScrollbarValue value) -> Result<empty,UiError> {return scroll_.valid() ? Result<empty,UiError>(Err(UiError::InvalidValue)) : set(ui,id,value);}
    auto set(UiDocument &ui,UiId id,ScrollbarValue value) -> Result<empty,UiError> {
        if (!value.valid()) return Err(UiError::InvalidValue);if (value.size==1) value.position=0;
        if (value==value_) return Ok(empty{});
        if (!layout_pending_) {
            auto queued=ui.defer(id,lifetime_.guard(),[id](UiDocument &ui)->Result<empty,UiError>{auto state=ui.state<ScrollbarState>(id);if (!state) return Ok(empty{});state->layout_pending_=false;return ui.install_layout(id,ScrollbarLayout{state->options_,state->value_});});
            if (queued.is_err()) return Err(queued.unwrap_err());layout_pending_=true;
        }
        value_=value;return Ok(empty{});
    }
    auto on_changed(UiDocument &ui,auto callback) -> Result<Binding,ControlError> {
        auto binding=changed_.connect(ui.bindings(),rstd::move(callback));if (binding.is_err()) return Err(ControlError{Some(UiError::Limit),{}});return Ok(rstd::move(binding).unwrap());
    }
    auto sync(UiDocument &ui,UiId id,const ViewportMetrics &metrics) -> Result<empty,UiError> {
        auto offset=vertical() ? metrics.offset.y : metrics.offset.x;auto maximum=vertical() ? metrics.maximum.y : metrics.maximum.x;
        auto viewport=vertical() ? metrics.viewport.height : metrics.viewport.width;auto extent=vertical() ? metrics.extent.height : metrics.extent.width;
        auto excess=offset<0 ? -offset : offset>maximum ? offset-maximum : Scalar(0);
        return set(ui,id,{maximum>0 ? bounded(offset/maximum) : Scalar(0),extent>0 ? bounded((viewport-excess)/extent) : Scalar(1)});
    }
    auto bind_scroll(UiDocument &ui,UiId id,UiId scroll) -> Result<empty,UiError> {
        if (ScrollView::content(ui,scroll).is_err()) return Err(UiError::InvalidId);
        auto generation=generation_+1;
        auto binding=ScrollView::on_metrics_changed(ui,scroll,[&ui,id,scroll,generation,guard=lifetime_.guard()](ViewportMetrics value) {
            ui.defer(id,guard,[id,scroll,value,generation](UiDocument &ui)->Result<empty,UiError>{auto state=ui.state<ScrollbarState>(id);return state && ui.valid(scroll) && state->generation_==generation && state->scroll_==scroll ? state->sync(ui,id,value) : Result<empty,UiError>(Ok(empty{}));}).unwrap();
        });
        if (binding.is_err()) return Err(binding.unwrap_err());
        metrics_=rstd::move(binding).unwrap();scroll_=scroll;generation_=generation;
        auto metrics=ScrollView::metrics(ui,scroll);
        if (metrics.is_ok()) {
            auto queued=ui.defer(id,lifetime_.guard(),[id,scroll,generation,value=*metrics](UiDocument &ui)->Result<empty,UiError>{auto state=ui.state<ScrollbarState>(id);return state && ui.valid(scroll) && state->generation_==generation && state->scroll_==scroll ? state->sync(ui,id,value) : Result<empty,UiError>(Ok(empty{}));});
            if (queued.is_err()) return Err(queued.unwrap_err());
        }
        return Ok(empty{});
    }
    void unbind_scroll() {metrics_.disconnect();scroll_={};++generation_;}
    void handle(EventContext &context,const RoutedEvent &event) {
        const auto &input=event.input;auto visual=context.visual(event.current);if (!visual) return;
        PointerKey pointer{input.device,input.pointer};bool vertical=this->vertical();auto coordinate=vertical ? event.local.y : event.local.x;
        auto track=vertical ? visual->layout_bounds.height : visual->layout_bounds.width;
        auto length=ScrollbarLayout{options_,value_}.length(track);auto travel=track-length;
        auto drag=[&]{if (travel>0) write(context,event.current,(coordinate-grab_*length)/travel);};
        switch (input.type) {
        case EventType::PointerDown: {
            if (pointer_.is_some() || (input.pointer_kind!=PointerKind::Touch && input.button!=PointerButton::Primary)) return;
            context.focus();if (scroll_.valid() && ScrollView::stop(context.document(),scroll_).is_err()) unbind_scroll();
            auto start=travel*value_.position;
            if (coordinate>=start && coordinate<=start+length && travel>0) {
                if (context.capture_pointer().is_err()) return;pointer_=Some(pointer);grab_=length>0 ? (coordinate-start)/length : Scalar(0);
            } else if (travel>0) write(context,event.current,value_.position+(coordinate<start ? -page() : page()));
            break;
        }
        case EventType::PointerMove:if (pointer_!=Some(pointer)) return;drag();break;
        case EventType::PointerUp:
            if (pointer_!=Some(pointer) || (input.pointer_kind!=PointerKind::Touch && input.button!=PointerButton::Primary)) return;
            drag();pointer_=None();context.release_pointer();break;
        case EventType::PointerCancel:
        case EventType::CaptureLost:if (pointer_!=Some(pointer)) return;pointer_=None();return;
        case EventType::KeyDown: {
            if (event.target!=event.current) return;Scalar next=value_.position;
            if (input.key==Key::Home) next=0;else if (input.key==Key::End) next=1;
            else if (input.key==Key::PageUp) next-=page();else if (input.key==Key::PageDown) next+=page();
            else if (input.key==(vertical ? Key::Up : Key::Left)) next-=options_.line_step;
            else if (input.key==(vertical ? Key::Down : Key::Right)) next+=options_.line_step;else return;
            write(context,event.current,next);break;
        }
        default:return;
        }
        context.prevent_default();context.stop_propagation();
    }
};
class Scrollbar {
  public:
    static auto create(UiSession &session,UiId handle,NodeStyle style={},ScrollbarOptions options={}) -> Result<UiId,ControlError> {
        auto &ui=session.document();if (!options.valid()) return Err(ControlError{Some(UiError::InvalidValue),{}});if (!ui.valid(handle)) return Err(ControlError{Some(UiError::InvalidId),{}});
        style.focusable=true;auto created=ui.create<Stack>(style);if (created.is_err()) return Err(ControlError{Some(created.unwrap_err()),{}});auto id=*created;
        auto fail=[&](UiError error)->Result<UiId,ControlError>{ui.destroy(id);return Err(ControlError{Some(error),{}});};
        auto state=ui.attach_state(id,ScrollbarState(ui,handle,options));if (state.is_err()) return fail(state.unwrap_err());
        auto behavior=session.input().attach_behavior<ScrollbarState>(id);if (behavior.is_err()) {ui.destroy(id);return Err(ControlError{{},Some(behavior.unwrap_err())});}
        auto layout=ui.install_layout(id,ScrollbarLayout{options,{}});if (layout.is_err()) return fail(layout.unwrap_err());
        auto attached=ui.attach(id,handle);if (attached.is_err()) return fail(attached.unwrap_err());return Ok(id);
    }
    // A bound ScrollView owns the displayed value; change its offset instead.
    static auto set_value(UiDocument &ui,UiId id,ScrollbarValue value) -> Result<empty,UiError> {auto state=ui.state<ScrollbarState>(id);return state ? state->set_value(ui,id,value) : Result<empty,UiError>(Err(UiError::InvalidId));}
    static auto status(UiDocument &ui,UiId id) -> Result<ScrollbarStatus,UiError> {auto state=ui.state<ScrollbarState>(id);return state ? Result<ScrollbarStatus,UiError>(Ok(state->status())) : Result<ScrollbarStatus,UiError>(Err(UiError::InvalidId));}
    static auto handle(UiDocument &ui,UiId id) -> Result<UiId,UiError> {auto state=ui.state<ScrollbarState>(id);return state ? Result<UiId,UiError>(Ok(state->thumb())) : Result<UiId,UiError>(Err(UiError::InvalidId));}
    static auto on_changed(UiDocument &ui,UiId id,auto callback) -> Result<Binding,ControlError> {auto state=ui.state<ScrollbarState>(id);return state ? state->on_changed(ui,rstd::move(callback)) : Result<Binding,ControlError>(Err(ControlError{Some(UiError::InvalidId),{}}));}
    static auto bind_scroll_view(UiDocument &ui,UiId id,UiId scroll) -> Result<empty,UiError> {auto state=ui.state<ScrollbarState>(id);return state ? state->bind_scroll(ui,id,scroll) : Result<empty,UiError>(Err(UiError::InvalidId));}
    static auto unbind_scroll_view(UiDocument &ui,UiId id) -> Result<empty,UiError> {auto state=ui.state<ScrollbarState>(id);if (!state) return Err(UiError::InvalidId);state->unbind_scroll();return Ok(empty{});}
};
} // namespace vui
