module vui.host;
import :platform.win32_window;
import :ffi.win32;
import :ffi.vulkan_win32;
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
    result.push(extension(ffi::vulkan_win32::surface));
    result.push(extension(ffi::vulkan_win32::win32_surface));
    return Ok(rstd::move(result));
}
auto Window::create_surface(VkInstance instance) -> Result<gpu::NativeSurface, gpu::Error> {
    if (!instance || impl_->native.is_none()) return Err(gpu::Error{gpu::Stage::State});
    auto create = reinterpret_cast<ffi::vulkan_win32::PFN_vkCreateWin32SurfaceKHR>(
        ffi::vulkan_win32::vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    if (!create) return Err(gpu::Error{gpu::Stage::Surface});
    auto &native = **impl_->native;
    ffi::vulkan_win32::VkWin32SurfaceCreateInfoKHR info{};
    info.sType = ffi::vulkan_win32::VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = native.instance(); info.hwnd = native.handle();
    VkSurfaceKHR surface{};
    auto result = create(instance, &info, nullptr, &surface);
    if (result != VK_SUCCESS) return Err(gpu::Error{gpu::Stage::Surface, result});
    return Ok(gpu::NativeSurface{surface, Box<dyn<gpu::SurfaceLifetime>>::make(
        NativeLease<detail::Win32Window>(impl_->native->clone()))});
}
auto Window::pixel_extent() const -> VkExtent2D {
    if (impl_->native.is_none()) return {};
    auto value = (*impl_->native)->view(); return {value.width, value.height};
}
}
