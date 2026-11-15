module;
#include <wayland-client.h>

// Protocol request helpers have internal linkage in the C headers.

inline constexpr auto alias_WL_KEYBOARD_RELEASE_SINCE_VERSION = WL_KEYBOARD_RELEASE_SINCE_VERSION;
inline constexpr auto alias_WL_OUTPUT_RELEASE_SINCE_VERSION = WL_OUTPUT_RELEASE_SINCE_VERSION;
inline constexpr auto alias_WL_POINTER_FRAME_SINCE_VERSION = WL_POINTER_FRAME_SINCE_VERSION;
inline constexpr auto alias_WL_POINTER_RELEASE_SINCE_VERSION = WL_POINTER_RELEASE_SINCE_VERSION;
inline constexpr auto alias_WL_SEAT_RELEASE_SINCE_VERSION = WL_SEAT_RELEASE_SINCE_VERSION;
inline constexpr auto alias_WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION = WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION;
inline constexpr auto alias_WL_TOUCH_RELEASE_SINCE_VERSION = WL_TOUCH_RELEASE_SINCE_VERSION;

#undef WL_KEYBOARD_RELEASE_SINCE_VERSION
#undef WL_OUTPUT_RELEASE_SINCE_VERSION
#undef WL_POINTER_FRAME_SINCE_VERSION
#undef WL_POINTER_RELEASE_SINCE_VERSION
#undef WL_SEAT_RELEASE_SINCE_VERSION
#undef WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION
#undef WL_TOUCH_RELEASE_SINCE_VERSION

