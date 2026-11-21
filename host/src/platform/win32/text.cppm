module vui.host:platform.win32.text;
import :platform.win32.ffi;
import vui;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

class Win32Text {
    void *sink_;
    void (*emit_)(void *, InputEvent);
    Option<TextInputState> state_;
    rstd::uint32_t placement_dpi_{};
    bool focused_{}, composing_{}, preedit_{}, cancelling_{};

    static auto read(ffi::HIMC context, unsigned kind) -> Option<Vec<u16>>;
    static auto text(slice<u16> units) -> Option<TextInput>;
    static auto cursor(slice<u16> units, long offset) -> Option<Index>;
    void publish(TextInputTransaction value);
    void clear_preedit();
    void cancel(ffi::HWND window);
    static auto position_valid(Rect area, rstd::uint32_t dpi) -> bool;
    auto place(ffi::HWND window, Rect area, rstd::uint32_t dpi) -> NativeTextInputResult;

  public:
    Win32Text(void *sink, void (*emit)(void *, InputEvent)) : sink_(sink), emit_(emit) {}
    static auto transaction(Option<slice<u16>> result, Option<slice<u16>> preedit,
                            long position, bool previous_preedit) -> Option<TextInputTransaction>;
    auto session() const -> Option<Serial> {
        return state_.is_some() ? Some(Serial(state_->request.session)) : None<Serial>();
    }
    auto composing() const -> bool { return composing_; }
    auto accepts_text() const -> bool { return focused_ && state_.is_some() && !composing_ && !cancelling_; }
    auto set_state(ffi::HWND window, Option<TextInputState> value, rstd::uint32_t dpi)
        -> NativeTextInputResult;
    void focus(ffi::HWND window, bool focused, rstd::uint32_t dpi);
    void reposition(ffi::HWND window, rstd::uint32_t dpi) {
        if (state_.is_some())
            placement_dpi_ = place(window, state_->request.area, dpi) == NativeTextInputResult::Succeeded ? dpi : 0;
    }
    void start() { if (!cancelling_ && focused_ && state_.is_some()) composing_ = true; }
    void end() { clear_preedit(); composing_ = false; }
    void close(ffi::HWND window) { cancel(window); state_ = None(); placement_dpi_ = 0; focused_ = false; }

    auto compose(ffi::HWND window, ffi::LPARAM flags) -> bool;
};
}
