module vui.host;
import :platform.x11.dispatch;

namespace vui::host::detail {
using namespace rstd::prelude;

auto XiDispatch::open() -> Option<XiDispatch> {
    auto opened = rstd::dlopn::Library::open(rstd::ffi::CStr::from_ptr("libXi.so.6"));
    if (opened.is_err()) return None();
    auto library = rstd::move(opened).unwrap();
    auto query = library.symbol<decltype(&ffi::XIQueryVersion)>(rstd::ffi::CStr::from_ptr("XIQueryVersion"));
    if (query.is_err()) return None();
    auto select = library.symbol<decltype(&ffi::XISelectEvents)>(rstd::ffi::CStr::from_ptr("XISelectEvents"));
    if (select.is_err()) return None();
    auto query_version = rstd::move(query).unwrap();
    auto select_events = rstd::move(select).unwrap();
    if (!query_version || !select_events) return None();
    return Some(XiDispatch(rstd::move(library), query_version, select_events));
}
}
