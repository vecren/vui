module;
#include <X11/Xlib.h>

// XI2 client ABI from libXi 1.8.4; see xi2.LICENSE.
extern "C" {
struct XIModifierState {
    int base, latched, locked, effective;
};
using XIGroupState = XIModifierState;
struct XIButtonState {
    int mask_len;
    unsigned char *mask;
};
struct XIValuatorState {
    int mask_len;
    unsigned char *mask;
    double *values;
};
struct XIEventMask {
    int deviceid, mask_len;
    unsigned char *mask;
};
struct XIHierarchyInfo {
    int deviceid, attachment, use, enabled, flags;
};
struct XIHierarchyEvent {
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    int extension, evtype;
    unsigned long time;
    int flags, num_info;
    XIHierarchyInfo *info;
};
struct XIDeviceEvent {
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    int extension, evtype;
    unsigned long time;
    int deviceid, sourceid, detail;
    Window root, event, child;
    double root_x, root_y, event_x, event_y;
    int flags;
    XIButtonState buttons;
    XIValuatorState valuators;
    XIModifierState mods;
    XIGroupState group;
};
int XIQueryVersion(Display *, int *, int *);
int XISelectEvents(Display *, Window, XIEventMask *, int);
}

module vui.host:ffi.xi2;
namespace ffi {
using ::XIModifierState;
using ::XIGroupState;
using ::XIButtonState;
using ::XIValuatorState;
using ::XIEventMask;
using ::XIHierarchyInfo;
using ::XIHierarchyEvent;
using ::XIDeviceEvent;
using ::XIQueryVersion;
using ::XISelectEvents;

inline constexpr int XIAllDevices = 0, XIAllMasterDevices = 1;
inline constexpr int XI_ButtonPress = 4, XI_ButtonRelease = 5, XI_Motion = 6;
inline constexpr int XI_HierarchyChanged = 11;
inline constexpr int XI_TouchBegin = 18, XI_TouchUpdate = 19, XI_TouchEnd = 20;
inline constexpr int XIPointerEmulated = 1 << 16;
inline constexpr int XIMasterRemoved = 1 << 1, XISlaveRemoved = 1 << 3,
                     XISlaveDetached = 1 << 5, XIDeviceDisabled = 1 << 7;
inline constexpr int XIMaskLen(int event) { return (event >> 3) + 1; }
inline void XISetMask(unsigned char *mask, int event) { mask[event >> 3] |= 1u << (event & 7); }
}
