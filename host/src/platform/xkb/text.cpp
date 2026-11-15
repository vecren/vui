module vui.host;
import :platform.xkb.text;
import :ffi.xkb;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

auto KeyboardCompose::configure(ffi::xkb_context *context, const char *locale) -> bool {
    clear();
    if (!context || !locale) return false;
    auto table = ffi::xkb_compose_table_new_from_locale(context, locale, ffi::XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (!table) return false;
    state_ = ffi::xkb_compose_state_new(table, ffi::XKB_COMPOSE_STATE_NO_FLAGS);
    ffi::xkb_compose_table_unref(table);
    return state_ != nullptr;
}

void KeyboardCompose::configure(ffi::xkb_context *context) {
    String locale = String::make("C"_str);
    auto all = rstd::env::var("LC_ALL"_str);
    auto ctype = rstd::env::var("LC_CTYPE"_str);
    auto lang = rstd::env::var("LANG"_str);
    if (all.is_ok() && !all->as_str()->is_empty()) locale = rstd::move(all).unwrap();
    else if (ctype.is_ok() && !ctype->as_str()->is_empty()) locale = rstd::move(ctype).unwrap();
    else if (lang.is_ok() && !lang->as_str()->is_empty()) locale = rstd::move(lang).unwrap();
    locale.push(U'\0');
    (void)configure(context, reinterpret_cast<const char *>(locale.as_str()->as_bytes().as_ptr().as_raw_ptr()));
}

auto KeyboardCompose::feed(rstd::uint32_t symbol) -> ComposeResult {
    if (!state_ || ffi::xkb_compose_state_feed(state_, symbol) != ffi::XKB_COMPOSE_FEED_ACCEPTED)
        return ComposeResult::Plain;
    auto status = ffi::xkb_compose_state_get_status(state_);
    if (status == ffi::XKB_COMPOSE_COMPOSING || status == ffi::XKB_COMPOSE_CANCELLED)
        return ComposeResult::Pending;
    return status == ffi::XKB_COMPOSE_COMPOSED ? ComposeResult::Composed : ComposeResult::Plain;
}

auto keyboard_text_from_utf8(const char *bytes, int size, bool &invalid) -> TextInput {
    invalid = false;
    if (!bytes || size <= 0) return {};
    auto decoded = rstd::str_::from_utf8(slice<u8>::from_raw_parts(
        reinterpret_cast<const rstd::byte *>(bytes), usize(size)));
    if (decoded.is_err()) { invalid = true; return {}; }
    auto chars = decoded.unwrap()->chars();
    for (auto cp = chars.next(); cp.is_some(); cp = chars.next()) {
        auto value = cp->to_primitive();
        if (value < 32 || (value >= 127 && value < 160)) return {};
    }
    auto result = TextInput::create(decoded.unwrap());
    if (result.is_err()) { invalid = true; return {}; }
    return rstd::move(result).unwrap();
}
} // namespace vui::host::detail
