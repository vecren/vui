module vui.host;
import :platform.win32_window;
import :ffi.win32;
import :platform.input_clock;
import :platform.input_queue;
import :platform.win32_text;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;
namespace win = ffi::win32;

void Win32Window::text_event(void *sink, InputEvent event) {
    auto &self = *static_cast<Win32Window *>(sink);
    auto state = self.state_.lock().unwrap();
    self.emit(*state, rstd::move(event), true);
}

void Win32Window::emit(State &state, InputEvent event, bool native_time) {
    if (native_time) {
        auto sample = state.clock.sample(rstd::uint32_t(win::GetMessageTime()));
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
    auto pressed = [](int key) { return (win::GetKeyState(key) & 0x8000) != 0; };
    return {pressed(win::key_shift), pressed(win::key_control), pressed(win::key_alt),
            pressed(win::key_left_meta) || pressed(win::key_right_meta)};
}

auto Win32Window::key(win::WPARAM value) -> Key {
    if (value >= 'A' && value <= 'Z') return Key(unsigned(Key::A) + unsigned(value - 'A'));
    switch (value) {
    case win::key_tab: return Key::Tab;
    case win::key_enter: return Key::Enter;
    case win::key_space: return Key::Space;
    case win::key_escape: return Key::Escape;
    case win::key_left: return Key::Left;
    case win::key_right: return Key::Right;
    case win::key_up: return Key::Up;
    case win::key_down: return Key::Down;
    case win::key_home: return Key::Home;
    case win::key_end: return Key::End;
    case win::key_page_up: return Key::PageUp;
    case win::key_page_down: return Key::PageDown;
    case win::key_backspace: return Key::Backspace;
    case win::key_delete: return Key::Delete;
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
        if (win::GetCapture() == window_) win::ReleaseCapture();
        releasing_capture_ = false;
        value.succeeded = true; break;
    case Command::Kind::Resize: {
        rstd::uint32_t dpi;
        { auto state = state_.lock().unwrap(); if (state->fullscreen) break; dpi = state->dpi; }
        auto width = (rstd::uint64_t(value.x) * dpi + 48) / 96;
        auto height = (rstd::uint64_t(value.y) * dpi + 48) / 96;
        win::RECT bounds{0, 0, long(width), long(height)};
        value.succeeded = win::AdjustWindowRectExForDpi(&bounds, win::window_style, false, 0, dpi) &&
            win::SetWindowPos(window_, nullptr, 0, 0, bounds.right - bounds.left,
                              bounds.bottom - bounds.top, win::set_no_move | win::set_no_zorder | win::set_no_activate);
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
        win::MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        auto handle = win::MonitorFromWindow(window_, win::nearest_monitor);
        if (!handle || !win::GetMonitorInfoW(handle, &monitor)) break;
        auto bounds = monitor.rcMonitor;
        auto frame_flags = win::set_no_zorder | win::set_no_activate | win::set_frame_changed;
        if (value.enabled) {
            win::WINDOWPLACEMENT placement{}; placement.length = sizeof(placement);
            if (!win::GetWindowPlacement(window_, &placement)) break;
            auto style = win::get_style(window_);
            if (!win::set_style(window_, style & ~win::LONG_PTR(win::window_style))) break;
            { auto state = state_.lock().unwrap(); state->fullscreen = true; }
            if (!win::SetWindowPos(window_, nullptr, bounds.left, bounds.top,
                bounds.right - bounds.left, bounds.bottom - bounds.top, frame_flags)) {
                { auto state = state_.lock().unwrap(); state->fullscreen = false; }
                (void)win::set_style(window_, style);
                (void)win::SetWindowPlacement(window_, &placement);
                (void)win::SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                    frame_flags | win::set_no_move | win::set_no_size);
                break;
            }
            windowed_style_ = style; windowed_placement_ = placement;
            value.succeeded = true;
        } else {
            auto style = win::get_style(window_);
            if (!win::set_style(window_, windowed_style_)) break;
            { auto state = state_.lock().unwrap(); state->fullscreen = false; }
            if (!win::SetWindowPlacement(window_, &windowed_placement_) ||
                !win::SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                    frame_flags | win::set_no_move | win::set_no_size)) {
                { auto state = state_.lock().unwrap(); state->fullscreen = true; }
                (void)win::set_style(window_, style);
                (void)win::SetWindowPos(window_, nullptr, bounds.left, bounds.top,
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
        if (value.enabled) { win::SetCapture(window_); value.succeeded = win::GetCapture() == window_; }
        else {
            releasing_capture_ = true;
            value.succeeded = win::GetCapture() != window_ || win::ReleaseCapture();
            releasing_capture_ = false;
        }
        break;
    case Command::Kind::Minimize: win::ShowWindow(window_, win::show_minimized); value.succeeded = true; break;
    case Command::Kind::Restore: win::ShowWindow(window_, win::show_restore); value.succeeded = true; break;
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

auto Win32Window::message(win::HWND window, win::UINT message, win::WPARAM wparam, win::LPARAM lparam) -> win::LRESULT {
    if (message == win::msg_command) {
        Command *value;
        { auto pending = pending_command_.lock().unwrap(); value = *pending; *pending = nullptr; }
        if (value) { command(*value); win::SetEvent(value->done); }
        return 0;
    }
    if (message == win::msg_erase) return 1;
    bool active;
    { auto state = state_.lock().unwrap(); active = state->active; }
    if (!active) return message == win::msg_close ? 0 : win::DefWindowProcW(window, message, wparam, lparam);
    if (message == win::msg_minmax) {
        rstd::uint32_t width, height, dpi;
        bool fullscreen;
        {
            auto state = state_.lock().unwrap();
            width = state->minimum_width; height = state->minimum_height; dpi = state->dpi;
            fullscreen = state->fullscreen;
        }
        if (fullscreen) return win::DefWindowProcW(window, message, wparam, lparam);
        win::RECT frame{};
        if ((width || height) && win::AdjustWindowRectExForDpi(&frame, win::window_style, false, 0, dpi)) {
            auto limits = reinterpret_cast<win::MINMAXINFO *>(lparam);
            if (width) limits->ptMinTrackSize.x = long((rstd::uint64_t(width) * dpi + 48) / 96) + frame.right - frame.left;
            if (height) limits->ptMinTrackSize.y = long((rstd::uint64_t(height) * dpi + 48) / 96) + frame.bottom - frame.top;
        }
        return 0;
    }
    if (message == win::msg_paint) {
        win::ValidateRect(window, nullptr);
        auto state = state_.lock().unwrap(); changed(*state); return 0;
    }
    if (message == win::msg_dpi) {
        auto bounds = reinterpret_cast<const win::RECT *>(lparam);
        auto dpi = rstd::uint32_t(wparam & 0xffffu);
        if (!dpi) return 0;
        bool fullscreen;
        {
            auto state = state_.lock().unwrap();
            state->dpi = dpi; fullscreen = state->fullscreen; ++state->revision; changed(*state);
        }
        win::MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        if (fullscreen) {
            auto handle = win::MonitorFromWindow(window, win::nearest_monitor);
            bounds = handle && win::GetMonitorInfoW(handle, &monitor) ? &monitor.rcMonitor : nullptr;
        }
        if (bounds) win::SetWindowPos(window, nullptr, bounds->left, bounds->top,
                                      bounds->right - bounds->left, bounds->bottom - bounds->top,
                                      win::set_no_zorder | win::set_no_activate);
        text_.reposition(window, dpi);
        return 0;
    }
    if (message == win::msg_size) {
        win::RECT bounds{};
        auto has_bounds = win::GetClientRect(window, &bounds);
        auto state = state_.lock().unwrap();
        state->minimized = wparam == win::size_minimized;
        if (has_bounds && !state->minimized && bounds.right > bounds.left && bounds.bottom > bounds.top) {
            state->width = rstd::uint32_t(bounds.right - bounds.left);
            state->height = rstd::uint32_t(bounds.bottom - bounds.top);
            auto dpi = win::GetDpiForWindow(window); if (dpi) state->dpi = dpi;
        }
        ++state->revision; changed(*state); return 0;
    }
    if (message == win::msg_close) {
        auto state = state_.lock().unwrap(); state->close_requested = true; changed(*state); return 0;
    }
    if (message == win::msg_capture_changed) {
        if (!releasing_capture_) {
            auto state = state_.lock().unwrap(); InputEvent event; event.type = EventType::InputReset;
            emit(*state, rstd::move(event));
        }
        return 0;
    }
    if (message == win::msg_focus || message == win::msg_blur) {
        rstd::uint32_t dpi;
        { auto state = state_.lock().unwrap(); dpi = state->dpi; }
        text_.focus(window, message == win::msg_focus, dpi);
        auto state = state_.lock().unwrap();
        if (message == win::msg_blur) state->surrogate = 0;
        InputEvent event; event.type = message == win::msg_focus ? EventType::WindowFocusGained : EventType::WindowFocusLost;
        emit(*state, rstd::move(event)); return 0;
    }
    if (message == win::msg_ime_context && wparam)
        return win::DefWindowProcW(window, message, wparam, lparam & ~win::LPARAM(win::ime_hide_composition));
    if (message == win::msg_ime_start && text_.session().is_some()) { text_.start(); return 0; }
    if (message == win::msg_ime_composition && text_.compose(window, lparam)) return 0;
    if (message == win::msg_ime_end && text_.session().is_some()) { text_.end(); return 0; }
    if (message == win::msg_ime_char && text_.session().is_some()) return 0;
    if (message == win::msg_mouse_move || message == win::msg_mouse_leave) {
        auto state = state_.lock().unwrap();
        if (message == win::msg_mouse_leave) {
            state->tracking = false;
            InputEvent event; event.type = EventType::WindowPointerLeave; emit(*state, rstd::move(event)); return 0;
        }
        auto position = point(lparam, state->dpi);
        if (!state->tracking) {
            win::TRACKMOUSEEVENT track{sizeof(win::TRACKMOUSEEVENT), win::pointer_leave, window, 0};
            state->tracking = win::TrackMouseEvent(&track);
            InputEvent enter; enter.type = EventType::WindowPointerEnter; enter.position = position;
            emit(*state, rstd::move(enter), true);
        }
        InputEvent event; event.type = EventType::PointerMove; event.position = position;
        event.modifiers = modifiers(); emit(*state, rstd::move(event), true); return 0;
    }
    if (message == win::msg_left_down || message == win::msg_left_up ||
        message == win::msg_right_down || message == win::msg_right_up ||
        message == win::msg_middle_down || message == win::msg_middle_up ||
        message == win::msg_extra_down || message == win::msg_extra_up) {
        auto state = state_.lock().unwrap(); InputEvent event;
        event.type = message == win::msg_left_down || message == win::msg_right_down ||
                     message == win::msg_middle_down || message == win::msg_extra_down
                         ? EventType::PointerDown : EventType::PointerUp;
        event.button = message == win::msg_left_down || message == win::msg_left_up ? PointerButton::Primary
            : message == win::msg_right_down || message == win::msg_right_up ? PointerButton::Secondary
            : message == win::msg_middle_down || message == win::msg_middle_up ? PointerButton::Middle
            : win::extra_button(wparam) == win::mouse_back ? PointerButton::Back : PointerButton::Forward;
        event.position = point(lparam, state->dpi); event.modifiers = modifiers();
        emit(*state, rstd::move(event), true);
        return message == win::msg_extra_down || message == win::msg_extra_up ? 1 : 0;
    }
    if (message == win::msg_wheel || message == win::msg_hwheel) {
        win::POINT position{win::x_coordinate(lparam), win::y_coordinate(lparam)};
        if (!win::ScreenToClient(window, &position)) return 0;
        auto state = state_.lock().unwrap(); InputEvent event;
        event.type = EventType::Wheel; event.scroll_unit = ScrollUnit::Lines;
        auto delta = Scalar(win::wheel_delta(wparam)) / Scalar(win::wheel_unit);
        event.scroll_delta = message == win::msg_wheel ? Point{0, -delta} : Point{delta, 0};
        auto scale = Scalar(96) / Scalar(state->dpi);
        event.position = {Scalar(position.x) * scale, Scalar(position.y) * scale};
        event.modifiers = modifiers(); emit(*state, rstd::move(event), true); return 0;
    }
    if (message == win::msg_key_down || message == win::msg_key_up ||
        message == win::msg_sys_key_down || message == win::msg_sys_key_up) {
        auto translated = key(wparam);
        if (translated == Key::Unknown) return win::DefWindowProcW(window, message, wparam, lparam);
        auto state = state_.lock().unwrap(); InputEvent event;
        event.type = message == win::msg_key_down || message == win::msg_sys_key_down ? EventType::KeyDown : EventType::KeyUp;
        event.key = translated; event.repeat = event.type == EventType::KeyDown &&
            (rstd::uint64_t(lparam) & (rstd::uint64_t(1) << 30)) != 0;
        event.modifiers = modifiers(); emit(*state, rstd::move(event), true);
        if (message == win::msg_sys_key_down || message == win::msg_sys_key_up)
            return win::DefWindowProcW(window, message, wparam, lparam);
        return 0;
    }
    if (message == win::msg_unichar && wparam == win::unicode_nochar) return 1;
    if (message == win::msg_char || message == win::msg_unichar) {
        auto state = state_.lock().unwrap();
        if (message == win::msg_unichar && (wparam > 0x10ffff || (wparam >= 0xd800 && wparam <= 0xdfff))) state->surrogate = 0;
        else text(*state, unsigned(wparam));
        return 0;
    }
    return win::DefWindowProcW(window, message, wparam, lparam);
}

auto __stdcall Win32Window::window_proc(win::HWND window, win::UINT message, win::WPARAM wparam,
                                   win::LPARAM lparam) -> win::LRESULT {
    auto self = reinterpret_cast<Win32Window *>(win::get_data(window));
    if (message == win::msg_create) {
        auto create = reinterpret_cast<const win::CREATESTRUCTW *>(lparam);
        self = static_cast<Win32Window *>(create->lpCreateParams);
        win::set_data(window, reinterpret_cast<win::LONG_PTR>(self));
    }
    return self ? self->message(window, message, wparam, lparam)
                : win::DefWindowProcW(window, message, wparam, lparam);
}

auto __stdcall Win32Window::thread_main(void *data) -> win::DWORD {
    auto &self = *static_cast<Win32Window *>(data);
    win::SetThreadDpiAwarenessContext(win::per_monitor_v2());
    win::MSG message{}; win::PeekMessageW(&message, nullptr, 0, 0, win::no_remove);
    self.instance_ = win::GetModuleHandleW(nullptr);
    win::WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = window_proc; cls.hInstance = self.instance_; cls.hCursor = win::arrow_cursor();
    cls.lpszClassName = class_name_;
    bool registered = win::RegisterClassExW(&cls) || win::GetLastError() == win::class_exists;
    auto dpi = win::GetDpiForSystem(); if (!dpi) dpi = 96;
    win::RECT bounds{0, 0, long((rstd::uint64_t(self.initial_width_) * dpi + 48) / 96),
                     long((rstd::uint64_t(self.initial_height_) * dpi + 48) / 96)};
    if (registered && win::AdjustWindowRectExForDpi(&bounds, win::window_style, false, 0, dpi))
        self.window_ = win::CreateWindowExW(0, class_name_, self.title_.as_slice().as_raw_ptr(),
            win::window_style, win::default_position, win::default_position,
            bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, self.instance_, &self);
    if (self.window_) win::ShowWindow(self.window_, win::show_normal);
    {
        auto state = self.state_.lock().unwrap(); state->opened = self.window_ != nullptr; state->ready = true;
    }
    win::SetEvent(self.ready_);
    if (self.window_) {
        int result;
        while ((result = win::GetMessageW(&message, nullptr, 0, 0)) > 0) {
            win::TranslateMessage(&message); win::DispatchMessageW(&message);
        }
        win::DestroyWindow(self.window_);
    }
    return 0;
}

Win32Window::~Win32Window() {
    if (thread_) {
        if (thread_id_) win::PostThreadMessageW(thread_id_, win::msg_quit, 0, 0);
        win::WaitForSingleObject(thread_, win::infinite); win::CloseHandle(thread_);
    }
    if (ready_) win::CloseHandle(ready_);
    if (changed_) win::CloseHandle(changed_);
}

auto Win32Window::open(const char *title, rstd::uint32_t width, rstd::uint32_t height) -> bool {
    auto decoded = rstd::ffi::CStr::from_ptr(title).to_str();
    if (decoded.is_err()) return false;
    auto name = rstd::ffi::OsString::from(decoded.unwrap());
    auto wide = rstd::os::windows::ffi::OsStrExt::encode_wide(name.as_os_str());
    while (auto value = wide.next()) title_.push(static_cast<wchar_t>(value->to_primitive()));
    title_.push(L'\0'); initial_width_ = width; initial_height_ = height;
    ready_ = win::CreateEventW(nullptr, true, false, nullptr);
    changed_ = win::CreateEventW(nullptr, true, false, nullptr);
    if (!ready_ || !changed_) return false;
    thread_ = win::CreateThread(nullptr, 0, thread_main, this, 0, &thread_id_);
    if (!thread_ || win::WaitForSingleObject(ready_, win::infinite) != win::wait_object) return false;
    auto state = state_.lock().unwrap(); return state->ready && state->opened;
}

auto Win32Window::snapshot() -> Win32Snapshot {
    auto state = state_.lock().unwrap();
    Win32Snapshot result{state->input.take(), state->revision, state->dropped,
        state->width, state->height, state->dpi, state->redraw, state->close_requested, state->minimized};
    state->dropped = 0; state->redraw = false; win::ResetEvent(changed_); return result;
}

auto Win32Window::view() -> Win32Snapshot {
    auto state = state_.lock().unwrap();
    return {{}, state->revision, state->dropped, state->width, state->height,
            state->dpi, state->redraw, state->close_requested, state->minimized};
}

auto Win32Window::execute(Command::Kind kind, int x, int y, bool enabled,
             Option<TextInputState> *text, NativeTextInputResult *text_result) -> bool {
    if (!window_ || !alive()) return false;
    auto done = win::CreateEventW(nullptr, true, false, nullptr);
    if (!done) return false;
    Command command{kind, done, x, y, enabled};
    command.text = text;
    bool queued{};
    {
        auto pending = pending_command_.lock().unwrap();
        if (!*pending) {
            *pending = &command;
            queued = win::PostMessageW(window_, win::msg_command, 0, 0);
            if (!queued) *pending = nullptr;
        }
    }
    win::HANDLE events[]{done, thread_};
    bool completed = queued && win::WaitForMultipleObjects(2, events, false, win::infinite) == win::wait_object;
    if (queued && !completed) {
        auto pending = pending_command_.lock().unwrap();
        if (*pending == &command) *pending = nullptr;
    }
    win::CloseHandle(done);
    if (completed && text_result) *text_result = command.text_result;
    return completed && command.succeeded;
}
} // namespace vui::host::detail
