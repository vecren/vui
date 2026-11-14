module vui.host;
import :platform.wayland_window;
import :platform.wayland;
import :ffi.wayland;
import vui.host.wayland.xdg_shell;
import vui.host.wayland.xdg_decoration;
import vui.host.wayland.wayland_fractional;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

void WaylandWindow::preferred_scale(void *data, ffi::wayland_fractional::wp_fractional_scale_v1 *, rstd::uint32_t numerator) {
    auto &self = *static_cast<WaylandWindow *>(data);
    if (!self.valid_scale(numerator, self.width_, self.height_)) { self.scale_failed_ = true; return; }
    if (self.scale_numerator_ != numerator) { self.scale_numerator_ = numerator; self.mark_scale_changed(); }
}

void WaylandWindow::setup_fractional() {
    if (!surface_ || fractional_ || !connection_.fractional() || !connection_.viewporter()) return;
    auto viewport = ffi::wayland_fractional::viewport_create(connection_.viewporter(), surface_);
    if (!viewport) { scale_failed_ = true; return; }
    auto fractional = ffi::wayland_fractional::fractional_create(connection_.fractional(), surface_);
    if (!fractional) { ffi::wayland_fractional::viewport_destroy(viewport); scale_failed_ = true; return; }
    if (ffi::wayland_fractional::fractional_listener(fractional, &fractional_listener_, this)) {
        ffi::wayland_fractional::fractional_destroy(fractional); ffi::wayland_fractional::viewport_destroy(viewport);
        scale_failed_ = true; return;
    }
    viewport_ = viewport; fractional_ = fractional;
    ffi::wayland_fractional::viewport_destination(viewport_, rstd::int32_t(width_), rstd::int32_t(height_));
    if (ffi::wayland::wl_surface_get_version(surface_) >= ffi::wayland::surface_buffer_scale_version)
        ffi::wayland::wl_surface_set_buffer_scale(surface_, 1);
    mark_scale_changed();
}

void WaylandWindow::update_scale() {
    if (!surface_) return;
    if (viewport_) ffi::wayland_fractional::viewport_destination(viewport_, rstd::int32_t(width_), rstd::int32_t(height_));
    if (fractional_) {
        if (!valid_scale(scale_numerator_, width_, height_)) scale_failed_ = true;
        return;
    }
    auto scale = ffi::wayland::wl_surface_get_version(surface_) >= ffi::wayland::surface_buffer_scale_version
        ? connection_.outputs().scale() : 1;
    auto numerator = rstd::uint64_t(scale) * 120;
    if (numerator > ~rstd::uint32_t{} || !valid_scale(rstd::uint32_t(numerator), width_, height_)) { scale_failed_ = true; return; }
    if (scale_numerator_ == numerator) return;
    scale_numerator_ = rstd::uint32_t(numerator); ffi::wayland::wl_surface_set_buffer_scale(surface_, scale);
    mark_scale_changed();
}

void WaylandWindow::frame_done(void *data, ffi::wayland::wl_callback *callback, rstd::uint32_t) {
    auto &self=*static_cast<WaylandWindow *>(data);
    ffi::wayland::wl_callback_destroy(callback);
    self.frame_signal_=nullptr; self.frame_signal_ready_=true;
}

void WaylandWindow::apply_configure() {
    decoration_mode_ = pending_decoration_mode_;
    if (pending_mode_known_ && !fullscreen_ && pending_fullscreen_) {
        windowed_width_ = width_; windowed_height_ = height_;
    }
    if (pending_mode_known_) fullscreen_ = pending_fullscreen_;
    if (pending_mode_known_) suspended_ = pending_suspended_;
    mode_known_ = pending_mode_known_;
    minimize_capability_ = pending_minimize_capability_;
    width_ = pending_width_;
    height_ = pending_height_;
    update_scale();
    configured_ = true;
    ++revision_;
    connection_.input().set_generation(revision_ + 1);
}

