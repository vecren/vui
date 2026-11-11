module vui.host;
import :platform.wayland_window;
import :platform.x11_window;
import :ffi.vulkan;
using namespace rstd::prelude;

namespace vui::host {
namespace {
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
};

Window::Window() : impl_(Box<Impl>::make()) {}
Window::~Window() = default;

auto Window::open(const char *title, Index width, Index height, WindowBackend backend)
    -> Result<empty, gpu::Error> {
    if (impl_->wayland.is_some() || impl_->x11.is_some() || !title || !width || !height ||
        width > 32768 || height > 32768 || unsigned(backend) > unsigned(WindowBackend::X11))
        return Err(gpu::Error{gpu::Stage::State});
    if (backend == WindowBackend::Automatic || backend == WindowBackend::Wayland) {
        auto window = rstd::sync::Arc<detail::WaylandWindow>::make();
        auto opened = window.get_mut().unwrap()->open(title, width, height);
        if (opened.is_ok()) {
            impl_->wayland = Some(rstd::move(window));
            impl_->selected = WindowBackend::Wayland;
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
    return Ok(empty{});
}
void Window::close() {
    impl_->wayland = None();
    impl_->x11 = None();
    impl_->selected = WindowBackend::Automatic;
}
auto Window::backend() const -> WindowBackend { return impl_->selected; }
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
