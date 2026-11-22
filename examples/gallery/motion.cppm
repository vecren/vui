export module vui.examples.gallery:motion;
import :style;
import :glass;
import vui.examples;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
namespace {

struct ArenaPaint {
    auto paint(PaintContext &context) -> Result<empty, UiError> {
        auto &canvas = context.canvas();
        auto size = context.size();
        for (Scalar x = 24; x < size.width; x += 24)
            canvas.rect({x, 0, 1, size.height}, edge);
        for (Scalar y = 24; y < size.height; y += 24)
            canvas.rect({0, y, size.width, 1}, edge);
        context.children();
        canvas.border(Shape::rounded_rect({0, 0, size.width, size.height}, context.style().radius), 2, edge);
        return Ok(empty{});
    }
};

struct Arena {
    auto prepare(cx::PrepareContext &context, Option<UiId> previous, NodeStyle style) -> cx::ComposeResult<UiNodeRef> {
        auto node = previous.is_some() ? context.edit().existing(*previous) : context.edit().create(LayoutKind::Stack, style);
        context.style(node, style);
        auto target = context.target();
        context.on_mount([target](cx::CommittedContext &context) -> cx::ComposeResult<cx::EffectLease> {
            auto id = context.resolve(target);
            if (id.is_err()) return Err(id.unwrap_err());
            auto installed = context.session().document().install_paint(id.unwrap(), ArenaPaint{});
            if (installed.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(installed.unwrap_err())));
            return Ok(cx::EffectLease{});
        });
        return Ok(node);
    }
};

