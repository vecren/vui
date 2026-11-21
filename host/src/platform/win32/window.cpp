module vui.host;
import :platform.input;
import :platform.win32.window;
import :platform.win32.ffi;
import :platform.win32.text;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

void Win32Window::text_event(void *sink, InputEvent event) {
    auto &self = *static_cast<Win32Window *>(sink);
    auto state = self.state_.lock().unwrap();
    self.emit(*state, rstd::move(event), true);
}

void Win32Window::emit(State &state, InputEvent event, bool native_time) {
    if (native_time) {
        auto sample = state.clock.sample(rstd::uint32_t(ffi::GetMessageTime()));
        if (sample.discontinuity) {
            InputEvent reset; reset.type = EventType::InputReset;
            auto dropped = state.input.push(rstd::move(reset), usize(0), [&](InputEvent &value) {
                if (state.tracing) value.stamp = state.trace.stamp();
            });
            state.dropped += rstd::min(dropped, ~Serial{} - state.dropped);
        }
        event.sample_time = sample.time;
    } else {
        auto now = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
        event.sample_time = {state.received_clock, now > ~Serial{} ? ~Serial{} : Serial(now), 1, TimeOrigin::Received};
    }
    event.coordinate_generation = state.revision + 1;
    auto dropped = state.input.push(rstd::move(event), usize(0), [&](InputEvent &value) {
        if (state.tracing) value.stamp = state.trace.stamp();
    });
    state.dropped += rstd::min(dropped, ~Serial{} - state.dropped);
    changed(state);
}

auto Win32Window::modifiers() -> Modifiers {
    auto pressed = [](int key) { return (ffi::GetKeyState(key) & 0x8000) != 0; };
    return {pressed(ffi::VK_SHIFT), pressed(ffi::VK_CONTROL), pressed(ffi::VK_MENU),
            pressed(ffi::VK_LWIN) || pressed(ffi::VK_RWIN)};
}

auto Win32Window::key(ffi::WPARAM value) -> Key {
    if (value >= 'A' && value <= 'Z') return Key(unsigned(Key::A) + unsigned(value - 'A'));
    switch (value) {
    case ffi::VK_TAB: return Key::Tab;
    case ffi::VK_RETURN: return Key::Enter;
    case ffi::VK_SPACE: return Key::Space;
    case ffi::VK_ESCAPE: return Key::Escape;
    case ffi::VK_LEFT: return Key::Left;
    case ffi::VK_RIGHT: return Key::Right;
    case ffi::VK_UP: return Key::Up;
    case ffi::VK_DOWN: return Key::Down;
    case ffi::VK_HOME: return Key::Home;
    case ffi::VK_END: return Key::End;
    case ffi::VK_PRIOR: return Key::PageUp;
    case ffi::VK_NEXT: return Key::PageDown;
    case ffi::VK_BACK: return Key::Backspace;
    case ffi::VK_DELETE: return Key::Delete;
    default: return Key::Unknown;
    }
}

