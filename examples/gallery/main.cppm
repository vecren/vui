export module vui.examples.gallery;
import vui.compose;
import vui.examples;
import rstd;

using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace cx = vui::compose;
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
    cx::NodeRef node;
    Point position{100, 110}, grab;
    PresentationLease lease;
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
            ui.set_background(event.current, {.05, .65, .4, 1}, 12).unwrap();
        } else if (event.input.type == EventType::PointerMove && lease.active()) {
            move(ui, {event.input.position.x - grab.x, event.input.position.y - grab.y});
        } else if (event.input.type == EventType::CaptureLost || event.input.type == EventType::PointerCancel) {
            if (lease.active()) ui.release_presentation(lease).unwrap();
            ui.set_background(event.current, {.05, .4, .7, 1}, 12).unwrap();
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
struct TileElement {
    rstd::rc::Rc<TileController> controller;
    auto prepare(cx::PrepareContext &context, Option<UiId> previous, NodeStyle style) -> cx::ComposeResult<UiNodeRef> {
        auto node = previous.is_some() ? context.edit().existing(*previous) : context.edit().create(LayoutKind::Leaf, style);
        context.style(node, style);
        if (previous.is_none()) context.edit().state(node, TileBehavior{controller});
        context.behavior<TileBehavior>(node);
        auto target = context.target();
        context.on_commit([controller = controller, target](cx::CommittedContext &) mutable -> cx::ComposeResult<empty> {
            controller.get()->node = target; return Ok(empty{});
        });
        return Ok(node);
    }
};
struct Gallery {
    rstd::rc::Rc<TileController> tile{rstd::rc::make_rc<TileController>()};
    ImageData picture{examples::make_sample_image()}, frame{examples::make_button_frame()};
    cx::NodeRef image;
    explicit Gallery(cx::NodeRef reference) : image(rstd::move(reference)) {}
    static auto label(ref<str> value, Point position, Scalar size = 16) -> cx::Element {
        return cx::text(value).format(TextFormatBuilder{}.size(size).build()).color({.8, .9, 1, 1})
            .style(NodeStyleBuilder{}.transform({1, 0, 0, 1, position.x, position.y}).hit_test_visible(false).build());
    }
    auto content(cx::CompositionContext &scope) -> cx::ComposeResult<cx::Element> {
        auto effect = scope.effect(cx::Key{"pulse"_str}, {}, [target = image](cx::CommittedContext &context) -> cx::ComposeResult<cx::EffectLease> {
            auto id = context.resolve(target); if (id.is_err()) return Err(id.unwrap_err());
            auto &ui = context.session().document(); auto binding = ui.bindings().make();
            pulse(ui, id.unwrap(), .3, binding.guard());
            return Ok(cx::EffectLease([binding = rstd::move(binding)]() mutable { binding.disconnect(); }));
        });
        if (effect.is_err()) return Err(effect.unwrap_err());
        Vec<cx::Element> items;
        for (unsigned i{}; i < 12; ++i) {
            auto title = rstd::format("项目 {:02}", i + 1);
            items.push(cx::button(label(title.as_str(), {8, 7}, 15),
                [controller = tile, i](cx::ActionContext &context, const ClickEvent &) mutable {
                    controller.get()->move(context.document(), {100 + 10 * Scalar(i), 110});
                }, ButtonStyleBuilder{}.normal({.04f, .1f + .03f * i, .25f, 1}).pressed({.1, .6, .25, 1})
                    .focused({.2, .4, .65, 1}).radius(3).build())
                .style(block(100, 34, {})).key(cx::Key{i + 1}));
        }
        auto reset = examples::skinned_button(label("重置 / Reset"_str, {8, 10}, 14),
            [controller = tile](cx::ActionContext &context, const ClickEvent &) mutable {
                controller.get()->move(context.document(), {100, 110});
            }, ImageRegion::whole(frame), {{1, 1, 1, 1}, Some(Insets{3, 3, 3, 3}), true})
            .style(NodeStyleBuilder{block(100, 44, {})}.transform({1, 0, 0, 1, 400, 260}).build());
        return Ok(cx::fragment({
            cx::native(LayoutKind::Leaf).style(NodeStyleBuilder{block(520, 300, {.06, .08, .1, 1}, 16)}
                .transform({1, 0, 0, 1, 60, 60}).build()),
            label("拖动色块 · 滚动列表 · Tab 切换焦点"_str, {80, 320}),
            label("Hello 世界\nسلام · café · é"_str, {100, 205}, 22)
                .style(NodeStyleBuilder{}.width(260).transform({1, 0, 0, 1, 100, 205}).hit_test_visible(false).build()),
            cx::image(picture).style(NodeStyleBuilder{block(120, 80, {})}.transform({1, 0, 0, 1, 230, 100})
                .hit_test_visible(false).build()).reference(image),
            label("Image · RGBA"_str, {248, 182}, 12),
            cx::element(TileElement{tile}).style(NodeStyleBuilder{block(100, 72, {.05, .4, .7, 1}, 12)}
                .transform({1, 0, 0, 1, 100, 110}).focusable(true).build()),
            reset,
            cx::scroll_view(cx::column(rstd::move(items)), ScrollStyleBuilder{}.line_step(40).wheel_mode(ScrollWheelMode::Direct).build())
                .style(NodeStyleBuilder{block(100, 150, {})}.transform({1, 0, 0, 1, 400, 85})
                    .clip({true, {0, 0, 100, 150}, 6}).build()),
        }));
    }
};
int main(int argc, char **argv) {
    unsigned limit{};
    if (!examples::frame_limit(argc, argv, limit)) return 2;
    examples::Application app;
    if (!app.open("vui gallery", 640, 420)) return 1;
    auto &session = app.session; auto &ui = session.document();
    auto service = Box<vectext::SystemTextService>::make();
    ui.set_text_service(Box<TextService>::from_raw(rstd::move(service).into_raw().template cast<TextService>())).unwrap();
    auto root = ui.create<Stack>(block(32768, 32768, {.025, .035, .05, 1})).unwrap();
    ui.mount(root).unwrap();
    cx::NodeRef picture;
    auto composition = cx::Composition::create(session, root).unwrap();
    composition.set_content([picture](cx::CompositionContext &scope) -> cx::ComposeResult<cx::Element> {
        auto remembered = scope.remember<rstd::rc::Rc<Gallery>>(cx::Key{"gallery"_str},
            [picture]() -> cx::ComposeResult<rstd::rc::Rc<Gallery>> { return Ok(rstd::rc::make_rc<Gallery>(picture)); });
        if (remembered.is_err()) return Err(remembered.unwrap_err());
        auto model = remembered.unwrap().get(); if (model.is_err()) return Err(model.unwrap_err());
        return (*model.unwrap()).get()->content(scope);
    }).unwrap();
    rstd::io::println("Drag the tile; Tab selects controls; arrows move the tile. Wheel over the right list to scroll; its buttons move the tile. Escape closes.");
    Serial animation_samples{}, animation_completions{};
    Scalar opacity_min{1}, opacity_max{};
    gpu::TextCacheStats cache; gpu::ImageCacheStats images;
    auto result = app.run(limit, [](const SurfaceInfo &) {}, [&](const AdvanceResult &update) {
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
