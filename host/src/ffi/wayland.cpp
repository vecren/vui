module;
#include "wayland_private.hpp"
extern "C" {
#include <wayland-protocol.inc>
}
#include "wayland_undef.hpp"

module vui.host;
import :ffi.wayland;

namespace ffi {

using namespace rstd::prelude;
using namespace rstd::literals;
auto WaylandDispatch::open() -> Result<rstd::sync::Arc<WaylandDispatch>, rstd::dlopn::Error> {
    static rstd::sync::Mutex<Option<rstd::sync::Arc<WaylandDispatch>>> cache;
    auto cached = cache.lock().unwrap();
    if (cached->is_some())
        return Ok((*cached)->clone());
    auto loaded = rstd::dlopn::Library::open(rstd::ffi::CStr::from_ptr("libwayland-client.so.0"));
    if (loaded.is_err())
        return Err(rstd::move(loaded).unwrap_err());
    auto shared = rstd::sync::Arc<WaylandDispatch>::make(rstd::move(loaded).unwrap());
    auto api = shared.get_mut().unwrap();
#define LOAD(name)                                                                                                     \
    {                                                                                                                  \
        auto symbol = api->library_.symbol<decltype(api->functions_.name)>(rstd::ffi::CStr::from_ptr(#name));          \
        if (symbol.is_err())                                                                                           \
            return Err(rstd::move(symbol).unwrap_err());                                                               \
        api->functions_.name = symbol.unwrap();                                                                        \
        if (!api->functions_.name)                                                                                     \
            return Err(rstd::dlopn::Error(String::make("missing " #name " symbol"_str)));                              \
    }
    LOAD(wl_display_connect);
    LOAD(wl_display_disconnect);
    LOAD(wl_display_dispatch_pending);
    LOAD(wl_display_flush);
    LOAD(wl_display_get_fd);
    LOAD(wl_display_prepare_read);
    LOAD(wl_display_read_events);
    LOAD(wl_display_cancel_read);
    LOAD(wl_display_roundtrip);
    LOAD(wl_proxy_marshal_flags);
    LOAD(wl_proxy_add_listener);
    LOAD(wl_proxy_destroy);
    LOAD(wl_proxy_get_version);
    LOAD(wl_proxy_get_user_data);
    LOAD(wl_proxy_set_user_data);
#undef LOAD
    __atomic_store_n(&vui_wayland_functions, &api->functions_, __ATOMIC_RELEASE);
    *cached = Some(shared.clone());
    return Ok(rstd::move(shared));
}
auto wl_display_connect(const char *name) -> ::wl_display * { return vui_wayland_api().wl_display_connect(name); }
void wl_display_disconnect(::wl_display *display) { vui_wayland_api().wl_display_disconnect(display); }
auto wl_display_dispatch_pending(::wl_display *display) -> int {
    return vui_wayland_api().wl_display_dispatch_pending(display);
}
auto wl_display_flush(::wl_display *display) -> int { return vui_wayland_api().wl_display_flush(display); }
auto wl_display_get_fd(::wl_display *display) -> int { return vui_wayland_api().wl_display_get_fd(display); }
auto wl_display_prepare_read(::wl_display *display) -> int {
    return vui_wayland_api().wl_display_prepare_read(display);
}
auto wl_display_read_events(::wl_display *display) -> int { return vui_wayland_api().wl_display_read_events(display); }
void wl_display_cancel_read(::wl_display *display) { vui_wayland_api().wl_display_cancel_read(display); }
auto wl_display_roundtrip(::wl_display *display) -> int { return vui_wayland_api().wl_display_roundtrip(display); }
auto wl_display_get_registry(::wl_display *d) -> decltype(::wl_display_get_registry(d)) {
    return ::wl_display_get_registry(d);
}
auto wl_registry_add_listener(::wl_registry *r, const ::wl_registry_listener *l, void *p)
    -> decltype(::wl_registry_add_listener(r, l, p)) {
    return ::wl_registry_add_listener(r, l, p);
}
auto wl_registry_bind(::wl_registry *r, uint32_t n, const ::wl_interface *i, uint32_t v)
    -> decltype(::wl_registry_bind(r, n, i, v)) {
    return ::wl_registry_bind(r, n, i, v);
}
void wl_registry_destroy(::wl_registry *r) { ::wl_registry_destroy(r); }
auto wl_compositor_create_surface(::wl_compositor *c) -> decltype(::wl_compositor_create_surface(c)) {
    return ::wl_compositor_create_surface(c);
}
void wl_compositor_destroy(::wl_compositor *c) { ::wl_compositor_destroy(c); }
void wl_surface_destroy(::wl_surface *s) { ::wl_surface_destroy(s); }
void wl_surface_commit(::wl_surface *s) { ::wl_surface_commit(s); }
auto wl_surface_frame(::wl_surface *s) -> decltype(::wl_surface_frame(s)) { return ::wl_surface_frame(s); }
auto wl_callback_add_listener(::wl_callback *c, const ::wl_callback_listener *l, void *p)
    -> decltype(::wl_callback_add_listener(c, l, p)) {
    return ::wl_callback_add_listener(c, l, p);
}
void wl_callback_destroy(::wl_callback *c) { ::wl_callback_destroy(c); }
auto wl_surface_add_listener(::wl_surface *s, const ::wl_surface_listener *l, void *p)
    -> decltype(::wl_surface_add_listener(s, l, p)) {
    return ::wl_surface_add_listener(s, l, p);
}
auto wl_surface_get_version(::wl_surface *s) -> decltype(::wl_surface_get_version(s)) {
    return ::wl_surface_get_version(s);
}
void wl_surface_set_buffer_scale(::wl_surface *s, int32_t scale) { ::wl_surface_set_buffer_scale(s, scale); }
auto wl_output_add_listener(::wl_output *s, const ::wl_output_listener *l, void *p)
    -> decltype(::wl_output_add_listener(s, l, p)) {
    return ::wl_output_add_listener(s, l, p);
}
auto wl_output_get_version(::wl_output *s) -> decltype(::wl_output_get_version(s)) {
    return ::wl_output_get_version(s);
}
void wl_output_release(::wl_output *s) { ::wl_output_release(s); }
void wl_output_destroy(::wl_output *s) { ::wl_output_destroy(s); }
auto wl_seat_add_listener(::wl_seat *s, const ::wl_seat_listener *l, void *p)
    -> decltype(::wl_seat_add_listener(s, l, p)) {
    return ::wl_seat_add_listener(s, l, p);
}
auto wl_seat_get_pointer(::wl_seat *s) -> decltype(::wl_seat_get_pointer(s)) { return ::wl_seat_get_pointer(s); }
auto wl_seat_get_touch(::wl_seat *s) -> decltype(::wl_seat_get_touch(s)) { return ::wl_seat_get_touch(s); }
auto wl_seat_get_keyboard(::wl_seat *s) -> decltype(::wl_seat_get_keyboard(s)) { return ::wl_seat_get_keyboard(s); }
auto wl_seat_get_version(::wl_seat *s) -> decltype(::wl_seat_get_version(s)) { return ::wl_seat_get_version(s); }
void wl_seat_release(::wl_seat *s) { ::wl_seat_release(s); }
void wl_seat_destroy(::wl_seat *s) { ::wl_seat_destroy(s); }
auto wl_pointer_add_listener(::wl_pointer *s, const ::wl_pointer_listener *l, void *p)
    -> decltype(::wl_pointer_add_listener(s, l, p)) {
    return ::wl_pointer_add_listener(s, l, p);
}
auto wl_pointer_get_version(::wl_pointer *s) -> decltype(::wl_pointer_get_version(s)) {
    return ::wl_pointer_get_version(s);
}
void wl_pointer_release(::wl_pointer *s) { ::wl_pointer_release(s); }
void wl_pointer_destroy(::wl_pointer *s) { ::wl_pointer_destroy(s); }
auto wl_touch_add_listener(::wl_touch *s, const ::wl_touch_listener *l, void *p)
    -> decltype(::wl_touch_add_listener(s, l, p)) {
    return ::wl_touch_add_listener(s, l, p);
}
auto wl_touch_get_version(::wl_touch *s) -> decltype(::wl_touch_get_version(s)) { return ::wl_touch_get_version(s); }
void wl_touch_release(::wl_touch *s) { ::wl_touch_release(s); }
void wl_touch_destroy(::wl_touch *s) { ::wl_touch_destroy(s); }
auto wl_keyboard_add_listener(::wl_keyboard *s, const ::wl_keyboard_listener *l, void *p)
    -> decltype(::wl_keyboard_add_listener(s, l, p)) {
    return ::wl_keyboard_add_listener(s, l, p);
}
auto wl_keyboard_get_version(::wl_keyboard *s) -> decltype(::wl_keyboard_get_version(s)) {
    return ::wl_keyboard_get_version(s);
}
void wl_keyboard_release(::wl_keyboard *s) { ::wl_keyboard_release(s); }
void wl_keyboard_destroy(::wl_keyboard *s) { ::wl_keyboard_destroy(s); }
auto wl_fixed_to_double(::wl_fixed_t value) -> decltype(::wl_fixed_to_double(value)) {
    return ::wl_fixed_to_double(value);
}
auto wl_fixed_from_double(double value) -> decltype(::wl_fixed_from_double(value)) {
    return ::wl_fixed_from_double(value);
}
} // namespace ffi
