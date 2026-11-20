module;
#include "../../../ffi/wayland_private.hpp"
extern "C" {
#include <xdg-decoration-unstable-v1-client-protocol.h>
#define types vui_xdg_decoration_unstable_v1_types
#include <xdg-decoration-unstable-v1-protocol.inc>
#undef types
}
#include "../../../ffi/wayland_undef.hpp"

module vui.host;
import :platform.wayland.protocol.xdg_decoration;

namespace ffi {
auto create(zxdg_decoration_manager_v1 *manager, xdg_toplevel *toplevel)
    -> decltype(::zxdg_decoration_manager_v1_get_toplevel_decoration(manager, toplevel)) {
    return zxdg_decoration_manager_v1_get_toplevel_decoration(manager, toplevel);
}
auto listen(zxdg_toplevel_decoration_v1 *decoration, const zxdg_toplevel_decoration_v1_listener *listener, void *data)
    -> decltype(::zxdg_toplevel_decoration_v1_add_listener(decoration, listener, data)) {
    return zxdg_toplevel_decoration_v1_add_listener(decoration, listener, data);
}
void server_side(zxdg_toplevel_decoration_v1 *decoration) {
    zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}
void destroy(zxdg_toplevel_decoration_v1 *decoration) { zxdg_toplevel_decoration_v1_destroy(decoration); }
void destroy_manager(zxdg_decoration_manager_v1 *manager) { zxdg_decoration_manager_v1_destroy(manager); }
} // namespace ffi