void Win32Window::command(Command &value) {
    switch (value.kind) {
    case Command::Kind::Synchronize: value.succeeded = true; break;
    case Command::Kind::Deactivate:
        text_.close(window_);
        {
            auto state = state_.lock().unwrap();
            state->active = false; state->surrogate = 0;
        }
        releasing_capture_ = true;
        if (ffi::GetCapture() == window_) ffi::ReleaseCapture();
        releasing_capture_ = false;
        value.succeeded = true; break;
    case Command::Kind::Resize: {
        rstd::uint32_t dpi;
        { auto state = state_.lock().unwrap(); if (state->fullscreen) break; dpi = state->dpi; }
        auto width = (rstd::uint64_t(value.x) * dpi + 48) / 96;
        auto height = (rstd::uint64_t(value.y) * dpi + 48) / 96;
        ffi::RECT bounds{0, 0, long(width), long(height)};
        value.succeeded = ffi::AdjustWindowRectExForDpi(&bounds, ffi::WS_OVERLAPPEDWINDOW, false, 0, dpi) &&
            ffi::SetWindowPos(window_, nullptr, 0, 0, bounds.right - bounds.left,
                              bounds.bottom - bounds.top, ffi::SWP_NOMOVE | ffi::SWP_NOZORDER | ffi::SWP_NOACTIVATE);
        break;
    }
    case Command::Kind::MinimumSize: {
        auto state = state_.lock().unwrap();
        state->minimum_width = rstd::uint32_t(value.x);
        state->minimum_height = rstd::uint32_t(value.y);
        value.succeeded = true;
        break;
    }
    case Command::Kind::Fullscreen: {
        bool fullscreen;
        { auto state = state_.lock().unwrap(); fullscreen = state->fullscreen; }
        if (fullscreen == value.enabled) { value.succeeded = true; break; }
        ffi::MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        auto handle = ffi::MonitorFromWindow(window_, ffi::MONITOR_DEFAULTTONEAREST);
        if (!handle || !ffi::GetMonitorInfoW(handle, &monitor)) break;
        auto bounds = monitor.rcMonitor;
        auto frame_flags = ffi::SWP_NOZORDER | ffi::SWP_NOACTIVATE | ffi::SWP_FRAMECHANGED;
        if (value.enabled) {
            ffi::WINDOWPLACEMENT placement{}; placement.length = sizeof(placement);
            if (!ffi::GetWindowPlacement(window_, &placement)) break;
            auto style = ffi::get_style(window_);
            if (!ffi::set_style(window_, style & ~ffi::LONG_PTR(ffi::WS_OVERLAPPEDWINDOW))) break;
            { auto state = state_.lock().unwrap(); state->fullscreen = true; }
            if (!ffi::SetWindowPos(window_, nullptr, bounds.left, bounds.top,
                bounds.right - bounds.left, bounds.bottom - bounds.top, frame_flags)) {
                { auto state = state_.lock().unwrap(); state->fullscreen = false; }
                (void)ffi::set_style(window_, style);
                (void)ffi::SetWindowPlacement(window_, &placement);
                (void)ffi::SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                    frame_flags | ffi::SWP_NOMOVE | ffi::SWP_NOSIZE);
                break;
            }
            windowed_style_ = style; windowed_placement_ = placement;
            value.succeeded = true;
        } else {
            auto style = ffi::get_style(window_);
            if (!ffi::set_style(window_, windowed_style_)) break;
            { auto state = state_.lock().unwrap(); state->fullscreen = false; }
            if (!ffi::SetWindowPlacement(window_, &windowed_placement_) ||
                !ffi::SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                    frame_flags | ffi::SWP_NOMOVE | ffi::SWP_NOSIZE)) {
                { auto state = state_.lock().unwrap(); state->fullscreen = true; }
                (void)ffi::set_style(window_, style);
                (void)ffi::SetWindowPos(window_, nullptr, bounds.left, bounds.top,
                    bounds.right - bounds.left, bounds.bottom - bounds.top, frame_flags);
                break;
            }
            value.succeeded = true;
        }
        break;
    }
    case Command::Kind::TextInput: {
        auto old = text_.session();
        rstd::uint32_t dpi;
        { auto state = state_.lock().unwrap(); dpi = state->dpi; }
        value.text_result = text_.set_state(window_, rstd::move(*value.text), dpi);
        if (old != text_.session()) { auto state = state_.lock().unwrap(); state->surrogate = 0; }
        value.succeeded = true;
        break;
    }
    case Command::Kind::Capture:
        if (value.enabled) { ffi::SetCapture(window_); value.succeeded = ffi::GetCapture() == window_; }
        else {
            releasing_capture_ = true;
            value.succeeded = ffi::GetCapture() != window_ || ffi::ReleaseCapture();
            releasing_capture_ = false;
        }
        break;
    case Command::Kind::Minimize: ffi::ShowWindow(window_, ffi::SW_MINIMIZE); value.succeeded = true; break;
    case Command::Kind::Restore: ffi::ShowWindow(window_, ffi::SW_RESTORE); value.succeeded = true; break;
    }
}