void WaylandWindow::resize(void *data, ffi::xdg_shell::xdg_toplevel *, rstd::int32_t width,
                   rstd::int32_t height, ffi::wayland::wl_array *states) {
    auto &self = *static_cast<WaylandWindow *>(data);
    self.pending_mode_known_ = !states || (states->size % sizeof(rstd::uint32_t) == 0 && (!states->size || states->data));
    self.pending_fullscreen_ = self.pending_mode_known_ ? false : self.fullscreen_;
    self.pending_suspended_ = self.pending_mode_known_ ? false : self.suspended_;
    if (self.pending_mode_known_ && states && states->data) {
        auto values = static_cast<const rstd::uint32_t *>(states->data);
        for (rstd::usize i{}; i < rstd::usize(states->size / sizeof(rstd::uint32_t)); i += rstd::usize(1)) {
            if (values[i.to_primitive()] == ffi::xdg_shell::fullscreen_state) self.pending_fullscreen_ = true;
            if (values[i.to_primitive()] == ffi::xdg_shell::suspended_state) self.pending_suspended_ = true;
        }
    }
    bool restoring = self.fullscreen_ && !self.pending_fullscreen_;
    self.pending_width_ = width > 0 ? rstd::uint32_t(width) : restoring ? self.windowed_width_ : self.width_;
    self.pending_height_ = height > 0 ? rstd::uint32_t(height) : restoring ? self.windowed_height_ : self.height_;
}

void WaylandWindow::capabilities(void *data, ffi::xdg_shell::xdg_toplevel *, ffi::wayland::wl_array *values) {
    auto &self = *static_cast<WaylandWindow *>(data);
    if (!values || values->size % sizeof(rstd::uint32_t) || (values->size && !values->data)) {
        self.pending_minimize_capability_ = None(); return;
    }
    bool minimize{}; auto items = static_cast<const rstd::uint32_t *>(values->data);
    for (rstd::usize i{}; i < rstd::usize(values->size / sizeof(rstd::uint32_t)); i += rstd::usize(1))
        if (items[i.to_primitive()] == ffi::xdg_shell::minimize_capability) minimize = true;
    self.pending_minimize_capability_ = Some(minimize);
}

auto WaylandWindow::open(const char *title, rstd::uint32_t width, rstd::uint32_t height,
          const char *display) -> Result<empty, WaylandWindowError> {
    if (surface_)
        return Err(WaylandWindowError::State);
    if (!title || !width || !height || width > 32768 || height > 32768)
        return Err(WaylandWindowError::InvalidSize);
    connection_.outputs().set_callback(this, [](void *data) { static_cast<WaylandWindow *>(data)->update_scale(); });
    connection_.set_scaling_callback(this, [](void *data) { static_cast<WaylandWindow *>(data)->setup_fractional(); });
    auto connected = connection_.open(display);
    if (connected.is_err())
        return Err(WaylandWindowError::Connection);
    width_ = pending_width_ = width;
    height_ = pending_height_ = height;
    windowed_width_ = width; windowed_height_ = height;
    minimum_width_ = minimum_height_ = 0;
    scale_numerator_ = 120; scale_failed_ = false;
    revision_ = 0;
    configured_ = close_requested_ = fullscreen_ = pending_fullscreen_ = mode_known_ = pending_mode_known_ = false;
    suspended_ = pending_suspended_ = false; minimize_capability_ = pending_minimize_capability_ = None();
    surface_ = ffi::wayland::wl_compositor_create_surface(connection_.compositor());
    if (!surface_)
        return fail(WaylandWindowError::Surface);
    if (ffi::wayland::wl_surface_add_listener(surface_, &surface_output_listener_, this) != 0)
        return fail(WaylandWindowError::Listener);
    setup_fractional();
    if (scale_failed_) return fail(WaylandWindowError::Protocol);
    connection_.input().set_surface(surface_);
    connection_.input().set_generation(revision_ + 1);
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
    if (auto manager = connection_.decoration()) {
        decoration_ = ffi::xdg_decoration::create(manager, toplevel_);
        if (!decoration_) return fail(WaylandWindowError::Surface);
        if (ffi::xdg_decoration::listen(decoration_, &decoration_listener_, this))
            return fail(WaylandWindowError::Listener);
        ffi::xdg_decoration::server_side(decoration_);
    }
    ffi::wayland::wl_surface_commit(surface_);
    return Ok(empty{});
}

