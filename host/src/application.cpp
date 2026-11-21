module vui.host;
import :application;
import :frames.worker;
using namespace rstd::prelude;

namespace vui::host {
auto connect_session(Window &window, UiSession &session) -> Result<empty, gpu::Error> {
    if (session.set_frame_waker(window.frame_waker()).is_err()) {
        return Err(gpu::Error{gpu::Stage::State});
    }
    session.input().set_native_capture(
        [&window](PointerKind kind, bool enabled) { return window.capture(kind, enabled); });
    if (session.input().set_native_text_input_state(
            [&window](Option<TextInputState> state) { return window.set_text_input(rstd::move(state)); }).is_err()) {
        return Err(gpu::Error{gpu::Stage::State});
    }
    if (session.input().set_clipboard(
            []() -> Result<String, ClipboardError> { return Err(ClipboardError::Unsupported); },
            [&window](ref<str> text) { return window.write_clipboard(text); }).is_err() ||
        session.input().set_clipboard_request(
            [&window](Serial id) { return window.request_clipboard(id); }).is_err() ||
        session.input().set_clipboard_write_request(
            [&window](Serial id, Serial activation, ref<str> text) { return window.request_clipboard_write(id, activation, text); }).is_err()) {
        return Err(gpu::Error{gpu::Stage::State});
    }
    return Ok(empty{});
}

auto WindowDriver::wait(Index milliseconds) -> HostEvents {
    auto result = window.wait(milliseconds);
    if (result.is_err()) {
        HostEvents events;
        events.error = Some(result.unwrap_err());
        return events;
    }
    auto events = rstd::move(result).unwrap();
    return {rstd::move(events.input), events.surface,
            window.configured() ? window.pixel_extent() : VkExtent2D{},
            events.redraw, events.close_requested, events.dropped_input, {}, events.minimized, events.suspended};
}

struct Application::Execution {
    ThreadedFrames frames;
    SessionRunner<ThreadedFrames> runner;

    Execution(UiSession &session, Option<FrameWaker> waker)
        : frames(session, true, rstd::move(waker)), runner(session, frames) {}
};

Application::Application() = default;

Application::~Application() {
    (void)session_.close();
    if (execution_.is_some()) {
        (*execution_)->frames.close();
        (*execution_)->frames.join();
    }
    (void)renderer_.shutdown(5'000'000'000);
}

auto Application::open(ApplicationOptions options) -> Result<empty, gpu::Error> {
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
    execution_ = Some(Box<Execution>::make(session_, window_.event_waker()));
    (*execution_)->frames.set_frame_interval(frame_interval_ns_);
    (void)(*execution_)->frames.set_present_mode(presentation_);
    opened_ = true;
    return Ok(empty{});
}

void Application::set_frame_interval(Serial value) {
    if (frame_interval_ns_ == value) return;
    frame_interval_ns_ = value;
    if (execution_.is_some()) (*execution_)->frames.set_frame_interval(value);
    session_.document().bindings().wake();
}

auto Application::set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error> {
    if (finished_ || unsigned(mode) > unsigned(gpu::PresentMode::FifoRelaxed))
        return Err(gpu::Error{gpu::Stage::State});
    if (presentation_ == mode) return Ok(empty{});
    if (execution_.is_some()) {
        auto configured = (*execution_)->frames.set_present_mode(mode);
        if (configured.is_err()) return configured;
    }
    presentation_ = mode;
    request_redraw();
    return Ok(empty{});
}

void Application::request_close() {
    if (running_) (*execution_)->runner.request_close();
    session_.document().bindings().wake();
}

void Application::request_redraw() {
    if (running_) (*execution_)->runner.request_redraw();
    session_.document().bindings().wake();
}

auto Application::request_capture() -> Result<Serial, FrameRequestError> {
    if (execution_.is_none()) return Err(FrameRequestError::Closing);
    return (*execution_)->frames.request_capture();
}

auto Application::request_capture(CaptureSelection selection) -> Result<Serial, FrameRequestError> {
    if (execution_.is_none()) return Err(FrameRequestError::Closing);
    auto requested = (*execution_)->frames.request_capture(selection);
    if (requested.is_ok()) request_redraw();
    return requested;
}

auto Application::snapshot() -> RenderSnapshot {
    return execution_.is_some() ? (*execution_)->frames.snapshot() : RenderSnapshot{};
}

auto Application::run_with_hooks(mut_ref<dyn<RunHooks>> hooks) -> Result<RunStats, RunError> {
    if (!opened_ || running_ || finished_) return Err(RunError{RunStage::State});
    auto &execution = **execution_;
    auto started = execution.frames.start(renderer_);
    if (started.is_err()) return Err(RunError{RunStage::Frame, {}, Some(started.unwrap_err())});
    auto callbacks = callback_hooks(
        [&hooks](const HostEvents &events) { return hooks->before_advance(events); },
        [&hooks](const AdvanceResult &result) { return hooks->after_advance(result); },
        [&hooks](const FrameEvent &event) { return hooks->on_frame_event(event); },
        [&hooks] { return hooks->on_close_requested(); });
    WindowDriver driver{window_};
    running_ = true;
    auto result = execution.runner.run(driver, callbacks);
    running_ = false;
    execution.frames.join();
    opened_ = false;
    finished_ = true;
    return result;
}
} // namespace vui::host
