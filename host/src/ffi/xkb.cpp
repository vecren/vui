module vui.host;
import :ffi.xkb;

namespace ffi {
using namespace rstd::prelude;
using namespace rstd::literals;

auto XkbDispatch::open() -> Result<rstd::sync::Arc<XkbDispatch>, rstd::dlopn::Error> {
    auto loaded = rstd::dlopn::Library::open(rstd::ffi::CStr::from_ptr("libxkbcommon.so.0"));
    if (loaded.is_err())
        return Err(rstd::move(loaded).unwrap_err());
    XkbDispatch api(rstd::move(loaded).unwrap());
#define LOAD(name)                                                                                                     \
    {                                                                                                                  \
        auto symbol = api.library_.symbol<decltype(api.name)>(rstd::ffi::CStr::from_ptr(#name));                       \
        if (symbol.is_err())                                                                                           \
            return Err(rstd::move(symbol).unwrap_err());                                                               \
        api.name = symbol.unwrap();                                                                                    \
        if (!api.name)                                                                                                 \
            return Err(rstd::dlopn::Error(String::make("missing " #name " symbol"_str)));                              \
    }
    LOAD(xkb_context_new);
    LOAD(xkb_context_unref);
    LOAD(xkb_keymap_new_from_string);
    LOAD(xkb_keymap_new_from_names);
    LOAD(xkb_keymap_new_from_buffer);
    LOAD(xkb_keymap_unref);
    LOAD(xkb_keymap_key_repeats);
    LOAD(xkb_keymap_mod_get_index);
    LOAD(xkb_state_new);
    LOAD(xkb_state_unref);
    LOAD(xkb_state_update_mask);
    LOAD(xkb_state_key_get_one_sym);
    LOAD(xkb_state_key_get_utf8);
    LOAD(xkb_keysym_to_utf8);
    LOAD(xkb_state_mod_name_is_active);
    LOAD(xkb_compose_table_new_from_locale);
    LOAD(xkb_compose_table_unref);
    LOAD(xkb_compose_state_new);
    LOAD(xkb_compose_state_unref);
    LOAD(xkb_compose_state_feed);
    LOAD(xkb_compose_state_get_status);
    LOAD(xkb_compose_state_get_utf8);
    LOAD(xkb_compose_state_reset);
#undef LOAD
    return Ok(rstd::sync::Arc<XkbDispatch>::make(rstd::move(api)));
}
} // namespace ffi
