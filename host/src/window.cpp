module;
#include <rstd/macro.hpp>

module vui.host;
import vvk;
#if RSTD_OS_WINDOWS
import :platform.win32_window;
import :ffi.win32;
using namespace rstd::prelude;

namespace vui::host {
namespace {
namespace win = ffi::win32;
struct WakeState {
    win::HANDLE event{};
    rstd::sync::Mutex<WakeStats> stats;
    bool repaint{};
    explicit WakeState(win::HANDLE value): event(value) {}
    ~WakeState() { if (event) win::CloseHandle(event); }
    auto request(bool draw = true) -> FrameWakeResult {
        auto guard = stats.lock().unwrap();
        if (guard->closed) return FrameWakeResult::Closed;
        repaint |= draw;
        if (guard->pending) { ++guard->coalesced; return FrameWakeResult::Coalesced; }
        if (!win::SetEvent(event)) { ++guard->failed; return FrameWakeResult::Failed; }
        guard->pending = true; ++guard->queued; return FrameWakeResult::Queued;
    }
    auto take(bool &draw) -> bool {
        auto guard = stats.lock().unwrap();
        if (!guard->pending) return false;
        if (!win::ResetEvent(event)) return false;
        guard->pending = false; draw |= rstd::exchange(repaint, false); return true;
    }
    void close() { auto guard = stats.lock().unwrap(); guard->closed = true; guard->pending = false; }
    auto snapshot() -> WakeStats { auto guard = stats.lock().unwrap(); return *guard; }
};
template <class T> struct NativeLease {
    rstd::sync::Arc<T> native;
    explicit NativeLease(rstd::sync::Arc<T> value): native(rstd::move(value)) {}
};
auto extension(const char *name) -> vrento::vulkan::Extension {
    return {true, rstd::ffi::CStr::from_ptr(name).to_str().unwrap()};
}
}

struct Window::Impl {
    Option<rstd::sync::Arc<detail::Win32Window>> native;
    Option<rstd::sync::Arc<WakeState>> wake;
    Serial observed_revision{};
    bool trace_input{};
};

Window::Window(): impl_(Box<Impl>::make()) {}
Window::~Window() { close(); }

auto Window::open(const char *title, Index width, Index height, WindowBackend backend)
    -> Result<empty, gpu::Error> {
    if (impl_->native.is_some() || !title || !width || !height || width > 32768 || height > 32768 ||
        (backend != WindowBackend::Automatic && backend != WindowBackend::Win32))
        return Err(gpu::Error{gpu::Stage::State});
    auto event = win::CreateEventW(nullptr, true, false, nullptr);
    if (!event) return Err(gpu::Error{gpu::Stage::Surface});
    auto wake = rstd::sync::Arc<WakeState>::make(event);
    auto native = rstd::sync::Arc<detail::Win32Window>::make();
    auto mutable_native = native.get_mut().unwrap();
    mutable_native->tracing(impl_->trace_input);
    if (!mutable_native->open(title, width, height)) return Err(gpu::Error{gpu::Stage::Surface});
    impl_->native = Some(rstd::move(native));
    impl_->wake = Some(rstd::move(wake)); impl_->observed_revision = 0;
    return Ok(empty{});
}
void Window::close() {
    if (impl_->wake.is_some()) (*impl_->wake)->close();
    if (impl_->native.is_some()) (void)(*impl_->native)->deactivate();
    impl_->wake = None(); impl_->native = None();
}
auto Window::backend() const -> WindowBackend {
    return impl_->native.is_some() ? WindowBackend::Win32 : WindowBackend::Automatic;
}
auto Window::configured() const -> bool {
    return impl_->native.is_some() && (*impl_->native)->alive() && (*impl_->native)->handle();
}
auto Window::synchronize() -> Result<empty, gpu::Error> {
    return configured() && (*impl_->native)->synchronize() ? Result<empty, gpu::Error>(Ok(empty{}))
                                                            : Err(gpu::Error{gpu::Stage::State});
}
auto Window::timing() const -> FrameTiming {
    auto now = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
    return {now > ~Serial{} ? ~Serial{} : Serial(now), {}};
}
auto Window::frame_waker() const -> Option<FrameWaker> {
    if (impl_->wake.is_none()) return None();
    return Some(FrameWaker::make([state = impl_->wake->clone()] { return state->request(); }));
}
auto Window::event_waker() const -> Option<FrameWaker> {
    if (impl_->wake.is_none()) return None();
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
    if (impl_->native.is_some()) (*impl_->native)->tracing(enabled);
}
auto Window::capture(PointerKind kind, bool enabled) -> NativeCaptureResult {
    if (kind != PointerKind::Mouse) return NativeCaptureResult::Unsupported;
    if (impl_->native.is_none()) return NativeCaptureResult::Failed;
    return (*impl_->native)->capture(enabled) ? NativeCaptureResult::Succeeded : NativeCaptureResult::Failed;
}
auto Window::set_text_input(Option<TextInputState> value) -> NativeTextInputResult {
    if (value.is_some() && !value->valid()) return NativeTextInputResult::Failed;
    if (impl_->native.is_none()) return value.is_none() ? NativeTextInputResult::Succeeded : NativeTextInputResult::Failed;
    return (*impl_->native)->set_text_input(rstd::move(value));
}
auto Window::request_clipboard(Serial) -> Result<empty, ClipboardError> {
    return Err(configured() ? ClipboardError::Unsupported : ClipboardError::Unavailable);
}
auto Window::request_clipboard_write(Serial, Serial, ref<str>) -> Result<empty, ClipboardError> {
    return Err(configured() ? ClipboardError::Unsupported : ClipboardError::Unavailable);
}
auto Window::write_clipboard(ref<str>) -> Result<empty, ClipboardError> {
    return Err(configured() ? ClipboardError::Unsupported : ClipboardError::Unavailable);
}
auto Window::resize(Index width, Index height) -> Result<empty, gpu::Error> {
    if (impl_->native.is_none() || !width || !height || width > 32768 || height > 32768 ||
        !(*impl_->native)->resize(int(width), int(height))) return Err(gpu::Error{gpu::Stage::State});
    return Ok(empty{});
}
auto Window::logical_size() const -> Option<Size> {
    if (impl_->native.is_none()) return None();
    auto value = (*impl_->native)->view(); auto scale = Scalar(96) / Scalar(value.dpi);
    return Some(Size{Scalar(value.width) * scale, Scalar(value.height) * scale});
}
auto Window::set_minimum_size(Index width, Index height) -> Result<empty, gpu::Error> {
    if (impl_->native.is_none() || width > 32768 || height > 32768 ||
        !(*impl_->native)->set_minimum_size(int(width), int(height))) return Err(gpu::Error{gpu::Stage::State});
    return Ok(empty{});
}
auto Window::minimum_size() const -> Option<Size> {
    return impl_->native.is_some() ? Some((*impl_->native)->minimum_size()) : None<Size>();
}
auto Window::position() const -> Option<Point> { return None(); }
auto Window::set_position(Point) -> WindowPositionResult {
    return configured() ? WindowPositionResult::Unsupported : WindowPositionResult::Failed;
}
auto Window::mode() const -> Option<WindowMode> {
    if (!configured()) return None();
    return Some((*impl_->native)->fullscreen() ? WindowMode::Fullscreen : WindowMode::Windowed);
}
auto Window::set_mode(WindowMode mode) -> Result<empty, WindowModeError> {
    if (unsigned(mode) > unsigned(WindowMode::Fullscreen)) return Err(WindowModeError::InvalidValue);
    if (!configured()) return Err(WindowModeError::Unavailable);
    return (*impl_->native)->set_fullscreen(mode == WindowMode::Fullscreen)
        ? Result<empty, WindowModeError>(Ok(empty{})) : Err(WindowModeError::Failed);
}
auto Window::minimized() const -> Option<bool> {
    return impl_->native.is_some() ? Some(bool((*impl_->native)->view().minimized)) : None<bool>();
}
auto Window::suspended() const -> Option<bool> { return minimized(); }
auto Window::minimize() -> Result<empty, WindowActionError> {
    if (impl_->native.is_none()) return Err(WindowActionError::Unavailable);
    return (*impl_->native)->minimize() ? Result<empty, WindowActionError>(Ok(empty{})) : Err(WindowActionError::Failed);
}
auto Window::restore() -> Result<empty, WindowActionError> {
    if (impl_->native.is_none()) return Err(WindowActionError::Unavailable);
    return (*impl_->native)->restore() ? Result<empty, WindowActionError>(Ok(empty{})) : Err(WindowActionError::Failed);
}
auto Window::displays() const -> DisplaySnapshot {
    return {configured() ? DisplayStatus::Unsupported : DisplayStatus::Unavailable};
}
auto Window::wait(Index milliseconds) -> Result<WindowEvents, gpu::Error> {
    if (impl_->native.is_none() || impl_->wake.is_none()) return Err(gpu::Error{gpu::Stage::State});
    auto &native = **impl_->native; auto &wake = **impl_->wake;
    if (!native.alive()) return Err(gpu::Error{gpu::Stage::Surface});
    bool redraw{};
    bool woke = wake.take(redraw);
    auto before = native.view();
    if (!woke && !before.redraw && before.revision == impl_->observed_revision) {
        win::HANDLE events[]{native.changed_handle(), wake.event, native.thread_handle()};
        auto timeout = milliseconds > 0xfffffffeu ? 0xfffffffeu : milliseconds;
        auto waited = win::WaitForMultipleObjects(3, events, false, timeout);
        if (waited == win::wait_failed || waited == win::wait_object + 2)
            return Err(gpu::Error{gpu::Stage::Surface});
    }
    (void)wake.take(redraw);
    auto value = native.snapshot();
    redraw = redraw || value.redraw || value.revision != impl_->observed_revision;
    impl_->observed_revision = value.revision;
    auto scale = Scalar(value.dpi) / Scalar(96);
    Size logical{Scalar(value.width) / scale, Scalar(value.height) / scale};
    return Ok(WindowEvents{rstd::move(value.input),
        {logical, scale, value.revision + 1, Some(PixelSize{value.width, value.height})},
        redraw, value.close_requested, value.dropped, Some(bool(value.minimized)), Some(bool(value.minimized))});
}
auto Window::instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> {
    if (!configured()) return Err(gpu::Error{gpu::Stage::State});
    Vec<vrento::vulkan::Extension> result;
    result.push(extension(VK_KHR_SURFACE_EXTENSION_NAME));
    result.push(extension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME));
    return Ok(rstd::move(result));
}
auto Window::create_surface(const vvk::Instance &instance) -> Result<gpu::NativeSurface, gpu::Error> {
    if (!instance || impl_->native.is_none()) return Err(gpu::Error{gpu::Stage::State});
    auto create = instance.Dispatch().vkCreateWin32SurfaceKHR;
    if (!create) return Err(gpu::Error{gpu::Stage::Surface});
    auto &native = **impl_->native;
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = native.instance(); info.hwnd = native.handle();
    VkSurfaceKHR surface{};
    auto result = create(*instance, &info, nullptr, &surface);
    if (result != VK_SUCCESS) return Err(gpu::Error{gpu::Stage::Surface, result});
    return Ok(gpu::NativeSurface{surface, Box<dyn<gpu::SurfaceLifetime>>::make(
        NativeLease<detail::Win32Window>(impl_->native->clone()))});
}
auto Window::pixel_extent() const -> VkExtent2D {
    if (impl_->native.is_none()) return {};
    auto value = (*impl_->native)->view(); return {value.width, value.height};
}
}

