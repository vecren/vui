module vui.host;
import :platform.x11_window;
import :platform.x11;
import :platform.input_queue;
import :platform.input_clock;
import :platform.x11_input;
import :platform.x11_clipboard;
import :platform.x11_clipboard_owner;
import :platform.x11_state;
import :platform.x11_displays;
import :ffi.x11;
import :platform.keys;
import :platform.x11_text;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;

void X11Window::emit(InputEvent input) {
    dropped_input_ += input_.push(rstd::move(input), clipboard_.pending(), [&](InputEvent &queued) {
        if (!queued.sample_time.clock) {
            auto now = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
            queued.sample_time = {sample_clock_, now > ~Serial{} ? ~Serial{} : Serial(now), 1,
                                  TimeOrigin::Received};
        }
        queued.coordinate_generation = revision_ + 1;
        if (trace_input_) queued.stamp = input_trace_.stamp();
    });
}

auto X11Window::native_time(rstd::uint32_t value) -> SampleTime {
    auto sample = native_clock_.sample(value);
    if (sample.discontinuity) { InputEvent reset; reset.type = EventType::InputReset; emit(rstd::move(reset)); }
    return sample.time;
}

auto X11Window::open(const char *title, rstd::uint32_t width, rstd::uint32_t height,
          const char *display) -> Result<empty, X11WindowError> {
    if (window_)
        return Err(X11WindowError::State);
    if (!title || !width || !height || width > 32768 || height > 32768)
        return Err(X11WindowError::InvalidSize);
    if (connection_.open(display).is_err())
        return Err(X11WindowError::Connection);
    auto native = connection_.display();
    auto screen = ffi::x11::XDefaultScreen(native);
    auto root = ffi::x11::XRootWindow(native, screen);
    window_ = ffi::x11::XCreateSimpleWindow(native, root, 0, 0, width, height, 0, 0, 0);
    if (!window_)
        return fail(X11WindowError::Window);
    ffi::x11::XSizeHints hints{};
    hints.flags = ffi::x11::gravity_hint;
    hints.win_gravity = ffi::x11::static_gravity;
    ffi::x11::XSetWMNormalHints(native, window_, &hints);
    protocols_ = ffi::x11::XInternAtom(native, "WM_PROTOCOLS", 0);
    delete_window_ = ffi::x11::XInternAtom(native, "WM_DELETE_WINDOW", 0);
    supported_atom_ = ffi::x11::XInternAtom(native, "_NET_SUPPORTED", 0);
    state_atom_ = ffi::x11::XInternAtom(native, "_NET_WM_STATE", 0);
    fullscreen_atom_ = ffi::x11::XInternAtom(native, "_NET_WM_STATE_FULLSCREEN", 0);
    auto wm_state = ffi::x11::XInternAtom(native, "WM_STATE", 0);
    auto desktop = ffi::x11::XInternAtom(native, "_NET_CURRENT_DESKTOP", 0);
    auto workarea = ffi::x11::XInternAtom(native, "_NET_WORKAREA", 0);
    if (!protocols_ || !delete_window_ ||
        !ffi::x11::XSetWMProtocols(native, window_, &delete_window_, 1))
        return fail(X11WindowError::Protocol);
    width_ = width;
    height_ = height;
    minimum_width_ = minimum_height_ = 0;
    revision_ = 0;
    close_requested_ = false;
    int supported{};
    detectable_repeat_ = ffi::x11::XkbSetDetectableAutoRepeat(native, 1, &supported) && supported;
    ffi::x11::XStoreName(native, window_, title);
    auto event_mask = ffi::x11::structure_notify_mask |
        ffi::x11::pointer_motion_mask | ffi::x11::button_press_mask |
        ffi::x11::button_release_mask | ffi::x11::focus_change_mask |
        ffi::x11::enter_window_mask | ffi::x11::leave_window_mask |
        ffi::x11::key_press_mask | ffi::x11::key_release_mask;
    ffi::x11::XSelectInput(native, window_, event_mask);
    ffi::x11::XSync(native, 0);
    (void)state_.open(ffi::x11::XDisplayString(native), root, window_, supported_atom_, state_atom_, fullscreen_atom_, wm_state);
    (void)displays_.open(ffi::x11::XDisplayString(native), root, window_, desktop, workarea);
    xi_.open(native, window_, root);
    clipboard_.open(native, root);
    (void)clipboard_owner_.open(ffi::x11::XDisplayString(native));
    text_.open(native, window_, event_mask);
    ffi::x11::XMapWindow(native, window_);
    ffi::x11::XFlush(native);
    return Ok(empty{});
}

