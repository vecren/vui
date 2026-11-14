export module vui.host:application;
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
class Application {
    Window window_;
    gpu::Renderer renderer_;
    UiSession session_;
    Runner *active_{};
    bool opened_{}, finished_{};
    Serial frame_interval_ns_{};
    gpu::PresentMode presentation_{gpu::PresentMode::Fifo};

  public:
    Application() = default;
    Application(const Application &) = delete;
    auto operator=(const Application &) -> Application & = delete;
    ~Application();
    auto window() -> Window & { return window_; }
    auto renderer() -> gpu::Renderer & { return renderer_; }
    auto session() -> UiSession & { return session_; }
    auto open(ApplicationOptions options = {}) -> Result<empty, gpu::Error>;
    void request_close() {
        if (active_) active_->request_close();
        session_.document().bindings().wake();
    }
    void request_redraw() {
        if (active_) active_->request_redraw();
        session_.document().bindings().wake();
    }
    void set_frame_interval(Serial nanoseconds);
    auto set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error>;
    template <class Hooks> requires rstd::Impled<Hooks, RunHooks>
    auto run(Hooks &hooks) -> Result<RunStats, RunError> {
        if (!opened_ || active_ || finished_) return Err(RunError{RunStage::State});
        Runner runner(session_, renderer_);
        runner.set_frame_interval(frame_interval_ns_);
        auto configured = runner.set_present_mode(presentation_);
        if (configured.is_err()) return Err(RunError{RunStage::Frame, {}, Some(configured.unwrap_err())});
        WindowDriver driver{window_};
        active_ = &runner;
        auto result = runner.run(driver, hooks);
        active_ = nullptr;
        opened_ = false;
        finished_ = true;
        return result;
    }
};
} // namespace vui::host
