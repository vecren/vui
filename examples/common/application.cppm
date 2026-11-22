export module vui.examples:application;
import vui.host;
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
    host::Application application_;
    Option<rstd::path::PathBuf> capture_path_;

    auto report(gpu::Error error) -> int {
        rstd::io::eprintln("renderer stage={} vk={}", unsigned(error.stage), int(error.result));
        return 1;
    }

    auto capture(ref<rstd::path::Path> path, const gpu::Pixels &image) -> bool {
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
    auto snapshot() -> host::RenderSnapshot { return application_.snapshot(); }
    UiSession &session{application_.session()};

    auto open(const char *title, Index width, Index height, bool validation = true,
              gpu::DrawOptions draw = {.timings = true}) -> bool {
        capture_path_ = None();
        auto path = rstd::env::var_os("VUI_CAPTURE"_str);
        if (path.is_some() && !path->is_empty())
            capture_path_ = Some(rstd::path::PathBuf::from(rstd::move(path).unwrap()));
        auto opened = application_.open({.title = title, .width = width, .height = height,
                                        .validation = validation, .readback = capture_path_.is_some(), .draw = draw});
        if (opened.is_err()) {
            report(opened.unwrap_err());
            return false;
        }
        if (capture_path_.is_some() && application_.request_capture().is_err()) return false;
        session.input().set_navigation([this](EventRouter &router, const InputEvent &input) {
            if (input.type == EventType::KeyDown && input.key == Key::Escape && !input.repeat)
                application_.request_close();
            else
                TabNavigation{}(router, input);
        });
        return true;
    }
    template <class BeforeUpdate, class AfterUpdate, class Presented>
    auto run(unsigned limit, BeforeUpdate before_update, AfterUpdate after_update,
             Presented presented, bool redraw_for_limit = true) -> int {
        unsigned frames{};
        auto hooks = host::callback_hooks(
            [&](const host::HostEvents &events) -> Result<empty, UiError> {
                before_update(events.surface);
                return Ok(empty{});
            },
            [&](const AdvanceResult &result) -> Result<empty, UiError> {
                if constexpr (rstd::mtp::same_as<decltype(after_update(result)), void>)
                    after_update(result);
                else {
                    auto checked = after_update(result);
                    if (checked.is_err()) return checked;
                }
                (void)session.document().poll_animation_events();
                (void)session.motion().poll_events();
                return Ok(empty{});
            },
            [&](const host::FrameEvent &event) -> Result<empty, UiError> {
                if (event.type == host::FrameEventType::CaptureReady) {
                    if (event.error.is_some() || event.pixels.is_none() ||
                        !capture(capture_path_->as_path(), *event.pixels)) return Err(UiError::InvalidValue);
                    return Ok(empty{});
                }
                if (event.type != host::FrameEventType::GpuComplete)
                    return Ok(empty{});
                presented();
                ++frames;
                if (limit && frames >= limit)
                    application_.request_close();
                else if (limit && redraw_for_limit)
                    application_.request_redraw();
                return Ok(empty{});
            },
            [] { return true; });
        auto result = application_.run(hooks);
        if (result.is_err()) {
            auto error = result.unwrap_err();
            if (error.gpu.is_some())
                return report(*error.gpu);
            rstd::io::eprintln("runner stage={}", unsigned(error.stage));
            return 1;
        }
        rstd::io::println("GPU completed requests={}, validation errors={}", frames,
                          application_.snapshot().validation_errors);
        return application_.snapshot().validation_errors ? 1 : 0;
    }
    auto run(unsigned limit) -> int {
        return run(limit, [](const SurfaceInfo &) {}, [](const AdvanceResult &) {}, [] {});
    }
};
} // namespace examples
