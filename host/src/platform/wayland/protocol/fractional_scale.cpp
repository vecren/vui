module;
#include "../../../ffi/wayland_private.hpp"
extern "C" {
#include <fractional-scale-v1-client-protocol.h>
#define types vui_fractional_scale_v1_types
#include <fractional-scale-v1-protocol.inc>
#undef types
#include <viewporter-client-protocol.h>
#define types vui_viewporter_types
#include <viewporter-protocol.inc>
#undef types
}
#include "../../../ffi/wayland_undef.hpp"

module vui.host;
import :platform.wayland.protocol.fractional_scale;

namespace ffi {
auto fractional_create(wp_fractional_scale_manager_v1 *manager, wl_surface *surface)
    -> decltype(::wp_fractional_scale_manager_v1_get_fractional_scale(manager, surface)) {
    return wp_fractional_scale_manager_v1_get_fractional_scale(manager, surface);
}
auto fractional_listener(wp_fractional_scale_v1 *scale, const wp_fractional_scale_v1_listener *listener, void *data)
    -> decltype(::wp_fractional_scale_v1_add_listener(scale, listener, data)) {
    return wp_fractional_scale_v1_add_listener(scale, listener, data);
}
void fractional_destroy(wp_fractional_scale_v1 *scale) { wp_fractional_scale_v1_destroy(scale); }
void fractional_manager_destroy(wp_fractional_scale_manager_v1 *manager) {
    wp_fractional_scale_manager_v1_destroy(manager);
}
auto viewport_create(wp_viewporter *manager, wl_surface *surface)
    -> decltype(::wp_viewporter_get_viewport(manager, surface)) {
    return wp_viewporter_get_viewport(manager, surface);
}
void viewport_destination(wp_viewport *viewport, int32_t width, int32_t height) {
    wp_viewport_set_destination(viewport, width, height);
}
void viewport_destroy(wp_viewport *viewport) { wp_viewport_destroy(viewport); }
void viewporter_destroy(wp_viewporter *manager) { wp_viewporter_destroy(manager); }
} // namespace ffi
