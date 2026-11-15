module;
#include <wayland-client.h>

inline constexpr auto alias_WL_DATA_DEVICE_RELEASE_SINCE_VERSION = WL_DATA_DEVICE_RELEASE_SINCE_VERSION;

#undef WL_DATA_DEVICE_RELEASE_SINCE_VERSION

module vui.host:ffi.wayland_data;
namespace ffi {
using ::wl_data_device_manager;
using ::wl_data_device_manager_interface;
using ::wl_data_device;
using ::wl_data_device_listener;
using ::wl_data_offer;
using ::wl_data_offer_listener;
using ::wl_data_source;
using ::wl_data_source_listener;

inline constexpr auto WL_DATA_DEVICE_RELEASE_SINCE_VERSION = alias_WL_DATA_DEVICE_RELEASE_SINCE_VERSION;

inline auto get_device(wl_data_device_manager *manager, wl_seat *seat) { return wl_data_device_manager_get_data_device(manager, seat); }
inline auto create_source(wl_data_device_manager *manager) { return wl_data_device_manager_create_data_source(manager); }
inline auto source_listener(wl_data_source *source, const wl_data_source_listener *listener, void *data) { return wl_data_source_add_listener(source, listener, data); }
inline void source_offer(wl_data_source *source, const char *type) { wl_data_source_offer(source, type); }
inline void destroy_source(wl_data_source *source) { wl_data_source_destroy(source); }
inline void set_selection(wl_data_device *device, wl_data_source *source, uint32_t serial) { wl_data_device_set_selection(device, source, serial); }
inline void destroy_manager(wl_data_device_manager *manager) { wl_data_device_manager_destroy(manager); }
inline auto device_listener(wl_data_device *device, const wl_data_device_listener *listener, void *data) { return wl_data_device_add_listener(device, listener, data); }
inline auto device_version(wl_data_device *device) { return wl_data_device_get_version(device); }
inline void release_device(wl_data_device *device) { wl_data_device_release(device); }
inline void destroy_device(wl_data_device *device) { wl_data_device_destroy(device); }
inline auto offer_listener(wl_data_offer *offer, const wl_data_offer_listener *listener, void *data) { return wl_data_offer_add_listener(offer, listener, data); }
inline void offer_receive(wl_data_offer *offer, const char *type, int fd) { wl_data_offer_receive(offer, type, fd); }
inline void offer_accept(wl_data_offer *offer, uint32_t serial, const char *type) { wl_data_offer_accept(offer, serial, type); }
inline void destroy_offer(wl_data_offer *offer) { wl_data_offer_destroy(offer); }
}
