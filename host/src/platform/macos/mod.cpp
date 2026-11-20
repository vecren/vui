module;
#include <rstd/macro.hpp>

module vui.host;
#if RSTD_OS_MACOS
import vvk;
import :platform.macos;
using namespace rstd::prelude;

namespace vui::host {
namespace {
struct WakeState {
    rstd::sync::Mutex<WakeStats> stats;
    bool repaint{};
    auto request(bool draw = true) -> FrameWakeResult {
        auto guard = stats.lock().unwrap();
        if (guard->closed)
            return FrameWakeResult::Closed;
        repaint |= draw;
        if (guard->pending) {
            ++guard->coalesced;
            return FrameWakeResult::Coalesced;
        }
        if (!detail::MacosWindow::post_empty_event()) {
            ++guard->failed;
            return FrameWakeResult::Failed;
        }
        guard->pending = true;
        ++guard->queued;
        return FrameWakeResult::Queued;
    }
    auto take(bool &draw) -> bool {
        auto guard = stats.lock().unwrap();
        if (!guard->pending)
            return false;
        guard->pending = false;
        draw |= rstd::exchange(repaint, false);
        return true;
    }
    void close() {
        auto guard = stats.lock().unwrap();
        guard->closed = true;
        guard->pending = false;
    }
    auto snapshot() -> WakeStats {
        auto guard = stats.lock().unwrap();
        return *guard;
    }
};
struct NativeLease {
    rstd::sync::Arc<detail::MacosWindow> native;
    explicit NativeLease(rstd::sync::Arc<detail::MacosWindow> value) : native(rstd::move(value)) {}
};
auto extension(const char *name) -> vrento::vulkan::Extension {
    return {true, rstd::ffi::CStr::from_ptr(name).to_str().unwrap()};
}
} // namespace

struct Window::Impl {
    Option<rstd::sync::Arc<detail::MacosWindow>> native;
    Option<rstd::sync::Arc<WakeState>> wake;
    Serial observed_revision{};
    bool trace_input{};
};

Window::Window() : impl_(Box<Impl>::make()) {}
Window::~Window() { close(); }

auto Window::open(const char *title, Index width, Index height, WindowBackend backend) -> Result<empty, gpu::Error> {
    if (!detail::MacosWindow::on_main_thread() || impl_->native.is_some() || !title || !width || !height ||
        width > 32768 || height > 32768 || (backend != WindowBackend::Automatic && backend != WindowBackend::Cocoa))
        return Err(gpu::Error{gpu::Stage::State});
    detail::MacosWindow::drain_retired();
    auto native = rstd::sync::Arc<detail::MacosWindow>::make();
    auto state = native.get_mut().unwrap();
    state->tracing(impl_->trace_input);
    if (!state->open(title, width, height))
        return Err(gpu::Error{gpu::Stage::Surface});
    impl_->native = Some(rstd::move(native));
    impl_->wake = Some(rstd::sync::Arc<WakeState>::make());
    impl_->observed_revision = 0;
    return Ok(empty{});
}
void Window::close() {
    if (impl_->wake.is_some())
        (*impl_->wake)->close();
    if (impl_->native.is_some())
        (*impl_->native)->deactivate();
    impl_->wake = None();
    impl_->native = None();
    if (detail::MacosWindow::on_main_thread())
        detail::MacosWindow::drain_retired();
}
auto Window::backend() const -> WindowBackend {
    return impl_->native.is_some() ? WindowBackend::Cocoa : WindowBackend::Automatic;
}
auto Window::configured() const -> bool { return impl_->native.is_some() && (*impl_->native)->alive(); }
auto Window::synchronize() -> Result<empty, gpu::Error> {
    if (!detail::MacosWindow::on_main_thread())
        return Err(gpu::Error{gpu::Stage::State});
    detail::MacosWindow::drain_retired();
    return configured() && (*impl_->native)->synchronize() ? Result<empty, gpu::Error>(Ok(empty{}))
                                                           : Err(gpu::Error{gpu::Stage::State});
}
auto Window::timing() const -> FrameTiming {
    auto now = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
    return {now > ~Serial{} ? ~Serial{} : Serial(now), {}};
}
auto Window::frame_waker() const -> Option<FrameWaker> {
    if (impl_->wake.is_none())
        return None();
    return Some(FrameWaker::make([state = impl_->wake->clone()] { return state->request(); }));
}
auto Window::event_waker() const -> Option<FrameWaker> {
    if (impl_->wake.is_none())
        return None();
    return Some(FrameWaker::make([state = impl_->wake->clone()] { return state->request(false); }));
}
auto Window::wake_stats() const -> WakeStats {
    return impl_->wake.is_some() ? (*impl_->wake)->snapshot() : WakeStats{0, 0, 0, false, true};
}
auto Window::request_frame_signal() -> Result<Serial, FrameSignalError> {
    return Err(configured() ? FrameSignalError::Unsupported : FrameSignalError::Unavailable);
}
auto Window::frame_signal(Serial) const -> Option<bool> { return None(); }
auto Window::cancel_frame_signal(Serial) -> bool { return false; }
void Window::set_input_tracing(bool enabled) {
    impl_->trace_input = enabled;
    if (impl_->native.is_some())
        (*impl_->native)->tracing(enabled);
}
auto Window::capture(PointerKind, bool) -> NativeCaptureResult {
    return configured() ? NativeCaptureResult::Unsupported : NativeCaptureResult::Failed;
}
auto Window::set_text_input(Option<TextInputState> state) -> NativeTextInputResult {
    if (state.is_some() && !state->valid())
        return NativeTextInputResult::Failed;
    if (impl_->native.is_none())
        return state.is_none() ? NativeTextInputResult::Succeeded : NativeTextInputResult::Failed;
    return (*impl_->native)->set_text_input(rstd::move(state));
}
auto Window::request_clipboard(Serial id) -> Result<empty, ClipboardError> {
    if (!configured())
        return Err(ClipboardError::Unavailable);
    return (*impl_->native)->request_clipboard(id);
}
auto Window::request_clipboard_write(Serial id, Serial activation, ref<str> text) -> Result<empty, ClipboardError> {
    if (!configured())
        return Err(ClipboardError::Unavailable);
    return (*impl_->native)->request_clipboard_write(id, activation, text);
}
auto Window::write_clipboard(ref<str> text) -> Result<empty, ClipboardError> {
    if (!configured())
        return Err(ClipboardError::Unavailable);
    return (*impl_->native)->write_clipboard(text);
}
auto Window::resize(Index width, Index height) -> Result<empty, gpu::Error> {
    if (!impl_->native.is_some() || !width || !height || width > 32768 || height > 32768 ||
        !(*impl_->native)->resize(width, height))
        return Err(gpu::Error{gpu::Stage::State});
    return Ok(empty{});
}
auto Window::logical_size() const -> Option<Size> {
    return impl_->native.is_some() ? Some((*impl_->native)->view().logical) : None<Size>();
}
auto Window::set_minimum_size(Index width, Index height) -> Result<empty, gpu::Error> {
    if (impl_->native.is_none() || width > 32768 || height > 32768 ||
        !(*impl_->native)->set_minimum_size(width, height))
        return Err(gpu::Error{gpu::Stage::State});
    return Ok(empty{});
}
auto Window::minimum_size() const -> Option<Size> {
    return impl_->native.is_some() ? Some((*impl_->native)->minimum_size()) : None<Size>();
}
auto Window::position() const -> Option<Point> {
    return impl_->native.is_some() ? (*impl_->native)->position() : None<Point>();
}
auto Window::set_position(Point value) -> WindowPositionResult {
    if (impl_->native.is_none() || !finite(value.x) || !finite(value.y))
        return WindowPositionResult::Failed;
    return (*impl_->native)->set_position(value) ? WindowPositionResult::Requested : WindowPositionResult::Failed;
}
auto Window::mode() const -> Option<WindowMode> {
    if (!configured())
        return None();
    return Some((*impl_->native)->fullscreen() ? WindowMode::Fullscreen : WindowMode::Windowed);
}
auto Window::set_mode(WindowMode mode) -> Result<empty, WindowModeError> {
    if (unsigned(mode) > unsigned(WindowMode::Fullscreen))
        return Err(WindowModeError::InvalidValue);
    if (!configured())
        return Err(WindowModeError::Unavailable);
    return (*impl_->native)->set_fullscreen(mode == WindowMode::Fullscreen)
               ? Result<empty, WindowModeError>(Ok(empty{}))
               : Err(WindowModeError::Failed);
}
auto Window::minimized() const -> Option<bool> {
    return impl_->native.is_some() ? Some(bool((*impl_->native)->view().minimized)) : None<bool>();
}
auto Window::suspended() const -> Option<bool> {
    return impl_->native.is_some() ? Some(bool((*impl_->native)->view().suspended)) : None<bool>();
}
auto Window::minimize() -> Result<empty, WindowActionError> {
    if (!configured())
        return Err(WindowActionError::Unavailable);
    return (*impl_->native)->minimize() ? Result<empty, WindowActionError>(Ok(empty{}))
                                        : Err(WindowActionError::Failed);
}
auto Window::restore() -> Result<empty, WindowActionError> {
    if (!configured())
        return Err(WindowActionError::Unavailable);
    return (*impl_->native)->restore() ? Result<empty, WindowActionError>(Ok(empty{})) : Err(WindowActionError::Failed);
}
auto Window::displays() const -> DisplaySnapshot {
    return configured() ? (*impl_->native)->displays() : DisplaySnapshot{};
}
auto Window::wait(Index milliseconds) -> Result<WindowEvents, gpu::Error> {
    if (!detail::MacosWindow::on_main_thread() || !configured() || impl_->wake.is_none())
        return Err(gpu::Error{gpu::Stage::State});
    auto &native = **impl_->native;
    auto &wake = **impl_->wake;
    detail::MacosWindow::drain_retired();
    bool redraw{};
    bool woke = wake.take(redraw);
    auto before = native.view();
    auto timeout = woke || before.redraw || before.revision != impl_->observed_revision ? Index(0) : milliseconds;
    if (!native.pump(timeout))
        return Err(gpu::Error{gpu::Stage::Surface});
    (void)wake.take(redraw);
    auto state = native.snapshot();
    redraw |= state.redraw || state.revision != impl_->observed_revision;
    impl_->observed_revision = state.revision;
    return Ok(WindowEvents{
        rstd::move(state.input),
        {state.logical, state.scale, state.revision + 1, Some(PixelSize{state.pixel_width, state.pixel_height})},
        redraw,
        state.close_requested,
        state.dropped,
        Some(bool(state.minimized)),
        Some(bool(state.suspended))});
}
auto Window::instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> {
    if (!configured())
        return Err(gpu::Error{gpu::Stage::State});
    Vec<vrento::vulkan::Extension> result;
    result.push(extension(VK_KHR_SURFACE_EXTENSION_NAME));
    result.push(extension(VK_EXT_METAL_SURFACE_EXTENSION_NAME));
    return Ok(rstd::move(result));
}
auto Window::create_surface(const vvk::Instance &instance) -> Result<gpu::NativeSurface, gpu::Error> {
    if (!configured() || !instance)
        return Err(gpu::Error{gpu::Stage::State});
    auto create = instance.Dispatch().vkCreateMetalSurfaceEXT;
    auto layer = (*impl_->native)->metal_layer();
    if (!create || !layer)
        return Err(gpu::Error{gpu::Stage::Surface});
    VkMetalSurfaceCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    info.pLayer = layer;
    VkSurfaceKHR surface{};
    auto result = create(*instance, &info, nullptr, &surface);
    if (result != VK_SUCCESS)
        return Err(gpu::Error{gpu::Stage::Surface, result});
    return Ok(gpu::NativeSurface{surface, Box<dyn<gpu::SurfaceLifetime>>::make(NativeLease(impl_->native->clone()))});
}
auto Window::pixel_extent() const -> VkExtent2D {
    if (impl_->native.is_none())
        return {};
    auto state = (*impl_->native)->view();
    return {state.pixel_width, state.pixel_height};
}
} // namespace vui::host
#endif
