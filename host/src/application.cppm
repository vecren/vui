export module vui.host:application;
import :frames;
import :runner;
import :window;
using namespace rstd::prelude;

export namespace vui::host {
struct ApplicationOptions {
    const char *title{"vui"};
    Index width{1280}, height{720};
    bool validation{true}, readback{};
    WindowBackend backend{WindowBackend::Automatic};
};
struct WindowDriver {
    Window &window;
    auto timing() -> FrameTiming { return window.timing(); }
    auto wait(Index milliseconds) -> HostEvents;
};
auto connect_session(Window &window, UiSession &session) -> Result<empty, gpu::Error>;
// UI methods and hooks run on the calling thread; renderer access stays private.
class Application {
    struct Execution;
    Window window_;
    gpu::Renderer renderer_;
    UiSession session_;
    Option<Box<Execution>> execution_;
    Serial frame_interval_ns_{};
    gpu::PresentMode presentation_{gpu::PresentMode::Fifo};
    bool opened_{}, running_{}, finished_{};
    auto run_with_hooks(mut_ref<dyn<RunHooks>> hooks) -> Result<RunStats, RunError>;
  public:
    Application();
    Application(const Application &) = delete;
    ~Application();
    auto window() -> Window & { return window_; }
    auto session() -> UiSession & { return session_; }
    // Producer contexts obtain a shared device before the renderer thread starts.
    auto device_session() const -> Result<vrento::vulkan::DeviceSession, gpu::Error> {
        if (!opened_ || running_) return Err(gpu::Error{gpu::Stage::State});
        return renderer_.device_session();
    }
    auto set_image_cache_limits(gpu::ImageCacheLimits limits) -> Result<empty, gpu::Error> {
        if (!opened_ || running_) return Err(gpu::Error{gpu::Stage::State});
        return renderer_.set_image_cache_limits(limits);
    }
    auto frame_waker() const -> Option<FrameWaker> { return window_.frame_waker(); }
    auto backend() const -> WindowBackend { return window_.backend(); }
    auto resize(Index width, Index height) -> Result<empty, gpu::Error> { return window_.resize(width, height); }
    auto open(ApplicationOptions options = {}) -> Result<empty, gpu::Error>;
    void request_close();
    void request_redraw();
    void set_frame_interval(Serial value);
    auto set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error>;
    auto request_capture() -> Result<Serial, FrameRequestError>;
    auto request_capture(CaptureSelection selection) -> Result<Serial, FrameRequestError>;
    auto snapshot() -> RenderSnapshot;
    template <class Hooks> requires rstd::Impled<Hooks, RunHooks>
    auto run(Hooks &hooks) -> Result<RunStats, RunError> {
        return run_with_hooks(dyn<RunHooks>::from_ref(hooks).as_mut_ref());
    }
};
} // namespace vui::host
