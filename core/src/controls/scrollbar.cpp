module vui.controls;
import :scrollbar;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto ScrollbarOptions::valid() const -> bool {
    Scalar insets[]{handle_insets.left,handle_insets.top,handle_insets.right,handle_insets.bottom};for (auto value:insets) if (!finite(value) || value<0 || value>1048576) return false;
    return (axis==ScrollbarAxis::Horizontal || axis==ScrollbarAxis::Vertical) && finite(minimum_handle) && minimum_handle>=0 && minimum_handle<=1048576 && finite(line_step) && line_step>0 && line_step<=1 &&
        (page_step==ScrollbarPageStep::Viewport || page_step==ScrollbarPageStep::HandleFraction) && finite(track_quantum) && track_quantum>=0 && track_quantum<=1;
}

auto ScrollbarLayout::bounds(Size size) const -> Rect {
    bool vertical=options.axis==ScrollbarAxis::Vertical;auto track=vertical ? size.height : size.width;auto handle=length(track);auto start=(track-handle)*value.position;
    Rect rect=vertical ? Rect{0,start,size.width,handle} : Rect{start,0,handle,size.height};
    const auto &insets=options.handle_insets;rect.x+=insets.left;rect.y+=insets.top;rect.width=rstd::cmp::max(Scalar(0),rect.width-insets.left-insets.right);rect.height=rstd::cmp::max(Scalar(0),rect.height-insets.top-insets.bottom);return rect;
}

void ScrollbarState::write(UiDocument &ui,UiId id,Scalar value) {
    value=bounded(value);if (value_.size==1) value=0;if (value==value_.position) return;
    auto next=value_;next.position=value;set(ui,id,next).unwrap();
    if (scroll_.valid()) {
        auto metrics=ScrollView::metrics(ui,scroll_);
        if (metrics.is_ok()) {auto offset=metrics->offset;auto position=options_.reverse_scroll ? 1-value : value;if (vertical()) offset.y=position*metrics->maximum.y;else offset.x=position*metrics->maximum.x;ScrollView::set_offset(ui,scroll_,offset).unwrap();}
        else unbind_scroll();
    }
    changed_.emit(value).unwrap();
}

void ScrollbarState::page_at(UiId id,Point local,Size size) {
    auto rect=ScrollbarLayout{options_,value_}.bounds(size);if (rect.contains(local)) return;
    auto coordinate=vertical() ? local.y : local.x,center=vertical() ? rect.y+rect.height/2 : rect.x+rect.width/2;
    auto next=bounded(value_.position+(coordinate<center ? -page() : page()));
    if (options_.track_quantum>0) next=bounded(__builtin_roundeven(next/options_.track_quantum)*options_.track_quantum);
    write(session_.document(),id,next);
}

auto ScrollbarState::sample_repeat(UiId id,MotionContext &context) -> Result<MotionStatus,MotionError> {
    if (!repeating_ || pointer_.is_none()) return Ok(MotionStatus::Idle);
    if (!session_.input().pointer_available(id)) {clear_press(false);return Ok(MotionStatus::Idle);}
    if (repeat_sampled_!=context.now_ns()) {
        repeat_sampled_=context.now_ns();auto generation=press_generation_;
        session_.document().defer(id,lifetime_.guard(),[id,generation](UiDocument &ui)->Result<empty,UiError>{
            auto state=ui.state<ScrollbarState>(id);if (!state || !state->repeating_ || state->press_generation_!=generation || !state->session_.input().pointer_available(id)) return Ok(empty{});
            auto snapshot=state->session_.input().snapshot();if (snapshot.is_none()) return Ok(empty{});auto transform=snapshot->local_to_surface(id);if (transform.is_err() || transform->inverse().is_none()) return Ok(empty{});
            auto bounds=snapshot->get(id)->layout_bounds;state->page_at(id,transform->inverse()->map(state->track_position_),{bounds.width,bounds.height});return Ok(empty{});
        }).unwrap();
    }
    return Ok(MotionStatus::Running);
}

auto ScrollbarState::connect(UiId id) -> Result<empty,ControlError> {
    if (!options_.repeat_track) return Ok(empty{});
    repeat_=rstd::rc::make_rc<Repeat>(session_,id);auto motion=session_.motion().bind(repeat_.downgrade());if (motion.is_err()) return Err(ControlError{Some(UiError::Limit),{}});motion_=rstd::move(motion).unwrap();
    auto availability=session_.input().on_pointer_availability_changed(id,[&session=session_,id](bool available){if (auto state=session.document().state<ScrollbarState>(id);state && !available) state->clear_press();});if (availability.is_err()) return Err(ControlError{{},Some(availability.unwrap_err())});availability_=rstd::move(availability).unwrap();return Ok(empty{});
}

auto ScrollbarState::set(UiDocument &ui,UiId id,ScrollbarValue value) -> Result<empty,UiError> {
    if (!value.valid()) return Err(UiError::InvalidValue);if (value.size==1) value.position=0;
    if (value==value_) return Ok(empty{});
    if (!layout_pending_) {
        auto queued=ui.defer(id,lifetime_.guard(),[id](UiDocument &ui)->Result<empty,UiError>{auto state=ui.state<ScrollbarState>(id);if (!state) return Ok(empty{});state->layout_pending_=false;return ui.install_layout(id,ScrollbarLayout{state->options_,state->value_});});
        if (queued.is_err()) return Err(queued.unwrap_err());layout_pending_=true;
    }
    value_=value;return Ok(empty{});
}

