module vui.host:platform.x11_preedit;
import :ffi.x11;
import :ffi.locale;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

class X11TextCodec {
    ffi::locale::locale_t locale_{};
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
        if (locale_) ffi::locale::freelocale(locale_);
        locale_ = nullptr;
    }
    auto configure(const char *name) -> bool {
        clear();
        if (name) locale_ = ffi::locale::newlocale(ffi::locale::ctype_mask, name, nullptr);
        return locale_ != nullptr;
    }
    auto decode(const ffi::x11::XIMText &text) const -> Option<String> {
        if (!locale_) return None();
        String result;
        if (text.encoding_is_wchar) {
            if (!text.string.wide_char) return None();
            for (unsigned i = 0; i < text.length; ++i) {
                auto value = text.string.wide_char[i];
                if (!scalar(value)) return None();
                result.push(char32_t(value));
            }
            return Some(rstd::move(result));
        }
        if (!text.string.multi_byte) return None();
        auto previous = ffi::locale::uselocale(locale_);
        if (!previous) return None();
        ffi::locale::mbstate_t state{};
        auto bytes = text.string.multi_byte;
        bool valid = true;
        for (unsigned i = 0; i < text.length && valid; ++i) {
            wchar_t value{};
            bool complete{};
            for (unsigned n = 0; n < 64; ++n) {
                auto consumed = ffi::locale::mbrtowc(&value, bytes++, 1, &state);
                if (consumed == decltype(consumed)(-2)) continue;
                if (consumed == decltype(consumed)(-1) || consumed == 0 || !scalar(value)) valid = false;
                else { result.push(char32_t(value)); complete = true; }
                break;
            }
            if (!complete) valid = false;
        }
        ffi::locale::uselocale(previous);
        return valid ? Some(rstd::move(result)) : None<String>();
    }
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
                 Index caret) -> bool {
        auto start = usize(first), count = usize(removed);
        if (start > characters_.len() || count > characters_.len() - start ||
            text->len() > usize(1048576)) return false;
        Vec<Character> inserted;
        auto chars = text->chars();
        for (auto cp = chars.next(); cp.is_some(); cp = chars.next())
            inserted.push(Character{char32_t(cp->to_primitive()), 0});
        if (!feedback.is_empty() && feedback.len() != inserted.len()) return false;
        auto next_length = characters_.len() - count + inserted.len();
        if (next_length > usize(1048576) || usize(caret) > next_length) return false;
        auto inherited = start != usize(0) ? characters_[start - usize(1)].feedback
                               : start + count < characters_.len() ? characters_[start + count].feedback : 0;
        for (usize i{}; i < inserted.len(); i += usize(1))
            inserted[i].feedback = feedback.is_empty() ? inherited : feedback[i];
        Vec<Character> next;
        for (usize i{}; i < start; i += usize(1)) next.push(Character(characters_[i]));
        for (auto ch : inserted) next.push(rstd::move(ch));
        for (usize i = start + count; i < characters_.len(); i += usize(1)) next.push(Character(characters_[i]));
        characters_ = rstd::move(next); caret_ = caret;
        return true;
    }
    auto update_feedback(Index first, slice<unsigned long> feedback, Index caret) -> bool {
        auto start = usize(first);
        if (start > characters_.len() || feedback.len() > characters_.len() - start ||
            usize(caret) > characters_.len()) return false;
        for (usize i{}; i < feedback.len(); i += usize(1)) characters_[start + i].feedback = feedback[i];
        caret_ = caret;
        return true;
    }
    auto move_caret(Index caret, bool visible) -> bool {
        if (usize(caret) > characters_.len()) return false;
        caret_ = caret; visible_ = visible; return true;
    }
    auto transaction() const -> TextInputTransaction {
        String value;
        Index cursor{}, begin{}, end{};
        bool in_selection{}, selected{};
        for (usize i{}; i < characters_.len(); i += usize(1)) {
            if (i == usize(caret_)) cursor = Index(value.len().to_primitive());
            bool highlight = (characters_[i].feedback & (ffi::x11::preedit_reverse | ffi::x11::preedit_highlight)) != 0;
            if (highlight && !selected && !in_selection) {
                begin = Index(value.len().to_primitive()); in_selection = true;
            } else if (!highlight && in_selection) {
                end = Index(value.len().to_primitive()); in_selection = false; selected = true;
            }
            value.push(characters_[i].value);
        }
        if (usize(caret_) == characters_.len()) cursor = Index(value.len().to_primitive());
        if (in_selection) { end = Index(value.len().to_primitive()); selected = true; }
        Index anchor = cursor;
        if (selected && cursor == end) anchor = begin;
        else if (selected && cursor == begin) anchor = end;
        TextInputTransaction transaction;
        transaction.preedit = TextInput::create(value.as_str()).unwrap();
        transaction.preedit_cursor = Some(TextPreeditCursor{anchor, cursor, visible_, anchor != cursor});
        return transaction;
    }
};
}
