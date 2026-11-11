module;
#include <wayland-client.h>

// Protocol request helpers have internal linkage in the C headers.
namespace vui_wayland_inline {
inline auto wl_display_get_registry(::wl_display *d) { return ::wl_display_get_registry(d); }
inline auto wl_registry_add_listener(::wl_registry *r, const ::wl_registry_listener *l, void *p) {
    return ::wl_registry_add_listener(r, l, p);
}
inline auto wl_registry_bind(::wl_registry *r, uint32_t n, const ::wl_interface *i, uint32_t v) {
    return ::wl_registry_bind(r, n, i, v);
}
inline void wl_registry_destroy(::wl_registry *r) { ::wl_registry_destroy(r); }
inline auto wl_compositor_create_surface(::wl_compositor *c) {
    return ::wl_compositor_create_surface(c);
}
inline void wl_compositor_destroy(::wl_compositor *c) { ::wl_compositor_destroy(c); }
inline void wl_surface_destroy(::wl_surface *s) { ::wl_surface_destroy(s); }
inline void wl_surface_commit(::wl_surface *s) { ::wl_surface_commit(s); }
} // namespace vui_wayland_inline

export module vui.host:ffi.wayland;

export namespace vui::host::ffi::wayland {
using ::wl_array;
using ::wl_compositor;
using ::wl_compositor_interface;
using ::wl_display;
using ::wl_display_cancel_read;
using ::wl_display_connect;
using ::wl_display_disconnect;
using ::wl_display_dispatch_pending;
using ::wl_display_flush;
using ::wl_display_get_fd;
using ::wl_display_prepare_read;
using ::wl_display_read_events;
using ::wl_display_roundtrip;
using ::wl_keyboard;
using ::wl_pointer;
using ::wl_registry;
using ::wl_registry_listener;
using ::wl_seat;
using ::wl_surface;
using ::wl_touch;
using ::vui_wayland_inline::wl_compositor_create_surface;
using ::vui_wayland_inline::wl_compositor_destroy;
using ::vui_wayland_inline::wl_display_get_registry;
using ::vui_wayland_inline::wl_registry_add_listener;
using ::vui_wayland_inline::wl_registry_bind;
using ::vui_wayland_inline::wl_registry_destroy;
using ::vui_wayland_inline::wl_surface_commit;
using ::vui_wayland_inline::wl_surface_destroy;
} // namespace vui::host::ffi::wayland