struct TileController {
    cx::Borrowed<UiSession> session;
    cx::NodeRef canvas, node;
    static constexpr Point origin{20, 44};
    Point position{origin}, grab;
    Option<PointerKey> pointer;
    PresentationLease lease;
    TileController(cx::Borrowed<UiSession> owner, cx::NodeRef reference) : session(owner), canvas(rstd::move(reference)) {}
    static auto primary(const InputEvent &input) -> bool {
        return input.pointer_kind == PointerKind::Touch || input.button == PointerButton::Primary;
    }
    auto observe(InputContext &, const InputPacket &packet) -> InputDecision {
        if (packet.input.type != EventType::PointerDown) return {};
        return !pointer && primary(packet.input) ? InputDecision::accept() : InputDecision::reject();
    }
    void cancel(InputContext &, CancelReason) {}
    void move(UiDocument &ui, Point value) {
        position = value; auto id = node.id().unwrap();
        if (lease.active()) {
            PresentationBatch batch; batch.transform(id, {1, 0, 0, 1, position.x, position.y});
            ui.write_presentation(lease, batch).unwrap();
        } else ui.set_transform(id, {1, 0, 0, 1, position.x, position.y}).unwrap();
    }
    void reset(UiDocument &ui) {
        if (lease.active()) {
            move(ui, origin);
            return;
        }
        position = origin;
        ui.animate_transform(node.id().unwrap(), {1, 0, 0, 1, origin.x, origin.y},
            {280'000'000, AnimationEasing::EaseInOut, AnimationEndPolicy::CommitFinal}).unwrap();
    }
    void move_in_canvas(EventContext &context, Point value) {
        const auto *area = context.visual(canvas.id().unwrap());
        const auto *tile = context.visual(node.id().unwrap());
        if (!area || !tile) return;
        const auto max_x = rstd::cmp::max(Scalar(0), area->layout_bounds.width - tile->layout_bounds.width);
        const auto max_y = rstd::cmp::max(Scalar(0), area->layout_bounds.height - tile->layout_bounds.height);
        move(context.document(), {rstd::min(max_x, rstd::cmp::max(Scalar(0), value.x)),
                                  rstd::min(max_y, rstd::cmp::max(Scalar(0), value.y))});
    }
    void follow(EventContext &context, Point position) {
        auto snapshot = session->input().snapshot();
        if (snapshot.is_none()) return;
        const auto matrix = snapshot->local_to_surface(canvas.id().unwrap());
        if (matrix.is_err()) return;
        const auto inverse = matrix->inverse();
        if (inverse.is_none()) return;
        const auto local = inverse->map(position);
        move_in_canvas(context, {local.x - grab.x, local.y - grab.y});
    }
    void handle(EventContext &context, const RoutedEvent &event) {
        if (event.current != event.target) return;
        auto &ui = context.document();
        const auto &input = event.input;
        const PointerKey key{input.device, input.pointer};
        if (input.type == EventType::PointerDown) {
            if (pointer || !primary(input) || session->input().captured(input.pointer, input.device) != Some(event.current))
                return;
            pointer = Some(PointerKey(key));
            grab = event.local;
            context.focus().unwrap();
            PropertyKey transform{event.current, PresentationProperty::Transform};
            lease = ui.take_over(slice<PropertyKey>::from_raw_parts(&transform, usize(1))).unwrap();
            return;
        }
        if (input.type == EventType::KeyDown) {
            auto next = position;
            switch (input.key) {
            case Key::Left: next.x -= 8; break;
            case Key::Right: next.x += 8; break;
            case Key::Up: next.y -= 8; break;
            case Key::Down: next.y += 8; break;
            default: return;
            }
            move_in_canvas(context, next);
            return;
        }
        if (pointer != Some(PointerKey(key))) return;
        const bool released = input.type == EventType::PointerUp && primary(input);
        if (input.type == EventType::PointerMove || released) follow(context, input.position);
        if (released || input.type == EventType::CaptureLost || input.type == EventType::PointerCancel) {
            pointer = None();
            if (lease.active()) ui.release_presentation(lease).unwrap();
        }
    }
};
struct TileBehavior {
    rstd::rc::Rc<TileController> controller;
    void handle(EventContext &context, const RoutedEvent &event) { controller.get()->handle(context, event); }
};
struct TileFeedback {
    bool focused{}, dragging{};
    void handle(EventContext &context, const RoutedEvent &event) {
        switch (event.input.type) {
        case EventType::FocusGained: focused = true; break;
        case EventType::FocusLost: focused = false; break;
        case EventType::CaptureGained: dragging = true; break;
        case EventType::CaptureLost:
        case EventType::PointerCancel: dragging = false; break;
        default: return;
        }
        context.document().set_background(event.current, dragging ? pressed_surface :
            focused ? selected_surface : control_surface, 8).unwrap();
    }
};
struct TileDragRole {};
struct TileFeedbackRole {};
struct TileElement {
    rstd::rc::Rc<TileController> controller;
    auto prepare(cx::PrepareContext &context, Option<UiId> previous, NodeStyle style) -> cx::ComposeResult<UiNodeRef> {
        auto node = previous.is_some() ? context.edit().existing(*previous) : context.edit().create(LayoutKind::Stack, style);
        context.style(node, style);
        auto feedback = NodeStateKey::of<TileFeedbackRole>();
        auto drag = NodeStateKey::of<TileDragRole>();
        if (previous.is_none()) {
            context.edit().state(node, TileFeedback{}, feedback);
            context.edit().state(node, TileBehavior{controller}, drag);
        }
        context.behavior<TileFeedback>(node, feedback);
        context.behavior<TileBehavior>(node, drag);
        auto target = context.target();
        context.on_mount([target, receiver = controller.downgrade()](cx::CommittedContext &committed)
            -> cx::ComposeResult<cx::EffectLease> {
            auto id = committed.resolve(target);
            if (id.is_err()) return Err(id.unwrap_err());
            auto participant = committed.session().input().arena().bind(id.unwrap(), receiver);
            if (participant.is_err()) {
                cx::ComposeError error(cx::ComposeStage::Input);
                error.input = Some(participant.unwrap_err());
                return Err(rstd::move(error));
            }
            return Ok(cx::EffectLease([binding = rstd::move(participant).unwrap().binding]() mutable {
                binding.disconnect();
            }));
        });
        context.on_commit([controller = controller, target](cx::CommittedContext &) mutable -> cx::ComposeResult<empty> {
            controller.get()->node = target; return Ok(empty{});
        });
        return Ok(node);
    }
};
struct MotionDemo {
    rstd::rc::Rc<TileController> tile;
    ImageData picture{examples::make_sample_image()};
    Scalar opacity{1};

