module vui.host;
import :threaded_application;
using namespace rstd::prelude;

namespace vui::host {
ThreadedApplication::~ThreadedApplication() {
    (void)session_.close();
    if (frames_.is_some()) { (*frames_)->close(); (*frames_)->join(); }
    (void)renderer_.shutdown(5'000'000'000);
}

auto ThreadedApplication::open(ApplicationOptions options) -> Result<empty, gpu::Error> {
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

auto ThreadedApplication::set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error> {
    if (frames_.is_none()) return Err(gpu::Error{gpu::Stage::State});
    auto result = (*frames_)->set_present_mode(mode);
    if (result.is_ok()) request_redraw();
    return result;
}
} // namespace vui::host
