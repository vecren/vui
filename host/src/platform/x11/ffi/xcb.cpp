module vui.host;
import :platform.x11.ffi.xcb;

namespace ffi {
using namespace rstd::prelude;

auto XcbDispatch::open() -> Option<XcbDispatch> {
    auto opened = rstd::dlopn::Library::open(rstd::ffi::CStr::from_ptr("libxcb.so.1"));
    if (opened.is_err())
        return None();
    auto result = XcbDispatch(rstd::move(opened).unwrap());
#define LOAD(name)                                                                                                     \
    {                                                                                                                  \
        auto symbol = result.library_.symbol<decltype(result.name)>(rstd::ffi::CStr::from_ptr(#name));                 \
        if (symbol.is_err())                                                                                           \
            return None();                                                                                             \
        result.name = rstd::move(symbol).unwrap();                                                                     \
        if (!result.name)                                                                                              \
            return None();                                                                                             \
    }
    LOAD(xcb_connect);
    LOAD(xcb_disconnect);
    LOAD(xcb_connection_has_error);
    LOAD(xcb_get_file_descriptor);
    LOAD(xcb_get_setup);
    LOAD(xcb_setup_roots_iterator);
    LOAD(xcb_screen_next);
    LOAD(xcb_generate_id);
    LOAD(xcb_create_window);
    LOAD(xcb_destroy_window);
    LOAD(xcb_change_window_attributes);
    LOAD(xcb_intern_atom);
    LOAD(xcb_intern_atom_reply);
    LOAD(xcb_set_selection_owner);
    LOAD(xcb_get_selection_owner);
    LOAD(xcb_get_selection_owner_reply);
    LOAD(xcb_change_property);
    LOAD(xcb_get_property);
    LOAD(xcb_get_property_reply);
    LOAD(xcb_get_property_value);
    LOAD(xcb_get_property_value_length);
    LOAD(xcb_get_geometry);
    LOAD(xcb_translate_coordinates);
    LOAD(xcb_get_extension_data);
    LOAD(xcb_send_event);
    LOAD(xcb_get_input_focus);
    LOAD(xcb_get_input_focus_reply);
    LOAD(xcb_poll_for_event);
    LOAD(xcb_poll_for_reply);
    LOAD(xcb_flush);
#undef LOAD
    return Some(rstd::move(result));
}
} // namespace ffi
