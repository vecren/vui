module vui.host:platform.wayland.outputs;
import :ffi.wayland;
import :platform.wayland.protocol.xdg_output;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

class WaylandOutputs {
    friend struct WaylandOutputsTest;
    struct Output {
        WaylandOutputs *owner{};
        ffi::wl_output *native{};
        ffi::zxdg_output_v1 *xdg{};
        rstd::uint32_t name{};
        rstd::int32_t scale{1}, pending_scale{1};
        DisplayInfo info;
        Option<PixelSize> pending_pixels;
        Option<Scalar> pending_refresh;
        Option<Point> pending_position;
        Option<Size> pending_size;
        bool entered{};
    };
    Vec<Box<Output>> outputs_;
    ffi::zxdg_output_manager_v1 *manager_{};
    rstd::uint32_t manager_name_{};
    Serial revision_{1};
    void *sink_{};
    void (*changed_)(void *){};
    bool failed_{};

    auto output(ffi::wl_output *native) -> Output * {
        for (auto &entry : outputs_) if (entry->native == native) return &*entry;
        return nullptr;
    }
    void changed() { ++revision_; if (changed_) changed_(sink_); }
    static void geometry(void *, ffi::wl_output *, rstd::int32_t, rstd::int32_t, rstd::int32_t,
                         rstd::int32_t, rstd::int32_t, const char *, const char *, rstd::int32_t) {}
    static void mode(void *data, ffi::wl_output *, rstd::uint32_t flags, rstd::int32_t width, rstd::int32_t height, rstd::int32_t refresh);
    static auto pending_bounds(const Output &output) -> Option<Rect>;
    static void publish_bounds(Output &output) {
        auto bounds = pending_bounds(output);
        if (output.info.bounds != bounds) { output.info.bounds = bounds; if (output.entered) output.owner->changed(); }
    }
    static void done(void *data, ffi::wl_output *);
    static void scale(void *data, ffi::wl_output *, rstd::int32_t value);
    static void name(void *, ffi::wl_output *, const char *) {}
    static void description(void *, ffi::wl_output *, const char *) {}
    ffi::wl_output_listener listener_{geometry, mode, done, scale, name, description};
    static void logical_position(void *data, ffi::zxdg_output_v1 *, rstd::int32_t x, rstd::int32_t y) {
        static_cast<Output *>(data)->pending_position = Some(Point{Scalar(x), Scalar(y)});
    }
    static void logical_size(void *data, ffi::zxdg_output_v1 *, rstd::int32_t width, rstd::int32_t height) {
        auto &output = *static_cast<Output *>(data);
        output.pending_size = width > 0 && height > 0 ? Some(Size{Scalar(width), Scalar(height)}) : None<Size>();
    }
    static void xdg_done(void *data, ffi::zxdg_output_v1 *native) {
        if (ffi::version(native) < 3) publish_bounds(*static_cast<Output *>(data));
    }
    static void xdg_name(void *, ffi::zxdg_output_v1 *, const char *) {}
    static void xdg_description(void *, ffi::zxdg_output_v1 *, const char *) {}
    ffi::zxdg_output_v1_listener xdg_listener_{logical_position, logical_size, xdg_done, xdg_name, xdg_description};
    void create_xdg(Output &entry);
    static void destroy(ffi::wl_output *native) {
        if (ffi::wl_output_get_version(native) >= ffi::WL_OUTPUT_RELEASE_SINCE_VERSION) ffi::wl_output_release(native);
        else ffi::wl_output_destroy(native);
    }
  public:
    WaylandOutputs() = default;
    WaylandOutputs(const WaylandOutputs &) = delete;
    auto operator=(const WaylandOutputs &) -> WaylandOutputs & = delete;
    ~WaylandOutputs() { close(); }
    void set_callback(void *sink, void (*callback)(void *)) { sink_ = sink; changed_ = callback; }
    void bind(ffi::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version);
    void bind_manager(ffi::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version);
    void remove_manager(rstd::uint32_t name) {
        if (!manager_ || manager_name_ != name) return;
        ffi::destroy_manager(manager_); manager_ = nullptr; manager_name_ = 0;
    }
    void remove(rstd::uint32_t name);
    void enter(ffi::wl_output *native) {
        auto entry = output(native); if (!entry || entry->entered) return;
        entry->entered = true; changed();
    }
    void leave(ffi::wl_output *native) {
        auto entry = output(native); if (!entry || !entry->entered) return;
        entry->entered = false; changed();
    }
    auto scale() const -> rstd::int32_t;
    auto failed() const -> bool { return failed_; }
    auto snapshot() const -> DisplaySnapshot;
    auto revision() const -> Serial { return revision_; }
    void close();
};
}
