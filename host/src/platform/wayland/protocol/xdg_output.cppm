module;
extern "C" {
#include <xdg-output-unstable-v1-client-protocol.h>
#include <xdg-output-unstable-v1-protocol.inc>
}

module vui.host:platform.wayland.protocol.xdg_output;
namespace ffi {
using ::zxdg_output_manager_v1;
using ::zxdg_output_manager_v1_interface;
using ::zxdg_output_v1;
using ::zxdg_output_v1_listener;

inline auto create(zxdg_output_manager_v1 *manager, wl_output *output) { return zxdg_output_manager_v1_get_xdg_output(manager, output); }
inline auto add_listener(zxdg_output_v1 *output, const zxdg_output_v1_listener *listener, void *data) { return zxdg_output_v1_add_listener(output, listener, data); }
inline auto version(zxdg_output_v1 *output) { return zxdg_output_v1_get_version(output); }
inline void destroy(zxdg_output_v1 *output) { zxdg_output_v1_destroy(output); }
inline void destroy_manager(zxdg_output_manager_v1 *manager) { zxdg_output_manager_v1_destroy(manager); }
}
