module;
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

namespace vui_x11_constants {
inline constexpr auto structure_notify_mask = StructureNotifyMask;
inline constexpr auto substructure_notify_mask = SubstructureNotifyMask;
inline constexpr auto substructure_redirect_mask = SubstructureRedirectMask;
inline constexpr auto min_size_hint = PMinSize;
inline constexpr auto gravity_hint = PWinGravity;
inline constexpr auto static_gravity = StaticGravity;
inline constexpr auto configure_notify = ConfigureNotify;
inline constexpr auto destroy_notify = DestroyNotify;
inline constexpr auto unmap_notify = UnmapNotify;
inline constexpr auto client_message = ClientMessage;
inline constexpr auto generic_event = GenericEvent;
inline constexpr auto success = Success;
inline constexpr auto selection_notify = SelectionNotify;
inline constexpr auto selection_request = SelectionRequest;
inline constexpr auto property_replace = PropModeReplace;
inline constexpr auto property_notify = PropertyNotify;
inline constexpr auto property_change_mask = PropertyChangeMask;
inline constexpr auto property_new_value = PropertyNewValue;
inline constexpr auto any_property_type = AnyPropertyType;
inline constexpr auto string_atom = XA_STRING;
inline constexpr auto integer_atom = XA_INTEGER;
inline constexpr auto cardinal_atom = XA_CARDINAL;
inline constexpr auto atom_atom = XA_ATOM;
inline constexpr auto pointer_motion_mask = PointerMotionMask;
inline constexpr auto button_press_mask = ButtonPressMask;
inline constexpr auto button_release_mask = ButtonReleaseMask;
inline constexpr auto focus_change_mask = FocusChangeMask;
inline constexpr auto enter_window_mask = EnterWindowMask;
inline constexpr auto leave_window_mask = LeaveWindowMask;
inline constexpr auto key_press_mask = KeyPressMask;
inline constexpr auto key_release_mask = KeyReleaseMask;
inline constexpr auto key_press = KeyPress;
inline constexpr auto key_release = KeyRelease;
inline constexpr auto queued_after_reading = QueuedAfterReading;
inline constexpr auto grab_mode_async = GrabModeAsync;
inline constexpr auto grab_success = GrabSuccess;
inline constexpr auto current_time = CurrentTime;
inline constexpr auto motion_notify = MotionNotify;
inline constexpr auto button_press = ButtonPress;
inline constexpr auto button_release = ButtonRelease;
inline constexpr auto focus_in = FocusIn;
inline constexpr auto focus_out = FocusOut;
inline constexpr auto enter_notify = EnterNotify;
inline constexpr auto leave_notify = LeaveNotify;
inline constexpr auto shift_mask = ShiftMask;
inline constexpr auto control_mask = ControlMask;
inline constexpr auto alt_mask = Mod1Mask;
inline constexpr auto meta_mask = Mod4Mask;
inline constexpr auto notify_grab = NotifyGrab;
inline constexpr auto notify_ungrab = NotifyUngrab;
inline constexpr auto notify_pointer = NotifyPointer, notify_nonlinear = NotifyNonlinear;
inline constexpr auto pointer_root = PointerRoot;
inline constexpr auto revert_to_pointer_root = RevertToPointerRoot;
inline constexpr auto lookup_chars = XLookupChars;
inline constexpr auto lookup_both = XLookupBoth;
inline constexpr auto buffer_overflow = XBufferOverflow;
inline constexpr auto input_style = XIMPreeditNothing | XIMStatusNothing;
inline constexpr auto callback_style = XIMPreeditCallbacks | XIMStatusNothing;
inline constexpr auto callback_none_style = XIMPreeditCallbacks | XIMStatusNone;
inline constexpr auto position_style = XIMPreeditPosition | XIMStatusNothing;
inline constexpr auto position_none_style = XIMPreeditPosition | XIMStatusNone;
inline constexpr auto caret_forward = XIMForwardChar, caret_backward = XIMBackwardChar,
    caret_absolute = XIMAbsolutePosition, caret_unchanged = XIMDontChange;
inline constexpr auto caret_invisible = XIMIsInvisible;
inline constexpr auto preedit_reverse = XIMReverse, preedit_highlight = XIMHighlight;
} // namespace vui_x11_constants

