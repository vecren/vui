export module vui.controls:scroll_view;
import vui;
import :types;
using namespace rstd::prelude;
export namespace vui {
struct ScrollStyle {
    ScrollAxes axes;
    Scalar line_step{40};
    auto valid() const -> bool { return finite(line_step) && line_step > 0; }
};
class ScrollState {
    UiId viewport_;
    Scalar line_step_;
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
    ScrollState(UiId viewport, Scalar step) : viewport_(viewport), line_step_(step) {}
    auto viewport() const -> UiId { return viewport_; }
    auto set_style(UiDocument &ui, ScrollStyle style) -> Result<empty, UiError> {
        if (!style.valid())
            return Err(UiError::InvalidValue);
        auto result = ui.set_viewport_axes(viewport_, style.axes);
        if (result.is_ok())
            line_step_ = style.line_step;
        return result;
    }
    void handle(EventContext &context, const RoutedEvent &event) {
        auto visual = context.visual(viewport_);
        if (!visual || visual->viewport.is_none())
            return;
        const auto &metrics = *visual->viewport;
        auto next = metrics.offset;
        if (event.input.type == EventType::PointerDown &&
            event.input.button == PointerButton::Primary) {
            if (context.focus().is_ok())
                context.prevent_default();
        } else if (event.input.type == EventType::Wheel) {
            auto remaining = context.scroll_remaining();
            auto step = event.input.scroll_unit == ScrollUnit::Lines ? line_step_ : Scalar(1);
            next = {shift(next.x, remaining.x, step, metrics.maximum.x),
                    shift(next.y, remaining.y, step, metrics.maximum.y)};
            if (next == metrics.offset)
                return;
            if (context.document().set_viewport_offset(viewport_, next).is_err())
                return;
            context.consume_scroll({consumed(next.x - metrics.offset.x, step, remaining.x),
                                    consumed(next.y - metrics.offset.y, step, remaining.y)});
            if (context.scroll_remaining() == Point{})
                context.prevent_default();
        } else if (event.input.type == EventType::KeyDown && event.target == event.current) {
            switch (event.input.key) {
            case Key::Left:
                next.x = shift(next.x, -1, line_step_, metrics.maximum.x);
                break;
            case Key::Right:
                next.x = shift(next.x, 1, line_step_, metrics.maximum.x);
                break;
            case Key::Up:
                next.y = shift(next.y, -1, line_step_, metrics.maximum.y);
                break;
            case Key::Down:
                next.y = shift(next.y, 1, line_step_, metrics.maximum.y);
                break;
            case Key::PageUp:
                next.y = shift(next.y, -metrics.viewport.height, 1, metrics.maximum.y);
                break;
            case Key::PageDown:
                next.y = shift(next.y, metrics.viewport.height, 1, metrics.maximum.y);
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
            if (context.document().set_viewport_offset(viewport_, next).is_ok())
                context.prevent_default();
        }
    }
};
class ScrollView {
  public:
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
        auto stored = ui.attach_state(id, ScrollState(view, scroll.line_step));
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
        return Ok(id);
    }
    static auto set_offset(UiDocument &ui, UiId id, Point value) -> Result<empty, UiError> {
        auto state = ui.state<ScrollState>(id);
        return state ? ui.set_viewport_offset(state->viewport(), value)
                     : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    static auto metrics(UiDocument &ui, UiId id) -> Result<ViewportMetrics, UiError> {
        auto state = ui.state<ScrollState>(id);
        return state ? ui.viewport_metrics(state->viewport())
                     : Result<ViewportMetrics, UiError>(Err(UiError::InvalidId));
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
