module vui.host:platform.wayland.connection;
import :ffi.wayland;
import :platform.wayland.protocol.xdg_shell;
import :platform.wayland.protocol.xdg_decoration;
import :platform.wayland.protocol.fractional_scale;
import :platform.wayland.input;
import :platform.wayland.outputs;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

enum class WaylandConnectionError { State, Connect, Registry, MissingGlobals, Protocol };

class WaylandConnection {
    ffi::wl_display *display_{};
    ffi::wl_registry *registry_{};
    ffi::wl_compositor *compositor_{};
    ffi::xdg_wm_base *shell_{};
    ffi::zxdg_decoration_manager_v1 *decoration_{};
    rstd::uint32_t decoration_name_{};
    ffi::wp_fractional_scale_manager_v1 *fractional_{};
    ffi::wp_viewporter *viewporter_{};
    rstd::uint32_t fractional_name_{}, viewporter_name_{};
    void *scaling_sink_{};
    void (*scaling_changed_)(void *){};
    bool listener_failed_{};
    WaylandInput input_;
    WaylandOutputs outputs_;

    static void ping(void *, ffi::xdg_wm_base *shell, rstd::uint32_t serial) {
        ffi::xdg_wm_base_pong(shell, serial);
    }
    static void global(void *data, ffi::wl_registry *registry, rstd::uint32_t name,
                       const char *interface, rstd::uint32_t version);
    static void removed(void *data, ffi::wl_registry *, rstd::uint32_t name);
    ffi::wl_registry_listener registry_listener_{global, removed};
    ffi::xdg_wm_base_listener shell_listener_{ping};

    auto fail(WaylandConnectionError error) -> Result<empty, WaylandConnectionError> {
        close();
        return Err(error);
    }

  public:
    WaylandConnection() = default;
    WaylandConnection(const WaylandConnection &) = delete;
    auto operator=(const WaylandConnection &) -> WaylandConnection & = delete;
    ~WaylandConnection() { close(); }

    auto open(const char *name = nullptr) -> Result<empty, WaylandConnectionError>;
    void close();
    auto display() const -> ffi::wl_display * { return display_; }
    auto compositor() const -> ffi::wl_compositor * { return compositor_; }
    auto shell() const -> ffi::xdg_wm_base * { return shell_; }
    auto decoration() const -> ffi::zxdg_decoration_manager_v1 * { return decoration_; }
    auto input() -> WaylandInput & { return input_; }
    auto outputs() -> WaylandOutputs & { return outputs_; }
    auto outputs() const -> const WaylandOutputs & { return outputs_; }
    void set_scaling_callback(void *sink, void (*callback)(void *)) { scaling_sink_ = sink; scaling_changed_ = callback; }
    auto fractional() const -> ffi::wp_fractional_scale_manager_v1 * { return fractional_; }
    auto viewporter() const -> ffi::wp_viewporter * { return viewporter_; }
    auto failed() const -> bool { return listener_failed_ || input_.failed() || outputs_.failed(); }
};
} // namespace vui::host::detail
