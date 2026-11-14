export module vui.examples.gallery;
import vui.compose;
import vui.examples;
import rstd;

using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace cx = vui::compose;
namespace theme = examples::theme;
using examples::block;

static void pulse(UiDocument &ui, UiId picture, Scalar target, BindingGuard guard) {
    auto id = ui.animate_opacity(picture, target,
        {1'200'000'000, AnimationEasing::EaseInOut, AnimationEndPolicy::CommitFinal}).unwrap();
    ui.on_animation_end(id, guard, [picture, target, guard](UiDocument &ui, const AnimationEvent &event) {
        auto mount = ui.mount_state(picture);
        if (event.reason == AnimationEndReason::Finished && mount.is_ok() && mount->mounted && mount->generation == event.mount_generation)
            pulse(ui, picture, target < 1 ? 1 : .3, guard);
    }).unwrap();
}
struct TileController {
    cx::Borrowed<UiSession> session;
    cx::NodeRef canvas, node;
    static constexpr Point origin{84, 232};
    Point position{origin}, grab;
    PresentationLease lease;
    TileController(cx::Borrowed<UiSession> owner, cx::NodeRef reference) : session(owner), canvas(rstd::move(reference)) {}
    void move(UiDocument &ui, Point value) {
        position = value; auto id = node.id().unwrap();
        if (lease.active()) {
            PresentationBatch batch; batch.transform(id, {1, 0, 0, 1, position.x, position.y});
            ui.write_presentation(lease, batch).unwrap();
        } else ui.set_transform(id, {1, 0, 0, 1, position.x, position.y}).unwrap();
    }
    void handle(EventContext &context, const RoutedEvent &event) {
        auto &ui = context.document();
        if (event.input.type == EventType::PointerDown && event.input.button == PointerButton::Primary) {
            grab = event.local; context.capture_pointer().unwrap(); context.focus().unwrap();
            PropertyKey key{event.current, PresentationProperty::Transform};
            lease = ui.take_over(slice<PropertyKey>::from_raw_parts(&key, usize(1))).unwrap();
        } else if (event.input.type == EventType::PointerMove && lease.active()) {
            auto snapshot = session->input().snapshot();
            if (snapshot.is_none()) return;
            auto matrix = snapshot->local_to_surface(canvas.id().unwrap()).unwrap().inverse().unwrap();
            auto pointer = matrix.map(event.input.position);
            move(ui, {pointer.x - grab.x, pointer.y - grab.y});
        } else if (event.input.type == EventType::CaptureLost || event.input.type == EventType::PointerCancel) {
            if (lease.active()) ui.release_presentation(lease).unwrap();
        } else if (event.input.type == EventType::KeyDown) {
            auto next = position;
            switch (event.input.key) {
            case Key::Left: next.x -= 8; break;
            case Key::Right: next.x += 8; break;
            case Key::Up: next.y -= 8; break;
            case Key::Down: next.y += 8; break;
            default: return;
            }
            move(ui, next);
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
        context.document().set_background(event.current, focused || dragging ? theme::ink : theme::accent, 12).unwrap();
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
        context.on_commit([controller = controller, target](cx::CommittedContext &) mutable -> cx::ComposeResult<empty> {
            controller.get()->node = target; return Ok(empty{});
        });
        return Ok(node);
    }
};
struct Gallery {
    rstd::rc::Rc<TileController> tile;
    ImageData picture{examples::make_sample_image()}, frame{examples::make_button_frame()};
    Gallery(cx::Borrowed<UiSession> session, cx::NodeRef canvas)
        : tile(rstd::rc::make_rc<TileController>(session, rstd::move(canvas))) {}
    static auto label(ref<str> value, Point position, Scalar size = 14, Color color = theme::ink, bool bold = false) -> cx::Element {
        return theme::label(value, size, color, bold)
            .style(NodeStyleBuilder{}.transform({1, 0, 0, 1, position.x, position.y}).hit_test_visible(false).build());
    }
    [[vui::compose]]
    static auto content(cx::Borrowed<UiSession> session, cx::NodeRef image) -> cx::ComposeResult<cx::Element> {
        auto saved_canvas = cx::node();
        if (saved_canvas.is_err()) return Err(saved_canvas.unwrap_err());
        auto canvas = saved_canvas.unwrap();
        auto remembered = cx::remember<rstd::rc::Rc<Gallery>>([session, canvas] { return rstd::rc::make_rc<Gallery>(session, canvas); });
        if (remembered.is_err()) return Err(remembered.unwrap_err());
        auto model = remembered.unwrap().get();
        if (model.is_err()) return Err(model.unwrap_err());
        auto gallery = model.unwrap()->clone();
        auto tile = gallery->tile.clone();
        auto effect = cx::effect({}, [target = image](cx::CommittedContext &context) -> cx::ComposeResult<cx::EffectLease> {
            auto id = context.resolve(target); if (id.is_err()) return Err(id.unwrap_err());
            auto &ui = context.session().document(); auto binding = ui.bindings().make();
            pulse(ui, id.unwrap(), .3, binding.guard());
            return Ok(cx::EffectLease([binding = rstd::move(binding)]() mutable { binding.disconnect(); }));
        });
        if (effect.is_err()) return Err(effect.unwrap_err());
        Vec<cx::Element> items;
        for (unsigned i{}; i < 12; ++i) {
            auto title = rstd::format("位置 {:02}", i + 1);
            items.push(cx::button(theme::button_label(title.as_str()),
                [controller = tile, i](cx::ActionContext &context, const ClickEvent &) mutable {
                    controller.get()->move(context.document(), {TileController::origin.x + 20 * Scalar(i), TileController::origin.y});
                }, theme::button_style())
                .style(block(200, 36, {})).key(cx::Key{i + 1}));
        }
        auto reset = examples::skinned_button(theme::button_label("重置位置"_str),
            [controller = tile](cx::ActionContext &context, const ClickEvent &) mutable {
                controller.get()->move(context.document(), TileController::origin);
            }, ImageRegion::whole(gallery->frame), {{1, 1, 1, 1}, Some(Insets{3, 3, 3, 3}), true})
            .style(NodeStyleBuilder{block(112, 36, {0, 0, 0, 0})}.transform({1, 0, 0, 1, 364, 312})
                .clip({true, {0, 0, 112, 36}, 8}).build());
        auto area = [](Rect bounds) {
            return cx::native(LayoutKind::Leaf).style(NodeStyleBuilder{block(bounds.width, bounds.height, theme::surface, 12)}
                .transform({1, 0, 0, 1, bounds.x, bounds.y}).build());
        };
        auto drag_label = theme::label("拖动我"_str, 16, theme::surface, true)
            .style(NodeStyleBuilder{}.horizontal(Alignment::Center).vertical(Alignment::Center).hit_test_visible(false).build());
        return Ok(cx::stack({
            label("vui gallery"_str, {32, 32}, 28, theme::ink, true),
            label("试试拖动、滚动，以及文字与图片。"_str, {32, 78}, 14, theme::muted),
            area({32, 120, 464, 244}), area({520, 120, 248, 244}),
            label("自由拖动"_str, {52, 138}, 18, theme::ink, true),
            label("拖动色块，或用方向键微调位置。"_str, {52, 172}, 14, theme::muted),
            label("位置预设"_str, {540, 138}, 18, theme::ink, true),
            area({32, 388, 464, 164}), area({520, 388, 248, 164}),
            label("文字"_str, {52, 405}, 14, theme::muted),
            label("Hello, 世界"_str, {52, 438}, 26, theme::ink, true),
            label("سلام  /  café  /  é"_str, {52, 484}, 20, theme::muted),
            label("图片与透明度"_str, {540, 405}, 14, theme::muted),
            cx::image(gallery->picture).style(NodeStyleBuilder{block(200, 80, {0, 0, 0, 0})}.transform({1, 0, 0, 1, 544, 444})
                .hit_test_visible(false).build()).reference(image),
            cx::element(TileElement{tile}, cx::elements({rstd::move(drag_label)}))
                .style(NodeStyleBuilder{block(112, 72, theme::accent, 12)}
                    .transform({1, 0, 0, 1, TileController::origin.x, TileController::origin.y}).focusable(true).build()),
            reset,
            cx::scroll_view(cx::column(rstd::move(items)).gap(6), ScrollStyleBuilder{}.line_step(42).wheel_mode(ScrollWheelMode::Direct).build())
                .style(NodeStyleBuilder{block(200, 164, {0, 0, 0, 0})}.transform({1, 0, 0, 1, 544, 180})
                    .clip({true, {0, 0, 200, 164}, 8}).build()),
            label("Tab 切换焦点，Esc 退出。"_str, {32, 568}, 12, theme::muted),
        }).style(NodeStyleBuilder{}.width(800).height(600).build()).reference(canvas));
    }
};
int main(int argc, char **argv) {
    unsigned limit{};
    if (!examples::frame_limit(argc, argv, limit)) return 2;
    examples::Application app;
    if (!app.open("vui gallery", 800, 600)) return 1;
    auto &session = app.session; auto &ui = session.document();
    auto service = Box<vectext::SystemTextService>::make();
    ui.set_text_service(Box<TextService>::from_raw(rstd::move(service).into_raw().template cast<TextService>())).unwrap();
    auto root = ui.create<CustomLayout>(block(800, 600, theme::canvas)).unwrap();
    ui.install_layout(root, ReferenceCanvas{{800, 600}, CanvasFit::Contain, {.5, .5}, true, Some(Scalar(1.2))}).unwrap();
    ui.mount(root).unwrap();
    cx::NodeRef picture;
    auto composition = cx::Composition::create(session, root).unwrap();
    composition.set_content(Gallery::content(cx::borrow(session), picture)).unwrap();
    rstd::io::println("Drag the tile; Tab selects controls; arrows move the tile. Wheel over the right list to scroll; its buttons move the tile. Escape closes.");
    Serial animation_samples{}, animation_completions{};
    Scalar opacity_min{1}, opacity_max{};
    gpu::TextCacheStats cache; gpu::ImageCacheStats images;
    auto result = app.run(limit, [&](const SurfaceInfo &surface) {
        ui.set_layout(root, LayoutStyleBuilder{}.width(surface.logical_size.width).height(surface.logical_size.height).build()).unwrap();
    }, [&](const AdvanceResult &update) {
        for (const auto &event : ui.poll_animation_events())
            if (event.reason == AnimationEndReason::Finished) ++animation_completions;
        auto node = picture.id();
        if (node.is_ok()) {
            auto opacity = ui.resolved_style(node.unwrap()).unwrap().opacity;
            opacity_min = rstd::min(opacity_min, opacity); opacity_max = rstd::cmp::max(opacity_max, opacity);
        }
        if (update.animations_active) ++animation_samples;
        return examples::composition_status(composition);
    }, [&] { cache = app.snapshot().text; images = app.snapshot().images; });
    rstd::io::println("animation samples={} completions={} opacity={:.3}..{:.3}", animation_samples, animation_completions, opacity_min, opacity_max);
    rstd::io::println("image uploads={} hits={} entries={}", images.uploads, images.hits, images.entries);
    rstd::io::println("glyph rasterizations={} uploads={} hits={} pages={}", cache.rasterizations, cache.uploads, cache.hits, cache.pages);
    return result;
}