void Win32Window::text(State &state, unsigned value) {
    if (!text_.accepts_text()) { state.surrogate = 0; return; }
    if (value >= 0xd800 && value <= 0xdbff) { state.surrogate = value; return; }
    if (value >= 0xdc00 && value <= 0xdfff) {
        if (!state.surrogate) return;
        value = 0x10000 + ((state.surrogate - 0xd800) << 10) + (value - 0xdc00);
    }
    state.surrogate = 0;
    auto session = text_.session();
    if (value < 32 || (value >= 127 && value < 160) || session.is_none()) return;
    auto scalar = rstd::Char::from_u32(u32(value));
    if (scalar.is_none()) return;
    rstd::byte bytes[4]{};
    auto count = scalar->encode_utf8(bytes);
    auto decoded = rstd::str_::from_utf8(slice<u8>::from_raw_parts(bytes, count));
    if (decoded.is_err()) return;
    auto content = TextInput::create(decoded.unwrap());
    if (content.is_err()) return;
    InputEvent event; event.type = EventType::TextInput; event.text = rstd::move(content).unwrap();
    event.text_session = *session;
    emit(state, rstd::move(event), true);
}

auto Win32Window::message(ffi::HWND window, ffi::UINT message, ffi::WPARAM wparam, ffi::LPARAM lparam) -> ffi::LRESULT {
    if (message == ffi::msg_command) {
        Command *value;
        { auto pending = pending_command_.lock().unwrap(); value = *pending; *pending = nullptr; }
        if (value) { command(*value); ffi::SetEvent(value->done); }
        return 0;
    }
    if (message == ffi::WM_ERASEBKGND) return 1;
    bool active;
    { auto state = state_.lock().unwrap(); active = state->active; }
    if (!active) return message == ffi::WM_CLOSE ? 0 : ffi::DefWindowProcW(window, message, wparam, lparam);
    if (message == ffi::WM_GETMINMAXINFO) {
        rstd::uint32_t width, height, dpi;
        bool fullscreen;
        {
            auto state = state_.lock().unwrap();
            width = state->minimum_width; height = state->minimum_height; dpi = state->dpi;
            fullscreen = state->fullscreen;
        }
        if (fullscreen) return ffi::DefWindowProcW(window, message, wparam, lparam);
        ffi::RECT frame{};
        if ((width || height) && ffi::AdjustWindowRectExForDpi(&frame, ffi::WS_OVERLAPPEDWINDOW, false, 0, dpi)) {
            auto limits = reinterpret_cast<ffi::MINMAXINFO *>(lparam);
            if (width) limits->ptMinTrackSize.x = long((rstd::uint64_t(width) * dpi + 48) / 96) + frame.right - frame.left;
            if (height) limits->ptMinTrackSize.y = long((rstd::uint64_t(height) * dpi + 48) / 96) + frame.bottom - frame.top;
        }
        return 0;
    }
    if (message == ffi::WM_PAINT) {
        ffi::ValidateRect(window, nullptr);
        auto state = state_.lock().unwrap(); changed(*state); return 0;
    }
    if (message == ffi::WM_DPICHANGED) {
        auto bounds = reinterpret_cast<const ffi::RECT *>(lparam);
        auto dpi = rstd::uint32_t(wparam & 0xffffu);
        if (!dpi) return 0;
        bool fullscreen;
        {
            auto state = state_.lock().unwrap();
            state->dpi = dpi; fullscreen = state->fullscreen; ++state->revision; changed(*state);
        }
        ffi::MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        if (fullscreen) {
            auto handle = ffi::MonitorFromWindow(window, ffi::MONITOR_DEFAULTTONEAREST);
            bounds = handle && ffi::GetMonitorInfoW(handle, &monitor) ? &monitor.rcMonitor : nullptr;
        }
        if (bounds) ffi::SetWindowPos(window, nullptr, bounds->left, bounds->top,
                                      bounds->right - bounds->left, bounds->bottom - bounds->top,
                                      ffi::SWP_NOZORDER | ffi::SWP_NOACTIVATE);
        text_.reposition(window, dpi);
        return 0;
    }
    if (message == ffi::WM_SIZE) {
        ffi::RECT bounds{};
        auto has_bounds = ffi::GetClientRect(window, &bounds);
        auto state = state_.lock().unwrap();
        state->minimized = wparam == ffi::SIZE_MINIMIZED;
        if (has_bounds && !state->minimized && bounds.right > bounds.left && bounds.bottom > bounds.top) {
            state->width = rstd::uint32_t(bounds.right - bounds.left);
            state->height = rstd::uint32_t(bounds.bottom - bounds.top);
            auto dpi = ffi::GetDpiForWindow(window); if (dpi) state->dpi = dpi;
        }
        ++state->revision; changed(*state); return 0;
    }
    if (message == ffi::WM_CLOSE) {
        auto state = state_.lock().unwrap(); state->close_requested = true; changed(*state); return 0;
    }
    if (message == ffi::WM_CAPTURECHANGED) {
        if (!releasing_capture_) {
            auto state = state_.lock().unwrap(); InputEvent event; event.type = EventType::InputReset;
            emit(*state, rstd::move(event));
        }
        return 0;
    }
    if (message == ffi::WM_SETFOCUS || message == ffi::WM_KILLFOCUS) {
        rstd::uint32_t dpi;
        { auto state = state_.lock().unwrap(); dpi = state->dpi; }
        text_.focus(window, message == ffi::WM_SETFOCUS, dpi);
        auto state = state_.lock().unwrap();
        if (message == ffi::WM_KILLFOCUS) state->surrogate = 0;
        InputEvent event; event.type = message == ffi::WM_SETFOCUS ? EventType::WindowFocusGained : EventType::WindowFocusLost;
        emit(*state, rstd::move(event)); return 0;
    }
    if (message == ffi::WM_IME_SETCONTEXT && wparam)
        return ffi::DefWindowProcW(window, message, wparam, lparam & ~ffi::LPARAM(ffi::ISC_SHOWUICOMPOSITIONWINDOW));
    if (message == ffi::WM_IME_STARTCOMPOSITION && text_.session().is_some()) { text_.start(); return 0; }
    if (message == ffi::WM_IME_COMPOSITION && text_.compose(window, lparam)) return 0;
    if (message == ffi::WM_IME_ENDCOMPOSITION && text_.session().is_some()) { text_.end(); return 0; }
    if (message == ffi::WM_IME_CHAR && text_.session().is_some()) return 0;
    if (message == ffi::WM_MOUSEMOVE || message == ffi::WM_MOUSELEAVE) {
        auto state = state_.lock().unwrap();
        if (message == ffi::WM_MOUSELEAVE) {
            state->tracking = false;
            InputEvent event; event.type = EventType::WindowPointerLeave; emit(*state, rstd::move(event)); return 0;
        }
        auto position = point(lparam, state->dpi);
        if (!state->tracking) {
            ffi::TRACKMOUSEEVENT track{sizeof(ffi::TRACKMOUSEEVENT), ffi::TME_LEAVE, window, 0};
            state->tracking = ffi::TrackMouseEvent(&track);
            InputEvent enter; enter.type = EventType::WindowPointerEnter; enter.position = position;
            emit(*state, rstd::move(enter), true);
        }
        InputEvent event; event.type = EventType::PointerMove; event.position = position;
        event.modifiers = modifiers(); emit(*state, rstd::move(event), true); return 0;
    }
    if (message == ffi::WM_LBUTTONDOWN || message == ffi::WM_LBUTTONUP ||
        message == ffi::WM_RBUTTONDOWN || message == ffi::WM_RBUTTONUP ||
        message == ffi::WM_MBUTTONDOWN || message == ffi::WM_MBUTTONUP ||
        message == ffi::WM_XBUTTONDOWN || message == ffi::WM_XBUTTONUP) {
        auto state = state_.lock().unwrap(); InputEvent event;
        event.type = message == ffi::WM_LBUTTONDOWN || message == ffi::WM_RBUTTONDOWN ||
                     message == ffi::WM_MBUTTONDOWN || message == ffi::WM_XBUTTONDOWN
                         ? EventType::PointerDown : EventType::PointerUp;
        event.button = message == ffi::WM_LBUTTONDOWN || message == ffi::WM_LBUTTONUP ? PointerButton::Primary
            : message == ffi::WM_RBUTTONDOWN || message == ffi::WM_RBUTTONUP ? PointerButton::Secondary
            : message == ffi::WM_MBUTTONDOWN || message == ffi::WM_MBUTTONUP ? PointerButton::Middle
            : ffi::GET_XBUTTON_WPARAM(wparam) == ffi::XBUTTON1 ? PointerButton::Back : PointerButton::Forward;
        event.position = point(lparam, state->dpi); event.modifiers = modifiers();
        emit(*state, rstd::move(event), true);
        return message == ffi::WM_XBUTTONDOWN || message == ffi::WM_XBUTTONUP ? 1 : 0;
    }
    if (message == ffi::WM_MOUSEWHEEL || message == ffi::WM_MOUSEHWHEEL) {
        ffi::POINT position{ffi::GET_X_LPARAM(lparam), ffi::GET_Y_LPARAM(lparam)};
        if (!ffi::ScreenToClient(window, &position)) return 0;
        auto state = state_.lock().unwrap(); InputEvent event;
        event.type = EventType::Wheel; event.scroll_unit = ScrollUnit::Lines;
        auto delta = Scalar(ffi::GET_WHEEL_DELTA_WPARAM(wparam)) / Scalar(ffi::WHEEL_DELTA);
        event.scroll_delta = message == ffi::WM_MOUSEWHEEL ? Point{0, -delta} : Point{delta, 0};
        auto scale = Scalar(96) / Scalar(state->dpi);
        event.position = {Scalar(position.x) * scale, Scalar(position.y) * scale};
        event.modifiers = modifiers(); emit(*state, rstd::move(event), true); return 0;
    }
    if (message == ffi::WM_KEYDOWN || message == ffi::WM_KEYUP ||
        message == ffi::WM_SYSKEYDOWN || message == ffi::WM_SYSKEYUP) {
        auto translated = key(wparam);
        if (translated == Key::Unknown) return ffi::DefWindowProcW(window, message, wparam, lparam);
        auto state = state_.lock().unwrap(); InputEvent event;
        event.type = message == ffi::WM_KEYDOWN || message == ffi::WM_SYSKEYDOWN ? EventType::KeyDown : EventType::KeyUp;
        event.key = translated; event.repeat = event.type == EventType::KeyDown &&
            (rstd::uint64_t(lparam) & (rstd::uint64_t(1) << 30)) != 0;
        event.modifiers = modifiers(); emit(*state, rstd::move(event), true);
        if (message == ffi::WM_SYSKEYDOWN || message == ffi::WM_SYSKEYUP)
            return ffi::DefWindowProcW(window, message, wparam, lparam);
        return 0;
    }
    if (message == ffi::WM_UNICHAR && wparam == ffi::UNICODE_NOCHAR) return 1;
    if (message == ffi::WM_CHAR || message == ffi::WM_UNICHAR) {
        auto state = state_.lock().unwrap();
        if (message == ffi::WM_UNICHAR && (wparam > 0x10ffff || (wparam >= 0xd800 && wparam <= 0xdfff))) state->surrogate = 0;
        else text(*state, unsigned(wparam));
        return 0;
    }
    return ffi::DefWindowProcW(window, message, wparam, lparam);
}

