export module vui.controls:scroll_view;
import vui;
export import :scroll_types;
import :types;
import :scroll_motion;
using namespace rstd::prelude;
export namespace vui {
class ScrollState {
    UiId viewport_;
    ScrollStyle style_;
    Signal<void(UserScroll)> user_scrolled_;
    Option<rstd::rc::Rc<ScrollInteraction>> interaction_;
    static auto shift(Scalar value, Scalar amount, Scalar step, Scalar maximum) -> Scalar {
        if (amount > 0)
            return amount >= (maximum - value) / step ? maximum : value + amount * step;
        return amount <= -value / step ? 0 : value + amount * step;
    }
    static auto consumed(Scalar change, Scalar step, Scalar amount) -> Scalar {
        auto result = change / step;
        return amount > 0 ? (result > amount ? amount : result)
                          : (result < amount ? amount : result);
    }

  public:
    ScrollState(UiId viewport, ScrollStyle style) : viewport_(viewport), style_(style) {}
    auto style() const -> ScrollStyle { return style_; }
    void configure_prepared(ScrollStyle style) { style_ = style; }
    void finish_configuration() {
        stop(); if (interaction_.is_some()) interaction_->get()->configure();
    }
    auto viewport() const -> UiId { return viewport_; }
    auto connect(UiSession &session,UiId root) -> Result<empty,ControlError> {
        auto value=ScrollInteraction::make(session,root,viewport_,style_,user_scrolled_);if (value.is_err()) return Err(value.unwrap_err());
        interaction_=Some(rstd::move(value).unwrap());return Ok(empty{});
    }
    void stop() {if (interaction_.is_some()) interaction_->get()->stop();}
    auto status() const -> ScrollStatus {return interaction_.is_some() ? interaction_->get()->status() : ScrollStatus{};}
    auto on_user_scrolled(UiDocument &ui, auto callback) -> Result<Binding, UiError> {
        auto connected = user_scrolled_.connect(ui.bindings(), rstd::move(callback));
        return connected.is_ok() ? Result<Binding, UiError>(Ok(rstd::move(connected).unwrap())) : Result<Binding, UiError>(Err(UiError::Limit));
    }
    auto set_style(UiDocument &ui, ScrollStyle style) -> Result<empty, UiError> {
        if (!style.valid())
            return Err(UiError::InvalidValue);
        stop();
        auto result = ui.set_viewport_axes(viewport_, style.axes);
        if (result.is_ok()) {
            style_ = style;
            if (interaction_.is_some()) interaction_->get()->configure();
        }
        return result;
    }
    void handle(EventContext &context, const RoutedEvent &event) {
        if (interaction_.is_some() && interaction_->get()->handle(context,event)) return;
        if (event.input.type==EventType::Wheel || (event.input.type==EventType::KeyDown && event.target==event.current)) stop();
        auto visual = context.visual(viewport_);
        if (!visual || visual->viewport.is_none())
            return;
        auto actual=context.document().viewport_metrics(viewport_);
        auto metrics=actual.is_ok() ? *actual : *visual->viewport;
        auto next = metrics.offset;
        if (event.input.type == EventType::PointerDown &&
            event.input.button == PointerButton::Primary) {
            if (context.focus().is_ok())
                context.prevent_default();
        } else if (event.input.type == EventType::Wheel) {
            auto remaining = context.scroll_remaining();
            bool horizontal=style_.wheel_mode==ScrollWheelMode::SingleAxis && style_.axes.horizontal && !style_.axes.vertical && __builtin_fabs(remaining.y)>__builtin_fabs(remaining.x);
            bool vertical=style_.wheel_mode==ScrollWheelMode::SingleAxis && style_.axes.vertical && !style_.axes.horizontal && __builtin_fabs(remaining.x)>__builtin_fabs(remaining.y);
            auto amount=horizontal ? Point{remaining.y,0} : vertical ? Point{0,remaining.x} : remaining;
            auto step = event.input.scroll_unit == ScrollUnit::Lines ? style_.line_step : Scalar(1);
            next = {shift(next.x, amount.x*style_.wheel_direction.x, step, metrics.maximum.x),
                    shift(next.y, amount.y*style_.wheel_direction.y, step, metrics.maximum.y)};
            if (next == metrics.offset)
                return;
            if (context.document().set_viewport_offset(viewport_, next).is_err())
                return;
            Point used{consumed((next.x-metrics.offset.x)*style_.wheel_direction.x,step,amount.x),consumed((next.y-metrics.offset.y)*style_.wheel_direction.y,step,amount.y)};
            context.consume_scroll(horizontal ? Point{0,used.x} : vertical ? Point{used.y,0} : used);
            if (context.scroll_remaining() == Point{})
                context.prevent_default();
            user_scrolled_.emit({next, metrics.maximum}).unwrap();
        } else if (event.input.type == EventType::KeyDown && event.target == event.current) {
            switch (event.input.key) {
            case Key::Left:
                next.x = shift(next.x, -1, style_.line_step, metrics.maximum.x);
                break;
            case Key::Right:
                next.x = shift(next.x, 1, style_.line_step, metrics.maximum.x);
                break;
            case Key::Up:
                next.y = shift(next.y, -1, style_.line_step, metrics.maximum.y);
                break;
            case Key::Down:
                next.y = shift(next.y, 1, style_.line_step, metrics.maximum.y);
                break;
            case Key::PageUp:
                if (style_.axes.horizontal && !style_.axes.vertical) next.x=shift(next.x,-metrics.viewport.width,1,metrics.maximum.x);
                else next.y = shift(next.y, -metrics.viewport.height, 1, metrics.maximum.y);
                break;
            case Key::PageDown:
                if (style_.axes.horizontal && !style_.axes.vertical) next.x=shift(next.x,metrics.viewport.width,1,metrics.maximum.x);
                else next.y = shift(next.y, metrics.viewport.height, 1, metrics.maximum.y);
                break;
            case Key::Home:
                next = {};
                break;
            case Key::End:
                next = metrics.maximum;
                break;
            default:
                return;
            }
            if (context.document().set_viewport_offset(viewport_, next).is_ok()) {
                context.prevent_default();
                if (next != metrics.offset) user_scrolled_.emit({next, metrics.maximum}).unwrap();
            }
        }
    }
};
class ScrollView {
  public:
    struct Prepared {
        UiNodeRef node;
        bool created{}, changed{};
    };
    static auto prepare(UiDocument &ui, UiEditBatch &edit, Option<UiId> previous,
                        NodeStyle style, ScrollStyle scroll, Option<NodeStyle> before = {}) -> Result<Prepared, UiError> {
        if (!scroll.valid()) return Err(UiError::InvalidValue);
        auto state = previous.is_some() ? ui.state<ScrollState>(*previous) : nullptr;
        if (previous.is_some() && !state) return Err(UiError::InvalidId);
        style.focusable = true;
        auto node = previous.is_some() ? edit.existing(*previous) : edit.create(LayoutKind::Stack, style);
        if (before.is_some()) { before->focusable = true; edit.style(node, style, *before); }
        else edit.style(node, style);
        bool changed = state && state->style() != scroll;
        if (changed) {
            struct Patch final : UiStatePatch {
                ScrollStyle style;
                explicit Patch(ScrollStyle value) : style(value) {}
                auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<ScrollState>(); }
                void publish(UiNodeState &target) override { static_cast<UiStoredState<ScrollState> &>(target).value.configure_prepared(style); }
            };
            auto patch = Box<Patch>::make(scroll);
            edit.patch(node, Box<UiStatePatch>::from_raw(rstd::move(patch).into_raw().template cast<UiStatePatch>()));
        }
        return Ok(Prepared{node, previous.is_none(), changed});
    }
    static auto prepare_state(UiDocument::PreparedEdit &edit, Prepared prepared, ScrollStyle scroll) -> Result<empty, UiError> {
        auto children = edit.children(prepared.node);
        if (children.len() != usize(1)) return Err(UiError::MissingChild);
        if (prepared.created) {
            auto stored = edit.state(prepared.node, ScrollState(children[usize()], scroll));
            if (stored.is_err()) return stored;
        }
        if (prepared.created || prepared.changed) return edit.viewport_axes(children[usize()], scroll.axes);
        return Ok(empty{});
    }
    static auto connect(UiSession &session, UiId id) -> Result<empty, ControlError> {
        auto state = session.document().state<ScrollState>(id);
        return state ? state->connect(session, id) : Result<empty, ControlError>(Err(ControlError{Some(UiError::InvalidId), {}}));
    }
    static void finish_configuration(UiDocument &ui, UiId id) {
        if (auto state = ui.state<ScrollState>(id)) state->finish_configuration();
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
        -> Result<UiId, ControlError> {
        auto &ui = session.document();
        if (!scroll.valid())
            return Err(ControlError{Some(UiError::InvalidValue), {}});
        if (!ui.valid(content))
            return Err(ControlError{Some(UiError::InvalidId), {}});
        style.focusable = true;
        auto root = ui.create<Stack>(style);
        if (root.is_err())
            return Err(ControlError{Some(root.unwrap_err()), {}});
        auto id = root.unwrap();
        auto failure = [&](UiError error) -> Result<UiId, ControlError> {
            ui.destroy(id);
            return Err(ControlError{Some(error), {}});
        };
        auto viewport = ui.append<Viewport>(id);
        if (viewport.is_err())
            return failure(viewport.unwrap_err());
        auto view = viewport.unwrap();
        ui.set_viewport_axes(view, scroll.axes).unwrap();
        auto stored = ui.attach_state(id, ScrollState(view, scroll));
        if (stored.is_err())
            return failure(stored.unwrap_err());
        auto behavior = session.input().attach_behavior<ScrollState>(id);
        if (behavior.is_err()) {
            ui.destroy(id);
            return Err(ControlError{{}, Some(behavior.unwrap_err())});
        }
        auto attached = ui.attach(view, content);
        if (attached.is_err())
            return failure(attached.unwrap_err());
        auto connected=ui.state<ScrollState>(id)->connect(session,id);
        if (connected.is_err()) {ui.destroy(id);return Err(connected.unwrap_err());}
        return Ok(id);
    }
    static auto set_offset(UiDocument &ui, UiId id, Point value) -> Result<empty, UiError> {
        auto state = ui.state<ScrollState>(id);
        if (state) state->stop();
        return state ? ui.set_viewport_offset(state->viewport(), value)
                     : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    static auto metrics(UiDocument &ui, UiId id) -> Result<ViewportMetrics, UiError> {
        auto state = ui.state<ScrollState>(id);
        return state ? ui.viewport_metrics(state->viewport())
                     : Result<ViewportMetrics, UiError>(Err(UiError::InvalidId));
    }
    static auto status(UiDocument &ui,UiId id) -> Result<ScrollStatus,UiError> {
        auto state=ui.state<ScrollState>(id);return state ? Result<ScrollStatus,UiError>(Ok(state->status())) : Result<ScrollStatus,UiError>(Err(UiError::InvalidId));
    }
    static auto stop(UiDocument &ui,UiId id) -> Result<empty,UiError> {
        auto state=ui.state<ScrollState>(id);if (!state) return Err(UiError::InvalidId);state->stop();return Ok(empty{});
    }
    static auto content(UiDocument &ui, UiId id) -> Result<Option<UiId>, UiError> {
        auto state = ui.state<ScrollState>(id);
        return state ? ui.viewport_content(state->viewport())
                     : Result<Option<UiId>, UiError>(Err(UiError::InvalidId));
    }
    static auto attach_content(UiDocument &ui, UiId id, UiId content) -> Result<empty, UiError> {
        auto state = ui.state<ScrollState>(id);
        return state ? ui.attach(state->viewport(), content)
                     : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    static auto set_style(UiDocument &ui, UiId id, ScrollStyle style) -> Result<empty, UiError> {
        auto state = ui.state<ScrollState>(id);
        return state ? state->set_style(ui, style)
                     : Result<empty, UiError>(Err(UiError::InvalidId));
    }
};
} // namespace vui
