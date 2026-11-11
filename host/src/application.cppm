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
    auto timing() -> FrameTiming {
        auto now = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
        return {now > ~Serial{} ? ~Serial{} : Serial(now), {}};
    }
    auto wait(Index milliseconds) -> HostEvents {
        auto result = window.wait(milliseconds);
        if (result.is_err()) {
            HostEvents events;
            events.error = Some(result.unwrap_err());
            return events;
        }
        auto events = rstd::move(result).unwrap();
        return {rstd::move(events.input), events.surface,
                window.configured() ? window.pixel_extent() : VkExtent2D{},
                events.redraw, events.close_requested, events.dropped_input};
    }
};
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
    ~Application() {
        (void)session_.close();
        (void)renderer_.shutdown(5'000'000'000);
    }
    auto window() -> Window & { return window_; }
    auto renderer() -> gpu::Renderer & { return renderer_; }
    auto session() -> UiSession & { return session_; }
    auto open(ApplicationOptions options = {}) -> Result<empty, gpu::Error> {
        if (opened_ || finished_) return Err(gpu::Error{gpu::Stage::State});
        auto opened = window_.open(options.title, options.width, options.height, options.backend);
        if (opened.is_err()) return opened;
        auto initialized = renderer_.initialize(window_, options.validation, options.readback);
        if (initialized.is_err()) {
            (void)renderer_.shutdown(5'000'000'000);
            window_.close();
            return initialized;
        }
        if (session_.set_frame_waker(window_.frame_waker()).is_err()) {
            (void)renderer_.shutdown(5'000'000'000);
            window_.close();
            return Err(gpu::Error{gpu::Stage::State});
        }
        session_.input().set_native_capture(
            [this](PointerKind kind, bool enabled) { return window_.capture(kind, enabled); });
        if (session_.input().set_native_text_input_state(
                [this](Option<TextInputState> state) { return window_.set_text_input(rstd::move(state)); }).is_err()) {
            (void)renderer_.shutdown(5'000'000'000);
            window_.close();
            return Err(gpu::Error{gpu::Stage::State});
        }
        if (session_.input().set_clipboard(
                []() -> Result<String, ClipboardError> { return Err(ClipboardError::Unsupported); },
                [this](ref<str> text) { return window_.write_clipboard(text); }).is_err() ||
            session_.input().set_clipboard_request(
                [this](Serial id) { return window_.request_clipboard(id); }).is_err() ||
            session_.input().set_clipboard_write_request(
                [this](Serial id, Serial activation, ref<str> text) { return window_.request_clipboard_write(id, activation, text); }).is_err()) {
            (void)renderer_.shutdown(5'000'000'000);
            window_.close();
            return Err(gpu::Error{gpu::Stage::State});
        }
        opened_ = true;
        return Ok(empty{});
    }
    void request_close() {
        if (active_) active_->request_close();
        session_.document().bindings().wake();
    }
    void request_redraw() {
        if (active_) active_->request_redraw();
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