auto __stdcall Win32Window::window_proc(ffi::HWND window, ffi::UINT message, ffi::WPARAM wparam,
                                   ffi::LPARAM lparam) -> ffi::LRESULT {
    auto self = reinterpret_cast<Win32Window *>(ffi::get_data(window));
    if (message == ffi::WM_NCCREATE) {
        auto create = reinterpret_cast<const ffi::CREATESTRUCTW *>(lparam);
        self = static_cast<Win32Window *>(create->lpCreateParams);
        ffi::set_data(window, reinterpret_cast<ffi::LONG_PTR>(self));
    }
    return self ? self->message(window, message, wparam, lparam)
                : ffi::DefWindowProcW(window, message, wparam, lparam);
}

auto __stdcall Win32Window::thread_main(void *data) -> ffi::DWORD {
    auto &self = *static_cast<Win32Window *>(data);
    ffi::SetThreadDpiAwarenessContext(ffi::DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ffi::MSG message{}; ffi::PeekMessageW(&message, nullptr, 0, 0, ffi::PM_NOREMOVE);
    self.instance_ = ffi::GetModuleHandleW(nullptr);
    ffi::WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = window_proc; cls.hInstance = self.instance_; cls.hCursor = ffi::arrow_cursor();
    cls.lpszClassName = class_name_;
    bool registered = ffi::RegisterClassExW(&cls) || ffi::GetLastError() == ffi::ERROR_CLASS_ALREADY_EXISTS;
    auto dpi = ffi::GetDpiForSystem(); if (!dpi) dpi = 96;
    ffi::RECT bounds{0, 0, long((rstd::uint64_t(self.initial_width_) * dpi + 48) / 96),
                     long((rstd::uint64_t(self.initial_height_) * dpi + 48) / 96)};
    if (registered && ffi::AdjustWindowRectExForDpi(&bounds, ffi::WS_OVERLAPPEDWINDOW, false, 0, dpi))
        self.window_ = ffi::CreateWindowExW(0, class_name_, self.title_.as_slice().as_raw_ptr(),
            ffi::WS_OVERLAPPEDWINDOW, ffi::CW_USEDEFAULT, ffi::CW_USEDEFAULT,
            bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, self.instance_, &self);
    if (self.window_) ffi::ShowWindow(self.window_, ffi::SW_SHOWNORMAL);
    {
        auto state = self.state_.lock().unwrap(); state->opened = self.window_ != nullptr; state->ready = true;
    }
    ffi::SetEvent(self.ready_);
    if (self.window_) {
        int result;
        while ((result = ffi::GetMessageW(&message, nullptr, 0, 0)) > 0) {
            ffi::TranslateMessage(&message); ffi::DispatchMessageW(&message);
        }
        ffi::DestroyWindow(self.window_);
    }
    return 0;
}

Win32Window::~Win32Window() {
    if (thread_) {
        if (thread_id_) ffi::PostThreadMessageW(thread_id_, ffi::WM_QUIT, 0, 0);
        ffi::WaitForSingleObject(thread_, ffi::INFINITE); ffi::CloseHandle(thread_);
    }
    if (ready_) ffi::CloseHandle(ready_);
    if (changed_) ffi::CloseHandle(changed_);
}

auto Win32Window::open(const char *title, rstd::uint32_t width, rstd::uint32_t height) -> bool {
    auto decoded = rstd::ffi::CStr::from_ptr(title).to_str();
    if (decoded.is_err()) return false;
    auto name = rstd::ffi::OsString::from(decoded.unwrap());
    auto wide = rstd::os::windows::ffi::OsStrExt::encode_wide(name.as_os_str());
    while (auto value = wide.next()) title_.push(static_cast<wchar_t>(value->to_primitive()));
    title_.push(L'\0'); initial_width_ = width; initial_height_ = height;
    ready_ = ffi::CreateEventW(nullptr, true, false, nullptr);
    changed_ = ffi::CreateEventW(nullptr, true, false, nullptr);
    if (!ready_ || !changed_) return false;
    thread_ = ffi::CreateThread(nullptr, 0, thread_main, this, 0, &thread_id_);
    if (!thread_ || ffi::WaitForSingleObject(ready_, ffi::INFINITE) != ffi::WAIT_OBJECT_0) return false;
    auto state = state_.lock().unwrap(); return state->ready && state->opened;
}

auto Win32Window::snapshot() -> Win32Snapshot {
    auto state = state_.lock().unwrap();
    Win32Snapshot result{state->input.take(), state->revision, state->dropped,
        state->width, state->height, state->dpi, state->redraw, state->close_requested, state->minimized};
    state->dropped = 0; state->redraw = false; ffi::ResetEvent(changed_); return result;
}

auto Win32Window::view() -> Win32Snapshot {
    auto state = state_.lock().unwrap();
    return {{}, state->revision, state->dropped, state->width, state->height,
            state->dpi, state->redraw, state->close_requested, state->minimized};
}

auto Win32Window::execute(Command::Kind kind, int x, int y, bool enabled,
             Option<TextInputState> *text, NativeTextInputResult *text_result) -> bool {
    if (!window_ || !alive()) return false;
    auto done = ffi::CreateEventW(nullptr, true, false, nullptr);
    if (!done) return false;
    Command command{kind, done, x, y, enabled};
    command.text = text;
    bool queued{};
    {
        auto pending = pending_command_.lock().unwrap();
        if (!*pending) {
            *pending = &command;
            queued = ffi::PostMessageW(window_, ffi::msg_command, 0, 0);
            if (!queued) *pending = nullptr;
        }
    }
    ffi::HANDLE events[]{done, thread_};
    bool completed = queued && ffi::WaitForMultipleObjects(2, events, false, ffi::INFINITE) == ffi::WAIT_OBJECT_0;
    if (queued && !completed) {
        auto pending = pending_command_.lock().unwrap();
        if (*pending == &command) *pending = nullptr;
    }
    ffi::CloseHandle(done);
    if (completed && text_result) *text_result = command.text_result;
    return completed && command.succeeded;
}
} // namespace vui::host::detail
