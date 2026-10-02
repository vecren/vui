export module vui.examples:application;
import vui.sdl;
import rstd;

export namespace examples {
using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;

auto frame_limit(int argc, char **argv, unsigned &limit) -> bool {
    if (argc == 1)
        return true;
    if (argc == 2) {
        auto text = rstd::ffi::CStr::from_ptr(argv[1]).to_str();
        if (text.is_ok()) {
            auto parsed = rstd::from_str<u32>(text.unwrap());
            if (parsed.is_ok() && parsed.unwrap() <= u32(100000)) {
                limit = parsed.unwrap().to_primitive();
                return true;
            }
        }
    }
    auto program = rstd::ffi::CStr::from_ptr(argv[0]).to_str().unwrap_or("example"_str);
    rstd::io::eprintln("usage: {} [frame-count; 0 means interactive]", program);
    return false;
}
class Application {
    Option<rstd::path::PathBuf> capture_path_;

    auto report(gpu::Error error) -> int {
        auto detail = rstd::ffi::CStr::from_ptr(window.error()).to_str().unwrap_or("unknown error"_str);
        rstd::io::eprintln("renderer stage={} vk={} SDL={}", unsigned(error.stage), int(error.result),
                          detail);
        return 1;
    }

    auto capture(ref<rstd::path::Path> path) -> bool {
        auto result = renderer.readback();
        if (result.is_err()) {
            report(result.unwrap_err());
            return false;
        }
        auto image = result.unwrap();
        auto header = rstd::format("P6\n{} {}\n255\n", image.width, image.height);
        auto pixels = Vec<u8>::from(header.as_str().as_bytes());
        pixels.reserve(usize(image.width) * usize(image.height) * usize(3));
        for (Index i{}; i < image.width * image.height; ++i) {
            pixels.push(u8(image.rgba[usize(i * 4)]));
            pixels.push(u8(image.rgba[usize(i * 4 + 1)]));
            pixels.push(u8(image.rgba[usize(i * 4 + 2)]));
        }
        auto written = rstd::fs::write(path, pixels.as_slice());
        if (written.is_err()) {
            rstd::io::eprintln("could not write capture {}: {}", path, written.unwrap_err());
            return false;
        }
        return true;
    }

  public:
    sdl::Window window;
    gpu::Renderer renderer;
    UiSession session;

    auto open(const char *title, Index width, Index height) -> bool {
        capture_path_ = None();
        auto path = rstd::env::var_os("VUI_CAPTURE"_str);
        if (path.is_some() && !path->is_empty())
            capture_path_ = Some(rstd::path::PathBuf::from(rstd::move(path).unwrap()));
        auto opened = window.open(title, width, height);
        if (opened.is_err()) {
            report(opened.unwrap_err());
            return false;
        }
        auto initialized = renderer.initialize(window, true, capture_path_.is_some());
        if (initialized.is_err()) {
            report(initialized.unwrap_err());
            return false;
        }
        session.set_frame_waker(window.frame_waker()).unwrap();
        return true;
    }
    template <class BeforeUpdate, class AfterUpdate, class Presented>
    auto run(unsigned limit, BeforeUpdate before_update, AfterUpdate after_update,
             Presented presented) -> int {
        sdl::FrameLoop frames_loop(session, renderer);
        bool dirty = true, closing{}, animating = session.document().animations_active();
        unsigned frames{};
        auto close = [&] {
            closing = true;
            session.close().unwrap();
            frames_loop.close();
        };
        while (!frames_loop.status().closed) {
            auto events = window.wait(
                dirty && !closing ? 0 : frames_loop.wait_milliseconds(animating && !closing));
            bool requested_close = events.close_requested;
            for (const auto &event : events.input)
                if (event.type == EventType::KeyDown && event.key == Key::Escape)
                    requested_close = true;
            if (requested_close && !closing)
                close();
            if (!closing) {
                before_update(events.surface);
                auto update =
                    session.advance(events.input.as_slice(), window.timing(), events.surface);
                if (update.is_err()) {
                    auto error = update.unwrap_err();
                    rstd::io::eprintln("session stage={} processed={}", unsigned(error.stage),
                                      error.processed_events);
                    return 1;
                }
                animating = update.unwrap().animations_active;
                after_update(update.unwrap());
                // Completion events are observations; callbacks have already run in
                // advance().
                (void)session.document().poll_animation_events();
                for (const auto &mutation : session.poll_mutations())
                    if (mutation.error.is_some()) {
                        rstd::io::eprintln("UI mutation failed: {}", unsigned(*mutation.error));
                        return 1;
                    }
                frames_loop.set_surface(events.minimized ? VkExtent2D{} : window.pixel_extent(),
                                        events.surface.generation);
                dirty = dirty || events.redraw || update.unwrap().frame_needed;
                if (dirty) {
                    auto requested = frames_loop.request();
                    if (requested.is_ok())
                        dirty = false;
                    else if (requested.unwrap_err() != sdl::FrameRequestError::Busy)
                        return 1;
                }
            }
            auto pumped = frames_loop.pump();
            if (pumped.is_err())
                return report(pumped.unwrap_err());
            for (const auto &event : frames_loop.poll_events()) {
                if (event.type == sdl::FrameEventType::Failed) {
                    if (event.error.is_some())
                        return report(*event.error);
                    rstd::io::eprintln("frame planning failed");
                    return 1;
                }
                if (event.present.is_some() && *event.present != gpu::PresentStatus::Queued)
                    dirty = true;
                if (event.type != sdl::FrameEventType::GpuComplete)
                    continue;
                if (capture_path_.is_some() && frames == 0 && !capture(capture_path_->as_path()))
                    return 1;
                presented();
                ++frames;
                if (limit && frames >= limit && !closing)
                    close();
                else if (limit && !closing)
                    dirty = true;
            }
        }
        rstd::io::println("GPU completed requests={}, validation errors={}", frames,
                          renderer.validation_errors());
        return renderer.validation_errors() ? 1 : 0;
    }
    auto run(unsigned limit) -> int {
        return run(limit, [](const SurfaceInfo &) {}, [](const AdvanceResult &) {}, [] {});
    }
};
} // namespace examples
