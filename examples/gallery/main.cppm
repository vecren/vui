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

struct TileController {
    cx::Borrowed<UiSession> session;
    cx::NodeRef canvas, node;
    static constexpr Point origin{32, 64};
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
    Scalar opacity{1};
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
        auto counter = cx::state(0);
        if (counter.is_err()) return Err(counter.unwrap_err());
        auto count = counter.unwrap();
        auto count_value = cx::read(count.value());
        if (count_value.is_err()) return Err(count_value.unwrap_err());
        auto selection = cx::state(0u);
        if (selection.is_err()) return Err(selection.unwrap_err());
        auto selected = selection.unwrap();
        auto selected_value = cx::read(selected.value());
        if (selected_value.is_err()) return Err(selected_value.unwrap_err());
        auto active_item = selected_value.unwrap().value();
        auto visibility = cx::state(false);
        if (visibility.is_err()) return Err(visibility.unwrap_err());
        auto shown = visibility.unwrap();
        auto shown_value = cx::read(shown.value());
        if (shown_value.is_err()) return Err(shown_value.unwrap_err());
        Vec<cx::Element> items;
        for (unsigned i{}; i < 12; ++i) {
            auto title = rstd::format("Item {:02}", i + 1);
            bool active = active_item == i;
            items.push(cx::button(theme::button_label(title.as_str(), active ? theme::surface : theme::ink),
                [selected, i] { return selected.set(i); }, theme::button_style(active))
                .style(block(208, 36, {0, 0, 0, 0})).key(cx::Key{i + 1}));
        }
        auto reset = examples::skinned_button(theme::button_label("Reset"_str),
            [controller = tile](cx::ActionContext &context, const ClickEvent &) mutable {
                controller.get()->move(context.document(), TileController::origin);
            }, ImageRegion::whole(gallery->frame), {{1, 1, 1, 1}, Some(Insets{3, 3, 3, 3}), true})
            .style(NodeStyleBuilder{block(80, 32, {0, 0, 0, 0})}.transform({1, 0, 0, 1, 396, 268})
                .clip({true, {0, 0, 80, 32}, 8}).build());
        auto fade = cx::button(theme::button_label("Fade"_str),
            [gallery, image](cx::ActionContext &context, const ClickEvent &) mutable {
                gallery.get()->opacity = gallery->opacity < 1 ? 1 : .25;
                context.document().animate_opacity(image.id().unwrap(), gallery->opacity,
                    {300'000'000, AnimationEasing::EaseInOut, AnimationEndPolicy::CommitFinal}).unwrap();
            }, theme::button_style()).style(NodeStyleBuilder{block(72, 32, {0, 0, 0, 0})}
                .transform({1, 0, 0, 1, 676, 400}).build());
        auto area = [](Rect bounds) {
            return cx::native(LayoutKind::Leaf).style(NodeStyleBuilder{block(bounds.width, bounds.height, theme::surface, 12)}
                .transform({1, 0, 0, 1, bounds.x, bounds.y}).build());
        };
        auto drag_label = theme::label("Drag"_str, 16, theme::surface, true)
            .style(NodeStyleBuilder{}.horizontal(Alignment::Center).vertical(Alignment::Center).hit_test_visible(false).build());
        auto drag = cx::element(TileElement{tile}, cx::elements({rstd::move(drag_label)}))
            .style(NodeStyleBuilder{block(112, 72, theme::accent, 12)}
                .transform({1, 0, 0, 1, TileController::origin.x, TileController::origin.y}).focusable(true).build());
        auto count_title = rstd::format("Count {}", count_value.unwrap().value());
        auto buttons = cx::row({
            cx::button(theme::button_label(count_title.as_str(), theme::surface),
                [count] { return count.update([](int value) { return value + 1; }); }, theme::button_style(true))
                .style(block(124, 36, {0, 0, 0, 0})),
            cx::button(theme::button_label("Open dialog"_str), [shown] { return shown.set(true); }, theme::button_style())
                .style(block(124, 36, {0, 0, 0, 0})),
            cx::button(theme::button_label("Disabled"_str, theme::muted), [] {},
                ButtonStyleBuilder{theme::button_style()}.normal(theme::canvas).build())
                .style(NodeStyleBuilder{block(124, 36, {0, 0, 0, 0})}.enabled(false).build()),
        }).style(NodeStyleBuilder{}.transform({1, 0, 0, 1, 52, 160}).build()).gap(16);
        Vec<cx::Element> overlays;
        if (shown_value.unwrap().value()) {
            auto panel = cx::column({theme::label("Dialog"_str, 22, theme::ink, true),
                cx::button(theme::button_label("Close"_str, theme::surface), [shown] { return shown.set(false); },
                    theme::button_style(true)).style(block(112, 36, {0, 0, 0, 0}))})
                .style(block(320, 152, theme::surface, 12)).padding(24).gap(24);
            overlays.push(cx::dialog(rstd::move(panel), [shown](PopupCloseReason) { return shown.set(false); }));
        }
        auto body = cx::stack({
            label("vui gallery"_str, {32, 32}, 28, theme::ink, true),
            area({32, 96, 464, 132}), area({520, 96, 248, 264}),
            label("Buttons"_str, {52, 116}, 18, theme::ink, true),
            label("Scroll view"_str, {540, 116}, 18, theme::ink, true),
            area({32, 252, 464, 316}), area({520, 384, 248, 184}),
            label("Drag"_str, {52, 272}, 18, theme::ink, true),
            label("Image"_str, {540, 404}, 18, theme::ink, true),
            cx::image(gallery->picture).style(NodeStyleBuilder{block(208, 96, {0, 0, 0, 0})}.transform({1, 0, 0, 1, 540, 448})
                .hit_test_visible(false).build()).reference(image),
            cx::stack({rstd::move(drag)}).style(NodeStyleBuilder{block(424, 236, theme::canvas, 8)}
                .transform({1, 0, 0, 1, 52, 312}).clip({true, {0, 0, 424, 236}, 8}).build()).reference(canvas),
            rstd::move(buttons), rstd::move(reset), rstd::move(fade),
            cx::scroll_view(cx::column(rstd::move(items)).gap(6), ScrollStyleBuilder{}.line_step(42).wheel_mode(ScrollWheelMode::Direct).build())
                .style(NodeStyleBuilder{block(208, 188, theme::surface)}.transform({1, 0, 0, 1, 540, 152})
                    .clip({true, {0, 0, 208, 188}, 8}).build()),
        }).style(NodeStyleBuilder{}.width(800).height(600).build());
        return Ok(cx::popup_host(rstd::move(body), rstd::move(overlays)));
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