auto ScrollbarState::sync(UiDocument &ui,UiId id,const ViewportMetrics &metrics) -> Result<empty,UiError> {
    auto offset=vertical() ? metrics.offset.y : metrics.offset.x;auto maximum=vertical() ? metrics.maximum.y : metrics.maximum.x;
    auto viewport=vertical() ? metrics.viewport.height : metrics.viewport.width;auto extent=vertical() ? metrics.extent.height : metrics.extent.width;
    auto excess=offset<0 ? -offset : offset>maximum ? offset-maximum : Scalar(0);
    auto position=maximum>0 ? bounded(offset/maximum) : Scalar(0);if (maximum>0 && options_.reverse_scroll) position=1-position;
    return set(ui,id,{position,extent>0 ? bounded((viewport-excess)/extent) : Scalar(1)});
}

auto ScrollbarState::bind_scroll(UiDocument &ui,UiId id,UiId scroll) -> Result<empty,UiError> {
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

void ScrollbarState::handle(EventContext &context,const RoutedEvent &event) {
    const auto &input=event.input;auto visual=context.visual(event.current);if (!visual) return;
    PointerKey pointer{input.device,input.pointer};bool vertical=this->vertical();auto coordinate=vertical ? event.local.y : event.local.x;
    auto track=vertical ? visual->layout_bounds.height : visual->layout_bounds.width;
    auto geometry=ScrollbarLayout{options_,value_};auto rect=geometry.bounds({visual->layout_bounds.width,visual->layout_bounds.height});auto length=vertical ? rect.height : rect.width;auto travel=track-geometry.length(track);
    auto lead=vertical ? options_.handle_insets.top : options_.handle_insets.left;
    auto drag=[&]{if (travel>0) write(context.document(),event.current,(coordinate-lead-grab_*length)/travel);};
    switch (input.type) {
    case EventType::PointerDown: {
        if (pointer_.is_some() || (input.pointer_kind!=PointerKind::Touch && input.button!=PointerButton::Primary)) return;
        context.focus();if (scroll_.valid() && ScrollView::stop(context.document(),scroll_).is_err()) unbind_scroll();
        auto start=vertical ? rect.y : rect.x;
        if (rect.contains(event.local) && travel>0) {
            if (context.capture_pointer().is_err()) return;pointer_=Some(pointer);grab_=length>0 ? (coordinate-start)/length : Scalar(0);
        } else if (travel>0) {
            if (options_.repeat_track) {if (context.capture_pointer().is_err()) return;pointer_=Some(pointer);repeating_=true;track_position_=input.position;++press_generation_;repeat_sampled_=context.document().animation_sample_time().unwrap_or(0);wake();}
            page_at(event.current,event.local,{visual->layout_bounds.width,visual->layout_bounds.height});
        }
        break;
    }
    case EventType::PointerMove:if (pointer_!=Some(pointer)) return;if (repeating_) {if (input.position==track_position_) break;repeating_=false;++press_generation_;grab_=.5;wake();}drag();break;
    case EventType::PointerUp:
        if (pointer_!=Some(pointer) || (input.pointer_kind!=PointerKind::Touch && input.button!=PointerButton::Primary)) return;
        if (!repeating_) drag();clear_press();context.release_pointer();break;
    case EventType::PointerCancel:
    case EventType::CaptureLost:if (pointer_!=Some(pointer)) return;clear_press();return;
    case EventType::KeyDown: {
        if (event.target!=event.current) return;Scalar next=value_.position;
        if (input.key==Key::Home) next=0;else if (input.key==Key::End) next=1;
        else if (input.key==Key::PageUp) next-=page();else if (input.key==Key::PageDown) next+=page();
        else if (input.key==(vertical ? Key::Up : Key::Left)) next-=options_.line_step;
        else if (input.key==(vertical ? Key::Down : Key::Right)) next+=options_.line_step;else return;
        write(context.document(),event.current,next);break;
    }
    default:return;
    }
    context.prevent_default();context.stop_propagation();
}

auto Scrollbar::create(UiSession &session,UiId handle,NodeStyle style,ScrollbarOptions options) -> Result<UiId,ControlError> {
    auto &ui=session.document();if (!options.valid()) return Err(ControlError{Some(UiError::InvalidValue),{}});if (!ui.valid(handle)) return Err(ControlError{Some(UiError::InvalidId),{}});
    style.focusable=true;auto created=ui.create<Stack>(style);if (created.is_err()) return Err(ControlError{Some(created.unwrap_err()),{}});auto id=*created;
    auto fail=[&](UiError error)->Result<UiId,ControlError>{ui.destroy(id);return Err(ControlError{Some(error),{}});};
    auto state=ui.attach_state(id,ScrollbarState(session,handle,options));if (state.is_err()) return fail(state.unwrap_err());
    auto behavior=session.input().attach_behavior<ScrollbarState>(id);if (behavior.is_err()) {ui.destroy(id);return Err(ControlError{{},Some(behavior.unwrap_err())});}
    auto layout=ui.install_layout(id,ScrollbarLayout{options,{}});if (layout.is_err()) return fail(layout.unwrap_err());
    auto connected=ui.state<ScrollbarState>(id)->connect(id);if (connected.is_err()) {ui.destroy(id);return Err(connected.unwrap_err());}
    auto attached=ui.attach(id,handle);if (attached.is_err()) return fail(attached.unwrap_err());return Ok(id);
}
} // namespace vui