auto X11Window::resize(rstd::uint32_t width, rstd::uint32_t height) -> Result<empty, X11WindowError> {
    if (!window_)
        return Err(X11WindowError::State);
    if (!width || !height || width > 32768 || height > 32768)
        return Err(X11WindowError::InvalidSize);
    ffi::x11::XResizeWindow(connection_.display(), window_, width, height);
    ffi::x11::XFlush(connection_.display());
    return Ok(empty{});
}

auto X11Window::set_minimum_size(rstd::uint32_t width, rstd::uint32_t height) -> Result<empty, X11WindowError> {
    if (!window_) return Err(X11WindowError::State);
    if (width > 32768 || height > 32768) return Err(X11WindowError::InvalidSize);
    ffi::x11::XSizeHints hints{};
    long supplied{};
    (void)ffi::x11::XGetWMNormalHints(connection_.display(), window_, &hints, &supplied);
    hints.flags &= ~ffi::x11::min_size_hint;
    if (width || height) {
        hints.flags |= ffi::x11::min_size_hint;
        hints.min_width = int(width); hints.min_height = int(height);
    }
    ffi::x11::XSetWMNormalHints(connection_.display(), window_, &hints);
    ffi::x11::XFlush(connection_.display());
    minimum_width_ = width; minimum_height_ = height;
    return Ok(empty{});
}

auto X11Window::position() const -> Option<Point> {
    if (!window_) return None();
    auto display = connection_.display(); auto root = ffi::x11::XRootWindow(display, ffi::x11::XDefaultScreen(display));
    ffi::x11::Window child{}; int x{}, y{};
    if (!ffi::x11::XTranslateCoordinates(display, window_, root, 0, 0, &x, &y, &child)) return None();
    return Some(Point{Scalar(x), Scalar(y)});
}

auto X11Window::set_position(int x, int y) -> Result<empty, X11WindowError> {
    if (!window_) return Err(X11WindowError::State);
    ffi::x11::XMoveWindow(connection_.display(), window_, x, y);
    ffi::x11::XFlush(connection_.display()); return Ok(empty{});
}

auto X11Window::minimize() -> bool {
    if (!window_) return false;
    auto display = connection_.display();
    auto result = ffi::x11::XIconifyWindow(display, window_, ffi::x11::XDefaultScreen(display));
    ffi::x11::XFlush(display); return result != 0;
}

auto X11Window::restore() -> bool {
    if (!window_) return false;
    auto display = connection_.display(); auto result = ffi::x11::XMapWindow(display, window_);
    ffi::x11::XFlush(display); return result != 0;
}

auto X11Window::set_fullscreen(bool enabled) -> bool {
    if (!window_) return false;
    auto display = connection_.display(); auto root = ffi::x11::XRootWindow(display, ffi::x11::XDefaultScreen(display));
    ffi::x11::XEvent event{};
    event.xclient.type = ffi::x11::client_message; event.xclient.window = window_;
    event.xclient.message_type = state_atom_; event.xclient.format = 32;
    event.xclient.data.l[0] = enabled ? 1 : 0; event.xclient.data.l[1] = long(fullscreen_atom_);
    event.xclient.data.l[3] = 1;
    auto result = ffi::x11::XSendEvent(display, root, 0,
        ffi::x11::substructure_notify_mask | ffi::x11::substructure_redirect_mask, &event);
    ffi::x11::XFlush(display); return result != 0;
}

auto X11Window::capture(bool enabled) -> NativeCaptureResult {
    if (!window_) return NativeCaptureResult::Failed;
    if (enabled == pointer_grabbed_) return NativeCaptureResult::Succeeded;
    auto display = connection_.display();
    if (enabled) {
        if (xi_.has_implicit_pointer_capture()) return NativeCaptureResult::Succeeded;
        auto result = ffi::x11::XGrabPointer(display, window_, 1,
            ffi::x11::button_press_mask | ffi::x11::button_release_mask |
            ffi::x11::pointer_motion_mask | ffi::x11::enter_window_mask |
            ffi::x11::leave_window_mask, ffi::x11::grab_mode_async,
            ffi::x11::grab_mode_async, 0, 0, ffi::x11::current_time);
        if (result != ffi::x11::grab_success) return NativeCaptureResult::Failed;
        pointer_grabbed_ = true;
    } else {
        ffi::x11::XUngrabPointer(display, ffi::x11::current_time);
        ffi::x11::XFlush(display);
        pointer_grabbed_ = false;
    }
    return NativeCaptureResult::Succeeded;
}

