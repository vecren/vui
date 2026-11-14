module vui.host;
import :platform.x11;
import :ffi.x11;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

void X11Connection::watch(ffi::x11::Display *, char *data, int fd, int opening, char **) {
    auto &self = *reinterpret_cast<X11Connection *>(data);
    for (usize i{}; i < self.internal_.len(); i += usize(1)) {
        if (self.internal_[i].fd == fd) { self.internal_.remove(i); break; }
    }
    if (opening) {
        if (++self.generation_ == 0) ++self.generation_;
        self.internal_.push(X11InternalConnection{fd, self.generation_});
    }
}

auto X11Connection::open(const char *name) -> Result<empty, X11ConnectionError> {
    if (display_)
        return Err(X11ConnectionError::State);
    static const bool threaded = ffi::x11::XInitThreads() != 0;
    if (!threaded) return Err(X11ConnectionError::Connect);
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

void X11Connection::close() {
    if (display_ && watching_) ffi::x11::XRemoveConnectionWatch(display_, watch, reinterpret_cast<char *>(this));
    watching_ = false;
    if (display_)
        ffi::x11::XCloseDisplay(display_);
    display_ = nullptr;
    internal_.clear();
}

auto X11Connection::current(X11InternalConnection item) const -> bool {
    for (auto entry : internal_)
        if (entry.fd == item.fd && entry.generation == item.generation) return true;
    return false;
}
} // namespace vui::host::detail
