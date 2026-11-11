module vui.host:platform.wayland_window;
import :platform.wayland;
import :ffi.wayland;
import :ffi.xdg_shell;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

enum class WaylandWindowError { State, InvalidSize, Connection, Surface, Listener, Protocol };

class WaylandWindow {
    WaylandConnection connection_;
    ffi::wayland::wl_surface *surface_{};
    ffi::xdg_shell::xdg_surface *xdg_{};
    ffi::xdg_shell::xdg_toplevel *toplevel_{};
    rstd::uint32_t width_{}, height_{}, pending_width_{}, pending_height_{};
    rstd::uint64_t revision_{};
    bool configured_{}, close_requested_{};

    static void configure(void *data, ffi::xdg_shell::xdg_surface *surface, rstd::uint32_t serial) {
        auto &self = *static_cast<WaylandWindow *>(data);
        ffi::xdg_shell::xdg_surface_ack_configure(surface, serial);
        self.width_ = self.pending_width_;
        self.height_ = self.pending_height_;
        self.configured_ = true;
        ++self.revision_;
    }
    static void resize(void *data, ffi::xdg_shell::xdg_toplevel *, rstd::int32_t width,
                       rstd::int32_t height, ffi::wayland::wl_array *) {
        auto &self = *static_cast<WaylandWindow *>(data);
        if (width > 0)
            self.pending_width_ = rstd::uint32_t(width);
        if (height > 0)
            self.pending_height_ = rstd::uint32_t(height);
    }
    static void request_close(void *data, ffi::xdg_shell::xdg_toplevel *) {
        static_cast<WaylandWindow *>(data)->close_requested_ = true;
    }
    static void bounds(void *, ffi::xdg_shell::xdg_toplevel *, rstd::int32_t, rstd::int32_t) {}
    static void capabilities(void *, ffi::xdg_shell::xdg_toplevel *, ffi::wayland::wl_array *) {}

    ffi::xdg_shell::xdg_surface_listener surface_listener_{configure};
    ffi::xdg_shell::xdg_toplevel_listener toplevel_listener_{resize, request_close, bounds,
                                                             capabilities};

    auto fail(WaylandWindowError error) -> Result<empty, WaylandWindowError> {
        close();
        return Err(error);
    }

  public:
    WaylandWindow() = default;
    WaylandWindow(const WaylandWindow &) = delete;
    auto operator=(const WaylandWindow &) -> WaylandWindow & = delete;
    ~WaylandWindow() { close(); }

    auto open(const char *title, rstd::uint32_t width, rstd::uint32_t height,
              const char *display = nullptr) -> Result<empty, WaylandWindowError> {
        if (surface_)
            return Err(WaylandWindowError::State);
        if (!title || !width || !height || width > 32768 || height > 32768)
            return Err(WaylandWindowError::InvalidSize);
        auto connected = connection_.open(display);
        if (connected.is_err())
            return Err(WaylandWindowError::Connection);
        width_ = pending_width_ = width;
        height_ = pending_height_ = height;
        revision_ = 0;
        configured_ = close_requested_ = false;
        surface_ = ffi::wayland::wl_compositor_create_surface(connection_.compositor());
        if (!surface_)
            return fail(WaylandWindowError::Surface);
        xdg_ = ffi::xdg_shell::xdg_wm_base_get_xdg_surface(connection_.shell(), surface_);
        if (!xdg_)
            return fail(WaylandWindowError::Surface);
        if (ffi::xdg_shell::xdg_surface_add_listener(xdg_, &surface_listener_, this) != 0)
            return fail(WaylandWindowError::Listener);
        toplevel_ = ffi::xdg_shell::xdg_surface_get_toplevel(xdg_);
        if (!toplevel_)
            return fail(WaylandWindowError::Surface);
        if (ffi::xdg_shell::xdg_toplevel_add_listener(toplevel_, &toplevel_listener_, this) != 0)
            return fail(WaylandWindowError::Listener);
        ffi::xdg_shell::xdg_toplevel_set_title(toplevel_, title);
        ffi::wayland::wl_surface_commit(surface_);
        return Ok(empty{});
    }
    auto roundtrip() -> Result<empty, WaylandWindowError> {
        if (!surface_)
            return Err(WaylandWindowError::State);
        if (ffi::wayland::wl_display_roundtrip(connection_.display()) < 0)
            return Err(WaylandWindowError::Protocol);
        return Ok(empty{});
    }
    void close() {
        if (toplevel_)
            ffi::xdg_shell::xdg_toplevel_destroy(toplevel_);
        if (xdg_)
            ffi::xdg_shell::xdg_surface_destroy(xdg_);
        if (surface_)
            ffi::wayland::wl_surface_destroy(surface_);
        toplevel_ = nullptr;
        xdg_ = nullptr;
        surface_ = nullptr;
        connection_.close();
        configured_ = close_requested_ = false;
    }
    auto display() const -> ffi::wayland::wl_display * { return connection_.display(); }
    auto surface() const -> ffi::wayland::wl_surface * { return surface_; }
    auto configured() const -> bool { return configured_; }
    auto close_requested() const -> bool { return close_requested_; }
    auto width() const -> rstd::uint32_t { return width_; }
    auto height() const -> rstd::uint32_t { return height_; }
    auto revision() const -> rstd::uint64_t { return revision_; }
};
} // namespace vui::host::detail