#else
import :platform.wayland_window;
import :platform.x11_window;
import :platform.x11;
import :ffi.linux;
import :ffi.wayland;
import :ffi.x11;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui::host {
namespace {
struct WakeState {
    int fd;
    rstd::sync::Mutex<WakeStats> stats;
    bool repaint{};
    explicit WakeState(int value) : fd(value) {}
    ~WakeState() { ffi::linux::close(fd); }
    auto request(bool draw = true) -> FrameWakeResult {
        auto guard = stats.lock().unwrap();
        auto &state = *guard;
        if (state.closed) return FrameWakeResult::Closed;
        repaint |= draw;
        if (state.pending) {
            ++state.coalesced;
            return FrameWakeResult::Coalesced;
        }
        rstd::uint64_t value = 1;
        long written;
        do { written = ffi::linux::write(fd, &value, sizeof(value)); }
        while (written < 0 && ffi::linux::error_number() == ffi::linux::interrupted);
        if (written != sizeof(value)) {
            ++state.failed;
            return FrameWakeResult::Failed;
        }
        state.pending = true;
        ++state.queued;
        return FrameWakeResult::Queued;
    }
    auto take(bool &draw) -> bool {
        auto guard = stats.lock().unwrap();
        auto &state = *guard;
        if (!state.pending) return false;
        rstd::uint64_t value{};
        long count;
        do { count = ffi::linux::read(fd, &value, sizeof(value)); }
        while (count < 0 && ffi::linux::error_number() == ffi::linux::interrupted);
        if (count != sizeof(value)) return false;
        state.pending = false;
        draw |= rstd::exchange(repaint, false);
        return true;
    }
    void close() {
        auto guard = stats.lock().unwrap();
        guard->closed = true;
        guard->pending = false;
    }
    auto snapshot() -> WakeStats { auto guard = stats.lock().unwrap(); return *guard; }
};
template <class T> struct NativeLease {
    rstd::sync::Arc<T> native;
    explicit NativeLease(rstd::sync::Arc<T> value) : native(rstd::move(value)) {}
};
auto extension(const char *name) -> vrento::vulkan::Extension {
    return {true, rstd::ffi::CStr::from_ptr(name).to_str().unwrap()};
}
} // namespace

struct Window::Impl {
    WindowBackend selected{WindowBackend::Automatic};
    Option<rstd::sync::Arc<detail::WaylandWindow>> wayland;
    Option<rstd::sync::Arc<detail::X11Window>> x11;
    Option<rstd::sync::Arc<WakeState>> wake;
    Vec<ffi::linux::pollfd> pollfds;
    Vec<detail::X11InternalConnection> x11_internal;
    Serial observed_revision{};
    Serial observed_display_revision{};
    bool trace_input{};
};

Window::Window() : impl_(Box<Impl>::make()) {}
Window::~Window() { close(); }

auto Window::open(const char *title, Index width, Index height, WindowBackend backend)
    -> Result<empty, gpu::Error> {
    if (impl_->wayland.is_some() || impl_->x11.is_some() || !title || !width || !height ||
        width > 32768 || height > 32768 || unsigned(backend) > unsigned(WindowBackend::X11))
        return Err(gpu::Error{gpu::Stage::State});
    if (backend == WindowBackend::Automatic) {
        auto session = rstd::env::var("XDG_SESSION_TYPE"_str);
        if (session.is_ok()) {
            if (session->as_str() == "wayland"_str && rstd::env::var_os("WAYLAND_DISPLAY"_str).is_some())
                backend = WindowBackend::Wayland;
            else if (session->as_str() == "x11"_str && rstd::env::var_os("DISPLAY"_str).is_some())
                backend = WindowBackend::X11;
        }
    }
    auto wake_fd = ffi::linux::eventfd(0, ffi::linux::event_flags);
    if (wake_fd < 0) return Err(gpu::Error{gpu::Stage::Surface});
    auto wake = rstd::sync::Arc<WakeState>::make(wake_fd);
    if (backend == WindowBackend::Automatic || backend == WindowBackend::Wayland) {
        auto window = rstd::sync::Arc<detail::WaylandWindow>::make();
        window.get_mut().unwrap()->input().set_input_tracing(impl_->trace_input);
        auto opened = window.get_mut().unwrap()->open(title, width, height);
        if (opened.is_ok()) {
            window.get_mut().unwrap()->input().clipboard().set_waker(
                FrameWaker::make([state = wake.clone()] { return state->request(); }));
            impl_->wayland = Some(rstd::move(window));
            impl_->selected = WindowBackend::Wayland;
            impl_->wake = Some(rstd::move(wake));
            impl_->observed_revision = 0;
            impl_->observed_display_revision = 0;
            return Ok(empty{});
        }
        if (backend == WindowBackend::Wayland)
            return Err(gpu::Error{gpu::Stage::Surface});
    }
    auto window = rstd::sync::Arc<detail::X11Window>::make();
    window.get_mut().unwrap()->set_input_tracing(impl_->trace_input);
    auto opened = window.get_mut().unwrap()->open(title, width, height);
    if (opened.is_err())
        return Err(gpu::Error{gpu::Stage::Surface});
    impl_->x11 = Some(rstd::move(window));
    impl_->selected = WindowBackend::X11;
    impl_->wake = Some(rstd::move(wake));
    impl_->observed_revision = 0;
    impl_->observed_display_revision = 0;
    return Ok(empty{});
}
void Window::close() {
    if (impl_->wake.is_some()) (*impl_->wake)->close();
    impl_->wake = None();
    if (impl_->x11.is_some()) {
        (void)(*impl_->x11)->capture(false);
        (void)(*impl_->x11)->set_text_input(None());
        (*impl_->x11)->close_clipboard();
        (*impl_->x11)->close_state();
        (*impl_->x11)->close_displays();
    }
    if (impl_->wayland.is_some()) {
        (*impl_->wayland)->clear_frame_signal();
        (void)(*impl_->wayland)->input().set_text_input(None());
        (*impl_->wayland)->input().clipboard().close();
    }
    impl_->wayland = None();
    impl_->x11 = None();
    impl_->selected = WindowBackend::Automatic;
}
auto Window::backend() const -> WindowBackend { return impl_->selected; }
auto Window::timing() const -> FrameTiming {
    auto now = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
    return {now > ~Serial{} ? ~Serial{} : Serial(now), {}};
}
auto Window::configured() const -> bool {
    if (impl_->wayland.is_some()) return (*impl_->wayland)->configured();
    return impl_->x11.is_some() && (*impl_->x11)->window() != 0;
}
auto Window::synchronize() -> Result<empty, gpu::Error> {
    if (impl_->wayland.is_some()) {
        auto native = impl_->wayland->as_ptr().as_raw_ptr();
        if (native->roundtrip().is_ok())
            return Ok(empty{});
    } else if (impl_->x11.is_some()) {
        auto native = impl_->x11->as_ptr().as_raw_ptr();
        if (native->synchronize().is_ok())
            return Ok(empty{});
    }
    return Err(gpu::Error{gpu::Stage::Surface});
}
auto Window::frame_waker() const -> Option<FrameWaker> {
    if (impl_->wake.is_none()) return None();
    return Some(FrameWaker::make([state = impl_->wake->clone()] { return state->request(); }));
}
auto Window::event_waker() const -> Option<FrameWaker> {
    if (impl_->wake.is_none()) return None();
    return Some(FrameWaker::make([state = impl_->wake->clone()] { return state->request(false); }));
}
auto Window::wake_stats() const -> WakeStats {
    return impl_->wake.is_some() ? (*impl_->wake)->snapshot() : WakeStats{0, 0, 0, false, true};
}
auto Window::request_frame_signal() -> Result<Serial,FrameSignalError> {
    if (impl_->wayland.is_some()) {
        auto &window=**impl_->wayland;
        if (window.frame_signal_pending()) return Err(FrameSignalError::Busy);
        auto result=window.request_frame_signal();
        if (result.is_err()) return Err(FrameSignalError::Failed);
        return Ok(result.unwrap());
    }
    return Err(impl_->x11.is_some() ? FrameSignalError::Unsupported : FrameSignalError::Unavailable);
}
auto Window::frame_signal(Serial token) const -> Option<bool> {
    return impl_->wayland.is_some() ? (*impl_->wayland)->frame_signal(token) : None<bool>();
}
auto Window::cancel_frame_signal(Serial token) -> bool {
    return impl_->wayland.is_some() && (*impl_->wayland)->cancel_frame_signal(token);
}
void Window::set_input_tracing(bool enabled) {
    impl_->trace_input = enabled;
    if (impl_->wayland.is_some()) (*impl_->wayland)->input().set_input_tracing(enabled);
    if (impl_->x11.is_some()) (*impl_->x11)->set_input_tracing(enabled);
}
auto Window::capture(PointerKind kind, bool enabled) -> NativeCaptureResult {
    if (kind != PointerKind::Mouse) return NativeCaptureResult::Unsupported;
    if (impl_->x11.is_some()) return (*impl_->x11)->capture(enabled);
    if (impl_->wayland.is_some()) return (*impl_->wayland)->input().capture(enabled);
    return NativeCaptureResult::Failed;
}
auto Window::set_text_input(Option<TextInputState> state) -> NativeTextInputResult {
    if (impl_->wayland.is_some()) return (*impl_->wayland)->input().set_text_input(rstd::move(state));
    if (impl_->x11.is_some()) return (*impl_->x11)->set_text_input(rstd::move(state));
    return state.is_some() ? NativeTextInputResult::Failed : NativeTextInputResult::Succeeded;
}
auto Window::request_clipboard(Serial id) -> Result<empty, ClipboardError> {
    if (impl_->x11.is_some()) return (*impl_->x11)->request_clipboard(id);
    if (impl_->wayland.is_some()) return (*impl_->wayland)->input().request_clipboard(id);
    return Err(ClipboardError::Unavailable);
}
auto Window::request_clipboard_write(Serial id, Serial activation, ref<str> text) -> Result<empty, ClipboardError> {
    if (impl_->wayland.is_some()) return (*impl_->wayland)->input().request_clipboard_write(id, activation, text);
    if (impl_->x11.is_some()) return Err(ClipboardError::Unsupported);
    return Err(ClipboardError::Unavailable);
}
auto Window::write_clipboard(ref<str> text) -> Result<empty, ClipboardError> {
    if (impl_->x11.is_some()) return (*impl_->x11)->write_clipboard(text);
    if (impl_->wayland.is_some()) return Err(ClipboardError::Unsupported);
    return Err(ClipboardError::Unavailable);
}
auto Window::resize(Index width, Index height) -> Result<empty, gpu::Error> {
    if (!width || !height || width > 32768 || height > 32768) return Err(gpu::Error{gpu::Stage::State});
    if (impl_->wayland.is_some()) {
        if ((*impl_->wayland)->resize(width, height).is_err()) return Err(gpu::Error{gpu::Stage::State});
        if (impl_->wake.is_some()) (void)(*impl_->wake)->request();
        return Ok(empty{});
    }
    if (impl_->x11.is_some() && (*impl_->x11)->resize(width, height).is_ok()) return Ok(empty{});
    return Err(gpu::Error{gpu::Stage::State});
}
auto Window::logical_size() const -> Option<Size> {
    if (impl_->wayland.is_some() && (*impl_->wayland)->surface())
        return Some(Size{Scalar((*impl_->wayland)->width()), Scalar((*impl_->wayland)->height())});
    if (impl_->x11.is_some() && (*impl_->x11)->window())
        return Some(Size{Scalar((*impl_->x11)->width()), Scalar((*impl_->x11)->height())});
    return None();
}
auto Window::set_minimum_size(Index width, Index height) -> Result<empty, gpu::Error> {
    if (width > 32768 || height > 32768) return Err(gpu::Error{gpu::Stage::State});
    if (impl_->wayland.is_some()) {
        if ((*impl_->wayland)->set_minimum_size(width, height).is_err()) return Err(gpu::Error{gpu::Stage::State});
        if (impl_->wake.is_some()) (void)(*impl_->wake)->request();
        return Ok(empty{});
    }
    if (impl_->x11.is_some() && (*impl_->x11)->set_minimum_size(width, height).is_ok()) return Ok(empty{});
    return Err(gpu::Error{gpu::Stage::State});
}
auto Window::minimum_size() const -> Option<Size> {
    if (impl_->wayland.is_some() && (*impl_->wayland)->surface()) return Some((*impl_->wayland)->minimum_size());
    if (impl_->x11.is_some() && (*impl_->x11)->window()) return Some((*impl_->x11)->minimum_size());
    return None();
}
auto Window::position() const -> Option<Point> {
    if (impl_->x11.is_some()) return (*impl_->x11)->position();
    return None();
}
auto Window::set_position(Point position) -> WindowPositionResult {
    if (!finite(position.x) || !finite(position.y) || position.x < -32768 || position.x > 32767 ||
        position.y < -32768 || position.y > 32767 || int(position.x) != position.x ||
        int(position.y) != position.y) return WindowPositionResult::Failed;
    if (impl_->wayland.is_some() && (*impl_->wayland)->surface()) return WindowPositionResult::Unsupported;
    if (impl_->x11.is_some() && (*impl_->x11)->set_position(int(position.x), int(position.y)).is_ok())
        return WindowPositionResult::Requested;
    return WindowPositionResult::Failed;
}
auto Window::mode() const -> Option<WindowMode> {
    Option<bool> value;
    if (impl_->wayland.is_some()) value = (*impl_->wayland)->fullscreen();
    else if (impl_->x11.is_some()) value = (*impl_->x11)->fullscreen();
    if (value.is_none()) return None();
    return Some(*value ? WindowMode::Fullscreen : WindowMode::Windowed);
}
auto Window::minimized() const -> Option<bool> {
    return impl_->x11.is_some() ? (*impl_->x11)->minimized() : None<bool>();
}
auto Window::suspended() const -> Option<bool> {
    if (impl_->wayland.is_some()) return (*impl_->wayland)->suspended();
    return minimized();
}
auto Window::minimize() -> Result<empty, WindowActionError> {
    if (impl_->wayland.is_some()) {
        auto capability = (*impl_->wayland)->minimize_capability();
        if (capability.is_none()) return Err(WindowActionError::Unavailable);
        if (!*capability) return Err(WindowActionError::Unsupported);
        if (!(*impl_->wayland)->minimize()) return Err(WindowActionError::Failed);
        if (impl_->wake.is_some()) (void)(*impl_->wake)->request();
        return Ok(empty{});
    }
    if (impl_->x11.is_some())
        return (*impl_->x11)->minimize() ? Result<empty, WindowActionError>(Ok(empty{})) : Err(WindowActionError::Failed);
    return Err(WindowActionError::Unavailable);
}
auto Window::restore() -> Result<empty, WindowActionError> {
    if (impl_->wayland.is_some()) return Err(WindowActionError::Unsupported);
    if (impl_->x11.is_some())
        return (*impl_->x11)->restore() ? Result<empty, WindowActionError>(Ok(empty{})) : Err(WindowActionError::Failed);
    return Err(WindowActionError::Unavailable);
}
auto Window::displays() const -> DisplaySnapshot {
    if (impl_->wayland.is_some() && (*impl_->wayland)->surface()) return (*impl_->wayland)->displays();
    if (impl_->x11.is_some() && (*impl_->x11)->window()) return (*impl_->x11)->displays();
    return {};
}
auto Window::set_mode(WindowMode mode) -> Result<empty, WindowModeError> {
    if (unsigned(mode) > unsigned(WindowMode::Fullscreen)) return Err(WindowModeError::InvalidValue);
    auto enabled = mode == WindowMode::Fullscreen;
    if (impl_->wayland.is_some()) {
        if ((*impl_->wayland)->set_fullscreen(enabled).is_err()) return Err(WindowModeError::Unavailable);
        if (impl_->wake.is_some()) (void)(*impl_->wake)->request();
        return Ok(empty{});
    }
    if (impl_->x11.is_some()) {
        auto supported = (*impl_->x11)->fullscreen_supported();
        if (supported.is_none()) return Err(WindowModeError::Unavailable);
        if (!*supported) return Err(WindowModeError::Unsupported);
        if (!(*impl_->x11)->set_fullscreen(enabled)) return Err(WindowModeError::Failed);
        return Ok(empty{});
    }
    return Err(WindowModeError::Unavailable);
}
auto Window::wait(Index milliseconds) -> Result<WindowEvents, gpu::Error> {
    if (impl_->wake.is_none()) return Err(gpu::Error{gpu::Stage::State});
    auto wake = impl_->wake->as_ptr().as_raw_ptr();
    bool redraw{};
    bool woke = wake->take(redraw);
    bool prepared{};
    Index timeout_ms = woke ? 0 : milliseconds;
    Option<usize> clipboard_owner_fd;
    Option<usize> state_fd;
    Option<usize> displays_fd;
    ffi::wayland::wl_display *wayland{};
    auto &fds = impl_->pollfds;
    fds.clear();
    fds.push(ffi::linux::pollfd{});
    fds.push(ffi::linux::pollfd{wake->fd, ffi::linux::input, 0});
    impl_->x11_internal.clear();
    if (impl_->wayland.is_some()) {
        auto native = impl_->wayland->as_ptr().as_raw_ptr();
        wayland = native->display();
        redraw = redraw || native->input().has_input();
        while (ffi::wayland::wl_display_prepare_read(wayland) != 0) {
            auto dispatched = ffi::wayland::wl_display_dispatch_pending(wayland);
            if (dispatched < 0) return Err(gpu::Error{gpu::Stage::Surface});
            redraw = redraw || dispatched > 0;
        }
        native->input().pump_repeat();
        native->input().clipboard().drain();
        redraw = redraw || native->input().has_input();
        redraw = redraw || native->display_revision() != impl_->observed_display_revision;
        timeout_ms = native->input().repeat_wait(timeout_ms);
        timeout_ms = native->input().clipboard().wait(timeout_ms);
        prepared = true;
        fds[usize(0)] = {ffi::wayland::wl_display_get_fd(wayland), ffi::linux::input, 0};
        if (ffi::wayland::wl_display_flush(wayland) < 0) {
            if (ffi::linux::error_number() != ffi::linux::again) {
                ffi::wayland::wl_display_cancel_read(wayland);
                return Err(gpu::Error{gpu::Stage::Surface});
            }
            fds[usize(0)].events |= ffi::linux::output;
        }
    } else if (impl_->x11.is_some()) {
        auto native = impl_->x11->as_ptr().as_raw_ptr();
        auto display = native->display();
        native->dispatch_displays();
        if (native->displays_failed()) { native->close_displays(); redraw = true; }
        redraw = redraw || native->displays_pending() || native->display_revision() != impl_->observed_display_revision;
        redraw = native->dispatch_state() || redraw;
        if (native->state_failed()) { native->close_state(); redraw = true; }
        redraw = redraw || native->state_pending();
        native->dispatch_clipboard_owner();
        redraw = redraw || native->clipboard_owner_pending();
        redraw = redraw || ffi::x11::XPending(display) > 0;
        if (native->dispatch(true).is_err()) return Err(gpu::Error{gpu::Stage::Surface});
        native->expire_clipboard();
        timeout_ms = native->clipboard_wait(timeout_ms);
        redraw = redraw || native->has_input();
        fds[usize(0)] = {ffi::x11::XConnectionNumber(display), ffi::linux::input, 0};
        ffi::x11::XFlush(display);
        native->copy_internal_connections(impl_->x11_internal);
        for (auto item : impl_->x11_internal)
            fds.push(ffi::linux::pollfd{item.fd, ffi::linux::input, 0});
        if (native->clipboard_owner_fd() >= 0) {
            clipboard_owner_fd = Some(usize(fds.len()));
            fds.push(ffi::linux::pollfd{native->clipboard_owner_fd(), ffi::linux::input, 0});
        }
        if (native->state_fd() >= 0) {
            state_fd = Some(usize(fds.len()));
            fds.push(ffi::linux::pollfd{native->state_fd(), ffi::linux::input, 0});
        }
        if (native->displays_fd() >= 0) {
            displays_fd = Some(usize(fds.len()));
            fds.push(ffi::linux::pollfd{native->displays_fd(), ffi::linux::input, 0});
        }
    } else return Err(gpu::Error{gpu::Stage::State});

    auto started = rstd::time::Instant::now();
    int result;
    for (;;) {
        auto elapsed = started.elapsed().as_millis().to_primitive();
        auto remaining = elapsed >= timeout_ms ? 0 : timeout_ms - elapsed;
        int timeout = redraw ? 0 : int(remaining > 2147483647 ? 2147483647 : remaining);
        result = ffi::linux::poll(fds.as_mut_slice().as_raw_ptr(), fds.len().to_primitive(), timeout);
        if (result >= 0 || ffi::linux::error_number() != ffi::linux::interrupted) break;
    }
    if (result < 0 || (fds[usize(0)].revents & ffi::linux::error) ||
        (fds[usize(1)].revents & ffi::linux::error)) {
        if (prepared) ffi::wayland::wl_display_cancel_read(wayland);
        return Err(gpu::Error{gpu::Stage::Surface});
    }
    if (prepared) {
        if (fds[usize(0)].revents & ffi::linux::input) {
            if (ffi::wayland::wl_display_read_events(wayland) < 0)
                return Err(gpu::Error{gpu::Stage::Surface});
            auto dispatched = ffi::wayland::wl_display_dispatch_pending(wayland);
            if (dispatched < 0) return Err(gpu::Error{gpu::Stage::Surface});
            redraw = redraw || dispatched > 0;
        } else ffi::wayland::wl_display_cancel_read(wayland);
        auto native = impl_->wayland->as_ptr().as_raw_ptr();
        native->input().clipboard().drain();
    } else {
        auto native = impl_->x11->as_ptr().as_raw_ptr();
        if (displays_fd.is_some() && fds[*displays_fd].revents) {
            native->dispatch_displays();
            if ((fds[*displays_fd].revents & ffi::linux::error) || native->displays_failed()) {
                native->close_displays(); redraw = true;
            }
        }
        if (state_fd.is_some() && fds[*state_fd].revents) {
            redraw = native->dispatch_state() || redraw;
            if ((fds[*state_fd].revents & ffi::linux::error) || native->state_failed()) {
                native->close_state(); redraw = true;
            }
        }
        if (clipboard_owner_fd.is_some() && fds[*clipboard_owner_fd].revents) {
            native->dispatch_clipboard_owner();
            if ((fds[*clipboard_owner_fd].revents & ffi::linux::error) || native->clipboard_owner_failed())
                native->disable_clipboard_owner();
        }
        for (usize i{}; i < impl_->x11_internal.len(); i += usize(1)) {
            auto entry = impl_->x11_internal[i];
            auto ready = fds[i + usize(2)].revents;
            if (!ready || !native->internal_connection_current(entry)) continue;
            if (ready & ffi::linux::invalid) return Err(gpu::Error{gpu::Stage::Surface});
            if (ready & (ffi::linux::input | ffi::linux::disconnected)) {
                native->process_internal_connection(entry);
                redraw = true;
                if ((ready & ffi::linux::disconnected) && native->internal_connection_current(entry))
                    return Err(gpu::Error{gpu::Stage::Surface});
            }
        }
        if (native->dispatch(true).is_err()) return Err(gpu::Error{gpu::Stage::Surface});
        native->expire_clipboard();
    }
    (void)wake->take(redraw);
    Serial revision{}, dropped{};
    Vec<InputEvent> input;
    bool close_requested{};
    if (impl_->wayland.is_some()) {
        auto native = impl_->wayland->as_ptr().as_raw_ptr();
        if (native->failed()) return Err(gpu::Error{gpu::Stage::Surface});
        native->input().pump_repeat();
        revision = native->revision(); close_requested = native->close_requested();
        input = native->input().take_input();
        dropped = native->input().take_dropped_input();
    } else {
        auto native = impl_->x11->as_ptr().as_raw_ptr();
        revision = native->revision(); close_requested = native->close_requested();
        input = native->take_input();
        dropped = native->take_dropped_input();
    }
    redraw = redraw || revision != impl_->observed_revision;
    impl_->observed_revision = revision;
    auto display_revision = impl_->wayland.is_some() ? (*impl_->wayland)->display_revision()
                           : impl_->x11.is_some() ? (*impl_->x11)->display_revision() : Serial{};
    redraw = redraw || display_revision != impl_->observed_display_revision;
    impl_->observed_display_revision = display_revision;
    auto logical = logical_size();
    auto scale = impl_->wayland.is_some() ? Scalar((*impl_->wayland)->scale()) : Scalar(1);
    auto extent = pixel_extent();
    return Ok(WindowEvents{rstd::move(input),
                           {logical.unwrap_or(Size{}), scale, revision + 1, Some(PixelSize{extent.width, extent.height})},
                           redraw, close_requested, dropped, minimized(), suspended()});
}
auto Window::instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> {
    if (impl_->selected == WindowBackend::Automatic)
        return Err(gpu::Error{gpu::Stage::State});
    Vec<vrento::vulkan::Extension> result;
    result.push(extension(VK_KHR_SURFACE_EXTENSION_NAME));
    result.push(extension(impl_->selected == WindowBackend::Wayland ? VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
                                                                    : VK_KHR_XLIB_SURFACE_EXTENSION_NAME));
    return Ok(rstd::move(result));
}
auto Window::create_surface(const vvk::Instance &instance) -> Result<gpu::NativeSurface, gpu::Error> {
    if (!instance)
        return Err(gpu::Error{gpu::Stage::State});
    VkSurfaceKHR surface{};
    if (impl_->wayland.is_some()) {
        auto create = instance.Dispatch().vkCreateWaylandSurfaceKHR;
        if (!create)
            return Err(gpu::Error{gpu::Stage::Surface});
        auto native = impl_->wayland->as_ptr().as_raw_ptr();
        VkWaylandSurfaceCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        info.display = native->display();
        info.surface = native->surface();
        auto result = create(*instance, &info, nullptr, &surface);
        if (result != VK_SUCCESS)
            return Err(gpu::Error{gpu::Stage::Surface, result});
        return Ok(gpu::NativeSurface{
            surface, Box<dyn<gpu::SurfaceLifetime>>::make(
                         NativeLease<detail::WaylandWindow>(impl_->wayland->clone()))});
    }
    if (impl_->x11.is_some()) {
        auto create = instance.Dispatch().vkCreateXlibSurfaceKHR;
        if (!create)
            return Err(gpu::Error{gpu::Stage::Surface});
        auto native = impl_->x11->as_ptr().as_raw_ptr();
        VkXlibSurfaceCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        info.dpy = native->display();
        info.window = native->window();
        auto result = create(*instance, &info, nullptr, &surface);
        if (result != VK_SUCCESS)
            return Err(gpu::Error{gpu::Stage::Surface, result});
        return Ok(
            gpu::NativeSurface{surface, Box<dyn<gpu::SurfaceLifetime>>::make(
                                            NativeLease<detail::X11Window>(impl_->x11->clone()))});
    }
    return Err(gpu::Error{gpu::Stage::State});
}
auto Window::pixel_extent() const -> VkExtent2D {
    if (impl_->wayland.is_some())
        return {(*impl_->wayland)->pixel_width(), (*impl_->wayland)->pixel_height()};
    if (impl_->x11.is_some())
        return {(*impl_->x11)->width(), (*impl_->x11)->height()};
    return {};
}
} // namespace vui::host

#endif
