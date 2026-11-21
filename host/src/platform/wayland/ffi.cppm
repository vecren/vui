module;
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
static const WaylandFunctions *vui_wayland_functions{};
static auto vui_wayland_api() -> const WaylandFunctions & {
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

extern "C" {
#include <wayland-client-protocol.h>
#define types vui_wayland_types
#include <wayland-protocol.inc>
#undef types

#include <xdg-shell-client-protocol.h>
#define types vui_xdg_shell_types
#include <xdg-shell-protocol.inc>
#undef types

#include <xdg-decoration-unstable-v1-client-protocol.h>
#define types vui_xdg_decoration_unstable_v1_types
#include <xdg-decoration-unstable-v1-protocol.inc>
#undef types

#include <xdg-output-unstable-v1-client-protocol.h>
#define types vui_xdg_output_unstable_v1_types
#include <xdg-output-unstable-v1-protocol.inc>
#undef types

#include <text-input-v3-client-protocol.h>
#define types vui_text_input_v3_types
#include <text-input-v3-protocol.inc>
#undef types

#include <fractional-scale-v1-client-protocol.h>
#define types vui_fractional_scale_v1_types
#include <fractional-scale-v1-protocol.inc>
#undef types
#include <viewporter-client-protocol.h>
#define types vui_viewporter_types
#include <viewporter-protocol.inc>
#undef types
}

inline constexpr auto alias_WL_DATA_DEVICE_RELEASE_SINCE_VERSION = WL_DATA_DEVICE_RELEASE_SINCE_VERSION;
inline constexpr auto alias_WL_KEYBOARD_RELEASE_SINCE_VERSION = WL_KEYBOARD_RELEASE_SINCE_VERSION;
inline constexpr auto alias_WL_OUTPUT_RELEASE_SINCE_VERSION = WL_OUTPUT_RELEASE_SINCE_VERSION;
inline constexpr auto alias_WL_POINTER_FRAME_SINCE_VERSION = WL_POINTER_FRAME_SINCE_VERSION;
inline constexpr auto alias_WL_POINTER_RELEASE_SINCE_VERSION = WL_POINTER_RELEASE_SINCE_VERSION;
inline constexpr auto alias_WL_SEAT_RELEASE_SINCE_VERSION = WL_SEAT_RELEASE_SINCE_VERSION;
inline constexpr auto alias_WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION = WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION;
inline constexpr auto alias_WL_TOUCH_RELEASE_SINCE_VERSION = WL_TOUCH_RELEASE_SINCE_VERSION;

#undef WL_DATA_DEVICE_RELEASE_SINCE_VERSION
#undef WL_KEYBOARD_RELEASE_SINCE_VERSION
#undef WL_OUTPUT_RELEASE_SINCE_VERSION
#undef WL_POINTER_FRAME_SINCE_VERSION
#undef WL_POINTER_RELEASE_SINCE_VERSION
#undef WL_SEAT_RELEASE_SINCE_VERSION
#undef WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION
#undef WL_TOUCH_RELEASE_SINCE_VERSION
#undef wl_display_interface
#undef wl_registry_interface
#undef wl_callback_interface
#undef wl_compositor_interface
#undef wl_shm_pool_interface
#undef wl_shm_interface
#undef wl_buffer_interface
#undef wl_data_offer_interface
#undef wl_data_source_interface
#undef wl_data_device_interface
#undef wl_data_device_manager_interface
#undef wl_shell_interface
#undef wl_shell_surface_interface
#undef wl_surface_interface
#undef wl_seat_interface
#undef wl_pointer_interface
#undef wl_keyboard_interface
#undef wl_touch_interface
#undef wl_output_interface
#undef wl_region_interface
#undef wl_subcompositor_interface
#undef wl_subsurface_interface
#undef xdg_wm_base_interface
#undef xdg_positioner_interface
#undef xdg_surface_interface
#undef xdg_toplevel_interface
#undef xdg_popup_interface
#undef zwp_text_input_v3_interface
#undef zwp_text_input_manager_v3_interface
#undef wp_fractional_scale_manager_v1_interface
#undef wp_fractional_scale_v1_interface
#undef wp_viewporter_interface
#undef wp_viewport_interface
#undef zxdg_output_manager_v1_interface
#undef zxdg_output_v1_interface
#undef zxdg_decoration_manager_v1_interface
#undef zxdg_toplevel_decoration_v1_interface
#undef wl_proxy_marshal_flags
#undef wl_proxy_add_listener
#undef wl_proxy_destroy
#undef wl_proxy_get_version
#undef wl_proxy_get_user_data
#undef wl_proxy_set_user_data
#undef WL_PRIVATE

module vui.host:platform.wayland.ffi;
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

auto WaylandDispatch::open() -> rstd::Result<rstd::sync::Arc<WaylandDispatch>, rstd::dlopn::Error> {
    using namespace rstd::prelude;
    using namespace rstd::literals;
    static rstd::sync::Mutex<Option<rstd::sync::Arc<WaylandDispatch>>> cache;
    auto cached = cache.lock().unwrap();
    if (cached->is_some())
        return Ok((*cached)->clone());
    auto loaded = rstd::dlopn::Library::open(rstd::ffi::CStr::from_ptr("libwayland-client.so.0"));
    if (loaded.is_err())
        return Err(rstd::move(loaded).unwrap_err());
    auto shared = rstd::sync::Arc<WaylandDispatch>::make(rstd::move(loaded).unwrap());
    auto api = shared.get_mut().unwrap();
#define LOAD(name)                                                                                                     \
    {                                                                                                                  \
        auto symbol = api->library_.symbol<decltype(api->functions_.name)>(rstd::ffi::CStr::from_ptr(#name));          \
        if (symbol.is_err())                                                                                           \
            return Err(rstd::move(symbol).unwrap_err());                                                               \
        api->functions_.name = symbol.unwrap();                                                                        \
        if (!api->functions_.name)                                                                                     \
            return Err(rstd::dlopn::Error(String::make("missing " #name " symbol"_str)));                              \
    }
    LOAD(wl_display_connect);
    LOAD(wl_display_disconnect);
    LOAD(wl_display_dispatch_pending);
    LOAD(wl_display_flush);
    LOAD(wl_display_get_fd);
    LOAD(wl_display_prepare_read);
    LOAD(wl_display_read_events);
    LOAD(wl_display_cancel_read);
    LOAD(wl_display_roundtrip);
    LOAD(wl_proxy_marshal_flags);
    LOAD(wl_proxy_add_listener);
    LOAD(wl_proxy_destroy);
    LOAD(wl_proxy_get_version);
    LOAD(wl_proxy_get_user_data);
    LOAD(wl_proxy_set_user_data);
#undef LOAD
    __atomic_store_n(&vui_wayland_functions, &api->functions_, __ATOMIC_RELEASE);
    *cached = Some(shared.clone());
    return Ok(rstd::move(shared));
}

auto wl_display_connect(const char *name) -> ::wl_display * { return vui_wayland_api().wl_display_connect(name); }
void wl_display_disconnect(::wl_display *display) { vui_wayland_api().wl_display_disconnect(display); }
auto wl_display_dispatch_pending(::wl_display *display) -> int {
    return vui_wayland_api().wl_display_dispatch_pending(display);
}
auto wl_display_flush(::wl_display *display) -> int { return vui_wayland_api().wl_display_flush(display); }
auto wl_display_get_fd(::wl_display *display) -> int { return vui_wayland_api().wl_display_get_fd(display); }
auto wl_display_prepare_read(::wl_display *display) -> int {
    return vui_wayland_api().wl_display_prepare_read(display);
}
auto wl_display_read_events(::wl_display *display) -> int { return vui_wayland_api().wl_display_read_events(display); }
void wl_display_cancel_read(::wl_display *display) { vui_wayland_api().wl_display_cancel_read(display); }
auto wl_display_roundtrip(::wl_display *display) -> int { return vui_wayland_api().wl_display_roundtrip(display); }
auto wl_display_get_registry(::wl_display *d) -> decltype(::wl_display_get_registry(d)) {
    return ::wl_display_get_registry(d);
}
auto wl_registry_add_listener(::wl_registry *r, const ::wl_registry_listener *l, void *p)
    -> decltype(::wl_registry_add_listener(r, l, p)) {
    return ::wl_registry_add_listener(r, l, p);
}
auto wl_registry_bind(::wl_registry *r, uint32_t n, const ::wl_interface *i, uint32_t v)
    -> decltype(::wl_registry_bind(r, n, i, v)) {
    return ::wl_registry_bind(r, n, i, v);
}
void wl_registry_destroy(::wl_registry *r) { ::wl_registry_destroy(r); }
auto wl_compositor_create_surface(::wl_compositor *c) -> decltype(::wl_compositor_create_surface(c)) {
    return ::wl_compositor_create_surface(c);
}
void wl_compositor_destroy(::wl_compositor *c) { ::wl_compositor_destroy(c); }
void wl_surface_destroy(::wl_surface *s) { ::wl_surface_destroy(s); }
void wl_surface_commit(::wl_surface *s) { ::wl_surface_commit(s); }
auto wl_surface_frame(::wl_surface *s) -> decltype(::wl_surface_frame(s)) { return ::wl_surface_frame(s); }
auto wl_callback_add_listener(::wl_callback *c, const ::wl_callback_listener *l, void *p)
    -> decltype(::wl_callback_add_listener(c, l, p)) {
    return ::wl_callback_add_listener(c, l, p);
}
void wl_callback_destroy(::wl_callback *c) { ::wl_callback_destroy(c); }
auto wl_surface_add_listener(::wl_surface *s, const ::wl_surface_listener *l, void *p)
    -> decltype(::wl_surface_add_listener(s, l, p)) {
    return ::wl_surface_add_listener(s, l, p);
}
auto wl_surface_get_version(::wl_surface *s) -> decltype(::wl_surface_get_version(s)) {
    return ::wl_surface_get_version(s);
}
void wl_surface_set_buffer_scale(::wl_surface *s, int32_t scale) { ::wl_surface_set_buffer_scale(s, scale); }
auto wl_output_add_listener(::wl_output *s, const ::wl_output_listener *l, void *p)
    -> decltype(::wl_output_add_listener(s, l, p)) {
    return ::wl_output_add_listener(s, l, p);
}
auto wl_output_get_version(::wl_output *s) -> decltype(::wl_output_get_version(s)) {
    return ::wl_output_get_version(s);
}
void wl_output_release(::wl_output *s) { ::wl_output_release(s); }
void wl_output_destroy(::wl_output *s) { ::wl_output_destroy(s); }
auto wl_seat_add_listener(::wl_seat *s, const ::wl_seat_listener *l, void *p)
    -> decltype(::wl_seat_add_listener(s, l, p)) {
    return ::wl_seat_add_listener(s, l, p);
}
auto wl_seat_get_pointer(::wl_seat *s) -> decltype(::wl_seat_get_pointer(s)) { return ::wl_seat_get_pointer(s); }
auto wl_seat_get_touch(::wl_seat *s) -> decltype(::wl_seat_get_touch(s)) { return ::wl_seat_get_touch(s); }
auto wl_seat_get_keyboard(::wl_seat *s) -> decltype(::wl_seat_get_keyboard(s)) { return ::wl_seat_get_keyboard(s); }
auto wl_seat_get_version(::wl_seat *s) -> decltype(::wl_seat_get_version(s)) { return ::wl_seat_get_version(s); }
void wl_seat_release(::wl_seat *s) { ::wl_seat_release(s); }
void wl_seat_destroy(::wl_seat *s) { ::wl_seat_destroy(s); }
auto wl_pointer_add_listener(::wl_pointer *s, const ::wl_pointer_listener *l, void *p)
    -> decltype(::wl_pointer_add_listener(s, l, p)) {
    return ::wl_pointer_add_listener(s, l, p);
}
auto wl_pointer_get_version(::wl_pointer *s) -> decltype(::wl_pointer_get_version(s)) {
    return ::wl_pointer_get_version(s);
}
void wl_pointer_release(::wl_pointer *s) { ::wl_pointer_release(s); }
void wl_pointer_destroy(::wl_pointer *s) { ::wl_pointer_destroy(s); }
auto wl_touch_add_listener(::wl_touch *s, const ::wl_touch_listener *l, void *p)
    -> decltype(::wl_touch_add_listener(s, l, p)) {
    return ::wl_touch_add_listener(s, l, p);
}
auto wl_touch_get_version(::wl_touch *s) -> decltype(::wl_touch_get_version(s)) { return ::wl_touch_get_version(s); }
void wl_touch_release(::wl_touch *s) { ::wl_touch_release(s); }
void wl_touch_destroy(::wl_touch *s) { ::wl_touch_destroy(s); }
auto wl_keyboard_add_listener(::wl_keyboard *s, const ::wl_keyboard_listener *l, void *p)
    -> decltype(::wl_keyboard_add_listener(s, l, p)) {
    return ::wl_keyboard_add_listener(s, l, p);
}
auto wl_keyboard_get_version(::wl_keyboard *s) -> decltype(::wl_keyboard_get_version(s)) {
    return ::wl_keyboard_get_version(s);
}
void wl_keyboard_release(::wl_keyboard *s) { ::wl_keyboard_release(s); }
void wl_keyboard_destroy(::wl_keyboard *s) { ::wl_keyboard_destroy(s); }
auto wl_fixed_to_double(::wl_fixed_t value) -> decltype(::wl_fixed_to_double(value)) {
    return ::wl_fixed_to_double(value);
}
auto wl_fixed_from_double(double value) -> decltype(::wl_fixed_from_double(value)) {
    return ::wl_fixed_from_double(value);
}

using ::wl_data_device_manager;
inline const auto &wl_data_device_manager_interface = ::vui_wl_data_device_manager_interface;
using ::wl_data_device;
using ::wl_data_device_listener;
using ::wl_data_offer;
using ::wl_data_offer_listener;
using ::wl_data_source;
using ::wl_data_source_listener;
inline constexpr auto WL_DATA_DEVICE_RELEASE_SINCE_VERSION = alias_WL_DATA_DEVICE_RELEASE_SINCE_VERSION;

auto get_device(wl_data_device_manager *manager, wl_seat *seat)
    -> decltype(::wl_data_device_manager_get_data_device(manager, seat)) {
    return ::wl_data_device_manager_get_data_device(manager, seat);
}
auto create_source(wl_data_device_manager *manager) -> decltype(::wl_data_device_manager_create_data_source(manager)) {
    return ::wl_data_device_manager_create_data_source(manager);
}
auto source_listener(wl_data_source *source, const wl_data_source_listener *listener, void *data)
    -> decltype(::wl_data_source_add_listener(source, listener, data)) {
    return ::wl_data_source_add_listener(source, listener, data);
}
void source_offer(wl_data_source *source, const char *type) { ::wl_data_source_offer(source, type); }
void destroy_source(wl_data_source *source) { ::wl_data_source_destroy(source); }
void set_selection(wl_data_device *device, wl_data_source *source, uint32_t serial) {
    ::wl_data_device_set_selection(device, source, serial);
}
void destroy_manager(wl_data_device_manager *manager) { ::wl_data_device_manager_destroy(manager); }
auto device_listener(wl_data_device *device, const wl_data_device_listener *listener, void *data)
    -> decltype(::wl_data_device_add_listener(device, listener, data)) {
    return ::wl_data_device_add_listener(device, listener, data);
}
auto device_version(wl_data_device *device) -> decltype(::wl_data_device_get_version(device)) {
    return ::wl_data_device_get_version(device);
}
void release_device(wl_data_device *device) { ::wl_data_device_release(device); }
void destroy_device(wl_data_device *device) { ::wl_data_device_destroy(device); }
auto offer_listener(wl_data_offer *offer, const wl_data_offer_listener *listener, void *data)
    -> decltype(::wl_data_offer_add_listener(offer, listener, data)) {
    return ::wl_data_offer_add_listener(offer, listener, data);
}
void offer_receive(wl_data_offer *offer, const char *type, int fd) { ::wl_data_offer_receive(offer, type, fd); }
void offer_accept(wl_data_offer *offer, uint32_t serial, const char *type) {
    ::wl_data_offer_accept(offer, serial, type);
}
void destroy_offer(wl_data_offer *offer) { ::wl_data_offer_destroy(offer); }

using ::xdg_surface;
using ::xdg_surface_listener;
using ::xdg_toplevel;
using ::xdg_toplevel_listener;
using ::xdg_wm_base;
inline const auto &xdg_wm_base_interface = ::vui_xdg_wm_base_interface;
using ::XDG_TOPLEVEL_STATE_FULLSCREEN;
using ::XDG_TOPLEVEL_STATE_SUSPENDED;
using ::XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
using ::xdg_wm_base_listener;

auto xdg_wm_base_add_listener(::xdg_wm_base *b, const ::xdg_wm_base_listener *l, void *p)
    -> decltype(::xdg_wm_base_add_listener(b, l, p)) {
    return ::xdg_wm_base_add_listener(b, l, p);
}
void xdg_wm_base_destroy(::xdg_wm_base *b) { ::xdg_wm_base_destroy(b); }
auto xdg_wm_base_get_xdg_surface(::xdg_wm_base *b, ::wl_surface *s) -> decltype(::xdg_wm_base_get_xdg_surface(b, s)) {
    return ::xdg_wm_base_get_xdg_surface(b, s);
}
void xdg_wm_base_pong(::xdg_wm_base *b, uint32_t serial) { ::xdg_wm_base_pong(b, serial); }
auto xdg_surface_add_listener(::xdg_surface *s, const ::xdg_surface_listener *l, void *p)
    -> decltype(::xdg_surface_add_listener(s, l, p)) {
    return ::xdg_surface_add_listener(s, l, p);
}
void xdg_surface_destroy(::xdg_surface *s) { ::xdg_surface_destroy(s); }
auto xdg_surface_get_toplevel(::xdg_surface *s) -> decltype(::xdg_surface_get_toplevel(s)) {
    return ::xdg_surface_get_toplevel(s);
}
void xdg_surface_ack_configure(::xdg_surface *s, uint32_t serial) { ::xdg_surface_ack_configure(s, serial); }
auto xdg_toplevel_add_listener(::xdg_toplevel *t, const ::xdg_toplevel_listener *l, void *p)
    -> decltype(::xdg_toplevel_add_listener(t, l, p)) {
    return ::xdg_toplevel_add_listener(t, l, p);
}
void xdg_toplevel_destroy(::xdg_toplevel *t) { ::xdg_toplevel_destroy(t); }
void xdg_toplevel_set_title(::xdg_toplevel *t, const char *title) { ::xdg_toplevel_set_title(t, title); }
void xdg_toplevel_set_min_size(::xdg_toplevel *t, int32_t width, int32_t height) {
    ::xdg_toplevel_set_min_size(t, width, height);
}
void xdg_toplevel_set_fullscreen(::xdg_toplevel *t) { ::xdg_toplevel_set_fullscreen(t, nullptr); }
void xdg_toplevel_unset_fullscreen(::xdg_toplevel *t) { ::xdg_toplevel_unset_fullscreen(t); }
void xdg_toplevel_set_minimized(::xdg_toplevel *t) { ::xdg_toplevel_set_minimized(t); }
auto xdg_toplevel_get_version(::xdg_toplevel *t) -> decltype(::xdg_toplevel_get_version(t)) {
    return ::xdg_toplevel_get_version(t);
}
void xdg_toplevel_set_app_id(::xdg_toplevel *t, const char *id) { ::xdg_toplevel_set_app_id(t, id); }

using ::zxdg_decoration_manager_v1;
inline const auto &zxdg_decoration_manager_v1_interface = ::vui_zxdg_decoration_manager_v1_interface;
using ::zxdg_toplevel_decoration_v1;
using ::zxdg_toplevel_decoration_v1_listener;
using ::ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
using ::ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;

auto create(zxdg_decoration_manager_v1 *manager, xdg_toplevel *toplevel)
    -> decltype(::zxdg_decoration_manager_v1_get_toplevel_decoration(manager, toplevel)) {
    return ::zxdg_decoration_manager_v1_get_toplevel_decoration(manager, toplevel);
}
auto listen(zxdg_toplevel_decoration_v1 *decoration, const zxdg_toplevel_decoration_v1_listener *listener, void *data)
    -> decltype(::zxdg_toplevel_decoration_v1_add_listener(decoration, listener, data)) {
    return ::zxdg_toplevel_decoration_v1_add_listener(decoration, listener, data);
}
void server_side(zxdg_toplevel_decoration_v1 *decoration) {
    ::zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}
void destroy(zxdg_toplevel_decoration_v1 *decoration) { ::zxdg_toplevel_decoration_v1_destroy(decoration); }
void destroy_manager(zxdg_decoration_manager_v1 *manager) { ::zxdg_decoration_manager_v1_destroy(manager); }

using ::zxdg_output_manager_v1;
inline const auto &zxdg_output_manager_v1_interface = ::vui_zxdg_output_manager_v1_interface;
using ::zxdg_output_v1;
using ::zxdg_output_v1_listener;

auto create(zxdg_output_manager_v1 *manager, wl_output *output)
    -> decltype(::zxdg_output_manager_v1_get_xdg_output(manager, output)) {
    return ::zxdg_output_manager_v1_get_xdg_output(manager, output);
}
auto add_listener(zxdg_output_v1 *output, const zxdg_output_v1_listener *listener, void *data)
    -> decltype(::zxdg_output_v1_add_listener(output, listener, data)) {
    return ::zxdg_output_v1_add_listener(output, listener, data);
}
auto version(zxdg_output_v1 *output) -> decltype(::zxdg_output_v1_get_version(output)) {
    return ::zxdg_output_v1_get_version(output);
}
void destroy(zxdg_output_v1 *output) { ::zxdg_output_v1_destroy(output); }
void destroy_manager(zxdg_output_manager_v1 *manager) { ::zxdg_output_manager_v1_destroy(manager); }

using ::zwp_text_input_manager_v3;
inline const auto &zwp_text_input_manager_v3_interface = ::vui_zwp_text_input_manager_v3_interface;
using ::zwp_text_input_v3;
using ::ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD;
using ::ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER;
using ::zwp_text_input_v3_listener;

auto get_text_input(::zwp_text_input_manager_v3 *m, ::wl_seat *s)
    -> decltype(::zwp_text_input_manager_v3_get_text_input(m, s)) {
    return ::zwp_text_input_manager_v3_get_text_input(m, s);
}
void manager_destroy(::zwp_text_input_manager_v3 *m) { ::zwp_text_input_manager_v3_destroy(m); }
auto add_listener(::zwp_text_input_v3 *t, const ::zwp_text_input_v3_listener *l, void *p)
    -> decltype(::zwp_text_input_v3_add_listener(t, l, p)) {
    return ::zwp_text_input_v3_add_listener(t, l, p);
}
void destroy(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_destroy(t); }
void enable(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_enable(t); }
void disable(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_disable(t); }
void surrounding(::zwp_text_input_v3 *t, const char *s, int32_t cursor, int32_t anchor) {
    ::zwp_text_input_v3_set_surrounding_text(t, s, cursor, anchor);
}
void cause(::zwp_text_input_v3 *t, uint32_t value) { ::zwp_text_input_v3_set_text_change_cause(t, value); }
void rectangle(::zwp_text_input_v3 *t, int32_t x, int32_t y, int32_t w, int32_t h) {
    ::zwp_text_input_v3_set_cursor_rectangle(t, x, y, w, h);
}
void commit(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_commit(t); }

using ::wp_fractional_scale_manager_v1;
inline const auto &wp_fractional_scale_manager_v1_interface = ::vui_wp_fractional_scale_manager_v1_interface;
using ::wp_fractional_scale_v1;
using ::wp_fractional_scale_v1_listener;
using ::wp_viewporter;
inline const auto &wp_viewporter_interface = ::vui_wp_viewporter_interface;
using ::wp_viewport;

auto fractional_create(wp_fractional_scale_manager_v1 *manager, wl_surface *surface)
    -> decltype(::wp_fractional_scale_manager_v1_get_fractional_scale(manager, surface)) {
    return ::wp_fractional_scale_manager_v1_get_fractional_scale(manager, surface);
}
auto fractional_listener(wp_fractional_scale_v1 *scale, const wp_fractional_scale_v1_listener *listener, void *data)
    -> decltype(::wp_fractional_scale_v1_add_listener(scale, listener, data)) {
    return ::wp_fractional_scale_v1_add_listener(scale, listener, data);
}
void fractional_destroy(wp_fractional_scale_v1 *scale) { ::wp_fractional_scale_v1_destroy(scale); }
void fractional_manager_destroy(wp_fractional_scale_manager_v1 *manager) {
    ::wp_fractional_scale_manager_v1_destroy(manager);
}
auto viewport_create(wp_viewporter *manager, wl_surface *surface)
    -> decltype(::wp_viewporter_get_viewport(manager, surface)) {
    return ::wp_viewporter_get_viewport(manager, surface);
}
void viewport_destination(wp_viewport *viewport, int32_t width, int32_t height) {
    ::wp_viewport_set_destination(viewport, width, height);
}
void viewport_destroy(wp_viewport *viewport) { ::wp_viewport_destroy(viewport); }
void viewporter_destroy(wp_viewporter *manager) { ::wp_viewporter_destroy(manager); }
} // namespace ffi
