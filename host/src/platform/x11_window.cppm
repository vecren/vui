module vui.host:platform.x11_window;
import :platform.x11;
import :ffi.x11;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

enum class X11WindowError { State, InvalidSize, Connection, Window, Protocol };

class X11Window {
    X11Connection connection_;
    ffi::x11::Window window_{};
    ffi::x11::Atom protocols_{}, delete_window_{};
    rstd::uint32_t width_{}, height_{};
    rstd::uint64_t revision_{};
    bool close_requested_{};

    auto fail(X11WindowError error) -> Result<empty, X11WindowError> {
        close();
        return Err(error);
    }

  public:
    X11Window() = default;
    X11Window(const X11Window &) = delete;
    auto operator=(const X11Window &) -> X11Window & = delete;
    ~X11Window() { close(); }

    auto open(const char *title, rstd::uint32_t width, rstd::uint32_t height,
              const char *display = nullptr) -> Result<empty, X11WindowError> {
        if (window_)
            return Err(X11WindowError::State);
        if (!title || !width || !height || width > 32768 || height > 32768)
            return Err(X11WindowError::InvalidSize);
        if (connection_.open(display).is_err())
            return Err(X11WindowError::Connection);
        auto native = connection_.display();
        auto screen = ffi::x11::XDefaultScreen(native);
        auto root = ffi::x11::XRootWindow(native, screen);
        window_ = ffi::x11::XCreateSimpleWindow(native, root, 0, 0, width, height, 0, 0, 0);
        if (!window_)
            return fail(X11WindowError::Window);
        protocols_ = ffi::x11::XInternAtom(native, "WM_PROTOCOLS", 0);
        delete_window_ = ffi::x11::XInternAtom(native, "WM_DELETE_WINDOW", 0);
        if (!protocols_ || !delete_window_ ||
            !ffi::x11::XSetWMProtocols(native, window_, &delete_window_, 1))
            return fail(X11WindowError::Protocol);
        width_ = width;
        height_ = height;
        revision_ = 0;
        close_requested_ = false;
        ffi::x11::XStoreName(native, window_, title);
        ffi::x11::XSelectInput(native, window_, ffi::x11::structure_notify_mask);
        ffi::x11::XMapWindow(native, window_);
        ffi::x11::XFlush(native);
        return Ok(empty{});
    }
    auto resize(rstd::uint32_t width, rstd::uint32_t height) -> Result<empty, X11WindowError> {
        if (!window_)
            return Err(X11WindowError::State);
        if (!width || !height || width > 32768 || height > 32768)
            return Err(X11WindowError::InvalidSize);
        ffi::x11::XResizeWindow(connection_.display(), window_, width, height);
        ffi::x11::XFlush(connection_.display());
        return Ok(empty{});
    }
    auto synchronize() -> Result<empty, X11WindowError> {
        if (!window_)
            return Err(X11WindowError::State);
        auto display = connection_.display();
        ffi::x11::XSync(display, 0);
        while (ffi::x11::XPending(display) > 0) {
            ffi::x11::XEvent event{};
            ffi::x11::XNextEvent(display, &event);
            if (event.xany.window != window_)
                continue;
            if (event.type == ffi::x11::configure_notify && event.xconfigure.width > 0 &&
                event.xconfigure.height > 0) {
                auto width = rstd::uint32_t(event.xconfigure.width),
                     height = rstd::uint32_t(event.xconfigure.height);
                if (width != width_ || height != height_) {
                    width_ = width;
                    height_ = height;
                    ++revision_;
                }
            } else if (event.type == ffi::x11::client_message &&
                       event.xclient.message_type == protocols_ && event.xclient.format == 32 &&
                       static_cast<ffi::x11::Atom>(event.xclient.data.l[0]) == delete_window_) {
                close_requested_ = true;
            } else if (event.type == ffi::x11::destroy_notify) {
                window_ = 0;
                close_requested_ = true;
            }
        }
        return Ok(empty{});
    }
    void close() {
        if (window_)
            ffi::x11::XDestroyWindow(connection_.display(), window_);
        window_ = 0;
        connection_.close();
        close_requested_ = false;
    }
    auto display() const -> ffi::x11::Display * { return connection_.display(); }
    auto window() const -> ffi::x11::Window { return window_; }
    auto close_requested() const -> bool { return close_requested_; }
    auto width() const -> rstd::uint32_t { return width_; }
    auto height() const -> rstd::uint32_t { return height_; }
    auto revision() const -> rstd::uint64_t { return revision_; }
};
} // namespace vui::host::detail
