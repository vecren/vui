module;
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

inline constexpr auto alias_AnyPropertyType = AnyPropertyType;
inline constexpr auto alias_ButtonPress = ButtonPress;
inline constexpr auto alias_ButtonPressMask = ButtonPressMask;
inline constexpr auto alias_ButtonRelease = ButtonRelease;
inline constexpr auto alias_ButtonReleaseMask = ButtonReleaseMask;
inline constexpr auto alias_ClientMessage = ClientMessage;
inline constexpr auto alias_ConfigureNotify = ConfigureNotify;
inline constexpr auto alias_ControlMask = ControlMask;
inline constexpr auto alias_CurrentTime = CurrentTime;
inline constexpr auto alias_DestroyNotify = DestroyNotify;
inline constexpr auto alias_EnterNotify = EnterNotify;
inline constexpr auto alias_EnterWindowMask = EnterWindowMask;
inline constexpr auto alias_FocusChangeMask = FocusChangeMask;
inline constexpr auto alias_FocusIn = FocusIn;
inline constexpr auto alias_FocusOut = FocusOut;
inline constexpr auto alias_GenericEvent = GenericEvent;
inline constexpr auto alias_GrabModeAsync = GrabModeAsync;
inline constexpr auto alias_GrabSuccess = GrabSuccess;
inline constexpr auto alias_KeyPress = KeyPress;
inline constexpr auto alias_KeyPressMask = KeyPressMask;
inline constexpr auto alias_KeyRelease = KeyRelease;
inline constexpr auto alias_KeyReleaseMask = KeyReleaseMask;
inline constexpr auto alias_LeaveNotify = LeaveNotify;
inline constexpr auto alias_LeaveWindowMask = LeaveWindowMask;
inline constexpr auto alias_Mod1Mask = Mod1Mask;
inline constexpr auto alias_Mod4Mask = Mod4Mask;
inline constexpr auto alias_MotionNotify = MotionNotify;
inline constexpr auto alias_NotifyGrab = NotifyGrab;
inline constexpr auto alias_NotifyNonlinear = NotifyNonlinear;
inline constexpr auto alias_NotifyPointer = NotifyPointer;
inline constexpr auto alias_NotifyUngrab = NotifyUngrab;
inline constexpr auto alias_PMinSize = PMinSize;
inline constexpr auto alias_PWinGravity = PWinGravity;
inline constexpr auto alias_PointerMotionMask = PointerMotionMask;
inline constexpr auto alias_PointerRoot = PointerRoot;
inline constexpr auto alias_PropModeReplace = PropModeReplace;
inline constexpr auto alias_PropertyChangeMask = PropertyChangeMask;
inline constexpr auto alias_PropertyNewValue = PropertyNewValue;
inline constexpr auto alias_PropertyNotify = PropertyNotify;
inline constexpr auto alias_QueuedAfterReading = QueuedAfterReading;
inline constexpr auto alias_RevertToPointerRoot = RevertToPointerRoot;
inline constexpr auto alias_SelectionNotify = SelectionNotify;
inline constexpr auto alias_SelectionRequest = SelectionRequest;
inline constexpr auto alias_ShiftMask = ShiftMask;
inline constexpr auto alias_StaticGravity = StaticGravity;
inline constexpr auto alias_StructureNotifyMask = StructureNotifyMask;
inline constexpr auto alias_SubstructureNotifyMask = SubstructureNotifyMask;
inline constexpr auto alias_SubstructureRedirectMask = SubstructureRedirectMask;
inline constexpr auto alias_Success = Success;
inline constexpr auto alias_UnmapNotify = UnmapNotify;
inline constexpr auto alias_XA_ATOM = XA_ATOM;
inline constexpr auto alias_XA_CARDINAL = XA_CARDINAL;
inline constexpr auto alias_XA_INTEGER = XA_INTEGER;
inline constexpr auto alias_XA_STRING = XA_STRING;
inline constexpr auto alias_XBufferOverflow = XBufferOverflow;
inline constexpr auto alias_XIMHighlight = XIMHighlight;
inline constexpr auto alias_XIMReverse = XIMReverse;
inline constexpr auto alias_XLookupBoth = XLookupBoth;
inline constexpr auto alias_XLookupChars = XLookupChars;

#undef AnyPropertyType
#undef ButtonPress
#undef ButtonPressMask
#undef ButtonRelease
#undef ButtonReleaseMask
#undef ClientMessage
#undef ConfigureNotify
#undef ControlMask
#undef CurrentTime
#undef DestroyNotify
#undef EnterNotify
#undef EnterWindowMask
#undef FocusChangeMask
#undef FocusIn
#undef FocusOut
#undef GenericEvent
#undef GrabModeAsync
#undef GrabSuccess
#undef KeyPress
#undef KeyPressMask
#undef KeyRelease
#undef KeyReleaseMask
#undef LeaveNotify
#undef LeaveWindowMask
#undef Mod1Mask
#undef Mod4Mask
#undef MotionNotify
#undef NotifyGrab
#undef NotifyNonlinear
#undef NotifyPointer
#undef NotifyUngrab
#undef PMinSize
#undef PWinGravity
#undef PointerMotionMask
#undef PointerRoot
#undef PropModeReplace
#undef PropertyChangeMask
#undef PropertyNewValue
#undef PropertyNotify
#undef QueuedAfterReading
#undef RevertToPointerRoot
#undef SelectionNotify
#undef SelectionRequest
#undef ShiftMask
#undef StaticGravity
#undef StructureNotifyMask
#undef SubstructureNotifyMask
#undef SubstructureRedirectMask
#undef Success
#undef UnmapNotify
#undef XA_ATOM
#undef XA_CARDINAL
#undef XA_INTEGER
#undef XA_STRING
#undef XBufferOverflow
#undef XIMHighlight
#undef XIMReverse
#undef XLookupBoth
#undef XLookupChars

