module;
#include "../../../ffi/wayland_private.hpp"
extern "C" {
#include <xdg-shell-client-protocol.h>
}

#include "../../../ffi/wayland_undef.hpp"

module vui.host:platform.wayland.protocol.xdg_shell;
namespace ffi {
using ::xdg_surface;
using ::xdg_surface_listener;
using ::xdg_toplevel;
using ::xdg_toplevel_listener;
using ::xdg_wm_base;
inline const auto &xdg_wm_base_interface = ::vui_xdg_wm_base_interface;
using ::XDG_TOPLEVEL_STATE_FULLSCREEN;
using ::XDG_TOPLEVEL_STATE_SUSPENDED;
using ::XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
using ::xdg_wm_base_listener;

auto xdg_wm_base_add_listener(::xdg_wm_base *b, const ::xdg_wm_base_listener *l, void *p)
    -> decltype(::xdg_wm_base_add_listener(b, l, p));
void xdg_wm_base_destroy(::xdg_wm_base *b);
auto xdg_wm_base_get_xdg_surface(::xdg_wm_base *b, ::wl_surface *s) -> decltype(::xdg_wm_base_get_xdg_surface(b, s));
void xdg_wm_base_pong(::xdg_wm_base *b, uint32_t serial);
auto xdg_surface_add_listener(::xdg_surface *s, const ::xdg_surface_listener *l, void *p)
    -> decltype(::xdg_surface_add_listener(s, l, p));
void xdg_surface_destroy(::xdg_surface *s);
auto xdg_surface_get_toplevel(::xdg_surface *s) -> decltype(::xdg_surface_get_toplevel(s));
void xdg_surface_ack_configure(::xdg_surface *s, uint32_t serial);
auto xdg_toplevel_add_listener(::xdg_toplevel *t, const ::xdg_toplevel_listener *l, void *p)
    -> decltype(::xdg_toplevel_add_listener(t, l, p));
void xdg_toplevel_destroy(::xdg_toplevel *t);
void xdg_toplevel_set_title(::xdg_toplevel *t, const char *title);
void xdg_toplevel_set_min_size(::xdg_toplevel *t, int32_t width, int32_t height);
void xdg_toplevel_set_fullscreen(::xdg_toplevel *t);
void xdg_toplevel_unset_fullscreen(::xdg_toplevel *t);
void xdg_toplevel_set_minimized(::xdg_toplevel *t);
auto xdg_toplevel_get_version(::xdg_toplevel *t) -> decltype(::xdg_toplevel_get_version(t));

void xdg_toplevel_set_app_id(::xdg_toplevel *t, const char *id);
} // namespace ffi