namespace vui_x11_calls {
inline auto styles(XIM im, XIMStyles **styles) { return XGetIMValues(im, XNQueryInputStyle, styles, nullptr); }
inline auto destroy_callback(XIM im, XIMCallback *callback) { return XSetIMValues(im, XNDestroyCallback, callback, nullptr); }
inline auto create_context(XIM im, Window window, XIMCallback *callback) {
    return XCreateIC(im, XNInputStyle, vui_x11_constants::input_style, XNClientWindow, window,
                     XNFocusWindow, window, XNDestroyCallback, callback, nullptr);
}
inline auto create_callback_context(XIM im, Window window, unsigned long style, XIMCallback *destroy,
                                    XIMCallback *start, XIMCallback *done, XIMCallback *draw, XIMCallback *caret) {
    auto attrs = XVaCreateNestedList(0, XNPreeditStartCallback, start, XNPreeditDoneCallback, done,
                                   XNPreeditDrawCallback, draw, XNPreeditCaretCallback, caret, nullptr);
    if (!attrs) return static_cast<XIC>(nullptr);
    auto context = XCreateIC(im, XNInputStyle, style, XNClientWindow, window, XNFocusWindow, window,
                            XNDestroyCallback, destroy, XNPreeditAttributes, attrs, nullptr);
    XFree(attrs);
    return context;
}
inline auto create_fontset(Display *display) {
    char **missing{}; int count{}; char *fallback{};
    auto font = XCreateFontSet(display, "fixed,*", &missing, &count, &fallback);
    if (missing) XFreeStringList(missing);
    return font;
}
inline auto create_position_context(XIM im, Window window, unsigned long style, XIMCallback *destroy,
                                    XFontSet font, XPoint *spot) {
    auto attrs = XVaCreateNestedList(0, XNSpotLocation, spot, XNFontSet, font, nullptr);
    if (!attrs) return static_cast<XIC>(nullptr);
    auto context = XCreateIC(im, XNInputStyle, style, XNClientWindow, window, XNFocusWindow, window,
                            XNDestroyCallback, destroy, XNPreeditAttributes, attrs, nullptr);
    XFree(attrs);
    return context;
}
inline auto set_spot(XIC context, XPoint *spot) -> bool {
    auto attrs = XVaCreateNestedList(0, XNSpotLocation, spot, nullptr);
    if (!attrs) return false;
    auto error = XSetICValues(context, XNPreeditAttributes, attrs, nullptr);
    XFree(attrs);
    return !error;
}
inline auto get_spot(XIC context, XPoint *spot) -> bool {
    XPoint *result{};
    auto attrs = XVaCreateNestedList(0, XNSpotLocation, &result, nullptr);
    if (!attrs) return false;
    auto error = XGetICValues(context, XNPreeditAttributes, attrs, nullptr);
    XFree(attrs);
    if (error || !result) { if (result) XFree(result); return false; }
    *spot = *result; XFree(result); return true;
}
inline auto filter_events(XIC ic, unsigned long *mask) { return XGetICValues(ic, XNFilterEvents, mask, nullptr); }
}

export module vui.host:ffi.x11;

