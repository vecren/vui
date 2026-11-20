module;
#include "../../../ffi/wayland_private.hpp"
extern "C" {
#include <fractional-scale-v1-client-protocol.h>
#include <viewporter-client-protocol.h>
}

#include "../../../ffi/wayland_undef.hpp"

module vui.host:platform.wayland.protocol.fractional_scale;
namespace ffi {
using ::wp_fractional_scale_manager_v1;
inline const auto &wp_fractional_scale_manager_v1_interface = ::vui_wp_fractional_scale_manager_v1_interface;
using ::wp_fractional_scale_v1;
using ::wp_fractional_scale_v1_listener;
using ::wp_viewporter;
inline const auto &wp_viewporter_interface = ::vui_wp_viewporter_interface;
using ::wp_viewport;

auto fractional_create(wp_fractional_scale_manager_v1 *manager, wl_surface *surface)
    -> decltype(::wp_fractional_scale_manager_v1_get_fractional_scale(manager, surface));
auto fractional_listener(wp_fractional_scale_v1 *scale, const wp_fractional_scale_v1_listener *listener, void *data)
    -> decltype(::wp_fractional_scale_v1_add_listener(scale, listener, data));
void fractional_destroy(wp_fractional_scale_v1 *scale);
void fractional_manager_destroy(wp_fractional_scale_manager_v1 *manager);
auto viewport_create(wp_viewporter *manager, wl_surface *surface)
    -> decltype(::wp_viewporter_get_viewport(manager, surface));
void viewport_destination(wp_viewport *viewport, int32_t width, int32_t height);
void viewport_destroy(wp_viewport *viewport);
void viewporter_destroy(wp_viewporter *manager);
} // namespace ffi
