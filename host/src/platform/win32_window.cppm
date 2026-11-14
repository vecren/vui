module vui.host:platform.win32_window;
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

struct Win32Snapshot {
    Vec<InputEvent> input;
    Serial revision{}, dropped{};
    rstd::uint32_t width{}, height{}, dpi{96};
    bool redraw{}, close_requested{}, minimized{};
};

class Win32Window {
    struct State {
        InputQueue input;
        InputTraceSource trace;
        NativeInputClock clock;
        Serial received_clock{new_session()}, revision{}, dropped{};
        rstd::uint32_t width{}, height{}, dpi{96};
        rstd::uint32_t minimum_width{}, minimum_height{};
        unsigned surrogate{};
        bool ready{}, opened{}, redraw{}, close_requested{}, minimized{}, tracking{}, tracing{}, fullscreen{}, active{true};
    };
    struct Command {
        enum class Kind { Synchronize, Deactivate, Resize, MinimumSize, Fullscreen, TextInput, Capture, Minimize, Restore } kind;
        win::HANDLE done{};
        int x{}, y{};
        bool enabled{}, succeeded{};
        Option<TextInputState> *text{};
        NativeTextInputResult text_result{NativeTextInputResult::Failed};
    };
    static void text_event(void *sink, InputEvent event);
    rstd::sync::Mutex<State> state_;
    Win32Text text_{this, text_event};
    rstd::sync::Mutex<Command *> pending_command_{nullptr};
    Vec<wchar_t> title_;
    win::HANDLE ready_{}, changed_{}, thread_{};
    win::HWND window_{};
    win::HINSTANCE instance_{};
    win::DWORD thread_id_{};
    bool releasing_capture_{};
    win::WINDOWPLACEMENT windowed_placement_{};
    win::LONG_PTR windowed_style_{};
    rstd::uint32_t initial_width_{}, initial_height_{};
    static constexpr wchar_t class_name_[] = L"vui.host.Win32Window";

    void changed(State &state) {
        state.redraw = true;
        win::SetEvent(changed_);
    }
    void emit(State &state, InputEvent event, bool native_time = false);
    static auto modifiers() -> Modifiers;
    static auto key(win::WPARAM value) -> Key;
    static auto point(win::LPARAM value, rstd::uint32_t dpi) -> Point {
        auto scale = Scalar(96) / Scalar(dpi ? dpi : 96);
        return {Scalar(win::x_coordinate(value)) * scale, Scalar(win::y_coordinate(value)) * scale};
    }
    void command(Command &value);
    void text(State &state, unsigned value);
    auto message(win::HWND window, win::UINT message, win::WPARAM wparam, win::LPARAM lparam) -> win::LRESULT;
    static auto __stdcall window_proc(win::HWND window, win::UINT message, win::WPARAM wparam,
                                       win::LPARAM lparam) -> win::LRESULT;
    static auto __stdcall thread_main(void *data) -> win::DWORD;
  public:
    Win32Window() = default;
    Win32Window(const Win32Window &) = delete;
    auto operator=(const Win32Window &) -> Win32Window & = delete;
    ~Win32Window();
    auto open(const char *title, rstd::uint32_t width, rstd::uint32_t height) -> bool;
    auto handle() const -> win::HWND { return window_; }
    auto instance() const -> win::HINSTANCE { return instance_; }
    auto changed_handle() const -> win::HANDLE { return changed_; }
    auto thread_handle() const -> win::HANDLE { return thread_; }
    auto alive() const -> bool { return thread_ && win::WaitForSingleObject(thread_, 0) == win::wait_timeout; }
    auto snapshot() -> Win32Snapshot;
    auto view() -> Win32Snapshot;
    void tracing(bool enabled) { auto state = state_.lock().unwrap(); state->tracing = enabled; }
    auto execute(Command::Kind kind, int x = 0, int y = 0, bool enabled = false,
                 Option<TextInputState> *text = nullptr, NativeTextInputResult *text_result = nullptr) -> bool;
    auto synchronize() -> bool { return execute(Command::Kind::Synchronize); }
    auto deactivate() -> bool { return execute(Command::Kind::Deactivate); }
    auto resize(int width, int height) -> bool { return execute(Command::Kind::Resize, width, height); }
    auto set_minimum_size(int width, int height) -> bool { return execute(Command::Kind::MinimumSize, width, height); }
    auto set_fullscreen(bool enabled) -> bool { return execute(Command::Kind::Fullscreen, 0, 0, enabled); }
    auto set_text_input(Option<TextInputState> value) -> NativeTextInputResult {
        NativeTextInputResult result{NativeTextInputResult::Failed};
        return execute(Command::Kind::TextInput, 0, 0, false, &value, &result) ? result : NativeTextInputResult::Failed;
    }
    auto fullscreen() -> bool { auto state = state_.lock().unwrap(); return state->fullscreen; }
    auto minimum_size() -> Size {
        auto state = state_.lock().unwrap();
        return {Scalar(state->minimum_width), Scalar(state->minimum_height)};
    }
    auto capture(bool enabled) -> bool { return execute(Command::Kind::Capture, 0, 0, enabled); }
    auto minimize() -> bool { return execute(Command::Kind::Minimize); }
    auto restore() -> bool { return execute(Command::Kind::Restore); }
};
}
