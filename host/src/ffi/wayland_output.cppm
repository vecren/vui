module;
extern "C" {
#include <xdg-output-unstable-v1-client-protocol.h>
#include <xdg-output-unstable-v1-protocol.inc>
}
namespace vui_xdg_output_inline {
inline auto create(zxdg_output_manager_v1 *manager, wl_output *output) { return zxdg_output_manager_v1_get_xdg_output(manager, output); }
inline auto add_listener(zxdg_output_v1 *output, const zxdg_output_v1_listener *listener, void *data) { return zxdg_output_v1_add_listener(output, listener, data); }
inline auto version(zxdg_output_v1 *output) { return zxdg_output_v1_get_version(output); }
inline void destroy(zxdg_output_v1 *output) { zxdg_output_v1_destroy(output); }
inline void destroy_manager(zxdg_output_manager_v1 *manager) { zxdg_output_manager_v1_destroy(manager); }
}
export module vui.host:ffi.wayland_output;
export namespace vui::host::ffi::wayland_output {
using ::zxdg_output_manager_v1;
using ::zxdg_output_manager_v1_interface;
using ::zxdg_output_v1;
using ::zxdg_output_v1_listener;
using ::vui_xdg_output_inline::create;
using ::vui_xdg_output_inline::add_listener;
using ::vui_xdg_output_inline::version;
using ::vui_xdg_output_inline::destroy;
using ::vui_xdg_output_inline::destroy_manager;
}
