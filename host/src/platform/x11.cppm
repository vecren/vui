module vui.host:platform.x11;
import :ffi.x11;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

enum class X11ConnectionError { State, Connect };

class X11Connection {
    ffi::x11::Display *display_{};

  public:
    X11Connection() = default;
    X11Connection(const X11Connection &) = delete;
    auto operator=(const X11Connection &) -> X11Connection & = delete;
    ~X11Connection() { close(); }

    auto open(const char *name = nullptr) -> Result<empty, X11ConnectionError> {
        if (display_)
            return Err(X11ConnectionError::State);
        display_ = ffi::x11::XOpenDisplay(name);
        if (!display_)
            return Err(X11ConnectionError::Connect);
        return Ok(empty{});
    }
    void close() {
        if (display_)
            ffi::x11::XCloseDisplay(display_);
        display_ = nullptr;
    }
    auto display() const -> ffi::x11::Display * { return display_; }
};
} // namespace vui::host::detail
