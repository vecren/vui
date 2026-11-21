module vui.host;
import :platform.x11.ffi.xcb_randr;

namespace ffi {
using namespace rstd::prelude;

auto RandrDispatch::open() -> Option<RandrDispatch> {
    auto opened = rstd::dlopn::Library::open(rstd::ffi::CStr::from_ptr("libxcb-randr.so.0"));
    if (opened.is_err())
        return None();
    auto result = RandrDispatch(rstd::move(opened).unwrap());
#define LOAD(name)                                                                                                     \
    {                                                                                                                  \
        auto symbol = result.library_.symbol<decltype(result.name)>(rstd::ffi::CStr::from_ptr(#name));                 \
        if (symbol.is_err())                                                                                           \
            return None();                                                                                             \
        result.name = rstd::move(symbol).unwrap();                                                                     \
        if (!result.name)                                                                                              \
            return None();                                                                                             \
    }
    LOAD(xcb_randr_id);
    LOAD(xcb_randr_query_version);
    LOAD(xcb_randr_query_version_reply);
    LOAD(xcb_randr_select_input);
    LOAD(xcb_randr_get_screen_resources);
    LOAD(xcb_randr_get_screen_resources_outputs);
    LOAD(xcb_randr_get_screen_resources_modes);
    LOAD(xcb_randr_get_screen_resources_current);
    LOAD(xcb_randr_get_screen_resources_current_outputs);
    LOAD(xcb_randr_get_screen_resources_current_modes);
    LOAD(xcb_randr_get_output_info);
    LOAD(xcb_randr_get_crtc_info);
#undef LOAD
    return Some(rstd::move(result));
}
} // namespace ffi
