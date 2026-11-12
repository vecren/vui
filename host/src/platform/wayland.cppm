module vui.host:platform.wayland;
import :ffi.wayland;
import vui.host.wayland.xdg_shell;
import vui.host.wayland.xdg_decoration;
import vui.host.wayland.wayland_fractional;
import :platform.wayland_input;
import :platform.wayland_outputs;
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
    ffi::xdg_decoration::zxdg_decoration_manager_v1 *decoration_{};
    rstd::uint32_t decoration_name_{};
    ffi::wayland_fractional::wp_fractional_scale_manager_v1 *fractional_{};
    ffi::wayland_fractional::wp_viewporter *viewporter_{};
    rstd::uint32_t fractional_name_{}, viewporter_name_{};
    void *scaling_sink_{};
    void (*scaling_changed_)(void *){};
    bool listener_failed_{};
    WaylandInput input_;
    WaylandOutputs outputs_;

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
        if (*text == "zxdg_decoration_manager_v1"_str && !self.decoration_) {
            self.decoration_ = static_cast<ffi::xdg_decoration::zxdg_decoration_manager_v1 *>(
                ffi::wayland::wl_registry_bind(registry, name,
                    &ffi::xdg_decoration::zxdg_decoration_manager_v1_interface, 1));
            if (!self.decoration_) self.listener_failed_ = true;
            else self.decoration_name_ = name;
        }
        if (*text == "wl_seat"_str) self.input_.bind(registry, name, version);
        if (*text == "wl_output"_str) self.outputs_.bind(registry, name, version);
        if (*text == "zxdg_output_manager_v1"_str) self.outputs_.bind_manager(registry, name, version);
        if (*text == "wp_fractional_scale_manager_v1"_str && !self.fractional_) {
            self.fractional_ = static_cast<ffi::wayland_fractional::wp_fractional_scale_manager_v1 *>(
                ffi::wayland::wl_registry_bind(registry, name, &ffi::wayland_fractional::wp_fractional_scale_manager_v1_interface, 1));
            if (!self.fractional_) self.listener_failed_ = true;
            else { self.fractional_name_ = name; if (self.scaling_changed_) self.scaling_changed_(self.scaling_sink_); }
        }
        if (*text == "wp_viewporter"_str && !self.viewporter_) {
            self.viewporter_ = static_cast<ffi::wayland_fractional::wp_viewporter *>(
                ffi::wayland::wl_registry_bind(registry, name, &ffi::wayland_fractional::wp_viewporter_interface, 1));
            if (!self.viewporter_) self.listener_failed_ = true;
            else { self.viewporter_name_ = name; if (self.scaling_changed_) self.scaling_changed_(self.scaling_sink_); }
        }
        if (*text == "wl_data_device_manager"_str) self.input_.bind_clipboard(registry, name, version);
        if (*text == "zwp_text_input_manager_v3"_str) self.input_.bind_text(registry, name, version);
    }
    static void removed(void *data, ffi::wayland::wl_registry *, rstd::uint32_t name) {
        auto &self = *static_cast<WaylandConnection *>(data);
        self.outputs_.remove_manager(name); self.outputs_.remove(name); self.input_.remove(name);
        if (self.decoration_ && self.decoration_name_ == name) {
            ffi::xdg_decoration::destroy_manager(self.decoration_);
            self.decoration_ = nullptr; self.decoration_name_ = 0;
        }
        if (self.fractional_ && self.fractional_name_ == name) {
            ffi::wayland_fractional::fractional_manager_destroy(self.fractional_); self.fractional_ = nullptr; self.fractional_name_ = 0;
        }
        if (self.viewporter_ && self.viewporter_name_ == name) {
            ffi::wayland_fractional::viewporter_destroy(self.viewporter_); self.viewporter_ = nullptr; self.viewporter_name_ = 0;
        }
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
        if (ffi::wayland::wl_display_roundtrip(display_) < 0 || listener_failed_ || input_.failed() || outputs_.failed())
            return fail(WaylandConnectionError::Protocol);
        if (!compositor_ || !shell_)
            return fail(WaylandConnectionError::MissingGlobals);
        if (ffi::wayland::wl_display_roundtrip(display_) < 0 || input_.failed() || outputs_.failed())
            return fail(WaylandConnectionError::Protocol);
        return Ok(empty{});
    }
    void close() {
        input_.close();
        outputs_.close();
        if (fractional_) ffi::wayland_fractional::fractional_manager_destroy(fractional_);
        if (viewporter_) ffi::wayland_fractional::viewporter_destroy(viewporter_);
        if (decoration_) ffi::xdg_decoration::destroy_manager(decoration_);
        if (shell_)
            ffi::xdg_shell::xdg_wm_base_destroy(shell_);
        if (compositor_)
            ffi::wayland::wl_compositor_destroy(compositor_);
        if (registry_)
            ffi::wayland::wl_registry_destroy(registry_);
        if (display_)
            ffi::wayland::wl_display_disconnect(display_);
        shell_ = nullptr;
        decoration_ = nullptr; decoration_name_ = 0;
        fractional_ = nullptr; viewporter_ = nullptr; fractional_name_ = viewporter_name_ = 0;
        compositor_ = nullptr;
        registry_ = nullptr;
        display_ = nullptr;
    }
    auto display() const -> ffi::wayland::wl_display * { return display_; }
    auto compositor() const -> ffi::wayland::wl_compositor * { return compositor_; }
    auto shell() const -> ffi::xdg_shell::xdg_wm_base * { return shell_; }
    auto decoration() const -> ffi::xdg_decoration::zxdg_decoration_manager_v1 * { return decoration_; }
    auto input() -> WaylandInput & { return input_; }
    auto outputs() -> WaylandOutputs & { return outputs_; }
    auto outputs() const -> const WaylandOutputs & { return outputs_; }
    void set_scaling_callback(void *sink, void (*callback)(void *)) { scaling_sink_ = sink; scaling_changed_ = callback; }
    auto fractional() const -> ffi::wayland_fractional::wp_fractional_scale_manager_v1 * { return fractional_; }
    auto viewporter() const -> ffi::wayland_fractional::wp_viewporter * { return viewporter_; }
    auto failed() const -> bool { return listener_failed_ || input_.failed() || outputs_.failed(); }
};
} // namespace vui::host::detail
