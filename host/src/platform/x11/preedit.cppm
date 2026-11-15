module vui.host:platform.x11.preedit;
import :ffi.x11;
import :ffi.locale;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

class X11TextCodec {
    ffi::locale_t locale_{};
    static auto scalar(wchar_t value) -> bool {
        auto cp = rstd::uint32_t(value);
        return cp != 0 && cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff);
    }
  public:
    X11TextCodec() = default;
    X11TextCodec(const X11TextCodec &) = delete;
    auto operator=(const X11TextCodec &) -> X11TextCodec & = delete;
    ~X11TextCodec() { clear(); }
    void clear() {
        if (locale_) ffi::freelocale(locale_);
        locale_ = nullptr;
    }
    auto configure(const char *name) -> bool;
    auto decode(const ffi::XIMText &text) const -> Option<String>;
};

class X11Preedit {
    struct Character { char32_t value{}; unsigned long feedback{}; };
    Vec<Character> characters_;
    Index caret_{};
    bool visible_{true};

  public:
    void clear() { characters_.clear(); caret_ = 0; visible_ = true; }
    auto length() const -> usize { return characters_.len(); }
    auto caret() const -> Index { return caret_; }
    auto visible() const -> bool { return visible_; }
    auto replace(Index first, Index removed, ref<str> text, slice<unsigned long> feedback,
                 Index caret) -> bool;
    auto update_feedback(Index first, slice<unsigned long> feedback, Index caret) -> bool;
    auto move_caret(Index caret, bool visible) -> bool {
        if (usize(caret) > characters_.len()) return false;
        caret_ = caret; visible_ = visible; return true;
    }
    auto transaction() const -> TextInputTransaction;
};
}
