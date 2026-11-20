module vui.host;
import :ffi.x11;

namespace ffi {
using namespace rstd::prelude;

auto XlibDispatch::open() -> Option<XlibDispatch> {
    auto opened = rstd::dlopn::Library::open(rstd::ffi::CStr::from_ptr("libX11.so.6"));
    if (opened.is_err())
        return None();
    auto result = XlibDispatch(rstd::move(opened).unwrap());
#define LOAD(name)                                                                                                     \
    {                                                                                                                  \
        auto symbol = result.library_.symbol<decltype(result.name)>(rstd::ffi::CStr::from_ptr(#name));                 \
        if (symbol.is_err())                                                                                           \
            return None();                                                                                             \
        result.name = rstd::move(symbol).unwrap();                                                                     \
        if (!result.name)                                                                                              \
            return None();                                                                                             \
    }
    LOAD(XCloseDisplay);
    LOAD(XSetWMNormalHints);
    LOAD(XGetWMNormalHints);
    LOAD(XTranslateCoordinates);
    LOAD(XConnectionNumber);
    LOAD(XAddConnectionWatch);
    LOAD(XRemoveConnectionWatch);
    LOAD(XProcessInternalConnection);
    LOAD(XInternalConnectionNumbers);
    LOAD(XCreateSimpleWindow);
    LOAD(XDefaultScreen);
    LOAD(XDestroyWindow);
    LOAD(XGetEventData);
    LOAD(XFreeEventData);
    LOAD(XQueryExtension);
    LOAD(XConvertSelection);
    LOAD(XGetSelectionOwner);
    LOAD(XSetSelectionOwner);
    LOAD(XGetWindowProperty);
    LOAD(XDeleteProperty);
    LOAD(XChangeProperty);
    LOAD(XFreeFontSet);
    LOAD(XOpenIM);
    LOAD(XLocaleOfIM);
    LOAD(XCloseIM);
    LOAD(XDestroyIC);
    LOAD(XFree);
    LOAD(XFilterEvent);
    LOAD(XSetICFocus);
    LOAD(XSetInputFocus);
    LOAD(XWarpPointer);
    LOAD(XUnsetICFocus);
    LOAD(Xutf8LookupString);
    LOAD(Xutf8ResetIC);
    LOAD(XSupportsLocale);
    LOAD(XSetLocaleModifiers);
    LOAD(XRegisterIMInstantiateCallback);
    LOAD(XUnregisterIMInstantiateCallback);
    LOAD(XEventsQueued);
    LOAD(XFlush);
    LOAD(XGrabPointer);
    LOAD(XUngrabPointer);
    LOAD(XInternAtom);
    LOAD(XMapWindow);
    LOAD(XIconifyWindow);
    LOAD(XMoveWindow);
    LOAD(XUnmapWindow);
    LOAD(XNextEvent);
    LOAD(XPeekEvent);
    LOAD(XInitThreads);
    LOAD(XOpenDisplay);
    LOAD(XDisplayString);
    LOAD(XPending);
    LOAD(XResizeWindow);
    LOAD(XRootWindow);
    LOAD(XSelectInput);
    LOAD(XSendEvent);
    LOAD(XSetWMProtocols);
    LOAD(XStoreName);
    LOAD(XSync);
    LOAD(XkbLookupKeySym);
    LOAD(XkbSetDetectableAutoRepeat);
    LOAD(XKeysymToKeycode);
    LOAD(XGetIMValues);
    LOAD(XSetIMValues);
    LOAD(XCreateIC);
    LOAD(XVaCreateNestedList);
    LOAD(XCreateFontSet);
    LOAD(XFreeStringList);
    LOAD(XSetICValues);
    LOAD(XGetICValues);
#undef LOAD
    return Some(rstd::move(result));
}

auto XlibDispatch::styles(XIM im, XIMStyles **styles) const -> char * {
    return XGetIMValues(im, XNQueryInputStyle, styles, nullptr);
}
auto XlibDispatch::destroy_callback(XIM im, XIMCallback *callback) const -> char * {
    return XSetIMValues(im, XNDestroyCallback, callback, nullptr);
}
auto XlibDispatch::create_context(XIM im, Window window, XIMCallback *callback) const -> XIC {
    return XCreateIC(im, XNInputStyle, input_style, XNClientWindow, window, XNFocusWindow, window, XNDestroyCallback,
                     callback, nullptr);
}
auto XlibDispatch::create_callback_context(XIM im, Window window, unsigned long style, XIMCallback *destroy,
                                           XIMCallback *start, XIMCallback *done, XIMCallback *draw,
                                           XIMCallback *caret) const -> XIC {
    auto attrs = XVaCreateNestedList(0, XNPreeditStartCallback, start, XNPreeditDoneCallback, done,
                                     XNPreeditDrawCallback, draw, XNPreeditCaretCallback, caret, nullptr);
    if (!attrs)
        return static_cast<XIC>(nullptr);
    auto context = XCreateIC(im, XNInputStyle, style, XNClientWindow, window, XNFocusWindow, window, XNDestroyCallback,
                             destroy, XNPreeditAttributes, attrs, nullptr);
    XFree(attrs);
    return context;
}
auto XlibDispatch::create_fontset(Display *display) const -> XFontSet {
    char **missing{};
    int count{};
    char *fallback{};
    auto font = XCreateFontSet(display, "fixed,*", &missing, &count, &fallback);
    if (missing)
        XFreeStringList(missing);
    return font;
}
auto XlibDispatch::create_position_context(XIM im, Window window, unsigned long style, XIMCallback *destroy,
                                           XFontSet font, XPoint *spot) const -> XIC {
    auto attrs = XVaCreateNestedList(0, XNSpotLocation, spot, XNFontSet, font, nullptr);
    if (!attrs)
        return static_cast<XIC>(nullptr);
    auto context = XCreateIC(im, XNInputStyle, style, XNClientWindow, window, XNFocusWindow, window, XNDestroyCallback,
                             destroy, XNPreeditAttributes, attrs, nullptr);
    XFree(attrs);
    return context;
}
auto XlibDispatch::set_spot(XIC context, XPoint *spot) const -> bool {
    auto attrs = XVaCreateNestedList(0, XNSpotLocation, spot, nullptr);
    if (!attrs)
        return false;
    auto error = XSetICValues(context, XNPreeditAttributes, attrs, nullptr);
    XFree(attrs);
    return !error;
}
auto XlibDispatch::get_spot(XIC context, XPoint *spot) const -> bool {
    XPoint *result{};
    auto attrs = XVaCreateNestedList(0, XNSpotLocation, &result, nullptr);
    if (!attrs)
        return false;
    auto error = XGetICValues(context, XNPreeditAttributes, attrs, nullptr);
    XFree(attrs);
    if (error || !result) {
        if (result)
            XFree(result);
        return false;
    }
    *spot = *result;
    XFree(result);
    return true;
}
auto XlibDispatch::filter_events(XIC ic, unsigned long *mask) const -> char * {
    return XGetICValues(ic, XNFilterEvents, mask, nullptr);
}
} // namespace ffi
