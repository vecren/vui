export module vui.host:threaded_application;
import :application;
import :threaded_frames;
using namespace rstd::prelude;

export namespace vui::host {
// UI methods and hooks run on the calling thread; renderer access stays private.
class ThreadedApplication {
    Window window_;
    gpu::Renderer renderer_;
    UiSession session_;
    Option<Box<ThreadedFrames>> frames_;
    SessionRunner<ThreadedFrames> *active_{};
    RenderSnapshot snapshot_;
    bool opened_{}, finished_{};
  public:
    ThreadedApplication() = default;
    ThreadedApplication(const ThreadedApplication &) = delete;
    ~ThreadedApplication() {
        (void)session_.close();
        if (frames_.is_some()) { (*frames_)->close(); (*frames_)->join(); }
        (void)renderer_.shutdown(5'000'000'000);
    }
    auto session() -> UiSession & { return session_; }
    // Producer contexts obtain a shared device before the renderer thread starts.
    auto device_session() const -> Result<vrento::vulkan::DeviceSession, gpu::Error> {
        if (!opened_ || active_) return Err(gpu::Error{gpu::Stage::State});
        return renderer_.device_session();
    }
    auto frame_waker() const -> Option<FrameWaker> { return window_.frame_waker(); }
    auto backend() const -> WindowBackend { return window_.backend(); }
    auto resize(Index width, Index height) -> Result<empty, gpu::Error> { return window_.resize(width, height); }
    auto open(ApplicationOptions options = {}) -> Result<empty, gpu::Error> {
        if (opened_ || finished_) return Err(gpu::Error{gpu::Stage::State});
        auto opened = window_.open(options.title, options.width, options.height, options.backend);
        if (opened.is_err()) return opened;
        auto initialized = renderer_.initialize(window_, options.validation, options.readback);
        if (initialized.is_err()) { (void)renderer_.shutdown(5'000'000'000); window_.close(); return initialized; }
        auto connected = connect_session(window_, session_);
        if (connected.is_err()) {
            (void)renderer_.shutdown(5'000'000'000);
            window_.close();
            return connected;
        }
        frames_ = Some(Box<ThreadedFrames>::make(session_, true, window_.event_waker()));
        opened_ = true;
        return Ok(empty{});
    }
    void request_close() { if (active_) active_->request_close(); session_.document().bindings().wake(); }
    void request_redraw() { if (active_) active_->request_redraw(); session_.document().bindings().wake(); }
    void set_frame_interval(Serial value) { if (frames_.is_some()) (*frames_)->set_frame_interval(value); }
    auto set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error> {
        if (frames_.is_none()) return Err(gpu::Error{gpu::Stage::State});
        auto result = (*frames_)->set_present_mode(mode);
        if (result.is_ok()) request_redraw();
        return result;
    }
    auto request_capture() -> Result<Serial, FrameRequestError> {
        if (frames_.is_none()) return Err(FrameRequestError::Closing);
        return (*frames_)->request_capture();
    }
    auto snapshot() -> RenderSnapshot {
        return frames_.is_some() ? (*frames_)->snapshot() : snapshot_;
    }
    template <class Hooks> requires rstd::Impled<Hooks, RunHooks>
    auto run(Hooks &hooks) -> Result<RunStats, RunError> {
        if (!opened_ || active_ || finished_) return Err(RunError{RunStage::State});
        auto &frames = **frames_;
        auto started = frames.start(renderer_);
        if (started.is_err()) return Err(RunError{RunStage::Frame, {}, Some(started.unwrap_err())});
        SessionRunner<ThreadedFrames> runner(session_, frames);
        WindowDriver driver{window_};
        active_ = &runner;
        auto result = runner.run(driver, hooks);
        active_ = nullptr;
        frames.join();
        snapshot_ = frames.snapshot();
        opened_ = false;
        finished_ = true;
        return result;
    }
};
} // namespace vui::host