auto WaylandWindow::roundtrip() -> Result<empty, WaylandWindowError> {
    if (!surface_)
        return Err(WaylandWindowError::State);
    if (ffi::wayland::wl_display_roundtrip(connection_.display()) < 0 || failed())
        return Err(WaylandWindowError::Protocol);
    return Ok(empty{});
}

auto WaylandWindow::request_frame_signal() -> Result<Serial,WaylandWindowError> {
    if (!surface_ || frame_signal_) return Err(WaylandWindowError::State);
    auto callback=ffi::wayland::wl_surface_frame(surface_);
    if (!callback) return Err(WaylandWindowError::Protocol);
    if (ffi::wayland::wl_callback_add_listener(callback,&frame_listener_,this)) {
        ffi::wayland::wl_callback_destroy(callback); return Err(WaylandWindowError::Listener);
    }
    frame_signal_=callback; frame_signal_token_=new_session(); frame_signal_ready_=false;
    return Ok(Serial(frame_signal_token_));
}

auto WaylandWindow::cancel_frame_signal(Serial token) -> bool {
    if (!token || token!=frame_signal_token_) return false;
    clear_frame_signal();
    return true;
}

auto WaylandWindow::resize(rstd::uint32_t width, rstd::uint32_t height) -> Result<empty, WaylandWindowError> {
    if (!surface_) return Err(WaylandWindowError::State);
    if (fullscreen_) return Err(WaylandWindowError::State);
    if (!width || !height || width > 32768 || height > 32768 ||
        !valid_scale(scale_numerator_, width, height)) return Err(WaylandWindowError::InvalidSize);
    if (width_ != width || height_ != height) {
        width_ = pending_width_ = width; height_ = pending_height_ = height;
        update_scale();
        ++revision_; connection_.input().set_generation(revision_ + 1);
    }
    return Ok(empty{});
}

auto WaylandWindow::set_fullscreen(bool enabled) -> Result<empty, WaylandWindowError> {
    if (!toplevel_) return Err(WaylandWindowError::State);
    if (enabled) ffi::xdg_shell::xdg_toplevel_set_fullscreen(toplevel_);
    else ffi::xdg_shell::xdg_toplevel_unset_fullscreen(toplevel_);
    return Ok(empty{});
}

auto WaylandWindow::minimize_capability() const -> Option<bool> {
    if (!toplevel_) return None();
    if (ffi::xdg_shell::xdg_toplevel_get_version(toplevel_) < 5) return Some(true);
    return minimize_capability_;
}

auto WaylandWindow::set_minimum_size(rstd::uint32_t width, rstd::uint32_t height) -> Result<empty, WaylandWindowError> {
    if (!surface_) return Err(WaylandWindowError::State);
    if (width > 32768 || height > 32768) return Err(WaylandWindowError::InvalidSize);
    ffi::xdg_shell::xdg_toplevel_set_min_size(toplevel_, rstd::int32_t(width), rstd::int32_t(height));
    ffi::wayland::wl_surface_commit(surface_);
    minimum_width_ = width; minimum_height_ = height;
    return Ok(empty{});
}

void WaylandWindow::close() {
    connection_.input().close();
    clear_frame_signal();
    if (fractional_) ffi::wayland_fractional::fractional_destroy(fractional_);
    if (viewport_) ffi::wayland_fractional::viewport_destroy(viewport_);
    fractional_ = nullptr; viewport_ = nullptr;
    if (decoration_) ffi::xdg_decoration::destroy(decoration_);
    decoration_ = nullptr; decoration_mode_ = pending_decoration_mode_ = 0;
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
    configured_ = close_requested_ = fullscreen_ = pending_fullscreen_ = mode_known_ = pending_mode_known_ = false;
    suspended_ = pending_suspended_ = false; minimize_capability_ = pending_minimize_capability_ = None();
    scale_numerator_ = 120; scale_failed_ = false;
    minimum_width_ = minimum_height_ = 0;
}
} // namespace vui::host::detail
