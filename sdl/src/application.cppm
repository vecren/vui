export module vui.sdl:application;
import :window;
import vui.host;
using namespace rstd::prelude;

export namespace vui::sdl {
struct ApplicationOptions {
    const char *title{"vui"};
    Index width{1280}, height{720};
    bool validation{true}, readback{};
};
struct WindowDriver {
    Window &window;
    auto timing() -> FrameTiming { return window.timing(); }
    auto wait(Index milliseconds) -> host::HostEvents {
        auto events = window.wait(milliseconds);
        return {rstd::move(events.input),
                events.surface,
                events.minimized ? VkExtent2D{} : window.pixel_extent(),
                events.redraw,
                events.close_requested,
                events.dropped_input};
    }
};
class Application {
    Window window_;
    gpu::Renderer renderer_;
    UiSession session_;
    host::Runner *active_{};
    bool opened_{};
    Serial frame_interval_ns_{};
    gpu::PresentMode presentation_{gpu::PresentMode::Fifo};

  public:
    ~Application() {
        (void)session_.close();
        (void)renderer_.shutdown(5'000'000'000);
    }
    auto window() -> Window & { return window_; }
    auto renderer() -> gpu::Renderer & { return renderer_; }
    auto session() -> UiSession & { return session_; }
    auto open(ApplicationOptions options = {}) -> Result<empty, gpu::Error> {
        auto opened = window_.open(options.title, options.width, options.height);
        if (opened.is_err())
            return opened;
        auto initialized = renderer_.initialize(window_, options.validation, options.readback);
        if (initialized.is_err())
            return initialized;
        auto waker = session_.set_frame_waker(window_.frame_waker());
        if (waker.is_err()) {
            renderer_.shutdown(5'000'000'000);
            return Err(gpu::Error{gpu::Stage::State});
        }
        session_.input().set_native_capture(
            [this](PointerKind kind, bool enabled) { return window_.capture(kind, enabled); });
        opened_ = true;
        return Ok(empty{});
    }
    void request_close() {
        if (active_)
            active_->request_close();
    }
    void request_redraw() {
        if (active_)
            active_->request_redraw();
        session_.document().bindings().wake();
    }
    void set_frame_interval(Serial nanoseconds) {
        if (frame_interval_ns_ == nanoseconds) return;
        frame_interval_ns_ = nanoseconds;
        if (active_) active_->set_frame_interval(nanoseconds);
        session_.document().bindings().wake();
    }
    auto set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error> {
        if (unsigned(mode) > unsigned(gpu::PresentMode::FifoRelaxed))
            return Err(gpu::Error{gpu::Stage::State});
        if (presentation_ == mode) return Ok(empty{});
        auto result = active_ ? active_->set_present_mode(mode) : Result<empty, gpu::Error>(Ok(empty{}));
        if (result.is_ok()) { presentation_ = mode; session_.document().bindings().wake(); }
        return result;
    }
    template <class Hooks>
        requires rstd::Impled<Hooks, host::RunHooks>
    auto run(Hooks &hooks) -> Result<host::RunStats, host::RunError> {
        if (!opened_ || active_)
            return Err(host::RunError{host::RunStage::State});
        host::Runner runner(session_, renderer_);
        runner.set_frame_interval(frame_interval_ns_);
        auto configured = runner.set_present_mode(presentation_);
        if (configured.is_err()) return Err(host::RunError{host::RunStage::Frame, {}, Some(configured.unwrap_err())});
        WindowDriver driver{window_};
        active_ = &runner;
        auto result = runner.run(driver, hooks);
        active_ = nullptr;
        opened_ = false;
        return result;
    }
};
} // namespace vui::sdl
