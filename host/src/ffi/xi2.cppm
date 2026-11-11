module;
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

namespace vui_xi2_constants {
inline constexpr auto all_devices = XIAllDevices, all_masters = XIAllMasterDevices;
inline constexpr auto button_press = XI_ButtonPress, button_release = XI_ButtonRelease, motion = XI_Motion;
inline constexpr auto touch_begin = XI_TouchBegin, touch_update = XI_TouchUpdate, touch_end = XI_TouchEnd;
inline constexpr auto hierarchy_changed = XI_HierarchyChanged, pointer_emulated = XIPointerEmulated;
inline constexpr auto slave_removed = XISlaveRemoved, master_removed = XIMasterRemoved,
                      slave_detached = XISlaveDetached, device_disabled = XIDeviceDisabled;
inline constexpr auto mask_size = XIMaskLen(XI_LASTEVENT);
inline void set_mask(unsigned char *mask, int event) { XISetMask(mask, event); }
}

export module vui.host:ffi.xi2;
export namespace vui::host::ffi::xi2 {
using ::XIDeviceEvent;
using ::XIEventMask;
using ::XIHierarchyEvent;
using ::XIHierarchyInfo;
using ::XIQueryVersion;
using ::XISelectEvents;
using ::vui_xi2_constants::all_devices;
using ::vui_xi2_constants::all_masters;
using ::vui_xi2_constants::button_press;
using ::vui_xi2_constants::button_release;
using ::vui_xi2_constants::motion;
using ::vui_xi2_constants::touch_begin;
using ::vui_xi2_constants::touch_update;
using ::vui_xi2_constants::touch_end;
using ::vui_xi2_constants::hierarchy_changed;
using ::vui_xi2_constants::pointer_emulated;
using ::vui_xi2_constants::slave_removed;
using ::vui_xi2_constants::master_removed;
using ::vui_xi2_constants::slave_detached;
using ::vui_xi2_constants::device_disabled;
using ::vui_xi2_constants::mask_size;
using ::vui_xi2_constants::set_mask;
}
