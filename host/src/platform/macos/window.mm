#include "native.hpp"
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CAMetalLayer.h>
#include <dispatch/dispatch.h>
#include <math.h>
#include <new>
#include <pthread.h>

asm(".linker_option \"-framework\", \"AppKit\"");
asm(".linker_option \"-framework\", \"QuartzCore\"");
asm(".linker_option \"-framework\", \"CoreGraphics\"");

@interface VuiMacosView : NSView <NSTextInputClient> {
  @public
    ffi::MacosWindow *data_;
    NSTrackingArea *tracking_;
    NSString *marked_;
    NSRange marked_selection_;
    NSUInteger marked_origin_;
}
@end

@interface VuiMacosDelegate : NSObject <NSWindowDelegate> {
  @public
    ffi::MacosWindow *data_;
}
- (void)displayChanged:(NSNotification *)notification;
@end

@interface VuiMacosApplicationDelegate : NSObject <NSApplicationDelegate>
@end

namespace ffi {
namespace {
auto native_string(MacosBytes text) -> NSString * {
    if ((!text.data && text.size) || text.size > UINT32_MAX)
        return nil;
    return [[[NSString alloc] initWithBytes:text.data ? text.data : "" length:text.size
                                   encoding:NSUTF8StringEncoding] autorelease];
}
auto native_boundary(NSString *text, NSUInteger offset) -> bool {
    if (!text || offset > text.length)
        return false;
    if (offset && offset < text.length) {
        auto before = [text characterAtIndex:offset - 1], after = [text characterAtIndex:offset];
        if (before >= 0xd800 && before <= 0xdbff && after >= 0xdc00 && after <= 0xdfff)
            return false;
    }
    return true;
}
auto byte_offset(NSString *text, NSUInteger offset, uint32_t &output) -> bool {
    if (!native_boundary(text, offset))
        return false;
    NSData *encoded = [[text substringToIndex:offset] dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    if (!encoded || encoded.length > UINT32_MAX)
        return false;
    output = uint32_t(encoded.length);
    return true;
}
auto native_offset(NSString *text, uint32_t offset) -> NSUInteger {
    NSData *encoded = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    if (!encoded || offset > encoded.length)
        return NSNotFound;
    NSString *prefix = [[[NSString alloc] initWithBytes:encoded.bytes length:offset
                                               encoding:NSUTF8StringEncoding] autorelease];
    return prefix ? prefix.length : NSNotFound;
}
auto bytes(NSData *data) -> MacosBytes { return {static_cast<const char *>(data.bytes), data.length}; }
auto modifiers(NSEventModifierFlags flags) -> unsigned {
    return unsigned(bool(flags & NSEventModifierFlagShift)) |
           (unsigned(bool(flags & NSEventModifierFlagControl)) << 1) |
           (unsigned(bool(flags & NSEventModifierFlagOption)) << 2) |
           (unsigned(bool(flags & NSEventModifierFlagCommand)) << 3);
}
auto phase(NSEventPhase value) -> MacosScrollPhase {
    if (value & (NSEventPhaseBegan | NSEventPhaseMayBegin))
        return MacosScrollPhase::Begin;
    if (value & (NSEventPhaseEnded | NSEventPhaseCancelled))
        return MacosScrollPhase::End;
    if (value & (NSEventPhaseChanged | NSEventPhaseStationary))
        return MacosScrollPhase::Update;
    return MacosScrollPhase::Unknown;
}
auto screen_top() -> CGFloat {
    NSScreen *screen = [[NSScreen screens] firstObject];
    return screen ? NSMaxY(screen.frame) : 0;
}
auto pixel_extent(CGFloat value) -> uint32_t {
    return isfinite(value) && value > 0 && value <= UINT32_MAX ? uint32_t(value + .5) : 0;
}
} // namespace

struct MacosWindow {
    void *context{};
    MacosCallbacks callbacks;
    NSWindow *window{};
    VuiMacosView *view{};
    VuiMacosDelegate *delegate{};
    CAMetalLayer *layer{};
    NSString *surrounding{};
    MacosTextState text_state;
    MacosGeometry dimensions;
    bool active{true}, cancelling_text{}, pointer_inside{}, focused{};
    MacosWindow *retired_next{};
    void *retired_owner{};
    void (*destroy_owner)(void *){};

