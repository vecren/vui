module vui.host:platform.wayland;
import :ffi.wayland;
import :ffi.xdg_shell;
import :platform.wayland_input;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

enum class WaylandConnectionError { State, Connect, Registry, MissingGlobals, Protocol };

class WaylandConnection {
    ffi::wayland::wl_display *display_{};
    ffi::wayland::wl_registry *registry_{};
    ffi::wayland::wl_compositor *compositor_{};
    ffi::xdg_shell::xdg_wm_base *shell_{};
    bool listener_failed_{};
    WaylandInput input_;

    static void ping(void *, ffi::xdg_shell::xdg_wm_base *shell, rstd::uint32_t serial) {
        ffi::xdg_shell::xdg_wm_base_pong(shell, serial);
    }
    static void global(void *data, ffi::wayland::wl_registry *registry, rstd::uint32_t name,
                       const char *interface, rstd::uint32_t version) {
        auto &self = *static_cast<WaylandConnection *>(data);
        auto text = rstd::ffi::CStr::from_ptr(interface).to_str();
        if (text.is_err())
            return;
        if (*text == "wl_compositor"_str && !self.compositor_) {
            auto selected = version < 4 ? version : 4;
            self.compositor_ =
                static_cast<ffi::wayland::wl_compositor *>(ffi::wayland::wl_registry_bind(
                    registry, name, &ffi::wayland::wl_compositor_interface, selected));
        }
        if (*text == "xdg_wm_base"_str && !self.shell_) {
            auto selected = version < 6 ? version : 6;
            self.shell_ = static_cast<ffi::xdg_shell::xdg_wm_base *>(ffi::wayland::wl_registry_bind(
                registry, name, &ffi::xdg_shell::xdg_wm_base_interface, selected));
            if (self.shell_ && ffi::xdg_shell::xdg_wm_base_add_listener(
                                   self.shell_, &self.shell_listener_, &self) != 0)
                self.listener_failed_ = true;
        }
        if (*text == "wl_seat"_str) self.input_.bind(registry, name, version);
        if (*text == "wl_data_device_manager"_str) self.input_.bind_clipboard(registry, name, version);
        if (*text == "zwp_text_input_manager_v3"_str) self.input_.bind_text(registry, name, version);
    }
    static void removed(void *data, ffi::wayland::wl_registry *, rstd::uint32_t name) {
        static_cast<WaylandConnection *>(data)->input_.remove(name);
    }
    ffi::wayland::wl_registry_listener registry_listener_{global, removed};
    ffi::xdg_shell::xdg_wm_base_listener shell_listener_{ping};

    auto fail(WaylandConnectionError error) -> Result<empty, WaylandConnectionError> {
        close();
        return Err(error);
    }

  public:
    WaylandConnection() = default;
    WaylandConnection(const WaylandConnection &) = delete;
    auto operator=(const WaylandConnection &) -> WaylandConnection & = delete;
    ~WaylandConnection() { close(); }

    auto open(const char *name = nullptr) -> Result<empty, WaylandConnectionError> {
        if (display_)
            return Err(WaylandConnectionError::State);
        listener_failed_ = false;
        display_ = ffi::wayland::wl_display_connect(name);
        if (!display_)
            return Err(WaylandConnectionError::Connect);
        registry_ = ffi::wayland::wl_display_get_registry(display_);
        if (!registry_ ||
            ffi::wayland::wl_registry_add_listener(registry_, &registry_listener_, this) != 0)
            return fail(WaylandConnectionError::Registry);
        if (ffi::wayland::wl_display_roundtrip(display_) < 0 || listener_failed_ || input_.failed())
            return fail(WaylandConnectionError::Protocol);
        if (!compositor_ || !shell_)
            return fail(WaylandConnectionError::MissingGlobals);
        if (ffi::wayland::wl_display_roundtrip(display_) < 0 || input_.failed())
            return fail(WaylandConnectionError::Protocol);
        return Ok(empty{});
    }
    void close() {
        input_.close();
        if (shell_)
            ffi::xdg_shell::xdg_wm_base_destroy(shell_);
        if (compositor_)
            ffi::wayland::wl_compositor_destroy(compositor_);
        if (registry_)
            ffi::wayland::wl_registry_destroy(registry_);
        if (display_)
            ffi::wayland::wl_display_disconnect(display_);
        shell_ = nullptr;
        compositor_ = nullptr;
        registry_ = nullptr;
        display_ = nullptr;
    }
    auto display() const -> ffi::wayland::wl_display * { return display_; }
    auto compositor() const -> ffi::wayland::wl_compositor * { return compositor_; }
    auto shell() const -> ffi::xdg_shell::xdg_wm_base * { return shell_; }
    auto input() -> WaylandInput & { return input_; }
};
} // namespace vui::host::detail
