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

    static void watch(ffi::x11::Display *, char *data, int fd, int opening, char **) {
        auto &self = *reinterpret_cast<X11Connection *>(data);
        for (usize i{}; i < self.internal_.len(); i += usize(1)) {
            if (self.internal_[i].fd == fd) { self.internal_.remove(i); break; }
        }
        if (opening) {
            if (++self.generation_ == 0) ++self.generation_;
            self.internal_.push(X11InternalConnection{fd, self.generation_});
        }
    }

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
        if (!ffi::x11::XAddConnectionWatch(display_, watch, reinterpret_cast<char *>(this))) {
            ffi::x11::XCloseDisplay(display_); display_ = nullptr;
            internal_.clear();
            return Err(X11ConnectionError::Watch);
        }
        watching_ = true;
        return Ok(empty{});
    }
    void close() {
        if (display_ && watching_) ffi::x11::XRemoveConnectionWatch(display_, watch, reinterpret_cast<char *>(this));
        watching_ = false;
        if (display_)
            ffi::x11::XCloseDisplay(display_);
        display_ = nullptr;
        internal_.clear();
    }
    auto display() const -> ffi::x11::Display * { return display_; }
    void copy_internal(Vec<X11InternalConnection> &result) const {
        result.clear();
        for (auto item : internal_) result.push(rstd::move(item));
    }
    auto current(X11InternalConnection item) const -> bool {
        for (auto entry : internal_)
            if (entry.fd == item.fd && entry.generation == item.generation) return true;
        return false;
    }
    void process(X11InternalConnection item) {
        if (display_ && current(item)) ffi::x11::XProcessInternalConnection(display_, item.fd);
    }
};
} // namespace vui::host::detail