module vui.host:ffi.wayland;
namespace ffi {
using ::wl_array;
using ::wl_callback;
using ::wl_callback_listener;
using ::wl_compositor;
using ::wl_compositor_interface;
using ::wl_display;
using ::wl_display_cancel_read;
using ::wl_display_connect;
using ::wl_display_disconnect;
using ::wl_display_dispatch_pending;
using ::wl_display_flush;
using ::wl_display_get_fd;
using ::wl_display_prepare_read;
using ::wl_display_read_events;
using ::wl_display_roundtrip;
using ::wl_keyboard;
using ::wl_pointer;
using ::wl_registry;
using ::wl_registry_listener;
using ::wl_seat;
using ::wl_surface;
using ::wl_surface_listener;
using ::wl_output;
using ::wl_output_interface;
using ::wl_output_listener;
using ::wl_touch;
using ::wl_fixed_t;
using ::wl_seat_interface;
using ::wl_seat_listener;
using ::wl_pointer_listener;
using ::wl_touch_listener;
using ::wl_keyboard_listener;
using ::WL_SEAT_CAPABILITY_POINTER;
using ::WL_SEAT_CAPABILITY_TOUCH;
using ::WL_SEAT_CAPABILITY_KEYBOARD;
using ::WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1;
using ::WL_KEYBOARD_KEY_STATE_PRESSED;
using ::WL_KEYBOARD_KEY_STATE_RELEASED;
using ::WL_POINTER_BUTTON_STATE_PRESSED;
using ::WL_POINTER_BUTTON_STATE_RELEASED;
using ::WL_POINTER_AXIS_VERTICAL_SCROLL;
using ::WL_POINTER_AXIS_HORIZONTAL_SCROLL;
using ::WL_POINTER_AXIS_SOURCE_WHEEL;
using ::WL_POINTER_AXIS_SOURCE_WHEEL_TILT;
using ::WL_OUTPUT_MODE_CURRENT;

inline constexpr auto WL_KEYBOARD_RELEASE_SINCE_VERSION = alias_WL_KEYBOARD_RELEASE_SINCE_VERSION;
inline constexpr auto WL_OUTPUT_RELEASE_SINCE_VERSION = alias_WL_OUTPUT_RELEASE_SINCE_VERSION;
inline constexpr auto WL_POINTER_FRAME_SINCE_VERSION = alias_WL_POINTER_FRAME_SINCE_VERSION;
inline constexpr auto WL_POINTER_RELEASE_SINCE_VERSION = alias_WL_POINTER_RELEASE_SINCE_VERSION;
inline constexpr auto WL_SEAT_RELEASE_SINCE_VERSION = alias_WL_SEAT_RELEASE_SINCE_VERSION;
inline constexpr auto WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION = alias_WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION;
inline constexpr auto WL_TOUCH_RELEASE_SINCE_VERSION = alias_WL_TOUCH_RELEASE_SINCE_VERSION;

inline auto wl_display_get_registry(::wl_display *d) { return ::wl_display_get_registry(d); }
inline auto wl_registry_add_listener(::wl_registry *r, const ::wl_registry_listener *l, void *p) {
    return ::wl_registry_add_listener(r, l, p);
}
inline auto wl_registry_bind(::wl_registry *r, uint32_t n, const ::wl_interface *i, uint32_t v) {
    return ::wl_registry_bind(r, n, i, v);
}
inline void wl_registry_destroy(::wl_registry *r) { ::wl_registry_destroy(r); }
inline auto wl_compositor_create_surface(::wl_compositor *c) {
    return ::wl_compositor_create_surface(c);
}
inline void wl_compositor_destroy(::wl_compositor *c) { ::wl_compositor_destroy(c); }
inline void wl_surface_destroy(::wl_surface *s) { ::wl_surface_destroy(s); }
inline void wl_surface_commit(::wl_surface *s) { ::wl_surface_commit(s); }
inline auto wl_surface_frame(::wl_surface *s) { return ::wl_surface_frame(s); }
inline auto wl_callback_add_listener(::wl_callback *c, const ::wl_callback_listener *l, void *p) { return ::wl_callback_add_listener(c, l, p); }
inline void wl_callback_destroy(::wl_callback *c) { ::wl_callback_destroy(c); }
inline auto wl_surface_add_listener(::wl_surface *s, const ::wl_surface_listener *l, void *p) { return ::wl_surface_add_listener(s, l, p); }
inline auto wl_surface_get_version(::wl_surface *s) { return ::wl_surface_get_version(s); }
inline void wl_surface_set_buffer_scale(::wl_surface *s, int32_t scale) { ::wl_surface_set_buffer_scale(s, scale); }
inline auto wl_output_add_listener(::wl_output *s, const ::wl_output_listener *l, void *p) { return ::wl_output_add_listener(s, l, p); }
inline auto wl_output_get_version(::wl_output *s) { return ::wl_output_get_version(s); }
inline void wl_output_release(::wl_output *s) { ::wl_output_release(s); }
inline void wl_output_destroy(::wl_output *s) { ::wl_output_destroy(s); }
inline auto wl_seat_add_listener(::wl_seat *s, const ::wl_seat_listener *l, void *p) { return ::wl_seat_add_listener(s, l, p); }
inline auto wl_seat_get_pointer(::wl_seat *s) { return ::wl_seat_get_pointer(s); }
inline auto wl_seat_get_touch(::wl_seat *s) { return ::wl_seat_get_touch(s); }
inline auto wl_seat_get_keyboard(::wl_seat *s) { return ::wl_seat_get_keyboard(s); }
inline auto wl_seat_get_version(::wl_seat *s) { return ::wl_seat_get_version(s); }
inline void wl_seat_release(::wl_seat *s) { ::wl_seat_release(s); }
inline void wl_seat_destroy(::wl_seat *s) { ::wl_seat_destroy(s); }
inline auto wl_pointer_add_listener(::wl_pointer *s, const ::wl_pointer_listener *l, void *p) { return ::wl_pointer_add_listener(s, l, p); }
inline auto wl_pointer_get_version(::wl_pointer *s) { return ::wl_pointer_get_version(s); }
inline void wl_pointer_release(::wl_pointer *s) { ::wl_pointer_release(s); }
inline void wl_pointer_destroy(::wl_pointer *s) { ::wl_pointer_destroy(s); }
inline auto wl_touch_add_listener(::wl_touch *s, const ::wl_touch_listener *l, void *p) { return ::wl_touch_add_listener(s, l, p); }
inline auto wl_touch_get_version(::wl_touch *s) { return ::wl_touch_get_version(s); }
inline void wl_touch_release(::wl_touch *s) { ::wl_touch_release(s); }
inline void wl_touch_destroy(::wl_touch *s) { ::wl_touch_destroy(s); }
inline auto wl_keyboard_add_listener(::wl_keyboard *s, const ::wl_keyboard_listener *l, void *p) { return ::wl_keyboard_add_listener(s, l, p); }
inline auto wl_keyboard_get_version(::wl_keyboard *s) { return ::wl_keyboard_get_version(s); }
inline void wl_keyboard_release(::wl_keyboard *s) { ::wl_keyboard_release(s); }
inline void wl_keyboard_destroy(::wl_keyboard *s) { ::wl_keyboard_destroy(s); }
inline auto wl_fixed_to_double(::wl_fixed_t value) { return ::wl_fixed_to_double(value); }
inline auto wl_fixed_from_double(double value) { return ::wl_fixed_from_double(value); }
}