export namespace vui::host::ffi::x11 {
using ::Atom;
using ::Display;
using ::Window;
using ::XCloseDisplay;
using ::XSizeHints;
using ::XSetWMNormalHints;
using ::XGetWMNormalHints;
using ::XTranslateCoordinates;
using ::XConnectionNumber;
using ::XAddConnectionWatch;
using ::XRemoveConnectionWatch;
using ::XProcessInternalConnection;
using ::XInternalConnectionNumbers;
using ::XCreateSimpleWindow;
using ::XDefaultScreen;
using ::XDestroyWindow;
using ::XEvent;
using ::XGetEventData;
using ::XFreeEventData;
using ::XQueryExtension;
using ::XConvertSelection;
using ::XGetSelectionOwner;
using ::XSetSelectionOwner;
using ::XGetWindowProperty;
using ::XDeleteProperty;
using ::XChangeProperty;
using ::XIM;
using ::XIC;
using ::XIMStyles;
using ::XIMText;
using ::XPoint;
using ::XFontSet;
using ::XFreeFontSet;
using ::XIMProc;
using ::XIMPreeditDrawCallbackStruct;
using ::XIMPreeditCaretCallbackStruct;
using ::XIMCallback;
using ::XKeyEvent;
using ::XOpenIM;
using ::XLocaleOfIM;
using ::XCloseIM;
using ::XDestroyIC;
using ::XFree;
using ::XFilterEvent;
using ::XSetICFocus;
using ::XSetInputFocus;
using ::XWarpPointer;
using ::XUnsetICFocus;
using ::Xutf8LookupString;
using ::Xutf8ResetIC;
using ::XSupportsLocale;
using ::XSetLocaleModifiers;
using ::XRegisterIMInstantiateCallback;
using ::XUnregisterIMInstantiateCallback;
using ::XEventsQueued;
using ::XFlush;
using ::XGrabPointer;
using ::XUngrabPointer;
using ::XInternAtom;
using ::XMapWindow;
using ::XIconifyWindow;
using ::XMoveWindow;
using ::XUnmapWindow;
using ::XNextEvent;
using ::XPeekEvent;
using ::XInitThreads;
using ::XOpenDisplay;
using ::XDisplayString;
using ::XPending;
using ::XResizeWindow;
using ::XRootWindow;
using ::XSelectInput;
using ::XSendEvent;
using ::XSetWMProtocols;
using ::XStoreName;
using ::XSync;
using ::XkbLookupKeySym;
using ::XkbSetDetectableAutoRepeat;
using ::XKeysymToKeycode;
using ::vui_x11_constants::client_message;
using ::vui_x11_constants::generic_event;
using ::vui_x11_constants::success;
using ::vui_x11_constants::selection_notify;
using ::vui_x11_constants::selection_request;
using ::vui_x11_constants::property_replace;
using ::vui_x11_constants::property_notify;
using ::vui_x11_constants::property_change_mask;
using ::vui_x11_constants::property_new_value;
using ::vui_x11_constants::any_property_type;
using ::vui_x11_constants::string_atom;
using ::vui_x11_constants::integer_atom;
using ::vui_x11_constants::cardinal_atom;
using ::vui_x11_constants::atom_atom;
using ::vui_x11_constants::configure_notify;
using ::vui_x11_constants::destroy_notify;
using ::vui_x11_constants::unmap_notify;
using ::vui_x11_constants::structure_notify_mask;
using ::vui_x11_constants::substructure_notify_mask;
using ::vui_x11_constants::substructure_redirect_mask;
using ::vui_x11_constants::min_size_hint;
using ::vui_x11_constants::gravity_hint;
using ::vui_x11_constants::static_gravity;
using ::vui_x11_constants::pointer_motion_mask;
using ::vui_x11_constants::button_press_mask;
using ::vui_x11_constants::button_release_mask;
using ::vui_x11_constants::focus_change_mask;
using ::vui_x11_constants::enter_window_mask;
using ::vui_x11_constants::leave_window_mask;
using ::vui_x11_constants::key_press_mask;
using ::vui_x11_constants::key_release_mask;
using ::vui_x11_constants::key_press;
using ::vui_x11_constants::key_release;
using ::vui_x11_constants::queued_after_reading;
using ::vui_x11_constants::grab_mode_async;
using ::vui_x11_constants::grab_success;
using ::vui_x11_constants::current_time;
using ::vui_x11_constants::motion_notify;
using ::vui_x11_constants::button_press;
using ::vui_x11_constants::button_release;
using ::vui_x11_constants::focus_in;
using ::vui_x11_constants::focus_out;
using ::vui_x11_constants::enter_notify;
using ::vui_x11_constants::leave_notify;
using ::vui_x11_constants::shift_mask;
using ::vui_x11_constants::control_mask;
using ::vui_x11_constants::alt_mask;
using ::vui_x11_constants::meta_mask;
using ::vui_x11_constants::notify_grab;
using ::vui_x11_constants::notify_ungrab;
using ::vui_x11_constants::notify_pointer;
using ::vui_x11_constants::notify_nonlinear;
using ::vui_x11_constants::pointer_root;
using ::vui_x11_constants::revert_to_pointer_root;
using ::vui_x11_constants::lookup_chars;
using ::vui_x11_constants::lookup_both;
using ::vui_x11_constants::buffer_overflow;
using ::vui_x11_constants::input_style;
using ::vui_x11_constants::callback_style;
using ::vui_x11_constants::callback_none_style;
using ::vui_x11_constants::position_style;
using ::vui_x11_constants::position_none_style;
using ::vui_x11_constants::caret_forward;
using ::vui_x11_constants::caret_backward;
using ::vui_x11_constants::caret_absolute;
using ::vui_x11_constants::caret_unchanged;
using ::vui_x11_constants::caret_invisible;
using ::vui_x11_constants::preedit_reverse;
using ::vui_x11_constants::preedit_highlight;
using ::vui_x11_calls::styles;
using ::vui_x11_calls::destroy_callback;
using ::vui_x11_calls::create_context;
using ::vui_x11_calls::create_callback_context;
using ::vui_x11_calls::create_fontset;
using ::vui_x11_calls::create_position_context;
using ::vui_x11_calls::set_spot;
using ::vui_x11_calls::get_spot;
using ::vui_x11_calls::filter_events;
} // namespace vui::host::ffi::x11
