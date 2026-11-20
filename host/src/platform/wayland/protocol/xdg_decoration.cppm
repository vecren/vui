module;
#include "../../../ffi/wayland_private.hpp"
extern "C" {
#include <xdg-decoration-unstable-v1-client-protocol.h>
}

#include "../../../ffi/wayland_undef.hpp"

module vui.host:platform.wayland.protocol.xdg_decoration;
namespace ffi {
using ::zxdg_decoration_manager_v1;
inline const auto &zxdg_decoration_manager_v1_interface = ::vui_zxdg_decoration_manager_v1_interface;
using ::zxdg_toplevel_decoration_v1;
using ::zxdg_toplevel_decoration_v1_listener;
using ::ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
using ::ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;

auto create(zxdg_decoration_manager_v1 *manager, xdg_toplevel *toplevel)
    -> decltype(::zxdg_decoration_manager_v1_get_toplevel_decoration(manager, toplevel));
auto listen(zxdg_toplevel_decoration_v1 *decoration, const zxdg_toplevel_decoration_v1_listener *listener, void *data)
    -> decltype(::zxdg_toplevel_decoration_v1_add_listener(decoration, listener, data));
void server_side(zxdg_toplevel_decoration_v1 *decoration);
void destroy(zxdg_toplevel_decoration_v1 *decoration);
void destroy_manager(zxdg_decoration_manager_v1 *manager);
} // namespace ffi
