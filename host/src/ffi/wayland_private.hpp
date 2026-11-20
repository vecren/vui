#pragma once
#include <wayland-client-core.h>

namespace ffi {
struct WaylandFunctions {
    decltype(&::wl_display_connect) wl_display_connect{};
    decltype(&::wl_display_disconnect) wl_display_disconnect{};
    decltype(&::wl_display_dispatch_pending) wl_display_dispatch_pending{};
    decltype(&::wl_display_flush) wl_display_flush{};
    decltype(&::wl_display_get_fd) wl_display_get_fd{};
    decltype(&::wl_display_prepare_read) wl_display_prepare_read{};
    decltype(&::wl_display_read_events) wl_display_read_events{};
    decltype(&::wl_display_cancel_read) wl_display_cancel_read{};
    decltype(&::wl_display_roundtrip) wl_display_roundtrip{};
    decltype(&::wl_proxy_marshal_flags) wl_proxy_marshal_flags{};
    decltype(&::wl_proxy_add_listener) wl_proxy_add_listener{};
    decltype(&::wl_proxy_destroy) wl_proxy_destroy{};
    decltype(&::wl_proxy_get_version) wl_proxy_get_version{};
    decltype(&::wl_proxy_get_user_data) wl_proxy_get_user_data{};
    decltype(&::wl_proxy_set_user_data) wl_proxy_set_user_data{};
};
inline const WaylandFunctions *vui_wayland_functions{};
inline auto vui_wayland_api() -> const WaylandFunctions & {
    return *__atomic_load_n(&vui_wayland_functions, __ATOMIC_ACQUIRE);
}
} // namespace ffi

#define wl_display_interface vui_wl_display_interface
#define wl_registry_interface vui_wl_registry_interface
#define wl_callback_interface vui_wl_callback_interface
#define wl_compositor_interface vui_wl_compositor_interface
#define wl_shm_pool_interface vui_wl_shm_pool_interface
#define wl_shm_interface vui_wl_shm_interface
#define wl_buffer_interface vui_wl_buffer_interface
#define wl_data_offer_interface vui_wl_data_offer_interface
#define wl_data_source_interface vui_wl_data_source_interface
#define wl_data_device_interface vui_wl_data_device_interface
#define wl_data_device_manager_interface vui_wl_data_device_manager_interface
#define wl_shell_interface vui_wl_shell_interface
#define wl_shell_surface_interface vui_wl_shell_surface_interface
#define wl_surface_interface vui_wl_surface_interface
#define wl_seat_interface vui_wl_seat_interface
#define wl_pointer_interface vui_wl_pointer_interface
#define wl_keyboard_interface vui_wl_keyboard_interface
#define wl_touch_interface vui_wl_touch_interface
#define wl_output_interface vui_wl_output_interface
#define wl_region_interface vui_wl_region_interface
#define wl_subcompositor_interface vui_wl_subcompositor_interface
#define wl_subsurface_interface vui_wl_subsurface_interface
#define xdg_wm_base_interface vui_xdg_wm_base_interface
#define xdg_positioner_interface vui_xdg_positioner_interface
#define xdg_surface_interface vui_xdg_surface_interface
#define xdg_toplevel_interface vui_xdg_toplevel_interface
#define xdg_popup_interface vui_xdg_popup_interface
#define zwp_text_input_v3_interface vui_zwp_text_input_v3_interface
#define zwp_text_input_manager_v3_interface vui_zwp_text_input_manager_v3_interface
#define wp_fractional_scale_manager_v1_interface vui_wp_fractional_scale_manager_v1_interface
#define wp_fractional_scale_v1_interface vui_wp_fractional_scale_v1_interface
#define wp_viewporter_interface vui_wp_viewporter_interface
#define wp_viewport_interface vui_wp_viewport_interface
#define zxdg_output_manager_v1_interface vui_zxdg_output_manager_v1_interface
#define zxdg_output_v1_interface vui_zxdg_output_v1_interface
#define zxdg_decoration_manager_v1_interface vui_zxdg_decoration_manager_v1_interface
#define zxdg_toplevel_decoration_v1_interface vui_zxdg_toplevel_decoration_v1_interface
#define wl_proxy_marshal_flags ffi::vui_wayland_api().wl_proxy_marshal_flags
#define wl_proxy_add_listener ffi::vui_wayland_api().wl_proxy_add_listener
#define wl_proxy_destroy ffi::vui_wayland_api().wl_proxy_destroy
#define wl_proxy_get_version ffi::vui_wayland_api().wl_proxy_get_version
#define wl_proxy_get_user_data ffi::vui_wayland_api().wl_proxy_get_user_data
#define wl_proxy_set_user_data ffi::vui_wayland_api().wl_proxy_set_user_data

#include <wayland-client-protocol.h>
