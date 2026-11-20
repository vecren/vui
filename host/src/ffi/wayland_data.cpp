module;
#include "wayland_private.hpp"
#include "wayland_undef.hpp"

module vui.host;
import :ffi.wayland_data;

namespace ffi {
auto get_device(wl_data_device_manager *manager, wl_seat *seat)
    -> decltype(::wl_data_device_manager_get_data_device(manager, seat)) {
    return wl_data_device_manager_get_data_device(manager, seat);
}
auto create_source(wl_data_device_manager *manager) -> decltype(::wl_data_device_manager_create_data_source(manager)) {
    return wl_data_device_manager_create_data_source(manager);
}
auto source_listener(wl_data_source *source, const wl_data_source_listener *listener, void *data)
    -> decltype(::wl_data_source_add_listener(source, listener, data)) {
    return wl_data_source_add_listener(source, listener, data);
}
void source_offer(wl_data_source *source, const char *type) { wl_data_source_offer(source, type); }
void destroy_source(wl_data_source *source) { wl_data_source_destroy(source); }
void set_selection(wl_data_device *device, wl_data_source *source, uint32_t serial) {
    wl_data_device_set_selection(device, source, serial);
}
void destroy_manager(wl_data_device_manager *manager) { wl_data_device_manager_destroy(manager); }
auto device_listener(wl_data_device *device, const wl_data_device_listener *listener, void *data)
    -> decltype(::wl_data_device_add_listener(device, listener, data)) {
    return wl_data_device_add_listener(device, listener, data);
}
auto device_version(wl_data_device *device) -> decltype(::wl_data_device_get_version(device)) {
    return wl_data_device_get_version(device);
}
void release_device(wl_data_device *device) { wl_data_device_release(device); }
void destroy_device(wl_data_device *device) { wl_data_device_destroy(device); }
auto offer_listener(wl_data_offer *offer, const wl_data_offer_listener *listener, void *data)
    -> decltype(::wl_data_offer_add_listener(offer, listener, data)) {
    return wl_data_offer_add_listener(offer, listener, data);
}
void offer_receive(wl_data_offer *offer, const char *type, int fd) { wl_data_offer_receive(offer, type, fd); }
void offer_accept(wl_data_offer *offer, uint32_t serial, const char *type) {
    wl_data_offer_accept(offer, serial, type);
}
void destroy_offer(wl_data_offer *offer) { wl_data_offer_destroy(offer); }
} // namespace ffi
