module;
#include "wayland_private.hpp"

inline constexpr auto alias_WL_DATA_DEVICE_RELEASE_SINCE_VERSION = WL_DATA_DEVICE_RELEASE_SINCE_VERSION;

#undef WL_DATA_DEVICE_RELEASE_SINCE_VERSION

#include "wayland_undef.hpp"

module vui.host:ffi.wayland_data;
namespace ffi {
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
    -> decltype(::wl_data_device_manager_get_data_device(manager, seat));
auto create_source(wl_data_device_manager *manager) -> decltype(::wl_data_device_manager_create_data_source(manager));
auto source_listener(wl_data_source *source, const wl_data_source_listener *listener, void *data)
    -> decltype(::wl_data_source_add_listener(source, listener, data));
void source_offer(wl_data_source *source, const char *type);
void destroy_source(wl_data_source *source);
void set_selection(wl_data_device *device, wl_data_source *source, uint32_t serial);
void destroy_manager(wl_data_device_manager *manager);
auto device_listener(wl_data_device *device, const wl_data_device_listener *listener, void *data)
    -> decltype(::wl_data_device_add_listener(device, listener, data));
auto device_version(wl_data_device *device) -> decltype(::wl_data_device_get_version(device));
void release_device(wl_data_device *device);
void destroy_device(wl_data_device *device);
auto offer_listener(wl_data_offer *offer, const wl_data_offer_listener *listener, void *data)
    -> decltype(::wl_data_offer_add_listener(offer, listener, data));
void offer_receive(wl_data_offer *offer, const char *type, int fd);
void offer_accept(wl_data_offer *offer, uint32_t serial, const char *type);
void destroy_offer(wl_data_offer *offer);
} // namespace ffi
