module;
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

namespace vui_vulkan_win32_constants {
inline constexpr auto surface = VK_KHR_SURFACE_EXTENSION_NAME;
inline constexpr auto win32_surface = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
}

export module vui.host:ffi.vulkan_win32;
export namespace vui::host::ffi::vulkan_win32 {
using ::PFN_vkCreateWin32SurfaceKHR;
using ::VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
using ::VkWin32SurfaceCreateInfoKHR;
using ::vkGetInstanceProcAddr;
using ::vui_vulkan_win32_constants::surface;
using ::vui_vulkan_win32_constants::win32_surface;
}