    ~MacosWindow() {
        @autoreleasepool {
            if (view)
                view->data_ = nullptr;
            if (delegate)
                delegate->data_ = nullptr;
            if (delegate)
                [[NSNotificationCenter defaultCenter] removeObserver:delegate];
            [window setDelegate:nil];
            [layer setDelegate:nil];
            [view setLayer:nil];
            [window setContentView:nil];
            [window orderOut:nil];
            [window close];
            [view release];
            [delegate release];
            [layer release];
            [window release];
            [surrounding release];
        }
    }
    auto is_active() const -> bool { return __atomic_load_n(&active, __ATOMIC_ACQUIRE); }
    void emit(MacosEvent event, NSEvent *native = nil) {
        if (!is_active() || !callbacks.event)
            return;
        if (native && isfinite(native.timestamp) && native.timestamp >= 0)
            event.timestamp = native.timestamp;
        callbacks.event(context, event);
    }
    void geometry() {
        if (!window || !view)
            return;
        auto bounds = view.bounds, backing = [view convertRectToBacking:bounds];
        auto scale = window.backingScaleFactor;
        if (!isfinite(scale) || scale <= 0)
            scale = 1;
        auto content = [window contentRectForFrameRect:window.frame];
        dimensions.width = bounds.size.width;
        dimensions.height = bounds.size.height;
        dimensions.x = content.origin.x;
        dimensions.y = screen_top() - NSMaxY(content);
        dimensions.scale = scale;
        dimensions.pixel_width = pixel_extent(backing.size.width);
        dimensions.pixel_height = pixel_extent(backing.size.height);
        dimensions.minimized = window.miniaturized;
        dimensions.suspended = dimensions.minimized || !(window.occlusionState & NSWindowOcclusionStateVisible) ||
                               !dimensions.pixel_width || !dimensions.pixel_height;
        dimensions.fullscreen = bool(window.styleMask & NSWindowStyleMaskFullScreen);
        [layer setContentsScale:scale];
        [layer setDrawableSize:CGSizeMake(dimensions.pixel_width, dimensions.pixel_height)];
        if (is_active() && callbacks.geometry)
            callbacks.geometry(context, dimensions);
    }
    void pointer(NSEvent *native, MacosEventKind kind) {
        auto point = [view convertPoint:native.locationInWindow fromView:nil];
        MacosEvent event;
        event.kind = kind;
        event.x = point.x;
        event.y = point.y;
        event.modifiers = modifiers(native.modifierFlags);
        event.button = int(native.buttonNumber);
        if (!pointer_inside && kind != MacosEventKind::Leave) {
            pointer_inside = true;
            MacosEvent entered = event;
            entered.kind = MacosEventKind::Enter;
            emit(entered, native);
        }
        if (kind == MacosEventKind::Leave)
            pointer_inside = false;
        emit(event, native);
    }
    void focus(bool value) {
        if (focused == value)
            return;
        if (!value)
            cancel_text();
        focused = value;
        MacosEvent event;
        event.kind = value ? MacosEventKind::FocusGained : MacosEventKind::FocusLost;
        emit(event);
    }
    auto selected() const -> NSRange {
        if (!text_state.enabled || !text_state.surrounding_present)
            return NSMakeRange(NSNotFound, 0);
        auto anchor = native_offset(surrounding, text_state.anchor),
             cursor = native_offset(surrounding, text_state.cursor);
        if (anchor == NSNotFound || cursor == NSNotFound)
            return NSMakeRange(NSNotFound, 0);
        return NSMakeRange(anchor < cursor ? anchor : cursor, anchor < cursor ? cursor - anchor : anchor - cursor);
    }
    auto displayed() const -> NSString * {
        NSString *source = surrounding ? surrounding : @"";
        auto range = selected();
        if (!view->marked_ || !view->marked_.length || range.location == NSNotFound ||
            NSMaxRange(range) > source.length)
            return source;
        NSMutableString *result = [[source mutableCopy] autorelease];
        [result replaceCharactersInRange:range withString:view->marked_];
        return result;
    }
    auto replacement(MacosTextTransaction &transaction, NSRange range) -> bool {
        if (range.location == NSNotFound)
            return true;
        if (range.length > NSUIntegerMax - range.location)
            return false;
        if (view->marked_ && view->marked_.length && range.location >= view->marked_origin_ &&
            NSMaxRange(range) <= view->marked_origin_ + view->marked_.length)
            return native_boundary(view->marked_, range.location - view->marked_origin_) &&
                   native_boundary(view->marked_, NSMaxRange(range) - view->marked_origin_);
        if (!text_state.surrounding_present)
            return range.length == 0;
        auto selection = selected();
        if (selection.location == NSNotFound || range.location > selection.location ||
            NSMaxRange(range) < NSMaxRange(selection))
            return false;
        uint32_t start{}, end{};
        if (!byte_offset(surrounding, range.location, start) || !byte_offset(surrounding, NSMaxRange(range), end))
            return false;
        auto low = text_state.anchor < text_state.cursor ? text_state.anchor : text_state.cursor;
        auto high = text_state.anchor > text_state.cursor ? text_state.anchor : text_state.cursor;
        if (start > low || end < high)
            return false;
        transaction.delete_before = low - start;
        transaction.delete_after = end - high;
        transaction.has_expected = true;
        transaction.expected_text = text_state.text;
        transaction.expected_anchor = text_state.anchor;
        transaction.expected_cursor = text_state.cursor;
        return true;
    }
    void publish(MacosTextTransaction transaction) {
        if (!text_state.enabled || cancelling_text || !is_active() || !callbacks.text)
            return;
        transaction.session = text_state.session;
        callbacks.text(context, transaction);
    }
    void clear_marked() {
        if (!view)
            return;
        [view->marked_ release];
        view->marked_ = nil;
        view->marked_selection_ = NSMakeRange(0, 0);
        view->marked_origin_ = 0;
    }
    void cancel_text() {
        if (!view)
            return;
        if (view->marked_.length && text_state.enabled) {
            MacosTextTransaction value;
            value.has_preedit = true;
            publish(value);
        }
        cancelling_text = true;
        [[view inputContext] discardMarkedText];
        clear_marked();
        cancelling_text = false;
    }
};

namespace {
pthread_mutex_t retired_mutex = PTHREAD_MUTEX_INITIALIZER;
MacosWindow *retired_head{};
void cleanup(MacosWindow *window) {
    auto owner = window->retired_owner;
    auto destroy = window->destroy_owner;
    delete window;
    if (destroy)
        destroy(owner);
}
void dispatch_event(NSEvent *event) {
    if (event.type == NSEventTypeKeyUp && (event.modifierFlags & NSEventModifierFlagCommand)) {
        NSWindow *window = event.window;
        if (!window)
            window = [NSApp keyWindow];
        NSView *content = window.contentView;
        if ([content isKindOfClass:[VuiMacosView class]]) {
            auto *owner = ((VuiMacosView *)content)->data_;
            if (owner && owner->window == window && owner->is_active()) {
                // NSApplication consumes Command key releases before the window receives them.
                [window sendEvent:event];
                return;
            }
        }
    }
    [NSApp sendEvent:event];
}
auto application() -> bool {
    if (![NSThread isMainThread])
        return false;
    static VuiMacosApplicationDelegate *delegate{};
    [NSApplication sharedApplication];
    if (!delegate) {
        delegate = [[VuiMacosApplicationDelegate alloc] init];
        [NSApp setDelegate:delegate];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSMenu *menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
        NSMenuItem *app = [[[NSMenuItem alloc] initWithTitle:@"" action:nullptr keyEquivalent:@""] autorelease];
        NSMenu *actions = [[[NSMenu alloc] initWithTitle:@""] autorelease];
        [actions addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
        [app setSubmenu:actions];
        [menu addItem:app];
        [NSApp setMainMenu:menu];
        [NSApp finishLaunching];
    }
    return true;
}
} // namespace

auto macos_on_main_thread() -> bool { return [NSThread isMainThread]; }
void macos_drain_retired() {
    if (!macos_on_main_thread())
        return;
    pthread_mutex_lock(&retired_mutex);
    auto pending = retired_head;
    retired_head = nullptr;
    pthread_mutex_unlock(&retired_mutex);
    while (pending) {
        auto next = pending->retired_next;
        cleanup(pending);
        pending = next;
    }
}
void macos_destroy(MacosWindow *window, void *owner, void (*destroy_owner)(void *)) {
    if (!window) {
        if (destroy_owner)
            destroy_owner(owner);
        return;
    }
    __atomic_store_n(&window->active, false, __ATOMIC_RELEASE);
    window->retired_owner = owner;
    window->destroy_owner = destroy_owner;
    if (macos_on_main_thread()) {
        cleanup(window);
        macos_drain_retired();
        return;
    }
    pthread_mutex_lock(&retired_mutex);
    window->retired_next = retired_head;
    retired_head = window;
    pthread_mutex_unlock(&retired_mutex);
    dispatch_async_f(dispatch_get_main_queue(), nullptr, [](void *) { macos_drain_retired(); });
}
auto macos_post_empty_event() -> bool {
    @autoreleasepool {
        if (!NSApp)
            return false;
        NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                            location:NSZeroPoint
                                       modifierFlags:0
                                           timestamp:0
                                        windowNumber:0
                                             context:nil
                                             subtype:0
                                               data1:0
                                               data2:0];
        if (!event)
            return false;
        [NSApp postEvent:event atStart:NO];
        return true;
    }
}
auto macos_open(const char *title, uint32_t width, uint32_t height, void *context, MacosCallbacks callbacks)
    -> MacosWindow * {
    if (!macos_on_main_thread() || !title || !width || !height || width > 32768 || height > 32768)
        return nullptr;
    macos_drain_retired();
    @autoreleasepool {
        if (!application())
            return nullptr;
        NSString *name = [NSString stringWithUTF8String:title];
        if (!name)
            return nullptr;
        auto data = new (std::nothrow_t{}) MacosWindow;
        if (!data)
            return nullptr;
        data->context = context;
        data->callbacks = callbacks;
        data->window =
            [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        if (!data->window) {
            delete data;
            return nullptr;
        }
        [data->window setReleasedWhenClosed:NO];
        [data->window setTitle:name];
        [data->window setAcceptsMouseMovedEvents:YES];
        data->view = [[VuiMacosView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
        data->delegate = [[VuiMacosDelegate alloc] init];
        data->layer = [[CAMetalLayer alloc] init];
        if (!data->view || !data->delegate || !data->layer) {
            delete data;
            return nullptr;
        }
        data->view->data_ = data;
        data->delegate->data_ = data;
        [data->layer setOpaque:YES];
        [data->view setLayer:data->layer];
        [data->view setWantsLayer:YES];
        [data->window setContentView:data->view];
        [data->window setDelegate:data->delegate];
        [[NSNotificationCenter defaultCenter] addObserver:data->delegate
                                                 selector:@selector(displayChanged:)
                                                     name:NSApplicationDidChangeScreenParametersNotification
                                                   object:nil];
        [data->window center];
        [data->window makeFirstResponder:data->view];
        [data->window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        data->geometry();
        data->focus(data->window.keyWindow);
        return data;
    }
}
void macos_deactivate(MacosWindow *window) {
    if (!window)
        return;
    __atomic_store_n(&window->active, false, __ATOMIC_RELEASE);
    if (macos_on_main_thread()) {
        @autoreleasepool {
            window->cancel_text();
            window->text_state.enabled = false;
            macos_drain_retired();
        }
    }
}
auto macos_pump(MacosWindow *window, uint32_t milliseconds) -> bool {
    if (!macos_on_main_thread() || !window)
        return false;
    macos_drain_retired();
    @autoreleasepool {
        NSDate *until =
            milliseconds ? [NSDate dateWithTimeIntervalSinceNow:double(milliseconds) / 1000] : [NSDate distantPast];
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:until
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event)
            dispatch_event(event);
        for (unsigned count{}; count < 4096; ++count) {
            event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:[NSDate distantPast]
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES];
            if (!event)
                break;
            dispatch_event(event);
        }
        [NSApp updateWindows];
        return true;
    }
}
auto macos_synchronize(MacosWindow *window) -> bool {
    if (!macos_pump(window, 0))
        return false;
    @autoreleasepool {
        window->geometry();
    }
    return true;
}
auto macos_metal_layer(const MacosWindow *window) -> const void * { return window ? window->layer : nullptr; }
auto macos_resize(MacosWindow *window, uint32_t width, uint32_t height) -> bool {
    if (!macos_on_main_thread() || !window || !width || !height || width > 32768 || height > 32768)
        return false;
    @autoreleasepool {
        [window->window setContentSize:NSMakeSize(width, height)];
        window->geometry();
    }
    return true;
}
auto macos_set_minimum_size(MacosWindow *window, uint32_t width, uint32_t height) -> bool {
    if (!macos_on_main_thread() || !window || width > 32768 || height > 32768)
        return false;
    @autoreleasepool {
        [window->window setContentMinSize:NSMakeSize(width, height)];
    }
    return true;
}
auto macos_set_position(MacosWindow *window, double x, double y) -> bool {
    if (!macos_on_main_thread() || !window || !isfinite(x) || !isfinite(y))
        return false;
    @autoreleasepool {
        auto content = [window->window contentRectForFrameRect:window->window.frame];
        content.origin = NSMakePoint(x, screen_top() - y - content.size.height);
        [window->window setFrameOrigin:[window->window frameRectForContentRect:content].origin];
        window->geometry();
    }
    return true;
}
auto macos_set_fullscreen(MacosWindow *window, bool enabled) -> bool {
    if (!macos_on_main_thread() || !window)
        return false;
    @autoreleasepool {
        if (window->dimensions.fullscreen != enabled)
            [window->window toggleFullScreen:nil];
    }
    return true;
}
auto macos_minimize(MacosWindow *window) -> bool {
    if (!macos_on_main_thread() || !window)
        return false;
    @autoreleasepool {
        [window->window miniaturize:nil];
        window->geometry();
    }
    return true;
}
auto macos_restore(MacosWindow *window) -> bool {
    if (!macos_on_main_thread() || !window)
        return false;
    @autoreleasepool {
        [window->window deminiaturize:nil];
        window->geometry();
    }
    return true;
}
auto macos_displays(MacosWindow *window, void *context, void (*emit)(void *, const MacosDisplay &)) -> bool {
    if (!macos_on_main_thread() || !window || !emit)
        return false;
    @autoreleasepool {
        auto top = screen_top();
        for (NSScreen *screen in [NSScreen screens]) {
            NSNumber *number = screen.deviceDescription[@"NSScreenNumber"];
            auto id = CGDirectDisplayID(number.unsignedIntValue);
            if (!id)
                continue;
            auto frame = screen.frame, usable = screen.visibleFrame;
            MacosDisplay info;
            info.id = id;
            info.x = frame.origin.x;
            info.y = top - NSMaxY(frame);
            info.width = frame.size.width;
            info.height = frame.size.height;
            info.usable_x = usable.origin.x;
            info.usable_y = top - NSMaxY(usable);
            info.usable_width = usable.size.width;
            info.usable_height = usable.size.height;
            info.pixel_width = uint32_t(CGDisplayPixelsWide(id));
            info.pixel_height = uint32_t(CGDisplayPixelsHigh(id));
            auto mode = CGDisplayCopyDisplayMode(id);
            if (mode) {
                auto rate = CGDisplayModeGetRefreshRate(mode);
                if (isfinite(rate) && rate > 0)
                    info.refresh_hz = rate;
                CGDisplayModeRelease(mode);
            }
            emit(context, info);
        }
    }
    return true;
}
auto macos_set_text_input(MacosWindow *window, const MacosTextState &state) -> bool {
    if (!macos_on_main_thread() || !window)
        return false;
    @autoreleasepool {
        NSString *source = state.surrounding_present ? native_string(state.surrounding) : @"";
        if (!source || (state.enabled && (!state.session || native_offset(source, state.anchor) == NSNotFound ||
                                          native_offset(source, state.cursor) == NSNotFound)))
            return false;
        if (state.reset || !state.enabled || !window->text_state.enabled || state.session != window->text_state.session)
            window->cancel_text();
        [source retain];
        [window->surrounding release];
        window->surrounding = source;
        window->text_state = state;
        window->text_state.surrounding = {};
        [[window->view inputContext] invalidateCharacterCoordinates];
    }
    return true;
}
auto macos_write_clipboard(MacosWindow *window, MacosBytes text) -> bool {
    if (!macos_on_main_thread() || !window)
        return false;
    @autoreleasepool {
        NSString *value = native_string(text);
        if (!value)
            return false;
        NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        return [pasteboard setString:value forType:NSPasteboardTypeString];
    }
}
} // namespace ffi

@implementation VuiMacosApplicationDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    for (NSWindow *window in sender.windows)
        [window performClose:nil];
    return NSTerminateCancel;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return NO;
}
@end

@implementation VuiMacosDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender {
    if (data_) {
        ffi::MacosEvent event;
        event.kind = ffi::MacosEventKind::Close;
        data_->emit(event);
    }
    return NO;
}
- (void)windowDidResize:(NSNotification *)notification {
    if (data_)
        data_->geometry();
}
- (void)windowDidMove:(NSNotification *)notification {
    if (data_)
        data_->geometry();
}
- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
    if (data_)
        data_->geometry();
}
- (void)windowDidMiniaturize:(NSNotification *)notification {
    if (data_)
        data_->geometry();
}
- (void)windowDidDeminiaturize:(NSNotification *)notification {
    if (data_)
        data_->geometry();
}
- (void)windowDidChangeOcclusionState:(NSNotification *)notification {
    if (data_)
        data_->geometry();
}
- (void)windowDidEnterFullScreen:(NSNotification *)notification {
    if (data_)
        data_->geometry();
}
- (void)windowDidExitFullScreen:(NSNotification *)notification {
    if (data_)
        data_->geometry();
}
- (void)windowDidBecomeKey:(NSNotification *)notification {
    if (data_)
        data_->focus(true);
}
- (void)windowDidResignKey:(NSNotification *)notification {
    if (data_)
        data_->focus(false);
}
- (void)windowDidExpose:(NSNotification *)notification {
    if (data_) {
        ffi::MacosEvent event;
        event.kind = ffi::MacosEventKind::Redraw;
        data_->emit(event);
    }
}
- (void)displayChanged:(NSNotification *)notification {
    if (data_)
        data_->geometry();
}
@end

@implementation VuiMacosView
- (BOOL)isFlipped {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent *)event {
    return YES;
}
- (void)dealloc {
    [tracking_ release];
    [marked_ release];
    [super dealloc];
}
- (void)updateTrackingAreas {
    if (tracking_) {
        [self removeTrackingArea:tracking_];
        [tracking_ release];
    }
    tracking_ = [[NSTrackingArea alloc] initWithRect:self.bounds
                                             options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                                                     NSTrackingActiveAlways | NSTrackingInVisibleRect
                                               owner:self
                                            userInfo:nil];
    [self addTrackingArea:tracking_];
    [super updateTrackingAreas];
}
- (void)mouseMoved:(NSEvent *)event {
    if (data_)
        data_->pointer(event, ffi::MacosEventKind::Move);
}
- (void)mouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}
- (void)rightMouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}
- (void)otherMouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}
- (void)mouseDown:(NSEvent *)event {
    if (data_)
        data_->pointer(event, ffi::MacosEventKind::Down);
}
- (void)rightMouseDown:(NSEvent *)event {
    [self mouseDown:event];
}
- (void)otherMouseDown:(NSEvent *)event {
    [self mouseDown:event];
}
- (void)mouseUp:(NSEvent *)event {
    if (data_)
        data_->pointer(event, ffi::MacosEventKind::Up);
}
- (void)rightMouseUp:(NSEvent *)event {
    [self mouseUp:event];
}
- (void)otherMouseUp:(NSEvent *)event {
    [self mouseUp:event];
}
- (void)mouseEntered:(NSEvent *)event {
    if (data_) {
        data_->pointer_inside = true;
        data_->pointer(event, ffi::MacosEventKind::Enter);
    }
}
- (void)mouseExited:(NSEvent *)event {
    if (data_)
        data_->pointer(event, ffi::MacosEventKind::Leave);
}
- (void)scrollWheel:(NSEvent *)native {
    if (!data_)
        return;
    auto point = [self convertPoint:native.locationInWindow fromView:nil];
    ffi::MacosEvent event;
    event.kind = ffi::MacosEventKind::Wheel;
    event.x = point.x;
    event.y = point.y;
    event.modifiers = ffi::modifiers(native.modifierFlags);
    event.delta_x = -native.scrollingDeltaX;
    event.delta_y = -native.scrollingDeltaY;
    event.precise = native.hasPreciseScrollingDeltas;
    event.phase = ffi::phase(native.phase);
    if (native.momentumPhase != NSEventPhaseNone) {
        event.momentum = 1;
        event.phase = ffi::phase(native.momentumPhase);
    } else if (native.phase != NSEventPhaseNone)
        event.momentum = 0;
    data_->emit(event, native);
}
- (void)keyDown:(NSEvent *)native {
    if (!data_ || !data_->is_active())
        return;
    ffi::MacosEvent event;
    event.kind = ffi::MacosEventKind::KeyDown;
    event.keycode = native.keyCode;
    event.repeat = [native isARepeat];
    event.modifiers = ffi::modifiers(native.modifierFlags);
    data_->emit(event, native);
    if (native.keyCode == 53 && marked_.length) {
        data_->cancel_text();
        return;
    }
    if (data_->text_state.enabled && !(event.modifiers & (2 | 8)))
        [self interpretKeyEvents:@[ native ]];
}
- (void)keyUp:(NSEvent *)native {
    if (!data_)
        return;
    ffi::MacosEvent event;
    event.kind = ffi::MacosEventKind::KeyUp;
    event.keycode = native.keyCode;
    event.modifiers = ffi::modifiers(native.modifierFlags);
    data_->emit(event, native);
}
- (BOOL)performKeyEquivalent:(NSEvent *)event {
    if ((event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) &&
        !(event.keyCode == 12 && (event.modifierFlags & NSEventModifierFlagCommand))) {
        [self keyDown:event];
        return YES;
    }
    return [super performKeyEquivalent:event];
}
- (BOOL)hasMarkedText {
    return marked_.length != 0;
}
- (NSRange)markedRange {
    return marked_.length ? NSMakeRange(marked_origin_, marked_.length) : NSMakeRange(NSNotFound, 0);
}
- (NSRange)selectedRange {
    if (marked_.length)
        return NSMakeRange(marked_origin_ + marked_selection_.location, marked_selection_.length);
    return data_ ? data_->selected() : NSMakeRange(NSNotFound, 0);
}
- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText {
    return @[];
}
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual {
    if (!data_ || range.location == NSNotFound || range.length > NSUIntegerMax - range.location)
        return nil;
    NSString *text = data_->displayed();
    if (range.location > text.length)
        return nil;
    if (range.length > text.length - range.location)
        range.length = text.length - range.location;
    if (!ffi::native_boundary(text, range.location) || !ffi::native_boundary(text, NSMaxRange(range)))
        return nil;
    if (actual)
        *actual = range;
    return [[[NSAttributedString alloc] initWithString:[text substringWithRange:range]] autorelease];
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    return NSNotFound;
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual {
    if (actual)
        *actual = range;
    if (!data_ || !data_->text_state.enabled)
        return NSZeroRect;
    auto area = data_->text_state;
    return [data_->window convertRectToScreen:[self convertRect:NSMakeRect(area.x, area.y, area.width, area.height)
                                                         toView:nil]];
}
- (void)setMarkedText:(id)value selectedRange:(NSRange)selected replacementRange:(NSRange)replacement {
    if (!data_ || !data_->text_state.enabled || data_->cancelling_text || !data_->is_active())
        return;
    NSString *text = [value isKindOfClass:[NSAttributedString class]] ? [value string] : value;
    NSData *encoded = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    uint32_t first{}, last{};
    if (!encoded || encoded.length > UINT32_MAX || !ffi::byte_offset(text, selected.location, first) ||
        selected.length > NSUIntegerMax - selected.location || !ffi::byte_offset(text, NSMaxRange(selected), last))
        return;
    ffi::MacosTextTransaction transaction;
    if (!data_->replacement(transaction, replacement))
        return;
    transaction.has_preedit = true;
    transaction.preedit = ffi::bytes(encoded);
    transaction.has_cursor = true;
    transaction.anchor = first;
    transaction.cursor = last;
    transaction.cursor_range = selected.length != 0;
    auto origin = marked_.length ? marked_origin_ : data_->selected().location;
    if (!marked_.length && replacement.location != NSNotFound)
        origin = replacement.location;
    [marked_ release];
    marked_ = [text copy];
    marked_selection_ = selected;
    marked_origin_ = origin == NSNotFound ? 0 : origin;
    data_->publish(transaction);
}
- (void)unmarkText {
    if (!data_)
        return;
    if (!data_->cancelling_text && marked_.length && data_->text_state.enabled) {
        NSData *encoded = [marked_ dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
        if (encoded && encoded.length <= UINT32_MAX) {
            ffi::MacosTextTransaction transaction;
            transaction.has_commit = true;
            transaction.commit = ffi::bytes(encoded);
            transaction.has_preedit = true;
            data_->publish(transaction);
        }
    }
    data_->clear_marked();
}
- (void)insertText:(id)value replacementRange:(NSRange)replacement {
    if (!data_ || !data_->text_state.enabled || data_->cancelling_text || !data_->is_active())
        return;
    NSString *text = [value isKindOfClass:[NSAttributedString class]] ? [value string] : value;
    if (text.length == 1 && [text characterAtIndex:0] >= 0xf700 && [text characterAtIndex:0] <= 0xf7ff)
        return;
    NSData *encoded = [text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    if (!encoded || encoded.length > UINT32_MAX)
        return;
    ffi::MacosTextTransaction transaction;
    if (!data_->replacement(transaction, replacement))
        return;
    transaction.has_commit = true;
    transaction.commit = ffi::bytes(encoded);
    transaction.has_preedit = true;
    data_->publish(transaction);
    data_->clear_marked();
}
- (void)doCommandBySelector:(SEL)selector {
    if (selector == @selector(cancelOperation:) && data_)
        data_->cancel_text();
}
@end
