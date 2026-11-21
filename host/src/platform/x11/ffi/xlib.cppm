module;
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
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

inline constexpr auto alias_XNQueryInputStyle = XNQueryInputStyle;
inline constexpr auto alias_XNDestroyCallback = XNDestroyCallback;
inline constexpr auto alias_XNInputStyle = XNInputStyle;
inline constexpr auto alias_XNClientWindow = XNClientWindow;
inline constexpr auto alias_XNFocusWindow = XNFocusWindow;
inline constexpr auto alias_XNPreeditStartCallback = XNPreeditStartCallback;
inline constexpr auto alias_XNPreeditDoneCallback = XNPreeditDoneCallback;
inline constexpr auto alias_XNPreeditDrawCallback = XNPreeditDrawCallback;
inline constexpr auto alias_XNPreeditCaretCallback = XNPreeditCaretCallback;
inline constexpr auto alias_XNPreeditAttributes = XNPreeditAttributes;
inline constexpr auto alias_XNSpotLocation = XNSpotLocation;
inline constexpr auto alias_XNFontSet = XNFontSet;
inline constexpr auto alias_XNFilterEvents = XNFilterEvents;

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

#undef XNQueryInputStyle
#undef XNDestroyCallback
#undef XNInputStyle
#undef XNClientWindow
#undef XNFocusWindow
#undef XNPreeditStartCallback
#undef XNPreeditDoneCallback
#undef XNPreeditDrawCallback
#undef XNPreeditCaretCallback
#undef XNPreeditAttributes
#undef XNSpotLocation
#undef XNFontSet
#undef XNFilterEvents

module vui.host:platform.x11.ffi.xlib;
import rstd.dlopn;
namespace ffi {
using ::Atom;
using ::Display;
using ::Window;
using ::XEvent;
using ::XFontSet;
using ::XIC;
using ::XIM;
using ::XIMAbsolutePosition;
using ::XIMBackwardChar;
using ::XIMCallback;
using ::XIMDontChange;
using ::XIMForwardChar;
using ::XIMIsInvisible;
using ::XIMPreeditCaretCallbackStruct;
using ::XIMPreeditDrawCallbackStruct;
using ::XIMProc;
using ::XIMStyles;
using ::XIMText;
using ::XKeyEvent;
using ::XPoint;
using ::XSizeHints;

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

inline constexpr auto XNQueryInputStyle = alias_XNQueryInputStyle;
inline constexpr auto XNDestroyCallback = alias_XNDestroyCallback;
inline constexpr auto XNInputStyle = alias_XNInputStyle;
inline constexpr auto XNClientWindow = alias_XNClientWindow;
inline constexpr auto XNFocusWindow = alias_XNFocusWindow;
inline constexpr auto XNPreeditStartCallback = alias_XNPreeditStartCallback;
inline constexpr auto XNPreeditDoneCallback = alias_XNPreeditDoneCallback;
inline constexpr auto XNPreeditDrawCallback = alias_XNPreeditDrawCallback;
inline constexpr auto XNPreeditCaretCallback = alias_XNPreeditCaretCallback;
inline constexpr auto XNPreeditAttributes = alias_XNPreeditAttributes;
inline constexpr auto XNSpotLocation = alias_XNSpotLocation;
inline constexpr auto XNFontSet = alias_XNFontSet;
inline constexpr auto XNFilterEvents = alias_XNFilterEvents;

class XlibDispatch {
    rstd::dlopn::Library library_;
    explicit XlibDispatch(rstd::dlopn::Library library) : library_(rstd::move(library)) {}

