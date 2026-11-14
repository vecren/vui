module vui.host:platform.x11;
import :ffi.x11;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

enum class X11ConnectionError { State, Connect, Watch };

struct X11InternalConnection { int fd{}; rstd::uint64_t generation{}; };

class X11Connection {
    friend struct X11ConnectionTest;
    ffi::x11::Display *display_{};
    Vec<X11InternalConnection> internal_;
    rstd::uint64_t generation_{};
    bool watching_{};

    static void watch(ffi::x11::Display *, char *data, int fd, int opening, char **);

  public:
    X11Connection() = default;
    X11Connection(const X11Connection &) = delete;
    auto operator=(const X11Connection &) -> X11Connection & = delete;
    ~X11Connection() { close(); }

    auto open(const char *name = nullptr) -> Result<empty, X11ConnectionError>;
    void close();
    auto display() const -> ffi::x11::Display * { return display_; }
    void copy_internal(Vec<X11InternalConnection> &result) const {
        result.clear();
        for (auto item : internal_) result.push(rstd::move(item));
    }
    auto current(X11InternalConnection item) const -> bool;
    void process(X11InternalConnection item) {
        if (display_ && current(item)) ffi::x11::XProcessInternalConnection(display_, item.fd);
    }
};
} // namespace vui::host::detail
