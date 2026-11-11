module;
#include <X11/Xlib.h>

namespace vui_x11_constants {
inline constexpr auto structure_notify_mask = StructureNotifyMask;
inline constexpr auto configure_notify = ConfigureNotify;
inline constexpr auto destroy_notify = DestroyNotify;
inline constexpr auto client_message = ClientMessage;
} // namespace vui_x11_constants

export module vui.host:ffi.x11;

export namespace vui::host::ffi::x11 {
using ::Atom;
using ::Display;
using ::Window;
using ::XCloseDisplay;
using ::XConnectionNumber;
using ::XCreateSimpleWindow;
using ::XDefaultScreen;
using ::XDestroyWindow;
using ::XEvent;
using ::XFlush;
using ::XInternAtom;
using ::XMapWindow;
using ::XNextEvent;
using ::XOpenDisplay;
using ::XPending;
using ::XResizeWindow;
using ::XRootWindow;
using ::XSelectInput;
using ::XSendEvent;
using ::XSetWMProtocols;
using ::XStoreName;
using ::XSync;
using ::vui_x11_constants::client_message;
using ::vui_x11_constants::configure_notify;
using ::vui_x11_constants::destroy_notify;
using ::vui_x11_constants::structure_notify_mask;
} // namespace vui::host::ffi::x11
