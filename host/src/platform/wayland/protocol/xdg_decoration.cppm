module;
extern "C" {
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <xdg-decoration-unstable-v1-protocol.inc>
}

module vui.host:platform.wayland.protocol.xdg_decoration;
namespace ffi {
using ::zxdg_decoration_manager_v1;
using ::zxdg_decoration_manager_v1_interface;
using ::zxdg_toplevel_decoration_v1;
using ::zxdg_toplevel_decoration_v1_listener;
using ::ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
using ::ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;

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
}
