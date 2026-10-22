module;
#include <SDL.h>
#include <SDL_vulkan.h>
module vui.sdl;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui::sdl {
struct NativeWindow {
    SDL_Window *window;
    explicit NativeWindow(SDL_Window *value) : window(value) {}
    NativeWindow(const NativeWindow &) = delete;
    ~NativeWindow() {
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    }
};
struct NativeLease {
    rstd::sync::Arc<NativeWindow> native;
    explicit NativeLease(rstd::sync::Arc<NativeWindow> value) : native(rstd::move(value)) {}
};
struct WakeState {
    SDL_mutex *mutex{SDL_CreateMutex()};
    Index event_type, window_id;
    WakeStats stats;
    bool queued{};
    WakeState(Index type, Index id) : event_type(type), window_id(id) {}
    ~WakeState() {
        if (mutex)
            SDL_DestroyMutex(mutex);
    }
    struct Lock {
        SDL_mutex *mutex;
        explicit Lock(SDL_mutex *value) : mutex(value) { SDL_LockMutex(mutex); }
        ~Lock() { SDL_UnlockMutex(mutex); }
    };
    auto request() -> FrameWakeResult {
        Lock lock(mutex);
        if (stats.closed)
            return FrameWakeResult::Closed;
        stats.pending = true;
        if (queued) {
            ++stats.coalesced;
            return FrameWakeResult::Coalesced;
        }
        // SDL filters run inside PushEvent; recursive requests must coalesce.
        queued = true;
        SDL_Event event{};
        event.type = event_type;
        event.user.windowID = window_id;
        if (SDL_PushEvent(&event) != 1) {
            queued = false;
            ++stats.failed;
            return FrameWakeResult::Failed;
        }
        ++stats.queued;
        return FrameWakeResult::Queued;
    }
    auto take() -> bool {
        Lock lock(mutex);
        auto pending = stats.pending;
        stats.pending = false;
        queued = false;
        return pending;
    }
    auto snapshot() -> WakeStats {
        Lock lock(mutex);
        return stats;
    }
    void close() {
        Lock lock(mutex);
        stats.closed = true;
        stats.pending = false;
        queued = false;
    }
};
struct Window::Impl {
    SDL_Window *window{};
    inline static Impl *text_owner{};
    Option<TextInputRequest> text_request;
    void stop_text_input() {
        text_request = None();
        if (text_owner == this) { SDL_StopTextInput(); text_owner = nullptr; }
    }
    auto set_text_input(Option<TextInputRequest> request) -> NativeTextInputResult {
        if (request.is_none()) { stop_text_input(); return NativeTextInputResult::Succeeded; }
        if (!window || !request->valid()) return NativeTextInputResult::Failed;
        if (auto focused = SDL_GetKeyboardFocus(); focused && focused != window) return NativeTextInputResult::Failed;
        int width{}, height{}; SDL_GetWindowSize(window, &width, &height);
        if (width <= 0 || height <= 0 || metrics.logical_size.width <= 0 || metrics.logical_size.height <= 0)
            return NativeTextInputResult::Failed;
        auto rect = request->area;
        auto x = __builtin_floor(rect.x * width / metrics.logical_size.width);
        auto y = __builtin_floor(rect.y * height / metrics.logical_size.height);
        auto right = __builtin_ceil((rect.x + rect.width) * width / metrics.logical_size.width);
        auto bottom = __builtin_ceil((rect.y + rect.height) * height / metrics.logical_size.height);
        auto valid = [](Scalar value) { return finite(value) && value >= -2147483648.0 && value <= 2147483647.0; };
        if (!valid(x) || !valid(y) || !valid(right) || !valid(bottom) ||
            right - x > 2147483647.0 || bottom - y > 2147483647.0) return NativeTextInputResult::Failed;
        SDL_Rect area{int(x), int(y), int(rstd::cmp::max(Scalar(1), right - x)), int(rstd::cmp::max(Scalar(1), bottom - y))};
        bool active = text_owner == this && SDL_IsTextInputActive();
        bool continuing = active && text_request.is_some() && text_request->session == request->session;
        if (!active && text_owner) text_owner->stop_text_input();
        if (active && !continuing) SDL_ClearComposition();
        if (!continuing) {
            auto window_id = SDL_GetWindowID(window);
            SDL_FilterEvents([](void *context, SDL_Event *event) -> int {
                auto id = *static_cast<Uint32 *>(context);
                if ((event->type == SDL_TEXTINPUT && event->text.windowID == id) ||
                    (event->type == SDL_TEXTEDITING && event->edit.windowID == id)) return 0;
                if (event->type == SDL_TEXTEDITING_EXT && event->editExt.windowID == id) {
                    SDL_free(event->editExt.text); event->editExt.text = nullptr; return 0;
                }
                return 1;
            }, &window_id);
        }
        SDL_SetTextInputRect(&area);
        if (!active) SDL_StartTextInput();
        if (!SDL_IsTextInputActive()) return NativeTextInputResult::Failed;
        text_owner = this; text_request = request;
        return NativeTextInputResult::Succeeded;
    }
    bool minimized{}, trace_input{};
    InputTraceSource input_trace;
    Serial sample_clock{new_session()};
    struct TouchDevice {
        SDL_TouchID native;
        DeviceId id;
    };
    struct TouchContact {
        SDL_TouchID device;
        SDL_FingerID finger;
        Index pointer;
    };
    Vec<TouchDevice> touch_devices;
    Vec<TouchContact> touch_contacts;
    Index next_contact{1};
    auto sample_time(Uint32 timestamp, Option<Serial> &age_at_receipt) const -> SampleTime {
        auto frequency = SDL_GetPerformanceFrequency();
        auto now = static_cast<Serial>(
            (static_cast<long double>(SDL_GetPerformanceCounter()) / frequency) * 1'000'000'000);
        auto age = static_cast<Uint32>(SDL_GetTicks() - timestamp);
        if (age > 0x7fffffffU || Serial(age) * 1'000'000 > now)
            return {sample_clock, now, 1'000'000, TimeOrigin::Received};
        age_at_receipt = Some(Serial(age) * 1'000'000);
        return {sample_clock, now - Serial(age) * 1'000'000, 1'000'000, TimeOrigin::Native};
    }
    auto touch_device(SDL_TouchID native) -> DeviceId {
        for (const auto &device : touch_devices)
            if (device.native == native)
                return device.id;
        if (touch_devices.len() >= usize(256))
            for (usize i{}; i < touch_devices.len(); ++i) {
                bool active{};
                for (const auto &contact : touch_contacts)
                    active |= contact.device == touch_devices[i].native;
                if (!active) {
                    touch_devices.remove(i);
                    break;
                }
            }
        auto id = (Serial(1) << 63) | new_session();
        touch_devices.push({native, id});
        return id;
    }
    Option<rstd::sync::Arc<NativeWindow>> native;
    Option<rstd::sync::Arc<WakeState>> wake;
    SurfaceInfo metrics{{}, 1, 1};
    auto extent() const -> VkExtent2D {
        int width{}, height{};
        if (window && !minimized)
            SDL_Vulkan_GetDrawableSize(window, &width, &height);
        return {width > 0 ? static_cast<Index>(width) : 0,
                height > 0 ? static_cast<Index>(height) : 0};
    }
    auto update_metrics() -> bool {
        minimized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
        auto pixels = extent();
        int width{}, height{};
        SDL_GetWindowSize(window, &width, &height);
        Scalar scale = pixels.width && width > 0 ? Scalar(pixels.width) / width : metrics.scale;
        Size size{pixels.width / scale, pixels.height / scale};
        if (size == metrics.logical_size && scale == metrics.scale)
            return false;
        metrics = {size, scale, metrics.generation + 1};
        return true;
    }
    auto position(int x, int y) const -> Point {
        int width{}, height{};
        SDL_GetWindowSize(window, &width, &height);
        return {width > 0 ? x * metrics.logical_size.width / width : 0,
                height > 0 ? y * metrics.logical_size.height / height : 0};
    }
    static auto modifiers(SDL_Keymod value) -> Modifiers {
        return {bool(value & KMOD_SHIFT), bool(value & KMOD_CTRL), bool(value & KMOD_ALT),
                bool(value & KMOD_GUI)};
    }
    static auto key(SDL_Keycode value) -> Key {
        if (value >= SDLK_a && value <= SDLK_z)
            return Key(unsigned(Key::A) + unsigned(value - SDLK_a));
        switch (value) {
        case SDLK_TAB:
            return Key::Tab;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return Key::Enter;
        case SDLK_SPACE:
            return Key::Space;
        case SDLK_ESCAPE:
            return Key::Escape;
        case SDLK_LEFT:
            return Key::Left;
        case SDLK_RIGHT:
            return Key::Right;
        case SDLK_UP:
            return Key::Up;
        case SDLK_DOWN:
            return Key::Down;
        case SDLK_HOME:
            return Key::Home;
        case SDLK_END:
            return Key::End;
        case SDLK_PAGEUP:
            return Key::PageUp;
        case SDLK_PAGEDOWN:
            return Key::PageDown;
        case SDLK_BACKSPACE:
            return Key::Backspace;
        case SDLK_DELETE:
            return Key::Delete;
        default:
            return Key::Unknown;
        }
    }
    void emit_input(Events &output, InputEvent input) {
        input.coordinate_generation = metrics.generation;
        if (trace_input)
            input.stamp = input_trace.stamp();
        output.input.push(rstd::move(input));
    }
    void text_event(Events &output, InputEvent input, const char *value, size_t size, int cursor = -1, int length = -1) {
        if (text_owner != this || text_request.is_none()) return;
        auto decoded = rstd::str_::from_utf8(slice<u8>::from_raw_parts(reinterpret_cast<const rstd::byte *>(value), usize(size)));
        if (decoded.is_err() || size > ~Index{} || cursor < -1 || length < -1) { ++output.dropped_input; return; }
        auto text = decoded.unwrap();
        auto offset = [&](Serial character) -> Option<Index> {
            Serial count{};
            for (usize i{}; i <= text->len(); ++i)
                if (text->is_char_boundary(i) && count++ == character) return Some(Index(i.to_primitive()));
            return None();
        };
        Option<TextInputSelection> selection;
        if (cursor >= 0) {
            auto start = offset(Serial(cursor));
            auto end = length >= 0 ? offset(Serial(cursor) + Serial(length)) : None<Index>();
            if (start.is_none() || (length >= 0 && end.is_none())) { ++output.dropped_input; return; }
            selection = Some(TextInputSelection{*start, end});
        }
        auto payload = TextInput::create(text, selection);
        if (payload.is_err()) { ++output.dropped_input; return; }
        input.text = rstd::move(payload).unwrap(); input.text_session = text_request->session;
        input.modifiers = modifiers(SDL_GetModState()); emit_input(output, rstd::move(input));
    }
    void consume(SDL_Event &value, Events &output) {
        event(value, output);
#if SDL_VERSION_ATLEAST(2, 0, 22)
        if (value.type == SDL_TEXTEDITING_EXT) { SDL_free(value.editExt.text); value.editExt.text = nullptr; }
#endif
    }
    void event(const SDL_Event &value, Events &output) {
        auto window_id = SDL_GetWindowID(window);
        if (wake.is_some() && value.type == (*wake)->event_type &&
            value.user.windowID == window_id) {
            output.redraw = (*wake)->take() || output.redraw;
            return;
        }
        InputEvent input;
        input.sample_time = sample_time(value.common.timestamp, input.age_at_receipt_ns);
        switch (value.type) {
        case SDL_QUIT:
            output.close_requested = true;
            return;
        case SDL_WINDOWEVENT:
            if (value.window.windowID != window_id)
                return;
            switch (value.window.event) {
            case SDL_WINDOWEVENT_CLOSE:
                output.close_requested = true;
                break;
            case SDL_WINDOWEVENT_MINIMIZED:
                minimized = true;
                output.redraw = true;
                SDL_CaptureMouse(SDL_FALSE);
                touch_contacts.clear();
                stop_text_input();
                input.type = EventType::WindowFocusLost;
                emit_input(output, rstd::move(input));
                break;
            case SDL_WINDOWEVENT_RESTORED:
            case SDL_WINDOWEVENT_MAXIMIZED:
                minimized = false;
                output.redraw = true;
                break;
            case SDL_WINDOWEVENT_EXPOSED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_DISPLAY_CHANGED:
                output.redraw = true;
                break;
            case SDL_WINDOWEVENT_FOCUS_LOST:
                SDL_CaptureMouse(SDL_FALSE);
                touch_contacts.clear();
                stop_text_input();
                input.type = EventType::WindowFocusLost;
                emit_input(output, rstd::move(input));
                break;
            case SDL_WINDOWEVENT_FOCUS_GAINED:
                input.type = EventType::WindowFocusGained;
                emit_input(output, rstd::move(input));
                break;
            default:
                break;
            }
            update_metrics();
            return;
        case SDL_MOUSEMOTION:
            if (value.motion.windowID != window_id || value.motion.which == SDL_TOUCH_MOUSEID)
                return;
            input.type = EventType::PointerMove;
            input.position = position(value.motion.x, value.motion.y);
            input.device = value.motion.which;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (value.button.windowID != window_id || value.button.which == SDL_TOUCH_MOUSEID)
                return;
            input.type =
                value.type == SDL_MOUSEBUTTONDOWN ? EventType::PointerDown : EventType::PointerUp;
            input.position = position(value.button.x, value.button.y);
            input.device = value.button.which;
            if (value.button.button == SDL_BUTTON_LEFT)
                input.button = PointerButton::Primary;
            else if (value.button.button == SDL_BUTTON_RIGHT)
                input.button = PointerButton::Secondary;
            else if (value.button.button == SDL_BUTTON_MIDDLE)
                input.button = PointerButton::Middle;
            else if (value.button.button == SDL_BUTTON_X1)
                input.button = PointerButton::Back;
            else if (value.button.button == SDL_BUTTON_X2)
                input.button = PointerButton::Forward;
            else
                return;
            break;
        case SDL_MOUSEWHEEL: {
            if (value.wheel.windowID != window_id || value.wheel.which == SDL_TOUCH_MOUSEID)
                return;
            input.type = EventType::Wheel;
            input.device = value.wheel.which;
            input.scroll_unit = ScrollUnit::Lines;
            auto direction = value.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
            input.scroll_delta = {direction * Scalar(value.wheel.preciseX),
                                  -direction * Scalar(value.wheel.preciseY)};
#if SDL_VERSION_ATLEAST(2, 26, 0)
            input.position = position(value.wheel.mouseX, value.wheel.mouseY);
#else
            int x{}, y{};
            SDL_GetMouseState(&x, &y);
            input.position = position(x, y);
#endif
            break;
        }
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP: {
            const auto &touch = value.tfinger;
            if (touch.windowID && touch.windowID != window_id)
                return;
            TouchContact *contact{};
            for (auto &current : touch_contacts)
                if (current.device == touch.touchId && current.finger == touch.fingerId)
                    contact = &current;
            if (!contact && value.type == SDL_FINGERDOWN) {
                if (touch_contacts.len() >= usize(32) || next_contact == ~Index{}) {
                    ++output.dropped_input;
                    return;
                }
                touch_contacts.push({touch.touchId, touch.fingerId, next_contact++});
                contact = &touch_contacts[touch_contacts.len() - usize(1)];
            }
            if (!contact)
                return;
            input.type = value.type == SDL_FINGERDOWN ? EventType::PointerDown
                         : value.type == SDL_FINGERUP ? EventType::PointerUp
                                                      : EventType::PointerMove;
            input.device = touch_device(touch.touchId);
            input.pointer = contact->pointer;
            input.pointer_kind = PointerKind::Touch;
            input.position = {Scalar(touch.x) * metrics.logical_size.width,
                              Scalar(touch.y) * metrics.logical_size.height};
            if (!finite(input.position.x) || !finite(input.position.y))
                return;
            if (finite(touch.pressure) && touch.pressure >= 0 && touch.pressure <= 1)
                input.pressure = Some(Scalar(touch.pressure));
            if (value.type == SDL_FINGERUP)
                touch_contacts.retain([&](const TouchContact &current) {
                    return current.device != touch.touchId || current.finger != touch.fingerId;
                });
            break;
        }
        case SDL_TEXTINPUT:
        case SDL_TEXTEDITING: {
            bool editing = value.type == SDL_TEXTEDITING;
            if ((editing ? value.edit.windowID : value.text.windowID) != window_id) return;
            const char *text = editing ? value.edit.text : value.text.text;
            auto limit = editing ? sizeof(value.edit.text) : sizeof(value.text.text);
            size_t length{}; while (length < limit && text[length]) ++length;
            if (length == limit) { ++output.dropped_input; return; }
            input.type = editing ? EventType::TextEditing : EventType::TextInput;
            text_event(output, rstd::move(input), text, length, editing ? value.edit.start : -1, editing ? value.edit.length : -1);
            return;
        }
#if SDL_VERSION_ATLEAST(2, 0, 22)
        case SDL_TEXTEDITING_EXT:
            if (value.editExt.windowID != window_id) return;
            if (!value.editExt.text) { ++output.dropped_input; return; }
            input.type = EventType::TextEditing;
            text_event(output, rstd::move(input), value.editExt.text, SDL_strlen(value.editExt.text), value.editExt.start, value.editExt.length);
            return;
#endif
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (value.key.windowID != window_id)
                return;
            input.type = value.type == SDL_KEYDOWN ? EventType::KeyDown : EventType::KeyUp;
            input.key = key(value.key.keysym.sym);
            input.modifiers = modifiers(static_cast<SDL_Keymod>(value.key.keysym.mod));
            input.repeat = value.key.repeat != 0;
            emit_input(output, rstd::move(input));
            return;
        default:
            return;
        }
        input.modifiers = modifiers(SDL_GetModState());
        emit_input(output, rstd::move(input));
    }
};
Window::Window() : impl_(Box<Impl>::make()) {}
Window::~Window() {
    impl_->stop_text_input();
    if (impl_->wake.is_some())
        (*impl_->wake)->close();
}
auto Window::open(const char *title, Index width, Index height) -> Result<empty, gpu::Error> {
    if (impl_->window || !width || !height || width > 32768 || height > 32768)
        return Err(gpu::Error{gpu::Stage::State});
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        return Err(gpu::Error{gpu::Stage::Surface});
    impl_->window =
        SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         static_cast<int>(width), static_cast<int>(height),
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!impl_->window) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        return Err(gpu::Error{gpu::Stage::Surface});
    }
    impl_->native = Some(rstd::sync::Arc<NativeWindow>::make(impl_->window));
    if (!Impl::text_owner) SDL_StopTextInput();
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif
#if SDL_VERSION_ATLEAST(2, 0, 22)
    SDL_SetHint(SDL_HINT_IME_SUPPORT_EXTENDED_TEXT, "1");
#endif
    auto type = SDL_RegisterEvents(1);
    if (type == Uint32(-1)) {
        impl_->native = None();
        impl_->window = nullptr;
        return Err(gpu::Error{gpu::Stage::Surface});
    }
    auto wake = rstd::sync::Arc<WakeState>::make(type, SDL_GetWindowID(impl_->window));
    if (!wake->mutex) {
        impl_->native = None();
        impl_->window = nullptr;
        return Err(gpu::Error{gpu::Stage::Surface});
    }
    impl_->wake = Some(rstd::move(wake));
    impl_->update_metrics();
    return Ok(empty{});
}
auto Window::poll() -> Events { return wait(0); }
auto Window::frame_waker() const -> Option<FrameWaker> {
    if (impl_->wake.is_none())
        return None();
    return Some(FrameWaker::make([state = impl_->wake->clone()] { return state->request(); }));
}
auto Window::wake_stats() const -> WakeStats {
    return impl_->wake.is_some() ? (*impl_->wake)->snapshot() : WakeStats{0, 0, 0, false, true};
}
auto Window::handle_event(const SDL_Event &event) -> Events {
    Events output;
    if (!impl_->window) {
        output.close_requested = true;
        return output;
    }
    output.redraw = impl_->update_metrics();
    output.redraw = (*impl_->wake)->take() || output.redraw;
    impl_->event(event, output);
    output.redraw = impl_->update_metrics() || output.redraw;
    output.surface = impl_->metrics;
    output.minimized = impl_->minimized;
    return output;
}
auto Window::wait(Index milliseconds) -> Events {
    Events output;
    if (!impl_->window) {
        output.close_requested = true;
        return output;
    }
    output.redraw = impl_->update_metrics();
    output.redraw = (*impl_->wake)->take() || output.redraw;
    SDL_Event event;
    if (milliseconds && !output.redraw &&
        SDL_WaitEventTimeout(&event, milliseconds > 1000 ? 1000 : static_cast<int>(milliseconds)))
        impl_->consume(event, output);
    unsigned count{};
    while (count++ < 4096 && output.input.len() < usize(2048) && SDL_PollEvent(&event))
        impl_->consume(event, output);
    output.redraw = impl_->update_metrics() || output.redraw;
    output.redraw = (*impl_->wake)->take() || output.redraw;
    output.surface = impl_->metrics;
    output.minimized = impl_->minimized;
    return output;
}
auto Window::capture(PointerKind kind, bool enabled) -> NativeCaptureResult {
    if (kind != PointerKind::Mouse)
        return NativeCaptureResult::Unsupported;
    if (!impl_->window)
        return NativeCaptureResult::Failed;
    return SDL_CaptureMouse(enabled ? SDL_TRUE : SDL_FALSE) == 0 ? NativeCaptureResult::Succeeded
                                                                 : NativeCaptureResult::Failed;
}
auto Window::set_text_input(Option<TextInputRequest> request) -> NativeTextInputResult { return impl_->set_text_input(request); }
auto Window::text_input_request() const -> Option<TextInputRequest> { return impl_->text_request; }
auto Window::read_clipboard() -> Result<String, ClipboardError> {
    if (!impl_->window) return Err(ClipboardError::Unavailable);
    struct Owner { char *value; ~Owner() { SDL_free(value); } } text{SDL_GetClipboardText()};
    if (!text.value) return Err(ClipboardError::Failed);
    auto length = SDL_strlen(text.value);
    if (length > 1048576) return Err(ClipboardError::Limit);
    auto decoded = rstd::str_::from_utf8(slice<u8>::from_raw_parts(reinterpret_cast<const rstd::byte *>(text.value), usize(length)));
    if (decoded.is_err()) return Err(ClipboardError::InvalidText);
    return Ok(String::make(decoded.unwrap()));
}
auto Window::write_clipboard(ref<str> text) -> Result<empty, ClipboardError> {
    if (!impl_->window) return Err(ClipboardError::Unavailable);
    if (text->len() > usize(1048576)) return Err(ClipboardError::Limit);
    if (rstd::str_::from_utf8(text->as_bytes()).is_err()) return Err(ClipboardError::InvalidText);
    auto chars = text->chars();
    for (auto cp = chars.next(); cp.is_some(); cp = chars.next()) if (cp->to_primitive() == 0) return Err(ClipboardError::InvalidText);
    auto terminated = String::make(text); terminated.push(U'\0');
    if (SDL_SetClipboardText(reinterpret_cast<const char *>(terminated.as_str()->as_bytes().as_ptr().as_raw_ptr())) != 0)
        return Err(ClipboardError::Failed);
    return Ok(empty{});
}
void Window::set_input_tracing(bool enabled) { impl_->trace_input = enabled; }
auto Window::timing() const -> FrameTiming {
    auto frequency = SDL_GetPerformanceFrequency();
    auto counter = SDL_GetPerformanceCounter();
    return {static_cast<Serial>((static_cast<long double>(counter) / frequency) * 1'000'000'000),
            {}};
}
auto Window::resize(Index width, Index height) -> Result<empty, gpu::Error> {
    if (!impl_->window || !width || !height || width > 32768 || height > 32768)
        return Err(gpu::Error{gpu::Stage::State});
    SDL_SetWindowSize(impl_->window, static_cast<int>(width), static_cast<int>(height));
    return Ok(empty{});
}
auto Window::set_mode(WindowMode mode) -> Result<empty, gpu::Error> {
    if (!impl_->window || unsigned(mode) > unsigned(WindowMode::Fullscreen))
        return Err(gpu::Error{gpu::Stage::State});
    if (this->mode() == Some(mode)) return Ok(empty{});
    auto flags = mode == WindowMode::Fullscreen ? Uint32(SDL_WINDOW_FULLSCREEN_DESKTOP) : 0u;
    if (SDL_SetWindowFullscreen(impl_->window, flags) != 0)
        return Err(gpu::Error{gpu::Stage::Surface});
    (*impl_->wake)->request();
    return Ok(empty{});
}
auto Window::set_minimum_size(Index width,Index height) -> Result<empty,gpu::Error> {
    if (!impl_->window || width>32768 || height>32768) return Err(gpu::Error{gpu::Stage::State});
    SDL_SetWindowMinimumSize(impl_->window,int(width),int(height));
    auto actual=minimum_size();
    if (actual!=Some(Size{Scalar(width),Scalar(height)})) return Err(gpu::Error{gpu::Stage::Surface});
    return Ok(empty{});
}
auto Window::minimum_size() const -> Option<Size> {
    if (!impl_->window) return None();
    int width{},height{};SDL_GetWindowMinimumSize(impl_->window,&width,&height);
    return Some(Size{Scalar(width),Scalar(height)});
}
auto Window::logical_size() const -> Option<Size> {
    if (!impl_->window) return None();
    int width{},height{};SDL_GetWindowSize(impl_->window,&width,&height);
    return Some(Size{Scalar(width),Scalar(height)});
}
namespace {
auto global_window_position() -> bool {
    auto driver=SDL_GetCurrentVideoDriver();
    return driver && (SDL_strcmp(driver,"x11")==0 || SDL_strcmp(driver,"windows")==0 || SDL_strcmp(driver,"cocoa")==0);
}
}
auto Window::position() const -> Option<Point> {
    if (!impl_->window || !global_window_position()) return None();
    int x{},y{};SDL_GetWindowPosition(impl_->window,&x,&y);return Some(Point{Scalar(x),Scalar(y)});
}
auto Window::set_position(Point point) -> WindowPositionResult {
    if (!impl_->window || !finite(point.x) || !finite(point.y) || point.x< -2147483648.0 || point.x>2147483647.0 ||
        point.y< -2147483648.0 || point.y>2147483647.0 || __builtin_floor(point.x)!=point.x || __builtin_floor(point.y)!=point.y)
        return WindowPositionResult::Failed;
    if (!global_window_position()) return WindowPositionResult::Unsupported;
    auto x=int(point.x),y=int(point.y);
    if (SDL_WINDOWPOS_ISCENTERED(x) || SDL_WINDOWPOS_ISUNDEFINED(x) || SDL_WINDOWPOS_ISCENTERED(y) || SDL_WINDOWPOS_ISUNDEFINED(y))
        return WindowPositionResult::Failed;
    SDL_SetWindowPosition(impl_->window,x,y);return WindowPositionResult::Requested;
}
auto Window::mode() const -> Option<WindowMode> {
    if (!impl_->window) return None();
    return Some((SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_FULLSCREEN)
                    ? WindowMode::Fullscreen : WindowMode::Windowed);
}
namespace {
auto display_refresh_rate(int display) -> Option<Index> {
    SDL_DisplayMode mode;
    if (display < 0 || SDL_GetCurrentDisplayMode(display, &mode) != 0 || mode.refresh_rate <= 0)
        return None();
    return Some(Index(mode.refresh_rate));
}
} // namespace
auto Window::refresh_rate() const -> Option<Index> {
    return impl_->window ? display_refresh_rate(SDL_GetWindowDisplayIndex(impl_->window)) : None<Index>();
}
auto Window::display_info() const -> Result<DisplayInfo, gpu::Error> {
    if (!impl_->window) return Err(gpu::Error{gpu::Stage::State});
    auto index = SDL_GetWindowDisplayIndex(impl_->window);
    SDL_Rect bounds{}, usable{};
    if (index < 0 || SDL_GetDisplayBounds(index, &bounds) != 0 ||
        SDL_GetDisplayUsableBounds(index, &usable) != 0 ||
        bounds.w <= 0 || bounds.h <= 0 || usable.w <= 0 || usable.h <= 0)
        return Err(gpu::Error{gpu::Stage::Surface});
    return Ok(DisplayInfo{Index(index), {Scalar(bounds.x), Scalar(bounds.y), Scalar(bounds.w), Scalar(bounds.h)},
                          {Scalar(usable.x), Scalar(usable.y), Scalar(usable.w), Scalar(usable.h)}, display_refresh_rate(index)});
}
void Window::minimize() {
    if (impl_->window)
        SDL_MinimizeWindow(impl_->window);
}
void Window::restore() {
    if (impl_->window) {
        SDL_RestoreWindow(impl_->window);
        SDL_RaiseWindow(impl_->window);
    }
}
auto Window::id() const -> Index { return impl_->window ? SDL_GetWindowID(impl_->window) : 0; }
auto Window::error() const -> const char * { return SDL_GetError(); }
auto Window::pixel_extent() const -> VkExtent2D { return impl_->extent(); }
auto Window::instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> {
    unsigned count{};
    if (!impl_->window || !SDL_Vulkan_GetInstanceExtensions(impl_->window, &count, nullptr))
        return Err(gpu::Error{gpu::Stage::Surface});
    Vec<const char *> names;
    for (unsigned i = 0; i < count; ++i)
        names.push(nullptr);
    if (!SDL_Vulkan_GetInstanceExtensions(impl_->window, &count, names.as_mut_slice().as_raw_ptr()))
        return Err(gpu::Error{gpu::Stage::Surface});
    Vec<vrento::vulkan::Extension> extensions;
    for (auto name : names)
        extensions.push({true, rstd::ffi::CStr::from_ptr(name).to_str().unwrap()});
    return Ok(rstd::move(extensions));
}
auto Window::create_surface(VkInstance instance) -> Result<gpu::NativeSurface, gpu::Error> {
    VkSurfaceKHR surface{};
    if (!impl_->window || !SDL_Vulkan_CreateSurface(impl_->window, instance, &surface))
        return Err(gpu::Error{gpu::Stage::Surface});
    return Ok(gpu::NativeSurface{
        surface, Box<dyn<gpu::SurfaceLifetime>>::make(NativeLease(impl_->native->clone()))});
}
} // namespace vui::sdl