    MotionDemo(cx::Borrowed<UiSession> session, cx::NodeRef canvas)
        : tile(rstd::rc::make_rc<TileController>(session, rstd::move(canvas))) {}
};

} // namespace

[[vui::compose]]
auto motion(cx::Borrowed<UiSession> session, cx::NodeRef image, Scalar width)
    -> cx::ComposeResult<cx::Element> {
    auto made = cx::node();
    if (made.is_err()) return Err(made.unwrap_err());
    auto canvas = made.unwrap();
    auto remembered = cx::remember<rstd::rc::Rc<MotionDemo>>([session, canvas] {
        return rstd::rc::make_rc<MotionDemo>(session, canvas);
    });
    if (remembered.is_err()) return Err(remembered.unwrap_err());
    auto model = remembered.unwrap().get();
    if (model.is_err()) return Err(model.unwrap_err());
    auto demo = model.unwrap()->clone();
    auto tile = demo->tile.clone();
    auto reset = control_finish(cx::button(button_label("Reset"_str),
        [controller = tile](cx::ActionContext &context, const ClickEvent &) mutable {
            controller.get()->reset(context.document());
        }, button_style()).style(NodeStyleBuilder{}.width(80).height(44).build()));
    auto fade = control_finish(cx::button(button_label("Fade"_str),
        [demo, image](cx::ActionContext &context, const ClickEvent &) mutable {
            demo.get()->opacity = demo->opacity < 1 ? 1 : .25;
            context.document().animate_opacity(image.id().unwrap(), demo->opacity,
                {300'000'000, AnimationEasing::EaseInOut, AnimationEndPolicy::CommitFinal}).unwrap();
        }, button_style()).style(NodeStyleBuilder{}.height(44).width(72).build()));
    auto drag = control_finish(cx::element(TileElement{tile}, cx::elements({button_label("Drag"_str)}))
        .style(NodeStyleBuilder{examples::block(104, 56, control_surface, 8)}
            .transform({1, 0, 0, 1, TileController::origin.x, TileController::origin.y}).focusable(true).build()));
    constexpr Scalar canvas_height = 176;
    auto canvas_width = rstd::cmp::max(Scalar(0), width - 2 * panel_padding);
    auto lens = glass(LayoutKind::Stack, {}, {.blur = 8, .tint = {0, 0, 0, .16f}})
        .style(NodeStyleBuilder{}.width(rstd::min(Scalar(136), canvas_width * .38)).height(96)
            .horizontal(Alignment::End).vertical(Alignment::Center).radius(panel_radius)
            .hit_test_visible(false).build());
    auto lens_area = cx::stack({rstd::move(lens)}).style(fill().padding(12).hit_test_visible(false).build());
    auto drag_card = glass(LayoutKind::Column, cx::elements({
        cx::row({label("Drag"_str, 16, foreground, true), rstd::move(reset)}).gap(12),
        cx::element(Arena{}, cx::elements({rstd::move(drag), rstd::move(lens_area)}))
            .style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).height(canvas_height)
                .background({0, 0, 0, .52f}).radius(9).clip({true, {0, 0, canvas_width, canvas_height}, 9}).build())
            .reference(canvas),
    })).style(panel().build()).gap(12);
    auto image_card = glass(LayoutKind::Column, cx::elements({
        cx::row({label("Image"_str, 16, foreground, true), rstd::move(fade)}).gap(12),
        cx::image(demo->picture).style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).height(120)
            .hit_test_visible(false).build()).reference(image),
    })).style(panel().build()).gap(12);
    return Ok(cx::scroll_view(
        cx::column({rstd::move(drag_card), rstd::move(image_card)})
            .style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).build()).gap(12),
        touch_scroll())
            .style(fill().build()));
}


} // namespace examples::gallery::detail
