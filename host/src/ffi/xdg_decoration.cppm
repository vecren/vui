module;
extern "C" {
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <xdg-decoration-unstable-v1-protocol.inc>
}

namespace vui_decoration_inline {
inline auto create(zxdg_decoration_manager_v1 *manager, xdg_toplevel *toplevel) {
    return zxdg_decoration_manager_v1_get_toplevel_decoration(manager, toplevel);
}
inline auto listen(zxdg_toplevel_decoration_v1 *decoration,
                   const zxdg_toplevel_decoration_v1_listener *listener, void *data) {
    return zxdg_toplevel_decoration_v1_add_listener(decoration, listener, data);
}
inline void server_side(zxdg_toplevel_decoration_v1 *decoration) {
    zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}
inline void destroy(zxdg_toplevel_decoration_v1 *decoration) {
    zxdg_toplevel_decoration_v1_destroy(decoration);
}
inline void destroy_manager(zxdg_decoration_manager_v1 *manager) {
    zxdg_decoration_manager_v1_destroy(manager);
}
inline constexpr auto server_side_mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
inline constexpr auto client_side_mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
}

export module vui.host:ffi.xdg_decoration;
export namespace vui::host::ffi::xdg_decoration {
using ::zxdg_decoration_manager_v1;
using ::zxdg_decoration_manager_v1_interface;
using ::zxdg_toplevel_decoration_v1;
using ::zxdg_toplevel_decoration_v1_listener;
using ::vui_decoration_inline::create;
using ::vui_decoration_inline::listen;
using ::vui_decoration_inline::server_side;
using ::vui_decoration_inline::destroy;
using ::vui_decoration_inline::destroy_manager;
using ::vui_decoration_inline::server_side_mode;
using ::vui_decoration_inline::client_side_mode;
}
