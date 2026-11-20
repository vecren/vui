module;
#include "../../../ffi/wayland_private.hpp"
extern "C" {
#include <xdg-output-unstable-v1-client-protocol.h>
#define types vui_xdg_output_unstable_v1_types
#include <xdg-output-unstable-v1-protocol.inc>
#undef types
}
#include "../../../ffi/wayland_undef.hpp"

module vui.host;
import :platform.wayland.protocol.xdg_output;

namespace ffi {
auto create(zxdg_output_manager_v1 *manager, wl_output *output)
    -> decltype(::zxdg_output_manager_v1_get_xdg_output(manager, output)) {
    return zxdg_output_manager_v1_get_xdg_output(manager, output);
}
auto add_listener(zxdg_output_v1 *output, const zxdg_output_v1_listener *listener, void *data)
    -> decltype(::zxdg_output_v1_add_listener(output, listener, data)) {
    return zxdg_output_v1_add_listener(output, listener, data);
}
auto version(zxdg_output_v1 *output) -> decltype(::zxdg_output_v1_get_version(output)) {
    return zxdg_output_v1_get_version(output);
}
void destroy(zxdg_output_v1 *output) { zxdg_output_v1_destroy(output); }
void destroy_manager(zxdg_output_manager_v1 *manager) { zxdg_output_manager_v1_destroy(manager); }
} // namespace ffi
