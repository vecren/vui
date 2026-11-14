module vui.host;
import :platform.x11_text;
import :ffi.x11;
import :ffi.xkb;
import :platform.keyboard_text;
import :platform.x11_preedit;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;
namespace x11 = ffi::x11;

auto X11Text::spot(Rect area, x11::XPoint &point) -> bool {
    if (!area.valid()) return false;
    auto x = __builtin_floor(area.x), y = __builtin_ceil(area.y + area.height);
    if (!finite(x) || !finite(y) || x < -32768 || x > 32767 || y < -32768 || y > 32767) return false;
    point.x = static_cast<short>(x); point.y = static_cast<short>(y); return true;
}

void X11Text::publish_preedit() {
    if (!accepts_preedit()) return;
    auto transaction = TextTransaction::create(preedit_.transaction());
    if (transaction.is_err()) { drop_(sink_); return; }
    InputEvent event; event.type = EventType::TextTransaction; event.text_session = preedit_session_;
    event.transaction = rstd::move(transaction).unwrap(); emit_(sink_, rstd::move(event));
}

auto X11Text::preedit_start(x11::XIM, char *data, char *) -> int {
    auto &self = *reinterpret_cast<X11Text *>(data);
    if (!self.suppress_ && self.focused_ && self.state_.is_some()) {
        self.preedit_.clear(); self.preedit_live_ = true;
        self.preedit_session_ = self.state_->request.session;
    }
    return -1;
}

void X11Text::preedit_draw(x11::XIM, char *data, char *raw) {
    auto &self = *reinterpret_cast<X11Text *>(data);
    if (!self.accepts_preedit()) return;
    auto draw = reinterpret_cast<x11::XIMPreeditDrawCallbackStruct *>(raw);
    if (!draw || draw->chg_first < 0 || draw->chg_length < 0 || draw->caret < 0) {
        self.drop_(self.sink_); self.clear_preedit(true); return;
    }
    auto first = Index(draw->chg_first), removed = Index(draw->chg_length), caret = Index(draw->caret);
    bool valid{};
    if (!draw->text) {
        valid = self.preedit_.replace(first, removed, ""_str, {}, caret);
    } else {
        auto &text = *draw->text;
        auto feedback = text.feedback ? slice<unsigned long>::from_raw_parts(text.feedback, usize(text.length))
                                      : slice<unsigned long>{};
        bool has_string = text.encoding_is_wchar ? text.string.wide_char != nullptr : text.string.multi_byte != nullptr;
        if (has_string) {
            auto decoded = self.codec_.decode(text);
            if (decoded.is_some()) valid = self.preedit_.replace(first, removed, decoded->as_str(), feedback, caret);
        } else if (text.feedback) valid = self.preedit_.update_feedback(first, feedback, caret);
        else valid = self.preedit_.move_caret(caret, self.preedit_.visible());
    }
    if (!valid) { self.drop_(self.sink_); self.clear_preedit(true); return; }
    self.publish_preedit();
}

void X11Text::preedit_caret(x11::XIM, char *data, char *raw) {
    auto &self = *reinterpret_cast<X11Text *>(data);
    if (!self.accepts_preedit() || !raw) return;
    auto caret = reinterpret_cast<x11::XIMPreeditCaretCallbackStruct *>(raw);
    auto position = self.preedit_.caret();
    if (caret->direction == x11::caret_forward && usize(position) < self.preedit_.length()) ++position;
    else if (caret->direction == x11::caret_backward && position) --position;
    else if (caret->direction == x11::caret_absolute && caret->position >= 0 &&
             usize(caret->position) <= self.preedit_.length()) position = Index(caret->position);
    caret->position = int(position);
    self.preedit_.move_caret(position, caret->style != x11::caret_invisible);
    self.publish_preedit();
}

void X11Text::im_destroyed(x11::XIM, char *data, char *) {
    auto &self = *reinterpret_cast<X11Text *>(data);
    self.clear_preedit(true);
    self.im_ = nullptr; self.ic_ = nullptr; self.ic_focused_ = false;
    self.callbacks_ = self.positioned_ = false;
    self.compose_.reset();
}

