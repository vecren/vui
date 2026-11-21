export module vui.examples.gallery.desktop;
import vui.examples.gallery;
import vui.examples;
import rstd;

using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;

int main(int argc, char **argv) {
    unsigned limit{};
    if (!examples::frame_limit(argc, argv, limit)) return 2;
    examples::Application app;
    if (!app.open("vui gallery", 800, 600)) return 1;
    auto &ui = app.session.document();
    examples::gallery::View gallery(app.session);
    if (gallery.mount().is_err()) return 1;
    Serial animation_samples{}, animation_completions{};
    Scalar opacity_min{1}, opacity_max{};
    gpu::TextCacheStats cache; gpu::ImageCacheStats images;
    auto result = app.run(limit, [&](const SurfaceInfo &surface) {
        auto size = surface.logical_size;
        gallery.resize(size, {0, 0, size.width, size.height}).unwrap();
    }, [&](const AdvanceResult &update) {
        for (const auto &event : ui.poll_animation_events())
            if (event.reason == AnimationEndReason::Finished) ++animation_completions;
        auto node = gallery.picture();
        if (node.is_ok()) {
            auto opacity = ui.resolved_style(node.unwrap()).unwrap().opacity;
            opacity_min = rstd::min(opacity_min, opacity); opacity_max = rstd::cmp::max(opacity_max, opacity);
        }
        if (update.animations_active) ++animation_samples;
        return gallery.status();
    }, [&] { cache = app.snapshot().text; images = app.snapshot().images; });
    rstd::io::println("animation samples={} completions={} opacity={:.3}..{:.3}", animation_samples, animation_completions, opacity_min, opacity_max);
    rstd::io::println("image uploads={} hits={} entries={}", images.uploads, images.hits, images.entries);
    rstd::io::println("glyph rasterizations={} uploads={} hits={} pages={}", cache.rasterizations, cache.uploads, cache.hits, cache.pages);
    return result;
}