module vui.host:ffi.x11;
namespace ffi {
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
using ::XIMForwardChar;
using ::XIMBackwardChar;
using ::XIMAbsolutePosition;
using ::XIMDontChange;
using ::XIMIsInvisible;

inline constexpr auto AnyPropertyType = alias_AnyPropertyType;
inline constexpr auto ButtonPress = alias_ButtonPress;
inline constexpr auto ButtonPressMask = alias_ButtonPressMask;
inline constexpr auto ButtonRelease = alias_ButtonRelease;
inline constexpr auto ButtonReleaseMask = alias_ButtonReleaseMask;
inline constexpr auto ClientMessage = alias_ClientMessage;
inline constexpr auto ConfigureNotify = alias_ConfigureNotify;
inline constexpr auto ControlMask = alias_ControlMask;
inline constexpr auto CurrentTime = alias_CurrentTime;
inline constexpr auto DestroyNotify = alias_DestroyNotify;
inline constexpr auto EnterNotify = alias_EnterNotify;
inline constexpr auto EnterWindowMask = alias_EnterWindowMask;
inline constexpr auto FocusChangeMask = alias_FocusChangeMask;
inline constexpr auto FocusIn = alias_FocusIn;
inline constexpr auto FocusOut = alias_FocusOut;
inline constexpr auto GenericEvent = alias_GenericEvent;
inline constexpr auto GrabModeAsync = alias_GrabModeAsync;
inline constexpr auto GrabSuccess = alias_GrabSuccess;
inline constexpr auto KeyPress = alias_KeyPress;
inline constexpr auto KeyPressMask = alias_KeyPressMask;
inline constexpr auto KeyRelease = alias_KeyRelease;
inline constexpr auto KeyReleaseMask = alias_KeyReleaseMask;
inline constexpr auto LeaveNotify = alias_LeaveNotify;
inline constexpr auto LeaveWindowMask = alias_LeaveWindowMask;
inline constexpr auto Mod1Mask = alias_Mod1Mask;
inline constexpr auto Mod4Mask = alias_Mod4Mask;
inline constexpr auto MotionNotify = alias_MotionNotify;
inline constexpr auto NotifyGrab = alias_NotifyGrab;
inline constexpr auto NotifyNonlinear = alias_NotifyNonlinear;
inline constexpr auto NotifyPointer = alias_NotifyPointer;
inline constexpr auto NotifyUngrab = alias_NotifyUngrab;
inline constexpr auto PMinSize = alias_PMinSize;
inline constexpr auto PWinGravity = alias_PWinGravity;
inline constexpr auto PointerMotionMask = alias_PointerMotionMask;
inline constexpr auto PointerRoot = alias_PointerRoot;
inline constexpr auto PropModeReplace = alias_PropModeReplace;
inline constexpr auto PropertyChangeMask = alias_PropertyChangeMask;
inline constexpr auto PropertyNewValue = alias_PropertyNewValue;
inline constexpr auto PropertyNotify = alias_PropertyNotify;
inline constexpr auto QueuedAfterReading = alias_QueuedAfterReading;
inline constexpr auto RevertToPointerRoot = alias_RevertToPointerRoot;
inline constexpr auto SelectionNotify = alias_SelectionNotify;
inline constexpr auto SelectionRequest = alias_SelectionRequest;
inline constexpr auto ShiftMask = alias_ShiftMask;
inline constexpr auto StaticGravity = alias_StaticGravity;
inline constexpr auto StructureNotifyMask = alias_StructureNotifyMask;
inline constexpr auto SubstructureNotifyMask = alias_SubstructureNotifyMask;
inline constexpr auto SubstructureRedirectMask = alias_SubstructureRedirectMask;
inline constexpr auto Success = alias_Success;
inline constexpr auto UnmapNotify = alias_UnmapNotify;
inline constexpr auto XA_ATOM = alias_XA_ATOM;
inline constexpr auto XA_CARDINAL = alias_XA_CARDINAL;
inline constexpr auto XA_INTEGER = alias_XA_INTEGER;
inline constexpr auto XA_STRING = alias_XA_STRING;
inline constexpr auto XBufferOverflow = alias_XBufferOverflow;
inline constexpr auto XIMHighlight = alias_XIMHighlight;
inline constexpr auto XIMReverse = alias_XIMReverse;
inline constexpr auto XLookupBoth = alias_XLookupBoth;
inline constexpr auto XLookupChars = alias_XLookupChars;

inline constexpr auto input_style = XIMPreeditNothing | XIMStatusNothing;
inline constexpr auto callback_style = XIMPreeditCallbacks | XIMStatusNothing;
inline constexpr auto callback_none_style = XIMPreeditCallbacks | XIMStatusNone;
inline constexpr auto position_style = XIMPreeditPosition | XIMStatusNothing;
inline constexpr auto position_none_style = XIMPreeditPosition | XIMStatusNone;

inline auto styles(XIM im, XIMStyles **styles) { return XGetIMValues(im, XNQueryInputStyle, styles, nullptr); }
inline auto destroy_callback(XIM im, XIMCallback *callback) { return XSetIMValues(im, XNDestroyCallback, callback, nullptr); }
inline auto create_context(XIM im, Window window, XIMCallback *callback) {
    return XCreateIC(im, XNInputStyle, input_style, XNClientWindow, window,
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