  public:
    XlibDispatch(const XlibDispatch &) = delete;
    XlibDispatch(XlibDispatch &&) = default;
    auto operator=(const XlibDispatch &) -> XlibDispatch & = delete;
    auto operator=(XlibDispatch &&) -> XlibDispatch & = default;
    static auto open() -> rstd::Option<XlibDispatch>;
    decltype(&::XCloseDisplay) XCloseDisplay{};
    decltype(&::XSetWMNormalHints) XSetWMNormalHints{};
    decltype(&::XGetWMNormalHints) XGetWMNormalHints{};
    decltype(&::XTranslateCoordinates) XTranslateCoordinates{};
    decltype(&::XConnectionNumber) XConnectionNumber{};
    decltype(&::XAddConnectionWatch) XAddConnectionWatch{};
    decltype(&::XRemoveConnectionWatch) XRemoveConnectionWatch{};
    decltype(&::XProcessInternalConnection) XProcessInternalConnection{};
    decltype(&::XInternalConnectionNumbers) XInternalConnectionNumbers{};
    decltype(&::XCreateSimpleWindow) XCreateSimpleWindow{};
    decltype(&::XDefaultScreen) XDefaultScreen{};
    decltype(&::XDestroyWindow) XDestroyWindow{};
    decltype(&::XGetEventData) XGetEventData{};
    decltype(&::XFreeEventData) XFreeEventData{};
    decltype(&::XQueryExtension) XQueryExtension{};
    decltype(&::XConvertSelection) XConvertSelection{};
    decltype(&::XGetSelectionOwner) XGetSelectionOwner{};
    decltype(&::XSetSelectionOwner) XSetSelectionOwner{};
    decltype(&::XGetWindowProperty) XGetWindowProperty{};
    decltype(&::XDeleteProperty) XDeleteProperty{};
    decltype(&::XChangeProperty) XChangeProperty{};
    decltype(&::XFreeFontSet) XFreeFontSet{};
    decltype(&::XOpenIM) XOpenIM{};
    decltype(&::XLocaleOfIM) XLocaleOfIM{};
    decltype(&::XCloseIM) XCloseIM{};
    decltype(&::XDestroyIC) XDestroyIC{};
    decltype(&::XFree) XFree{};
    decltype(&::XFilterEvent) XFilterEvent{};
    decltype(&::XSetICFocus) XSetICFocus{};
    decltype(&::XSetInputFocus) XSetInputFocus{};
    decltype(&::XWarpPointer) XWarpPointer{};
    decltype(&::XUnsetICFocus) XUnsetICFocus{};
    decltype(&::Xutf8LookupString) Xutf8LookupString{};
    decltype(&::Xutf8ResetIC) Xutf8ResetIC{};
    decltype(&::XSupportsLocale) XSupportsLocale{};
    decltype(&::XSetLocaleModifiers) XSetLocaleModifiers{};
    decltype(&::XRegisterIMInstantiateCallback) XRegisterIMInstantiateCallback{};
    decltype(&::XUnregisterIMInstantiateCallback) XUnregisterIMInstantiateCallback{};
    decltype(&::XEventsQueued) XEventsQueued{};
    decltype(&::XFlush) XFlush{};
    decltype(&::XGrabPointer) XGrabPointer{};
    decltype(&::XUngrabPointer) XUngrabPointer{};
    decltype(&::XInternAtom) XInternAtom{};
    decltype(&::XMapWindow) XMapWindow{};
    decltype(&::XIconifyWindow) XIconifyWindow{};
    decltype(&::XMoveWindow) XMoveWindow{};
    decltype(&::XUnmapWindow) XUnmapWindow{};
    decltype(&::XNextEvent) XNextEvent{};
    decltype(&::XPeekEvent) XPeekEvent{};
    decltype(&::XInitThreads) XInitThreads{};
    decltype(&::XOpenDisplay) XOpenDisplay{};
    decltype(&::XDisplayString) XDisplayString{};
    decltype(&::XPending) XPending{};
    decltype(&::XResizeWindow) XResizeWindow{};
    decltype(&::XRootWindow) XRootWindow{};
    decltype(&::XSelectInput) XSelectInput{};
    decltype(&::XSendEvent) XSendEvent{};
    decltype(&::XSetWMProtocols) XSetWMProtocols{};
    decltype(&::XStoreName) XStoreName{};
    decltype(&::XSync) XSync{};
    decltype(&::XkbLookupKeySym) XkbLookupKeySym{};
    decltype(&::XkbSetDetectableAutoRepeat) XkbSetDetectableAutoRepeat{};
    decltype(&::XKeysymToKeycode) XKeysymToKeycode{};
    decltype(&::XGetIMValues) XGetIMValues{};
    decltype(&::XSetIMValues) XSetIMValues{};
    decltype(&::XCreateIC) XCreateIC{};
    decltype(&::XVaCreateNestedList) XVaCreateNestedList{};
    decltype(&::XCreateFontSet) XCreateFontSet{};
    decltype(&::XFreeStringList) XFreeStringList{};
    decltype(&::XSetICValues) XSetICValues{};
    decltype(&::XGetICValues) XGetICValues{};
    auto styles(XIM im, XIMStyles **styles) const -> char *;
    auto destroy_callback(XIM im, XIMCallback *callback) const -> char *;
    auto create_context(XIM im, Window window, XIMCallback *callback) const -> XIC;
    auto create_callback_context(XIM im, Window window, unsigned long style, XIMCallback *destroy, XIMCallback *start,
                                 XIMCallback *done, XIMCallback *draw, XIMCallback *caret) const -> XIC;
    auto create_fontset(Display *display) const -> XFontSet;
    auto create_position_context(XIM im, Window window, unsigned long style, XIMCallback *destroy, XFontSet font,
                                 XPoint *spot) const -> XIC;
    auto set_spot(XIC context, XPoint *spot) const -> bool;
    auto get_spot(XIC context, XPoint *spot) const -> bool;
    auto filter_events(XIC ic, unsigned long *mask) const -> char *;
};
} // namespace ffi
