module;
#include "wayland_private.hpp"

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

#include "wayland_undef.hpp"

module vui.host:ffi.wayland;
import rstd.dlopn;
namespace ffi {
class WaylandDispatch {
    rstd::dlopn::Library library_;
    WaylandFunctions functions_;

  public:
    explicit WaylandDispatch(rstd::dlopn::Library library) : library_(rstd::move(library)) {}
    WaylandDispatch(const WaylandDispatch &) = delete;
    auto operator=(const WaylandDispatch &) -> WaylandDispatch & = delete;
    static auto open() -> rstd::Result<rstd::sync::Arc<WaylandDispatch>, rstd::dlopn::Error>;
};

using ::wl_array;
using ::wl_callback;
using ::wl_callback_listener;
using ::wl_compositor;
inline const auto &wl_compositor_interface = ::vui_wl_compositor_interface;
using ::wl_display;
using ::wl_keyboard;
using ::wl_output;
using ::wl_pointer;
using ::wl_registry;
using ::wl_registry_listener;
using ::wl_seat;
using ::wl_surface;
using ::wl_surface_listener;
inline const auto &wl_output_interface = ::vui_wl_output_interface;
using ::wl_fixed_t;
using ::wl_output_listener;
using ::wl_touch;
inline const auto &wl_seat_interface = ::vui_wl_seat_interface;
using ::WL_KEYBOARD_KEY_STATE_PRESSED;
using ::WL_KEYBOARD_KEY_STATE_RELEASED;
using ::WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1;
using ::wl_keyboard_listener;
using ::WL_OUTPUT_MODE_CURRENT;
using ::WL_POINTER_AXIS_HORIZONTAL_SCROLL;
using ::WL_POINTER_AXIS_SOURCE_WHEEL;
using ::WL_POINTER_AXIS_SOURCE_WHEEL_TILT;
using ::WL_POINTER_AXIS_VERTICAL_SCROLL;
using ::WL_POINTER_BUTTON_STATE_PRESSED;
using ::WL_POINTER_BUTTON_STATE_RELEASED;
using ::wl_pointer_listener;
using ::WL_SEAT_CAPABILITY_KEYBOARD;
using ::WL_SEAT_CAPABILITY_POINTER;
using ::WL_SEAT_CAPABILITY_TOUCH;
using ::wl_seat_listener;
using ::wl_touch_listener;

inline constexpr auto WL_KEYBOARD_RELEASE_SINCE_VERSION = alias_WL_KEYBOARD_RELEASE_SINCE_VERSION;
inline constexpr auto WL_OUTPUT_RELEASE_SINCE_VERSION = alias_WL_OUTPUT_RELEASE_SINCE_VERSION;
inline constexpr auto WL_POINTER_FRAME_SINCE_VERSION = alias_WL_POINTER_FRAME_SINCE_VERSION;
inline constexpr auto WL_POINTER_RELEASE_SINCE_VERSION = alias_WL_POINTER_RELEASE_SINCE_VERSION;
inline constexpr auto WL_SEAT_RELEASE_SINCE_VERSION = alias_WL_SEAT_RELEASE_SINCE_VERSION;
inline constexpr auto WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION = alias_WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION;
inline constexpr auto WL_TOUCH_RELEASE_SINCE_VERSION = alias_WL_TOUCH_RELEASE_SINCE_VERSION;

auto wl_display_get_registry(::wl_display *d) -> decltype(::wl_display_get_registry(d));
auto wl_registry_add_listener(::wl_registry *r, const ::wl_registry_listener *l, void *p)
    -> decltype(::wl_registry_add_listener(r, l, p));
auto wl_registry_bind(::wl_registry *r, uint32_t n, const ::wl_interface *i, uint32_t v)
    -> decltype(::wl_registry_bind(r, n, i, v));
void wl_registry_destroy(::wl_registry *r);
auto wl_compositor_create_surface(::wl_compositor *c) -> decltype(::wl_compositor_create_surface(c));
void wl_compositor_destroy(::wl_compositor *c);
void wl_surface_destroy(::wl_surface *s);
void wl_surface_commit(::wl_surface *s);
auto wl_surface_frame(::wl_surface *s) -> decltype(::wl_surface_frame(s));
auto wl_callback_add_listener(::wl_callback *c, const ::wl_callback_listener *l, void *p)
    -> decltype(::wl_callback_add_listener(c, l, p));
void wl_callback_destroy(::wl_callback *c);
auto wl_surface_add_listener(::wl_surface *s, const ::wl_surface_listener *l, void *p)
    -> decltype(::wl_surface_add_listener(s, l, p));
auto wl_surface_get_version(::wl_surface *s) -> decltype(::wl_surface_get_version(s));
void wl_surface_set_buffer_scale(::wl_surface *s, int32_t scale);
auto wl_output_add_listener(::wl_output *s, const ::wl_output_listener *l, void *p)
    -> decltype(::wl_output_add_listener(s, l, p));
auto wl_output_get_version(::wl_output *s) -> decltype(::wl_output_get_version(s));
void wl_output_release(::wl_output *s);
void wl_output_destroy(::wl_output *s);
auto wl_seat_add_listener(::wl_seat *s, const ::wl_seat_listener *l, void *p)
    -> decltype(::wl_seat_add_listener(s, l, p));
auto wl_seat_get_pointer(::wl_seat *s) -> decltype(::wl_seat_get_pointer(s));
auto wl_seat_get_touch(::wl_seat *s) -> decltype(::wl_seat_get_touch(s));
auto wl_seat_get_keyboard(::wl_seat *s) -> decltype(::wl_seat_get_keyboard(s));
auto wl_seat_get_version(::wl_seat *s) -> decltype(::wl_seat_get_version(s));
void wl_seat_release(::wl_seat *s);
void wl_seat_destroy(::wl_seat *s);
auto wl_pointer_add_listener(::wl_pointer *s, const ::wl_pointer_listener *l, void *p)
    -> decltype(::wl_pointer_add_listener(s, l, p));
auto wl_pointer_get_version(::wl_pointer *s) -> decltype(::wl_pointer_get_version(s));
void wl_pointer_release(::wl_pointer *s);
void wl_pointer_destroy(::wl_pointer *s);
auto wl_touch_add_listener(::wl_touch *s, const ::wl_touch_listener *l, void *p)
    -> decltype(::wl_touch_add_listener(s, l, p));
auto wl_touch_get_version(::wl_touch *s) -> decltype(::wl_touch_get_version(s));
void wl_touch_release(::wl_touch *s);
void wl_touch_destroy(::wl_touch *s);
auto wl_keyboard_add_listener(::wl_keyboard *s, const ::wl_keyboard_listener *l, void *p)
    -> decltype(::wl_keyboard_add_listener(s, l, p));
auto wl_keyboard_get_version(::wl_keyboard *s) -> decltype(::wl_keyboard_get_version(s));
void wl_keyboard_release(::wl_keyboard *s);
void wl_keyboard_destroy(::wl_keyboard *s);
auto wl_fixed_to_double(::wl_fixed_t value) -> decltype(::wl_fixed_to_double(value));
auto wl_fixed_from_double(double value) -> decltype(::wl_fixed_from_double(value));

auto wl_display_connect(const char *name) -> ::wl_display *;
void wl_display_disconnect(::wl_display *display);
auto wl_display_dispatch_pending(::wl_display *display) -> int;
auto wl_display_flush(::wl_display *display) -> int;
auto wl_display_get_fd(::wl_display *display) -> int;
auto wl_display_prepare_read(::wl_display *display) -> int;
auto wl_display_read_events(::wl_display *display) -> int;
void wl_display_cancel_read(::wl_display *display);
auto wl_display_roundtrip(::wl_display *display) -> int;
} // namespace ffi