auto X11Window::set_text_input(Option<TextInputState> state) -> NativeTextInputResult {
    if (!window_) return state.is_some() ? NativeTextInputResult::Failed : NativeTextInputResult::Succeeded;
    if (state.is_some() && !state->valid()) return NativeTextInputResult::Failed;
    auto next = state.is_some() ? Some(Serial(state->request.session)) : None<Serial>();
    if (text_.session() != next) {
        if (dispatch().is_err() || !window_)
            return state.is_some() ? NativeTextInputResult::Failed : NativeTextInputResult::Succeeded;
    }
    return text_.set_state(rstd::move(state));
}

auto X11Window::dispatch(bool bounded) -> Result<empty, X11WindowError> {
    if (!window_)
        return Err(X11WindowError::State);
    auto display = connection_.display();
    unsigned clipboard_events{};
    while (ffi::x11::XPending(display) > 0) {
        ffi::x11::XEvent event{};
        ffi::x11::XNextEvent(display, &event);
        auto code = (event.type == ffi::x11::key_press || event.type == ffi::x11::key_release)
                        ? event.xkey.keycode : 0;
        auto filtered = text_.filter(event);
        if (event.type == ffi::x11::key_press || event.type == ffi::x11::key_release)
            event.xkey.keycode = code;
        if (event.type == ffi::x11::generic_event) {
            auto &cookie = event.xcookie;
            if (xi_.enabled() && cookie.extension == xi_.opcode() && ffi::x11::XGetEventData(display, &cookie)) {
                xi_.event(cookie.evtype, cookie.data);
                ffi::x11::XFreeEventData(display, &cookie);
            }
            continue;
        }
        if (clipboard_.event(event)) {
            if (bounded && ++clipboard_events >= 8) break;
            continue;
        }
        if (event.xany.window != window_)
            continue;
        if (event.type == ffi::x11::configure_notify && event.xconfigure.width > 0 &&
            event.xconfigure.height > 0) {
            displays_.window_changed();
            auto width = rstd::uint32_t(event.xconfigure.width),
                 height = rstd::uint32_t(event.xconfigure.height);
            if (width != width_ || height != height_) {
                width_ = width;
                height_ = height;
                ++revision_;
            }
        } else if (event.type == ffi::x11::client_message &&
                   event.xclient.message_type == protocols_ && event.xclient.format == 32 &&
                   static_cast<ffi::x11::Atom>(event.xclient.data.l[0]) == delete_window_) {
            close_requested_ = true;
        } else if (event.type == ffi::x11::destroy_notify) {
            (void)capture(false);
            text_.close();
            xi_.close();
            clipboard_.close();
            clipboard_owner_.close();
            state_.close();
            displays_.close();
            window_ = 0;
            close_requested_ = true;
        } else if (event.type == ffi::x11::unmap_notify) {
            pointer_grabbed_ = false;
            text_.focus(false);
            xi_.clear_contacts();
            xi_.clear_pointer_buttons();
            InputEvent input; input.type = EventType::WindowFocusLost;
            emit(rstd::move(input));
        } else if (event.type == ffi::x11::focus_in || event.type == ffi::x11::focus_out) {
            if (event.xfocus.mode == ffi::x11::notify_grab ||
                event.xfocus.mode == ffi::x11::notify_ungrab) continue;
            if (event.type == ffi::x11::focus_out && event.xfocus.detail == ffi::x11::notify_pointer) {
                auto queued = ffi::x11::XEventsQueued(display, ffi::x11::queued_after_reading);
                if (!queued) {
                    ffi::x11::XSync(display, 0);
                    queued = ffi::x11::XEventsQueued(display, ffi::x11::queued_after_reading);
                }
                if (queued) {
                    ffi::x11::XEvent next{};
                    ffi::x11::XPeekEvent(display, &next);
                    if (next.type == ffi::x11::focus_in && next.xfocus.window == window_ &&
                        next.xfocus.detail == ffi::x11::notify_nonlinear &&
                        next.xfocus.mode == event.xfocus.mode && next.xfocus.serial == event.xfocus.serial)
                        continue;
                }
            }
            InputEvent input;
            input.type = event.type == ffi::x11::focus_in ? EventType::WindowFocusGained
                                                           : EventType::WindowFocusLost;
            text_.focus(event.type == ffi::x11::focus_in);
            if (event.type == ffi::x11::focus_out) {
                (void)capture(false);
                for (auto &key : pressed_) key = false;
                xi_.clear_contacts();
                xi_.clear_pointer_buttons();
            }
            emit(rstd::move(input));
        } else if (event.type == ffi::x11::key_press || event.type == ffi::x11::key_release) {
            auto code = event.xkey.keycode;
            if (code >= 256) continue;
            if (event.type == ffi::x11::key_release && !detectable_repeat_ &&
                ffi::x11::XEventsQueued(display, ffi::x11::queued_after_reading)) {
                ffi::x11::XEvent next{};
                ffi::x11::XPeekEvent(display, &next);
                if (next.type == ffi::x11::key_press && next.xkey.window == window_ &&
                    next.xkey.keycode == code && next.xkey.time - event.xkey.time < 20) continue;
            }
            unsigned consumed{};
            unsigned long symbol{};
            ffi::x11::XkbLookupKeySym(display, code, event.xkey.state, &consumed, &symbol);
            InputEvent input;
            input.type = event.type == ffi::x11::key_press ? EventType::KeyDown : EventType::KeyUp;
            input.key = key_from_symbol(symbol);
            input.modifiers = modifiers(event.xkey.state);
            input.repeat = event.type == ffi::x11::key_press && pressed_[code];
            pressed_[code] = event.type == ffi::x11::key_press;
            auto sample = native_time(rstd::uint32_t(event.xkey.time));
            input.sample_time = sample;
            emit(rstd::move(input));
            if (event.type == ffi::x11::key_press && !filtered) {
                bool invalid{};
                auto mods = modifiers(event.xkey.state);
                auto text = text_.lookup(event.xkey, rstd::uint32_t(symbol),
                                         mods.control || mods.alt || mods.meta, invalid);
                if (invalid) ++dropped_input_;
                auto session = text_.session();
                if (text.present() && session.is_some() && !mods.control && !mods.alt && !mods.meta) {
                    InputEvent typed; typed.type = EventType::TextInput; typed.text = rstd::move(text);
                    typed.text_session = *session; typed.modifiers = mods;
                    typed.sample_time = sample;
                    emit(rstd::move(typed));
                }
            }
        } else if (event.type == ffi::x11::motion_notify ||
                   event.type == ffi::x11::enter_notify || event.type == ffi::x11::leave_notify) {
            if (xi_.enabled() && event.type == ffi::x11::motion_notify) continue;
            InputEvent input;
            input.type = event.type == ffi::x11::motion_notify ? EventType::PointerMove
                         : event.type == ffi::x11::enter_notify ? EventType::WindowPointerEnter
                                                                : EventType::WindowPointerLeave;
            if (event.type == ffi::x11::motion_notify) {
                input.position = {Scalar(event.xmotion.x), Scalar(event.xmotion.y)};
                input.modifiers = modifiers(event.xmotion.state);
                input.sample_time = native_time(rstd::uint32_t(event.xmotion.time));
            } else {
                input.position = {Scalar(event.xcrossing.x), Scalar(event.xcrossing.y)};
                input.modifiers = modifiers(event.xcrossing.state);
                input.sample_time = native_time(rstd::uint32_t(event.xcrossing.time));
            }
            emit(rstd::move(input));
        } else if (event.type == ffi::x11::button_press || event.type == ffi::x11::button_release) {
            if (xi_.enabled()) continue;
            InputEvent input;
            auto button = event.xbutton.button;
            input.position = {Scalar(event.xbutton.x), Scalar(event.xbutton.y)};
            input.modifiers = modifiers(event.xbutton.state);
            if (button >= 4 && button <= 7) {
                if (event.type == ffi::x11::button_release) continue;
                input.type = EventType::Wheel;
                input.scroll_unit = ScrollUnit::Lines;
                input.scroll_delta = button == 4 ? Point{0, -1} : button == 5 ? Point{0, 1}
                                     : button == 6 ? Point{-1, 0} : Point{1, 0};
            } else {
                input.button = button == 1 ? PointerButton::Primary : button == 2 ? PointerButton::Middle
                               : button == 3 ? PointerButton::Secondary : button == 8 ? PointerButton::Back
                               : button == 9 ? PointerButton::Forward : PointerButton::None;
                if (input.button == PointerButton::None) continue;
                input.type = event.type == ffi::x11::button_press ? EventType::PointerDown : EventType::PointerUp;
            }
            input.sample_time = native_time(rstd::uint32_t(event.xbutton.time));
            emit(rstd::move(input));
        }
    }
    return Ok(empty{});
}

auto X11Window::synchronize() -> Result<empty, X11WindowError> {
    if (!window_)
        return Err(X11WindowError::State);
    ffi::x11::XSync(connection_.display(), 0);
    return dispatch();
}

void X11Window::close() {
    if (window_) (void)capture(false);
    text_.close();
    xi_.close();
    clipboard_.close();
    clipboard_owner_.close();
    state_.close();
    displays_.close();
    if (window_)
        ffi::x11::XDestroyWindow(connection_.display(), window_);
    window_ = 0;
    connection_.close();
    close_requested_ = false;
    minimum_width_ = minimum_height_ = 0;
    input_.clear();
    native_clock_.reset();
    dropped_input_ = 0;
    for (auto &key : pressed_) key = false;
}
} // namespace vui::host::detail
