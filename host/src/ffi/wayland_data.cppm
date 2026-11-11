module;
#include <wayland-client.h>

namespace vui_wayland_data_inline {
inline auto get_device(wl_data_device_manager *manager, wl_seat *seat) { return wl_data_device_manager_get_data_device(manager, seat); }
inline void destroy_manager(wl_data_device_manager *manager) { wl_data_device_manager_destroy(manager); }
inline auto device_listener(wl_data_device *device, const wl_data_device_listener *listener, void *data) { return wl_data_device_add_listener(device, listener, data); }
inline auto device_version(wl_data_device *device) { return wl_data_device_get_version(device); }
inline void release_device(wl_data_device *device) { wl_data_device_release(device); }
inline void destroy_device(wl_data_device *device) { wl_data_device_destroy(device); }
inline auto offer_listener(wl_data_offer *offer, const wl_data_offer_listener *listener, void *data) { return wl_data_offer_add_listener(offer, listener, data); }
inline void offer_receive(wl_data_offer *offer, const char *type, int fd) { wl_data_offer_receive(offer, type, fd); }
inline void offer_accept(wl_data_offer *offer, uint32_t serial, const char *type) { wl_data_offer_accept(offer, serial, type); }
inline void destroy_offer(wl_data_offer *offer) { wl_data_offer_destroy(offer); }
inline constexpr auto device_release_version = WL_DATA_DEVICE_RELEASE_SINCE_VERSION;
}

export module vui.host:ffi.wayland_data;
export namespace vui::host::ffi::wayland_data {
using ::wl_data_device_manager;
using ::wl_data_device_manager_interface;
using ::wl_data_device;
using ::wl_data_device_listener;
using ::wl_data_offer;
using ::wl_data_offer_listener;
using ::vui_wayland_data_inline::get_device;
using ::vui_wayland_data_inline::destroy_manager;
using ::vui_wayland_data_inline::device_listener;
using ::vui_wayland_data_inline::device_version;
using ::vui_wayland_data_inline::release_device;
using ::vui_wayland_data_inline::destroy_device;
using ::vui_wayland_data_inline::offer_listener;
using ::vui_wayland_data_inline::offer_receive;
using ::vui_wayland_data_inline::offer_accept;
using ::vui_wayland_data_inline::destroy_offer;
using ::vui_wayland_data_inline::device_release_version;
}
