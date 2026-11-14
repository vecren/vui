module vui.host:platform.x11_text;
import :ffi.x11;
import :ffi.xkb;
import :platform.keyboard_text;
import :platform.x11_preedit;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;
namespace x11 = ffi::x11;

class X11Text {
    friend struct X11TextTest;
    void *sink_{};
    void (*emit_)(void *, InputEvent){};
    void (*drop_)(void *){};
    x11::Display *display_{};
    x11::Window window_{};
    x11::XIM im_{};
    x11::XIC ic_{};
    x11::XFontSet font_{};
    x11::XPoint spot_{};
    xkb::xkb_context *key_context_{};
    KeyboardCompose compose_;
    X11TextCodec codec_;
    X11Preedit preedit_;
    unsigned long event_mask_{};
    bool registered_{}, focused_{}, ic_focused_{}, suppress_{}, preedit_live_{}, callbacks_{}, positioned_{};
    Serial preedit_session_{};
    Option<TextInputState> state_;

    static auto spot(Rect area, x11::XPoint &point) -> bool;
    void free_font() {
        if (font_) x11::XFreeFontSet(display_, font_);
        font_ = nullptr;
    }

    auto accepts_preedit() const -> bool {
        return !suppress_ && focused_ && state_.is_some() && preedit_live_ &&
               preedit_session_ == state_->request.session;
    }
    void publish_preedit();
    void clear_preedit(bool publish) {
        if (publish && accepts_preedit()) { preedit_.clear(); publish_preedit(); }
        preedit_.clear(); preedit_live_ = false; preedit_session_ = 0;
    }
    static auto preedit_start(x11::XIM, char *data, char *) -> int;
    static void preedit_done(x11::XIM, char *data, char *) {
        auto &self = *reinterpret_cast<X11Text *>(data);
        self.clear_preedit(true);
    }
    static void preedit_draw(x11::XIM, char *data, char *raw);
    static void preedit_caret(x11::XIM, char *data, char *raw);

    static void im_destroyed(x11::XIM, char *data, char *);
    static void ic_destroyed(x11::XIM, char *data, char *);
    static void instantiate(x11::Display *, char *data, char *) {
        reinterpret_cast<X11Text *>(data)->open_im();
    }
    void update_focus();
    void reset();
    void open_im();
  public:
    X11Text(void *sink, void (*emit)(void *, InputEvent), void (*drop)(void *)) : sink_(sink), emit_(emit), drop_(drop) {}
    X11Text(const X11Text &) = delete;
    auto operator=(const X11Text &) -> X11Text & = delete;
    ~X11Text() { close(); }
    void open(x11::Display *display, x11::Window window, unsigned long event_mask);
    void close();
    auto set_state(Option<TextInputState> state) -> NativeTextInputResult;
    void focus(bool focused) {
        if (focused_ && !focused) reset();
        focused_ = focused; update_focus();
    }
    auto session() const -> Option<Serial> {
        return state_.is_some() ? Some(Serial(state_->request.session)) : None<Serial>();
    }
    auto filter(x11::XEvent &event) -> bool { return x11::XFilterEvent(&event, 0) != 0; }
    auto lookup(x11::XKeyEvent &event, rstd::uint32_t symbol, bool shortcut, bool &invalid) -> TextInput;
};
}