void X11Text::ic_destroyed(x11::XIM, char *data, char *) {
    auto &self = *reinterpret_cast<X11Text *>(data);
    self.clear_preedit(true);
    self.ic_ = nullptr; self.ic_focused_ = false;
    self.callbacks_ = self.positioned_ = false;
    self.compose_.reset();
}

void X11Text::update_focus() {
    bool active = ic_ && focused_ && state_.is_some();
    if (active == ic_focused_) return;
    if (active) x11::XSetICFocus(ic_);
    else if (ic_) x11::XUnsetICFocus(ic_);
    ic_focused_ = active;
}

void X11Text::reset() {
    compose_.reset();
    clear_preedit(true);
    if (!ic_) return;
    suppress_ = true;
    auto discarded = x11::Xutf8ResetIC(ic_);
    suppress_ = false;
    if (discarded) x11::XFree(discarded);
}

void X11Text::open_im() {
    if (im_ || !display_ || !window_) return;
    free_font();
    im_ = x11::XOpenIM(display_, nullptr, nullptr, nullptr);
    if (!im_) return;
    x11::XIMStyles *styles{};
    bool supported{}, callback_style{}, callback_none_style{}, position_style{}, position_none_style{};
    if (!x11::styles(im_, &styles) && styles) {
        for (unsigned i = 0; i < styles->count_styles; ++i)
            supported |= styles->supported_styles[i] == x11::input_style;
        for (unsigned i = 0; i < styles->count_styles; ++i) {
            callback_style |= styles->supported_styles[i] == x11::callback_style;
            callback_none_style |= styles->supported_styles[i] == x11::callback_none_style;
            position_style |= styles->supported_styles[i] == x11::position_style;
            position_none_style |= styles->supported_styles[i] == x11::position_none_style;
        }
    }
    if (styles) x11::XFree(styles);
    if (!supported && !callback_style && !callback_none_style && !position_style && !position_none_style) {
        x11::XCloseIM(im_); im_ = nullptr; return;
    }
    x11::XIMCallback im_callback{reinterpret_cast<char *>(this), im_destroyed};
    if (x11::destroy_callback(im_, &im_callback)) { x11::XCloseIM(im_); im_ = nullptr; return; }
    x11::XIMCallback ic_callback{reinterpret_cast<char *>(this), ic_destroyed};
    if ((callback_style || callback_none_style) && codec_.configure(x11::XLocaleOfIM(im_))) {
        x11::XIMCallback start{reinterpret_cast<char *>(this), reinterpret_cast<x11::XIMProc>(preedit_start)};
        x11::XIMCallback done{reinterpret_cast<char *>(this), preedit_done};
        x11::XIMCallback draw{reinterpret_cast<char *>(this), preedit_draw};
        x11::XIMCallback caret{reinterpret_cast<char *>(this), preedit_caret};
        if (callback_style) ic_ = x11::create_callback_context(im_, window_, x11::callback_style,
                                                                &ic_callback, &start, &done, &draw, &caret);
        if (!ic_ && callback_none_style)
            ic_ = x11::create_callback_context(im_, window_, x11::callback_none_style,
                                                &ic_callback, &start, &done, &draw, &caret);
        callbacks_ = ic_ != nullptr;
    }
    if (!ic_ && (position_style || position_none_style)) {
        x11::XPoint initial{};
        bool valid = state_.is_none() || spot(state_->request.area, initial);
        if (valid) font_ = x11::create_fontset(display_);
        if (font_) {
            if (position_style) ic_ = x11::create_position_context(im_, window_, x11::position_style,
                                                                    &ic_callback, font_, &initial);
            if (!ic_ && position_none_style)
                ic_ = x11::create_position_context(im_, window_, x11::position_none_style,
                                                    &ic_callback, font_, &initial);
            if (ic_) { positioned_ = true; spot_ = initial; }
            else free_font();
        }
    }
    if (!ic_ && supported) ic_ = x11::create_context(im_, window_, &ic_callback);
    if (!ic_) { x11::XCloseIM(im_); im_ = nullptr; return; }
    compose_.reset();
    unsigned long filter{};
    if (!x11::filter_events(ic_, &filter)) x11::XSelectInput(display_, window_, event_mask_ | filter);
    update_focus();
}

