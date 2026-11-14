module vui.host:platform.win32_text;
import :ffi.win32;
import vui;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;
namespace win = ffi::win32;

class Win32Text {
    void *sink_;
    void (*emit_)(void *, InputEvent);
    Option<TextInputState> state_;
    rstd::uint32_t placement_dpi_{};
    bool focused_{}, composing_{}, preedit_{}, cancelling_{};

    static auto read(win::HIMC context, unsigned kind) -> Option<Vec<u16>>;
    static auto text(slice<u16> units) -> Option<TextInput>;
    static auto cursor(slice<u16> units, long offset) -> Option<Index>;
    void publish(TextInputTransaction value);
    void clear_preedit();
    void cancel(win::HWND window);
    static auto position_valid(Rect area, rstd::uint32_t dpi) -> bool;
    auto place(win::HWND window, Rect area, rstd::uint32_t dpi) -> NativeTextInputResult;

  public:
    Win32Text(void *sink, void (*emit)(void *, InputEvent)) : sink_(sink), emit_(emit) {}
    static auto transaction(Option<slice<u16>> result, Option<slice<u16>> preedit,
                            long position, bool previous_preedit) -> Option<TextInputTransaction>;
    auto session() const -> Option<Serial> {
        return state_.is_some() ? Some(Serial(state_->request.session)) : None<Serial>();
    }
    auto composing() const -> bool { return composing_; }
    auto accepts_text() const -> bool { return focused_ && state_.is_some() && !composing_ && !cancelling_; }
    auto set_state(win::HWND window, Option<TextInputState> value, rstd::uint32_t dpi)
        -> NativeTextInputResult;
    void focus(win::HWND window, bool focused, rstd::uint32_t dpi);
    void reposition(win::HWND window, rstd::uint32_t dpi) {
        if (state_.is_some())
            placement_dpi_ = place(window, state_->request.area, dpi) == NativeTextInputResult::Succeeded ? dpi : 0;
    }
    void start() { if (!cancelling_ && focused_ && state_.is_some()) composing_ = true; }
    void end() { clear_preedit(); composing_ = false; }
    void close(win::HWND window) { cancel(window); state_ = None(); placement_dpi_ = 0; focused_ = false; }

    auto compose(win::HWND window, win::LPARAM flags) -> bool;
};
}
