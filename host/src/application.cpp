module vui.host;
import :application;
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

Application::~Application() {
    (void)session_.close();
    (void)renderer_.shutdown(5'000'000'000);
}

auto Application::open(ApplicationOptions options) -> Result<empty, gpu::Error> {
    if (opened_ || finished_) return Err(gpu::Error{gpu::Stage::State});
    auto opened = window_.open(options.title, options.width, options.height, options.backend);
    if (opened.is_err()) return opened;
    auto initialized = renderer_.initialize(window_, options.validation, options.readback);
    if (initialized.is_err()) {
        (void)renderer_.shutdown(5'000'000'000);
        window_.close();
        return initialized;
    }
    auto connected = connect_session(window_, session_);
    if (connected.is_err()) {
        (void)renderer_.shutdown(5'000'000'000);
        window_.close();
        return connected;
    }
    opened_ = true;
    return Ok(empty{});
}

void Application::set_frame_interval(Serial nanoseconds) {
    if (frame_interval_ns_ == nanoseconds) return;
    frame_interval_ns_ = nanoseconds;
    if (active_) active_->set_frame_interval(nanoseconds);
    session_.document().bindings().wake();
}

auto Application::set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error> {
    if (unsigned(mode) > unsigned(gpu::PresentMode::FifoRelaxed))
        return Err(gpu::Error{gpu::Stage::State});
    if (presentation_ == mode) return Ok(empty{});
    auto result = active_ ? active_->set_present_mode(mode) : Result<empty, gpu::Error>(Ok(empty{}));
    if (result.is_ok()) { presentation_ = mode; session_.document().bindings().wake(); }
    return result;
}
} // namespace vui::host
