module vui.host;
import :platform.wayland_window;
import :platform.x11_window;
import :platform.x11;
import :ffi.vulkan;
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
    explicit WakeState(int value) : fd(value) {}
    ~WakeState() { ffi::linux::close(fd); }
    auto request() -> FrameWakeResult {
        auto guard = stats.lock().unwrap();
        auto &state = *guard;
        if (state.closed) return FrameWakeResult::Closed;
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
    auto take() -> bool {
        auto guard = stats.lock().unwrap();
        auto &state = *guard;
        if (!state.pending) return false;
        rstd::uint64_t value{};
        long count;
        do { count = ffi::linux::read(fd, &value, sizeof(value)); }
        while (count < 0 && ffi::linux::error_number() == ffi::linux::interrupted);
        if (count != sizeof(value)) return false;
        state.pending = false;
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
        auto opened = window.get_mut().unwrap()->open(title, width, height);
        if (opened.is_ok()) {
            window.get_mut().unwrap()->input().clipboard().set_waker(
                FrameWaker::make([state = wake.clone()] { return state->request(); }));
            impl_->wayland = Some(rstd::move(window));
            impl_->selected = WindowBackend::Wayland;
            impl_->wake = Some(rstd::move(wake));
            impl_->observed_revision = 0;
            return Ok(empty{});
        }
        if (backend == WindowBackend::Wayland)
            return Err(gpu::Error{gpu::Stage::Surface});
    }
    auto window = rstd::sync::Arc<detail::X11Window>::make();
    auto opened = window.get_mut().unwrap()->open(title, width, height);
    if (opened.is_err())
        return Err(gpu::Error{gpu::Stage::Surface});
    impl_->x11 = Some(rstd::move(window));
    impl_->selected = WindowBackend::X11;
    impl_->wake = Some(rstd::move(wake));
    impl_->observed_revision = 0;
    return Ok(empty{});
}
void Window::close() {
    if (impl_->wake.is_some()) (*impl_->wake)->close();
    impl_->wake = None();
    if (impl_->x11.is_some()) {
        (void)(*impl_->x11)->capture(false);
        (void)(*impl_->x11)->set_text_input(None());
        (*impl_->x11)->close_clipboard();
    }
    if (impl_->wayland.is_some()) {
        (void)(*impl_->wayland)->input().set_text_input(None());
        (*impl_->wayland)->input().clipboard().close();
    }
    impl_->wayland = None();
    impl_->x11 = None();
    impl_->selected = WindowBackend::Automatic;
}
auto Window::backend() const -> WindowBackend { return impl_->selected; }
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
auto Window::wake_stats() const -> WakeStats {
    return impl_->wake.is_some() ? (*impl_->wake)->snapshot() : WakeStats{0, 0, 0, false, true};
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
auto Window::write_clipboard(ref<str> text) -> Result<empty, ClipboardError> {
    if (impl_->x11.is_some()) return (*impl_->x11)->write_clipboard(text);
    if (impl_->wayland.is_some()) return Err(ClipboardError::Unsupported);
    return Err(ClipboardError::Unavailable);
}
auto Window::wait(Index milliseconds) -> Result<WindowEvents, gpu::Error> {
    if (impl_->wake.is_none()) return Err(gpu::Error{gpu::Stage::State});
    auto wake = impl_->wake->as_ptr().as_raw_ptr();
    bool redraw = wake->take();
    bool prepared{};
    Index timeout_ms = milliseconds;
    Option<usize> clipboard_owner_fd;
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
    redraw = wake->take() || redraw;
    Serial revision{}, dropped{};
    Vec<InputEvent> input;
    bool close_requested{};
    if (impl_->wayland.is_some()) {
        auto native = impl_->wayland->as_ptr().as_raw_ptr();
        if (native->input().failed()) return Err(gpu::Error{gpu::Stage::Surface});
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
    auto extent = pixel_extent();
    return Ok(WindowEvents{rstd::move(input),
                           {{Scalar(extent.width), Scalar(extent.height)}, 1, revision + 1},
                           redraw, close_requested, dropped});
}
auto Window::instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> {
    if (impl_->selected == WindowBackend::Automatic)
        return Err(gpu::Error{gpu::Stage::State});
    Vec<vrento::vulkan::Extension> result;
    result.push(extension(ffi::vulkan::surface));
    result.push(extension(impl_->selected == WindowBackend::Wayland ? ffi::vulkan::wayland_surface
                                                                    : ffi::vulkan::xlib_surface));
    return Ok(rstd::move(result));
}
auto Window::create_surface(VkInstance instance) -> Result<gpu::NativeSurface, gpu::Error> {
    if (!instance)
        return Err(gpu::Error{gpu::Stage::State});
    VkSurfaceKHR surface{};
    if (impl_->wayland.is_some()) {
        auto create = reinterpret_cast<ffi::vulkan::PFN_vkCreateWaylandSurfaceKHR>(
            ffi::vulkan::vkGetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR"));
        if (!create)
            return Err(gpu::Error{gpu::Stage::Surface});
        auto native = impl_->wayland->as_ptr().as_raw_ptr();
        ffi::vulkan::VkWaylandSurfaceCreateInfoKHR info{};
        info.sType = ffi::vulkan::VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        info.display = native->display();
        info.surface = native->surface();
        auto result = create(instance, &info, nullptr, &surface);
        if (result != VK_SUCCESS)
            return Err(gpu::Error{gpu::Stage::Surface, result});
        return Ok(gpu::NativeSurface{
            surface, Box<dyn<gpu::SurfaceLifetime>>::make(
                         NativeLease<detail::WaylandWindow>(impl_->wayland->clone()))});
    }
    if (impl_->x11.is_some()) {
        auto create = reinterpret_cast<ffi::vulkan::PFN_vkCreateXlibSurfaceKHR>(
            ffi::vulkan::vkGetInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR"));
        if (!create)
            return Err(gpu::Error{gpu::Stage::Surface});
        auto native = impl_->x11->as_ptr().as_raw_ptr();
        ffi::vulkan::VkXlibSurfaceCreateInfoKHR info{};
        info.sType = ffi::vulkan::VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        info.dpy = native->display();
        info.window = native->window();
        auto result = create(instance, &info, nullptr, &surface);
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
        return {(*impl_->wayland)->width(), (*impl_->wayland)->height()};
    if (impl_->x11.is_some())
        return {(*impl_->x11)->width(), (*impl_->x11)->height()};
    return {};
}
} // namespace vui::host
