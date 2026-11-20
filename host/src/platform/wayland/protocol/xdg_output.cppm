module;
#include "../../../ffi/wayland_private.hpp"
extern "C" {
#include <xdg-output-unstable-v1-client-protocol.h>
}

#include "../../../ffi/wayland_undef.hpp"

module vui.host:platform.wayland.protocol.xdg_output;
namespace ffi {
using ::zxdg_output_manager_v1;
inline const auto &zxdg_output_manager_v1_interface = ::vui_zxdg_output_manager_v1_interface;
using ::zxdg_output_v1;
using ::zxdg_output_v1_listener;

auto create(zxdg_output_manager_v1 *manager, wl_output *output)
    -> decltype(::zxdg_output_manager_v1_get_xdg_output(manager, output));
auto add_listener(zxdg_output_v1 *output, const zxdg_output_v1_listener *listener, void *data)
    -> decltype(::zxdg_output_v1_add_listener(output, listener, data));
auto version(zxdg_output_v1 *output) -> decltype(::zxdg_output_v1_get_version(output));
void destroy(zxdg_output_v1 *output);
void destroy_manager(zxdg_output_manager_v1 *manager);
} // namespace ffi
