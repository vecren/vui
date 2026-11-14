module vui.host:platform.wayland_window;
import :platform.wayland;
import :ffi.wayland;
import vui.host.wayland.xdg_shell;
import vui.host.wayland.xdg_decoration;
import vui.host.wayland.wayland_fractional;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

enum class WaylandWindowError { State, InvalidSize, Connection, Surface, Listener, Protocol };

class WaylandWindow {
    friend struct WaylandWindowTest;
    WaylandConnection connection_;
    ffi::wayland::wl_surface *surface_{};
    ffi::wayland::wl_callback *frame_signal_{};
    Serial frame_signal_token_{};
    bool frame_signal_ready_{};
    ffi::xdg_shell::xdg_surface *xdg_{};
    ffi::xdg_shell::xdg_toplevel *toplevel_{};
    ffi::xdg_decoration::zxdg_toplevel_decoration_v1 *decoration_{};
    rstd::uint32_t decoration_mode_{}, pending_decoration_mode_{};
    rstd::uint32_t width_{}, height_{}, pending_width_{}, pending_height_{};
    rstd::uint32_t minimum_width_{}, minimum_height_{};
    rstd::uint32_t windowed_width_{}, windowed_height_{};
    ffi::wayland_fractional::wp_viewport *viewport_{};
    ffi::wayland_fractional::wp_fractional_scale_v1 *fractional_{};
    rstd::uint32_t scale_numerator_{120};
    rstd::uint64_t revision_{};
    bool configured_{}, close_requested_{}, fullscreen_{}, pending_fullscreen_{}, mode_known_{}, pending_mode_known_{}, scale_failed_{};
    bool suspended_{}, pending_suspended_{};
    Option<bool> minimize_capability_, pending_minimize_capability_;

    static auto pixels(rstd::uint32_t size, rstd::uint32_t numerator) -> rstd::uint64_t {
        return (rstd::uint64_t(size) * numerator + 60) / 120;
    }
    auto valid_scale(rstd::uint32_t numerator, rstd::uint32_t width, rstd::uint32_t height) const -> bool {
        return numerator && pixels(width, numerator) && pixels(height, numerator) &&
            pixels(width, numerator) <= ~rstd::uint32_t{} && pixels(height, numerator) <= ~rstd::uint32_t{};
    }
    void mark_scale_changed() { ++revision_; connection_.input().set_generation(revision_ + 1); }
    static void preferred_scale(void *data, ffi::wayland_fractional::wp_fractional_scale_v1 *, rstd::uint32_t numerator);
    ffi::wayland_fractional::wp_fractional_scale_v1_listener fractional_listener_{preferred_scale};
    void setup_fractional();
    void update_scale();
    static void enter_output(void *data, ffi::wayland::wl_surface *, ffi::wayland::wl_output *output) {
        static_cast<WaylandWindow *>(data)->connection_.outputs().enter(output);
    }
    static void leave_output(void *data, ffi::wayland::wl_surface *, ffi::wayland::wl_output *output) {
        static_cast<WaylandWindow *>(data)->connection_.outputs().leave(output);
    }
    ffi::wayland::wl_surface_listener surface_output_listener_{enter_output, leave_output};
    static void frame_done(void *data, ffi::wayland::wl_callback *callback, rstd::uint32_t);
    ffi::wayland::wl_callback_listener frame_listener_{frame_done};

    static void decoration_configure(void *data, ffi::xdg_decoration::zxdg_toplevel_decoration_v1 *,
                                     rstd::uint32_t mode) {
        static_cast<WaylandWindow *>(data)->pending_decoration_mode_ = mode;
    }
    ffi::xdg_decoration::zxdg_toplevel_decoration_v1_listener decoration_listener_{decoration_configure};

    void apply_configure();
    static void configure(void *data, ffi::xdg_shell::xdg_surface *surface, rstd::uint32_t serial) {
        ffi::xdg_shell::xdg_surface_ack_configure(surface, serial);
        static_cast<WaylandWindow *>(data)->apply_configure();
    }
    static void resize(void *data, ffi::xdg_shell::xdg_toplevel *, rstd::int32_t width,
                       rstd::int32_t height, ffi::wayland::wl_array *states);
    static void request_close(void *data, ffi::xdg_shell::xdg_toplevel *) {
        static_cast<WaylandWindow *>(data)->close_requested_ = true;
    }
    static void bounds(void *, ffi::xdg_shell::xdg_toplevel *, rstd::int32_t, rstd::int32_t) {}
    static void capabilities(void *data, ffi::xdg_shell::xdg_toplevel *, ffi::wayland::wl_array *values);

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
              const char *display = nullptr) -> Result<empty, WaylandWindowError>;
    auto roundtrip() -> Result<empty, WaylandWindowError>;
    auto request_frame_signal() -> Result<Serial,WaylandWindowError>;
    auto frame_signal(Serial token) const -> Option<bool> {
        if (!token || token!=frame_signal_token_) return None();
        return Some(bool(frame_signal_ready_));
    }
    auto frame_signal_pending() const -> bool { return frame_signal_!=nullptr; }
    auto cancel_frame_signal(Serial token) -> bool;
    void clear_frame_signal() {
        if (frame_signal_) ffi::wayland::wl_callback_destroy(frame_signal_);
        frame_signal_=nullptr; frame_signal_token_=0; frame_signal_ready_=false;
    }
    auto resize(rstd::uint32_t width, rstd::uint32_t height) -> Result<empty, WaylandWindowError>;
    auto set_fullscreen(bool enabled) -> Result<empty, WaylandWindowError>;
    auto fullscreen() const -> Option<bool> { return configured_ && mode_known_ ? Some(bool(fullscreen_)) : None<bool>(); }
    auto suspended() const -> Option<bool> {
        return toplevel_ && ffi::xdg_shell::xdg_toplevel_get_version(toplevel_) >= 6 && configured_ && mode_known_
            ? Some(bool(suspended_)) : None<bool>();
    }
    auto minimize_capability() const -> Option<bool>;
    auto minimize() -> bool {
        if (!toplevel_) return false;
        ffi::xdg_shell::xdg_toplevel_set_minimized(toplevel_); return true;
    }
    auto set_minimum_size(rstd::uint32_t width, rstd::uint32_t height) -> Result<empty, WaylandWindowError>;
    void close();
    auto display() const -> ffi::wayland::wl_display * { return connection_.display(); }
    auto surface() const -> ffi::wayland::wl_surface * { return surface_; }
    auto configured() const -> bool { return configured_; }
    auto close_requested() const -> bool { return close_requested_; }
    auto width() const -> rstd::uint32_t { return width_; }
    auto height() const -> rstd::uint32_t { return height_; }
    auto scale() const -> Scalar { return Scalar(scale_numerator_) / 120; }
    auto pixel_width() const -> rstd::uint32_t { return rstd::uint32_t(pixels(width_, scale_numerator_)); }
    auto pixel_height() const -> rstd::uint32_t { return rstd::uint32_t(pixels(height_, scale_numerator_)); }
    auto failed() const -> bool { return scale_failed_ || connection_.failed(); }
    auto displays() const -> DisplaySnapshot { return connection_.outputs().snapshot(); }
    auto display_revision() const -> Serial { return connection_.outputs().revision(); }
    auto minimum_size() const -> Size { return {Scalar(minimum_width_), Scalar(minimum_height_)}; }
    auto revision() const -> rstd::uint64_t { return revision_; }
    auto input() -> WaylandInput & { return connection_.input(); }
};
} // namespace vui::host::detail
