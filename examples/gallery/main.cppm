export module vui.examples.gallery;
import vui.controls;
import vui.examples;
import rstd;

using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;

using examples::block;
using examples::make_sample_image;

static void pulse(UiDocument &ui, UiId picture, Scalar target) {
    auto id = ui.animate_opacity(
                    picture, target,
                    {1'200'000'000, AnimationEasing::EaseInOut, AnimationEndPolicy::CommitFinal})
                  .unwrap();
    ui.on_animation_end(id, [picture, target](UiDocument &document, const AnimationEvent &event) {
          auto mount = document.mount_state(picture);
          if (event.reason == AnimationEndReason::Finished && mount.is_ok() &&
              mount.unwrap().mounted && mount.unwrap().generation == event.mount_generation)
              pulse(document, picture, target < 1 ? 1 : .3);
      }).unwrap();
}
int main(int argc, char **argv) {
    unsigned limit{};
    if (!examples::frame_limit(argc, argv, limit))
        return 2;
    examples::Application app;
    if (!app.open("vui gallery", 640, 420))
        return 1;
    auto &session = app.session;
    auto &ui = session.document();
    Vec<Binding> click_bindings_;
    auto service = Box<vectext::SystemTextService>::make();
    ui.set_text_service(
          Box<TextService>::from_raw(rstd::move(service).into_raw().template cast<TextService>()))
        .unwrap();
    auto text_label = [&](UiId parent, ref<str> value, Point position, Scalar size = 16) {
        NodeStyle style;
        style.transform = {1, 0, 0, 1, position.x, position.y};
        style.hit_test_visible = false;
        auto label = ui.append<Text>(parent, style).unwrap();
        ui.set_text(label, value).unwrap();
        ui.set_text_format(label, {.size = size}).unwrap();
        ui.set_text_color(label, {0.8f, 0.9f, 1, 1}).unwrap();
        return label;
    };
    auto root = ui.create<Stack>(block(32768, 32768, {0.025f, 0.035f, 0.05f, 1})).unwrap();
    auto panel_style = block(520, 300, {0.06f, 0.08f, 0.1f, 1}, 16);
    panel_style.transform = {1, 0, 0, 1, 60, 60};
    auto panel = ui.append<Rectangle>(root, panel_style).unwrap();
    (void)panel;
    text_label(root, "拖动色块 · 滚动列表 · Tab 切换焦点"_str, {80, 320}, 16);
    auto multilingual = text_label(root, "Hello 世界\nسلام · café · é"_str, {100, 205}, 22);
    ui.set_layout(multilingual, {.width = Some(Scalar(260))}).unwrap();
    auto picture_style = block(120, 80, {0, 0, 0, 0});
    picture_style.transform = {1, 0, 0, 1, 230, 100};
    picture_style.hit_test_visible = false;
    auto picture = ui.append<Image>(root, picture_style).unwrap();
    ui.set_image(picture, Some(make_sample_image())).unwrap();
    text_label(root, "Image · RGBA"_str, {248, 182}, 12);
    auto tile_style = block(100, 72, {0.05f, 0.4f, 0.7f, 1}, 12);
    Point position{100, 110}, grab;
    tile_style.transform.tx = position.x;
    tile_style.transform.ty = position.y;
    tile_style.focusable = true;
    auto tile = ui.append<Rectangle>(root, tile_style).unwrap();
    ui.mount(root).unwrap();
    pulse(ui, picture, .3);
    session.input()
        .on(tile, EventType::PointerDown,
            [&](auto &context, const auto &event) {
                if (event.input.button != PointerButton::Primary)
                    return;
                grab = event.local;
                context.capture_pointer().unwrap();
                context.focus().unwrap();
                context.document().set_background(tile, {0.05f, 0.65f, 0.4f, 1}, 12).unwrap();
            })
        .unwrap();
    session.input()
        .on(tile, EventType::PointerMove,
            [&](auto &context, const auto &event) {
                if (session.input().captured(event.input.pointer) != Some(tile))
                    return;
                position = {event.input.position.x - grab.x, event.input.position.y - grab.y};
                context.document()
                    .set_transform(tile, {1, 0, 0, 1, position.x, position.y})
                    .unwrap();
            })
        .unwrap();
    auto reset = [&](auto &context, const auto &) {
        context.document().set_background(tile, {0.05f, 0.4f, 0.7f, 1}, 12).unwrap();
    };
    session.input().on(tile, EventType::CaptureLost, reset).unwrap();
    session.input()
        .on(tile, EventType::KeyDown,
            [&](auto &context, const auto &event) {
                switch (event.input.key) {
                case Key::Left:
                    position.x -= 8;
                    break;
                case Key::Right:
                    position.x += 8;
                    break;
                case Key::Up:
                    position.y -= 8;
                    break;
                case Key::Down:
                    position.y += 8;
                    break;
                default:
                    return;
                }
                context.document()
                    .set_transform(tile, {1, 0, 0, 1, position.x, position.y})
                    .unwrap();
            })
        .unwrap();
    auto button_style = block(100, 44, {});
    button_style.transform = {1, 0, 0, 1, 400, 260};
    auto button = Button::create(session, button_style).unwrap();
    ui.attach(root, button).unwrap();
    text_label(button, "重置 / Reset"_str, {8, 10}, 14);
    click_bindings_.push(
        Button::on_clicked(session.document(), button, [&](auto &context, const auto &) {
            position = {100, 110};
            context.document().set_transform(tile, {1, 0, 0, 1, position.x, position.y}).unwrap();
        }).unwrap());
    auto scroll_content = ui.create<Column>().unwrap();
    for (unsigned i = 0; i < 12; ++i) {
        auto item = Button::create(session, block(100, 34, {}),
                                   {{0.04f, 0.1f + 0.03f * i, 0.25f, 1},
                                    {0.1f, 0.6f, 0.25f, 1},
                                    {0.2f, 0.4f, 0.65f, 1},
                                    3})
                        .unwrap();
        ui.attach(scroll_content, item).unwrap();
        auto title = rstd::format("项目 {:02}", i + 1);
        text_label(item, title.as_str(), {8, 7}, 15);
        click_bindings_.push(
            Button::on_clicked(session.document(), item, [&, i](auto &context, const auto &) {
                position = {100 + 10 * Scalar(i), 110};
                context.document()
                    .set_transform(tile, {1, 0, 0, 1, position.x, position.y})
                    .unwrap();
            }).unwrap());
    }
    auto scroll_style = block(100, 150, {});
    scroll_style.transform = {1, 0, 0, 1, 400, 85};
    scroll_style.clip = {true, {0, 0, 100, 150}, 6};
    auto scroll = ScrollView::create(session, scroll_content, scroll_style).unwrap();
    ui.attach(root, scroll).unwrap();
    rstd::io::println("Drag the tile; Tab selects controls; arrows move the tile. Wheel over "
                      "the right list "
                      "to scroll; its buttons move the tile. The lower-right button resets it. "
                      "Focused scroll view supports arrows, PageUp/PageDown, Home/End. Escape "
                      "closes.");
    Serial animation_samples{}, animation_completions{};
    Scalar opacity_min{1}, opacity_max{};
    gpu::TextCacheStats cache;
    gpu::ImageCacheStats image_stats;
    auto result = app.run(
        limit, [](const SurfaceInfo &) {},
        [&](const AdvanceResult &update) {
            for (const auto &event : ui.poll_animation_events())
                if (event.reason == AnimationEndReason::Finished)
                    ++animation_completions;
            auto opacity = ui.resolved_style(picture).unwrap().opacity;
            opacity_min = opacity < opacity_min ? opacity : opacity_min;
            opacity_max = opacity > opacity_max ? opacity : opacity_max;
            if (update.animations_active)
                ++animation_samples;
        },
        [&] {
            cache = app.renderer.text_cache_stats();
            image_stats = app.renderer.image_cache_stats();
        });
    rstd::io::println("animation samples={} completions={} opacity={:.3}..{:.3}", animation_samples,
                      animation_completions, opacity_min, opacity_max);
    rstd::io::println("image uploads={} hits={} entries={}", image_stats.uploads, image_stats.hits,
                      image_stats.entries);
    rstd::io::println("glyph rasterizations={} uploads={} hits={} pages={}", cache.rasterizations,
                      cache.uploads, cache.hits, cache.pages);
    return result;
}
