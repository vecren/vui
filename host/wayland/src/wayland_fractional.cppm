module;
extern "C" {
#include <fractional-scale-v1-client-protocol.h>
#include <fractional-scale-v1-protocol.inc>
#include <viewporter-client-protocol.h>
#include <viewporter-protocol.inc>
}

namespace vui_fractional_inline {
inline auto fractional_create(wp_fractional_scale_manager_v1 *manager, wl_surface *surface) {
    return wp_fractional_scale_manager_v1_get_fractional_scale(manager, surface);
}
inline auto fractional_listener(wp_fractional_scale_v1 *scale, const wp_fractional_scale_v1_listener *listener, void *data) {
    return wp_fractional_scale_v1_add_listener(scale, listener, data);
}
inline void fractional_destroy(wp_fractional_scale_v1 *scale) { wp_fractional_scale_v1_destroy(scale); }
inline void fractional_manager_destroy(wp_fractional_scale_manager_v1 *manager) { wp_fractional_scale_manager_v1_destroy(manager); }
inline auto viewport_create(wp_viewporter *manager, wl_surface *surface) { return wp_viewporter_get_viewport(manager, surface); }
inline void viewport_destination(wp_viewport *viewport, int32_t width, int32_t height) { wp_viewport_set_destination(viewport, width, height); }
inline void viewport_destroy(wp_viewport *viewport) { wp_viewport_destroy(viewport); }
inline void viewporter_destroy(wp_viewporter *manager) { wp_viewporter_destroy(manager); }
}

export module vui.host.wayland.wayland_fractional;
export namespace vui::host::ffi::wayland_fractional {
using ::wp_fractional_scale_manager_v1;
using ::wp_fractional_scale_manager_v1_interface;
using ::wp_fractional_scale_v1;
using ::wp_fractional_scale_v1_listener;
using ::wp_viewporter;
using ::wp_viewporter_interface;
using ::wp_viewport;
using ::vui_fractional_inline::fractional_create;
using ::vui_fractional_inline::fractional_listener;
using ::vui_fractional_inline::fractional_destroy;
using ::vui_fractional_inline::fractional_manager_destroy;
using ::vui_fractional_inline::viewport_create;
using ::vui_fractional_inline::viewport_destination;
using ::vui_fractional_inline::viewport_destroy;
using ::vui_fractional_inline::viewporter_destroy;
}
