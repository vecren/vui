module;
extern "C" {
#include <xdg-shell-client-protocol.h>
#include <xdg-shell-protocol.inc>
}

// Protocol request helpers have internal linkage in the generated header.
namespace vui_xdg_inline {
inline auto xdg_wm_base_add_listener(::xdg_wm_base *b, const ::xdg_wm_base_listener *l, void *p) {
    return ::xdg_wm_base_add_listener(b, l, p);
}
inline void xdg_wm_base_destroy(::xdg_wm_base *b) { ::xdg_wm_base_destroy(b); }
inline auto xdg_wm_base_get_xdg_surface(::xdg_wm_base *b, ::wl_surface *s) {
    return ::xdg_wm_base_get_xdg_surface(b, s);
}
inline void xdg_wm_base_pong(::xdg_wm_base *b, uint32_t serial) { ::xdg_wm_base_pong(b, serial); }
inline auto xdg_surface_add_listener(::xdg_surface *s, const ::xdg_surface_listener *l, void *p) {
    return ::xdg_surface_add_listener(s, l, p);
}
inline void xdg_surface_destroy(::xdg_surface *s) { ::xdg_surface_destroy(s); }
inline auto xdg_surface_get_toplevel(::xdg_surface *s) { return ::xdg_surface_get_toplevel(s); }
inline void xdg_surface_ack_configure(::xdg_surface *s, uint32_t serial) {
    ::xdg_surface_ack_configure(s, serial);
}
inline auto xdg_toplevel_add_listener(::xdg_toplevel *t, const ::xdg_toplevel_listener *l,
                                      void *p) {
    return ::xdg_toplevel_add_listener(t, l, p);
}
inline void xdg_toplevel_destroy(::xdg_toplevel *t) { ::xdg_toplevel_destroy(t); }
inline void xdg_toplevel_set_title(::xdg_toplevel *t, const char *title) {
    ::xdg_toplevel_set_title(t, title);
}
inline void xdg_toplevel_set_min_size(::xdg_toplevel *t, int32_t width, int32_t height) {
    ::xdg_toplevel_set_min_size(t, width, height);
}
inline void xdg_toplevel_set_fullscreen(::xdg_toplevel *t) { ::xdg_toplevel_set_fullscreen(t, nullptr); }
inline void xdg_toplevel_unset_fullscreen(::xdg_toplevel *t) { ::xdg_toplevel_unset_fullscreen(t); }
inline void xdg_toplevel_set_minimized(::xdg_toplevel *t) { ::xdg_toplevel_set_minimized(t); }
inline auto xdg_toplevel_get_version(::xdg_toplevel *t) { return ::xdg_toplevel_get_version(t); }
inline constexpr auto fullscreen_state = XDG_TOPLEVEL_STATE_FULLSCREEN;
inline constexpr auto suspended_state = XDG_TOPLEVEL_STATE_SUSPENDED;
inline constexpr auto minimize_capability = XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
inline void xdg_toplevel_set_app_id(::xdg_toplevel *t, const char *id) {
    ::xdg_toplevel_set_app_id(t, id);
}
} // namespace vui_xdg_inline

export module vui.host:ffi.xdg_shell;

export namespace vui::host::ffi::xdg_shell {
using ::xdg_surface;
using ::xdg_surface_listener;
using ::xdg_toplevel;
using ::xdg_toplevel_listener;
using ::xdg_wm_base;
using ::xdg_wm_base_interface;
using ::xdg_wm_base_listener;
using ::vui_xdg_inline::xdg_surface_ack_configure;
using ::vui_xdg_inline::xdg_surface_add_listener;
using ::vui_xdg_inline::xdg_surface_destroy;
using ::vui_xdg_inline::xdg_surface_get_toplevel;
using ::vui_xdg_inline::xdg_toplevel_add_listener;
using ::vui_xdg_inline::xdg_toplevel_destroy;
using ::vui_xdg_inline::xdg_toplevel_set_app_id;
using ::vui_xdg_inline::xdg_toplevel_set_title;
using ::vui_xdg_inline::xdg_toplevel_set_min_size;
using ::vui_xdg_inline::xdg_toplevel_set_fullscreen;
using ::vui_xdg_inline::xdg_toplevel_unset_fullscreen;
using ::vui_xdg_inline::xdg_toplevel_set_minimized;
using ::vui_xdg_inline::xdg_toplevel_get_version;
using ::vui_xdg_inline::fullscreen_state;
using ::vui_xdg_inline::suspended_state;
using ::vui_xdg_inline::minimize_capability;
using ::vui_xdg_inline::xdg_wm_base_add_listener;
using ::vui_xdg_inline::xdg_wm_base_destroy;
using ::vui_xdg_inline::xdg_wm_base_get_xdg_surface;
using ::vui_xdg_inline::xdg_wm_base_pong;
} // namespace vui::host::ffi::xdg_shell
