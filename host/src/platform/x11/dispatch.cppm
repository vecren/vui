module vui.host:platform.x11.dispatch;
import :platform.x11.ffi.xi2;
import rstd.dlopn;

namespace vui::host::detail {
using namespace rstd::prelude;

class XiDispatch {
    rstd::dlopn::Library library_;
    XiDispatch(rstd::dlopn::Library library, decltype(&ffi::XIQueryVersion) query,
               decltype(&ffi::XISelectEvents) select)
        : library_(rstd::move(library)), XIQueryVersion(query), XISelectEvents(select) {}

  public:
    decltype(&ffi::XIQueryVersion) XIQueryVersion;
    decltype(&ffi::XISelectEvents) XISelectEvents;
    XiDispatch(const XiDispatch &) = delete;
    XiDispatch(XiDispatch &&) = default;
    auto operator=(const XiDispatch &) -> XiDispatch & = delete;
    auto operator=(XiDispatch &&) -> XiDispatch & = default;
    static auto open() -> Option<XiDispatch>;
};
}