void X11Text::open(x11::Display *display, x11::Window window, unsigned long event_mask) {
    display_ = display; window_ = window; event_mask_ = event_mask;
    key_context_ = xkb::xkb_context_new(xkb::XKB_CONTEXT_NO_FLAGS);
    compose_.configure(key_context_);
    if (!x11::XSupportsLocale() || !x11::XSetLocaleModifiers("")) return;
    registered_ = x11::XRegisterIMInstantiateCallback(display_, nullptr, nullptr, nullptr,
                                                        instantiate, reinterpret_cast<char *>(this));
    if (!registered_) open_im();
}

void X11Text::close() {
    reset();
    state_ = None(); update_focus();
    if (registered_) x11::XUnregisterIMInstantiateCallback(display_, nullptr, nullptr, nullptr,
                                                           instantiate, reinterpret_cast<char *>(this));
    registered_ = false;
    if (ic_) { auto ic = ic_; ic_ = nullptr; x11::XDestroyIC(ic); }
    if (im_) { auto im = im_; im_ = nullptr; x11::XCloseIM(im); }
    free_font();
    compose_.clear();
    codec_.clear(); callbacks_ = positioned_ = false;
    if (key_context_) xkb::xkb_context_unref(key_context_);
    key_context_ = nullptr;
    display_ = nullptr; window_ = 0; focused_ = false; ic_focused_ = false;
}

auto X11Text::set_state(Option<TextInputState> state) -> NativeTextInputResult {
    if (state.is_some() && !state->valid()) return NativeTextInputResult::Failed;
    if (positioned_ && state.is_some()) {
        x11::XPoint point{};
        if (!spot(state->request.area, point)) return NativeTextInputResult::Failed;
        if (point.x != spot_.x || point.y != spot_.y) {
            if (!x11::set_spot(ic_, &point)) return NativeTextInputResult::Failed;
            spot_ = point;
            x11::XFlush(display_);
        }
    }
    auto old = session();
    auto next = state.is_some() ? Some(state->request.session) : None<Serial>();
    if (old != next) reset();
    state_ = rstd::move(state); update_focus();
    return state_.is_none() ? NativeTextInputResult::Succeeded
                           : ic_ ? NativeTextInputResult::Succeeded : NativeTextInputResult::Unsupported;
}

auto X11Text::lookup(x11::XKeyEvent &event, rstd::uint32_t symbol, bool shortcut, bool &invalid) -> TextInput {
    invalid = false;
    if (!focused_ || state_.is_none()) return {};
    if (!ic_) {
        if (shortcut) { compose_.reset(); return {}; }
        auto result = compose_.feed(symbol);
        if (result == ComposeResult::Pending) return {};
        char bytes[4096]{};
        auto size = result == ComposeResult::Composed ? compose_.utf8(bytes, sizeof(bytes))
                                                       : xkb::xkb_keysym_to_utf8(symbol, bytes, sizeof(bytes));
        // xkb_keysym_to_utf8 includes the terminating NUL in its return value.
        if (result == ComposeResult::Plain && size > 0) --size;
        if (size <= 0 || size >= int(sizeof(bytes))) return {};
        return keyboard_text_from_utf8(bytes, size, invalid);
    }
    if (!ic_focused_) return {};
    char local[128]{};
    int status{};
    auto count = x11::Xutf8LookupString(ic_, &event, local, sizeof(local), nullptr, &status);
    Vec<u8> large;
    const char *bytes = local;
    if (status == x11::buffer_overflow && count > 0) {
        large.resize(usize(count), u8(0));
        auto raw = reinterpret_cast<char *>(large.as_mut_slice().as_raw_ptr());
        count = x11::Xutf8LookupString(ic_, &event, raw, count, nullptr, &status);
        bytes = raw;
    }
    if ((status != x11::lookup_chars && status != x11::lookup_both) || count <= 0) return {};
    return keyboard_text_from_utf8(bytes, count, invalid);
}
} // namespace vui::host::detail
