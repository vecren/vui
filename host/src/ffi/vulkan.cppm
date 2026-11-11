module;
#define VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>

namespace vui_vulkan_constants {
inline constexpr auto surface = VK_KHR_SURFACE_EXTENSION_NAME;
inline constexpr auto wayland_surface = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
inline constexpr auto xlib_surface = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
} // namespace vui_vulkan_constants

export module vui.host:ffi.vulkan;

export namespace vui::host::ffi::vulkan {
using ::PFN_vkCreateWaylandSurfaceKHR;
using ::PFN_vkCreateXlibSurfaceKHR;
using ::VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
using ::VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
using ::vkGetInstanceProcAddr;
using ::VkWaylandSurfaceCreateInfoKHR;
using ::VkXlibSurfaceCreateInfoKHR;
using ::vui_vulkan_constants::surface;
using ::vui_vulkan_constants::wayland_surface;
using ::vui_vulkan_constants::xlib_surface;
} // namespace vui::host::ffi::vulkan
