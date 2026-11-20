module vui.host;
import :platform.wayland.connection;
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

void WaylandConnection::global(void *data, ffi::wl_registry *registry, rstd::uint32_t name, const char *interface,
                               rstd::uint32_t version) {
    auto &self = *static_cast<WaylandConnection *>(data);
    auto text = rstd::ffi::CStr::from_ptr(interface).to_str();
    if (text.is_err())
        return;
    if (*text == "wl_compositor"_str && !self.compositor_) {
        auto selected = version < 4 ? version : 4;
        self.compositor_ = static_cast<ffi::wl_compositor *>(
            ffi::wl_registry_bind(registry, name, &ffi::wl_compositor_interface, selected));
    }
    if (*text == "xdg_wm_base"_str && !self.shell_) {
        auto selected = version < 6 ? version : 6;
        self.shell_ = static_cast<ffi::xdg_wm_base *>(
            ffi::wl_registry_bind(registry, name, &ffi::xdg_wm_base_interface, selected));
        if (self.shell_ && ffi::xdg_wm_base_add_listener(self.shell_, &self.shell_listener_, &self) != 0)
            self.listener_failed_ = true;
    }
    if (*text == "zxdg_decoration_manager_v1"_str && !self.decoration_) {
        self.decoration_ = static_cast<ffi::zxdg_decoration_manager_v1 *>(
            ffi::wl_registry_bind(registry, name, &ffi::zxdg_decoration_manager_v1_interface, 1));
        if (!self.decoration_)
            self.listener_failed_ = true;
        else
            self.decoration_name_ = name;
    }
    if (*text == "wl_seat"_str)
        self.input_.bind(registry, name, version);
    if (*text == "wl_output"_str)
        self.outputs_.bind(registry, name, version);
    if (*text == "zxdg_output_manager_v1"_str)
        self.outputs_.bind_manager(registry, name, version);
    if (*text == "wp_fractional_scale_manager_v1"_str && !self.fractional_) {
        self.fractional_ = static_cast<ffi::wp_fractional_scale_manager_v1 *>(
            ffi::wl_registry_bind(registry, name, &ffi::wp_fractional_scale_manager_v1_interface, 1));
        if (!self.fractional_)
            self.listener_failed_ = true;
        else {
            self.fractional_name_ = name;
            if (self.scaling_changed_)
                self.scaling_changed_(self.scaling_sink_);
        }
    }
    if (*text == "wp_viewporter"_str && !self.viewporter_) {
        self.viewporter_ =
            static_cast<ffi::wp_viewporter *>(ffi::wl_registry_bind(registry, name, &ffi::wp_viewporter_interface, 1));
        if (!self.viewporter_)
            self.listener_failed_ = true;
        else {
            self.viewporter_name_ = name;
            if (self.scaling_changed_)
                self.scaling_changed_(self.scaling_sink_);
        }
    }
    if (*text == "wl_data_device_manager"_str)
        self.input_.bind_clipboard(registry, name, version);
    if (*text == "zwp_text_input_manager_v3"_str)
        self.input_.bind_text(registry, name, version);
}

void WaylandConnection::removed(void *data, ffi::wl_registry *, rstd::uint32_t name) {
    auto &self = *static_cast<WaylandConnection *>(data);
    self.outputs_.remove_manager(name);
    self.outputs_.remove(name);
    self.input_.remove(name);
    if (self.decoration_ && self.decoration_name_ == name) {
        ffi::destroy_manager(self.decoration_);
        self.decoration_ = nullptr;
        self.decoration_name_ = 0;
    }
    if (self.fractional_ && self.fractional_name_ == name) {
        ffi::fractional_manager_destroy(self.fractional_);
        self.fractional_ = nullptr;
        self.fractional_name_ = 0;
    }
    if (self.viewporter_ && self.viewporter_name_ == name) {
        ffi::viewporter_destroy(self.viewporter_);
        self.viewporter_ = nullptr;
        self.viewporter_name_ = 0;
    }
}

auto WaylandConnection::open(const char *name) -> Result<empty, WaylandConnectionError> {
    if (display_)
        return Err(WaylandConnectionError::State);
    listener_failed_ = false;
    auto loaded = ffi::WaylandDispatch::open();
    if (loaded.is_err())
        return Err(WaylandConnectionError::Library);
    api_ = Some(rstd::move(loaded).unwrap());
    if (!input_.load())
        return fail(WaylandConnectionError::Library);
    display_ = ffi::wl_display_connect(name);
    if (!display_)
        return fail(WaylandConnectionError::Connect);
    registry_ = ffi::wl_display_get_registry(display_);
    if (!registry_ || ffi::wl_registry_add_listener(registry_, &registry_listener_, this) != 0)
        return fail(WaylandConnectionError::Registry);
    if (ffi::wl_display_roundtrip(display_) < 0 || listener_failed_ || input_.failed() || outputs_.failed())
        return fail(WaylandConnectionError::Protocol);
    if (!compositor_ || !shell_)
        return fail(WaylandConnectionError::MissingGlobals);
    if (ffi::wl_display_roundtrip(display_) < 0 || input_.failed() || outputs_.failed())
        return fail(WaylandConnectionError::Protocol);
    return Ok(empty{});
}

void WaylandConnection::close() {
    input_.close();
    outputs_.close();
    if (fractional_)
        ffi::fractional_manager_destroy(fractional_);
    if (viewporter_)
        ffi::viewporter_destroy(viewporter_);
    if (decoration_)
        ffi::destroy_manager(decoration_);
    if (shell_)
        ffi::xdg_wm_base_destroy(shell_);
    if (compositor_)
        ffi::wl_compositor_destroy(compositor_);
    if (registry_)
        ffi::wl_registry_destroy(registry_);
    if (display_)
        ffi::wl_display_disconnect(display_);
    shell_ = nullptr;
    decoration_ = nullptr;
    decoration_name_ = 0;
    fractional_ = nullptr;
    viewporter_ = nullptr;
    fractional_name_ = viewporter_name_ = 0;
    compositor_ = nullptr;
    registry_ = nullptr;
    display_ = nullptr;
    api_ = None();
}
} // namespace vui::host::detail
