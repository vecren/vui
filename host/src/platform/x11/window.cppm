module vui.host:platform.x11.window;
import :platform.input;
import :platform.x11.connection;
import :platform.x11.input;
import :platform.x11.clipboard;
import :platform.x11.clipboard_owner;
import :platform.x11.state;
import :platform.x11.displays;
import :ffi.x11;
import :platform.xkb;
import :platform.x11.text;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;

enum class X11WindowError { State, InvalidSize, Connection, Window, Protocol };

class X11Window {
    friend struct X11TextTest;
    friend struct X11ConnectionTest;
    friend struct X11InputTest;
    X11Connection connection_;
    X11Text text_{this,
        [](void *data, InputEvent event) { static_cast<X11Window *>(data)->emit(rstd::move(event)); },
        [](void *data) { ++static_cast<X11Window *>(data)->dropped_input_; }};
    NativeInputClock native_clock_;
    X11Input xi_{this,
        [](void *data, InputEvent event) { static_cast<X11Window *>(data)->emit(rstd::move(event)); },
        [](void *data) { ++static_cast<X11Window *>(data)->dropped_input_; }, native_clock_};
    X11Clipboard clipboard_{this,
        [](void *data, InputEvent event) { static_cast<X11Window *>(data)->emit(rstd::move(event)); }};
    X11ClipboardOwner clipboard_owner_;
    X11State state_;
    X11Displays displays_;
    ffi::Window window_{};
    ffi::Atom protocols_{}, delete_window_{}, supported_atom_{}, state_atom_{}, fullscreen_atom_{};
    rstd::uint32_t width_{}, height_{};
    rstd::uint32_t minimum_width_{}, minimum_height_{};
    rstd::uint64_t revision_{};
    bool close_requested_{};
    bool detectable_repeat_{}, pointer_grabbed_{}, pressed_[256]{};
    InputQueue input_;
    InputTraceSource input_trace_;
    bool trace_input_{};
    Serial dropped_input_{}, sample_clock_{new_session()};

    void emit(InputEvent input);
    auto native_time(rstd::uint32_t value) -> SampleTime;
    static auto modifiers(unsigned state) -> Modifiers {
        return {bool(state & ffi::ShiftMask), bool(state & ffi::ControlMask),
                bool(state & ffi::Mod1Mask), bool(state & ffi::Mod4Mask)};
    }

    auto fail(X11WindowError error) -> Result<empty, X11WindowError> {
        close();
        return Err(error);
    }

  public:
    X11Window() = default;
    X11Window(const X11Window &) = delete;
    auto operator=(const X11Window &) -> X11Window & = delete;
    ~X11Window() { close(); }

    auto open(const char *title, rstd::uint32_t width, rstd::uint32_t height,
              const char *display = nullptr) -> Result<empty, X11WindowError>;
    auto resize(rstd::uint32_t width, rstd::uint32_t height) -> Result<empty, X11WindowError>;
    auto set_minimum_size(rstd::uint32_t width, rstd::uint32_t height) -> Result<empty, X11WindowError>;
    auto position() const -> Option<Point>;
    auto set_position(int x, int y) -> Result<empty, X11WindowError>;
    auto fullscreen() const -> Option<bool> { return window_ ? state_.active() : None<bool>(); }
    auto fullscreen_supported() const -> Option<bool> { return window_ ? state_.supported() : None<bool>(); }
    auto minimized() const -> Option<bool> { return window_ ? state_.minimized() : None<bool>(); }
    auto minimize() -> bool;
    auto restore() -> bool;
    auto set_fullscreen(bool enabled) -> bool;
    auto capture(bool enabled) -> NativeCaptureResult;
    auto set_text_input(Option<TextInputState> state) -> NativeTextInputResult;
    auto dispatch(bool bounded = false) -> Result<empty, X11WindowError>;
    auto synchronize() -> Result<empty, X11WindowError>;
    void close();
    auto display() const -> ffi::Display * { return connection_.display(); }
    auto request_clipboard(Serial id) -> Result<empty, ClipboardError> {
        if (!input_.can_reserve(clipboard_.pending())) return Err(ClipboardError::Limit);
        return clipboard_.request(id);
    }
    auto write_clipboard(ref<str> text) -> Result<empty, ClipboardError> { return clipboard_owner_.write(text); }
    auto clipboard_wait(Index milliseconds) const -> Index { return clipboard_owner_.wait(clipboard_.wait(milliseconds)); }
    void expire_clipboard() { clipboard_.expire(); clipboard_owner_.expire(); }
    void dispatch_clipboard_owner() { clipboard_owner_.dispatch(); }
    auto clipboard_owner_fd() const -> int { return clipboard_owner_.fd(); }
    auto clipboard_owner_pending() const -> bool { return clipboard_owner_.pending(); }
    auto clipboard_owner_failed() const -> bool { return clipboard_owner_.failed(); }
    void disable_clipboard_owner() { clipboard_owner_.close(); }
    void close_clipboard() { clipboard_.close(); clipboard_owner_.close(); }
    auto dispatch_state() -> bool { state_.dispatch(); return state_.take_changed(); }
    auto state_fd() const -> int { return state_.fd(); }
    auto state_pending() const -> bool { return state_.pending(); }
    auto state_failed() const -> bool { return state_.failed(); }
    void close_state() { state_.close(); }
    void dispatch_displays() { displays_.dispatch(); }
    auto displays() const -> DisplaySnapshot { return displays_.snapshot(); }
    auto display_revision() const -> Serial { return displays_.revision(); }
    auto displays_fd() const -> int { return displays_.fd(); }
    auto displays_pending() const -> bool { return displays_.pending(); }
    auto displays_failed() const -> bool { return displays_.failed(); }
    void close_displays() { displays_.close(); }
    void copy_internal_connections(Vec<X11InternalConnection> &result) const { connection_.copy_internal(result); }
    auto internal_connection_current(X11InternalConnection item) const -> bool { return connection_.current(item); }
    void process_internal_connection(X11InternalConnection item) { connection_.process(item); }
    auto window() const -> ffi::Window { return window_; }
    auto close_requested() const -> bool { return close_requested_; }
    auto width() const -> rstd::uint32_t { return width_; }
    auto height() const -> rstd::uint32_t { return height_; }
    auto minimum_size() const -> Size { return {Scalar(minimum_width_), Scalar(minimum_height_)}; }
    auto revision() const -> rstd::uint64_t { return revision_; }
    auto has_input() const -> bool { return !input_.is_empty() || dropped_input_ != 0; }
    void set_input_tracing(bool enabled) { trace_input_ = enabled; }
    auto take_input() -> Vec<InputEvent> { return input_.take(); }
    auto take_dropped_input() -> Serial { return rstd::exchange(dropped_input_, Serial{}); }
};
} // namespace vui::host::detail
