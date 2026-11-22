export module vui.examples.gallery.desktop;
import vui.examples.gallery;
import vui.examples;
import rstd;

using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace {
using Gallery = examples::gallery::View;

auto parse_args(int argc, char **argv, Option<Index> &canvas, unsigned &limit, bool &clip_elision) -> bool {
    if (argc < 2 || rstd::ffi::CStr::from_ptr(argv[1]).to_str().unwrap_or(""_str) != "--canvas"_str)
        return examples::frame_limit(argc, argv, limit);
    if (argc > 3 && rstd::ffi::CStr::from_ptr(argv[argc - 1]).to_str().unwrap_or(""_str) == "--clip-elision"_str) {
        clip_elision = true;
        --argc;
    }
    if (argc == 3 || argc == 4) {
        auto count = rstd::from_str<u32>(rstd::ffi::CStr::from_ptr(argv[2]).to_str().unwrap_or(""_str));
        if (count.is_ok() && Gallery::supports_canvas_load(count->to_primitive())) {
            canvas = Some(Index(count->to_primitive()));
            char *frames[]{argv[0], argv[3]};
            return examples::frame_limit(argc - 2, frames, limit);
        }
    }
    rstd::io::eprintln("usage: vui-gallery --canvas <256|1024|4096> [frame-count] [--clip-elision]");
    return false;
}

struct CanvasProfile {
    Option<gpu::DrawStats> draws;
    VkExtent2D extent{};
    Serial completed{}, timed{}, gpu_samples{};
    CpuTiming prepare, resolve, visibility, record;
    double gpu_nanoseconds{};

    void observe(const host::RenderSnapshot &snapshot) {
        if (!snapshot.draws || (draws && draws->frame.id == snapshot.draws->frame.id)) return;
        draws = snapshot.draws;
        extent = snapshot.extent;
        if (++completed <= Gallery::canvas_warmup_frames || !snapshot.timings ||
            snapshot.timings->frame.id != draws->frame.id) return;
        const auto &time = *snapshot.timings;
        ++timed;
        prepare.add(time.prepare);
        resolve.add(time.resolve);
        visibility.add(time.visibility);
        record.add(time.record);
        if (time.gpu_status == gpu::GpuTimingStatus::Available && time.gpu_nanoseconds) {
            ++gpu_samples;
            gpu_nanoseconds += *time.gpu_nanoseconds;
        }
    }

    void report(Index particles, CpuTiming paint) const {
        if (!draws) return;
        rstd::io::println("canvas particles={} extent={}x{} items={} batches={} instance_bytes={} surface_culled={} clip_culled={}",
            particles, extent.width, extent.height, draws->items, draws->batches, draws->instance_bytes,
            draws->surface_culled, draws->clip_culled);
        rstd::io::println("canvas scissored_batches={} scissor_updates={}", draws->scissored_batches, draws->scissor_updates);
        rstd::io::println("canvas clip_elided_items={}", draws->clip_elided_items);
        if (paint.calls)
            rstd::io::println("canvas paint_samples={} paint_ms_avg={:.3}", paint.calls,
                double(paint.nanoseconds) / (1e6 * double(paint.calls)));
        if (timed) {
            const auto divisor = 1e6 * double(timed);
            rstd::io::println("canvas render_samples={} prepare_ms_avg={:.3} resolve_ms_avg={:.3} visibility_ms_avg={:.3} record_ms_avg={:.3}",
                timed, double(prepare.nanoseconds) / divisor, double(resolve.nanoseconds) / divisor,
                double(visibility.nanoseconds) / divisor, double(record.nanoseconds) / divisor);
        }
        if (gpu_samples)
            rstd::io::println("canvas gpu_samples={} gpu_ms_avg={:.3}", gpu_samples, gpu_nanoseconds / (1e6 * double(gpu_samples)));
    }
};
} // namespace

int main(int argc, char **argv) {
    unsigned limit{};
    Option<Index> canvas;
    bool clip_elision{};
    if (!parse_args(argc, argv, canvas, limit, clip_elision)) return 2;
    examples::Application app;
    if (!app.open("vui gallery", 800, 600, canvas.is_none(), {.timings = true, .clip_elision = clip_elision}))
        return 1;
    auto &ui = app.session.document();
    Gallery gallery(app.session, canvas);
    if (gallery.mount().is_err()) return 1;
    Serial animation_samples{}, animation_completions{};
    Scalar opacity_min{1}, opacity_max{};
    gpu::TextCacheStats cache; gpu::ImageCacheStats images;
    CanvasProfile profile;
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
    }, [&] {
        auto snapshot = app.snapshot();
        cache = snapshot.text;
        images = snapshot.images;
        if (canvas) profile.observe(snapshot);
    }, canvas.is_none());
    if (canvas) profile.report(*canvas, gallery.canvas_paint_time());
    rstd::io::println("animation samples={} completions={} opacity={:.3}..{:.3}", animation_samples, animation_completions, opacity_min, opacity_max);
    rstd::io::println("image uploads={} hits={} entries={}", images.uploads, images.hits, images.entries);
    rstd::io::println("glyph rasterizations={} uploads={} hits={} pages={}", cache.rasterizations, cache.uploads, cache.hits, cache.pages);
    return result;
}
