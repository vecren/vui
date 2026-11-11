module vui.host:platform.wayland_outputs;
import :ffi.wayland;
import :ffi.wayland_output;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;
namespace wl = ffi::wayland;
namespace xo = ffi::wayland_output;

class WaylandOutputs {
    friend struct WaylandOutputsTest;
    struct Output {
        WaylandOutputs *owner{};
        wl::wl_output *native{};
        xo::zxdg_output_v1 *xdg{};
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
    xo::zxdg_output_manager_v1 *manager_{};
    rstd::uint32_t manager_name_{};
    Serial revision_{1};
    void *sink_{};
    void (*changed_)(void *){};
    bool failed_{};

    auto output(wl::wl_output *native) -> Output * {
        for (auto &entry : outputs_) if (entry->native == native) return &*entry;
        return nullptr;
    }
    void changed() { ++revision_; if (changed_) changed_(sink_); }
    static void geometry(void *, wl::wl_output *, rstd::int32_t, rstd::int32_t, rstd::int32_t,
                         rstd::int32_t, rstd::int32_t, const char *, const char *, rstd::int32_t) {}
    static void mode(void *data, wl::wl_output *, rstd::uint32_t flags, rstd::int32_t width, rstd::int32_t height, rstd::int32_t refresh) {
        if (!(flags & wl::WL_OUTPUT_MODE_CURRENT)) return;
        auto &output = *static_cast<Output *>(data);
        output.pending_pixels = width > 0 && height > 0 ? Some(PixelSize{Index(width), Index(height)}) : None<PixelSize>();
        output.pending_refresh = refresh > 0 ? Some(Scalar(refresh) / 1000) : None<Scalar>();
    }
    static auto pending_bounds(const Output &output) -> Option<Rect> {
        if (output.pending_position.is_none() || output.pending_size.is_none()) return None();
        return Some(Rect{output.pending_position->x, output.pending_position->y,
                         output.pending_size->width, output.pending_size->height});
    }
    static void publish_bounds(Output &output) {
        auto bounds = pending_bounds(output);
        if (output.info.bounds != bounds) { output.info.bounds = bounds; if (output.entered) output.owner->changed(); }
    }
    static void done(void *data, wl::wl_output *) {
        auto &output = *static_cast<Output *>(data);
        bool updated = output.info.pixel_size != output.pending_pixels || output.info.refresh_hz != output.pending_refresh;
        output.info.pixel_size = output.pending_pixels; output.info.refresh_hz = output.pending_refresh;
        if (output.scale != output.pending_scale) {
            output.scale = output.pending_scale; updated = true;
        }
        if (output.xdg && xo::version(output.xdg) >= 3) {
            auto bounds = pending_bounds(output);
            if (output.info.bounds != bounds) { output.info.bounds = bounds; updated = true; }
        }
        if (updated && output.entered) output.owner->changed();
    }
    static void scale(void *data, wl::wl_output *, rstd::int32_t value) {
        auto &output = *static_cast<Output *>(data);
        if (value <= 0) { output.owner->failed_ = true; return; }
        output.pending_scale = value;
    }
    static void name(void *, wl::wl_output *, const char *) {}
    static void description(void *, wl::wl_output *, const char *) {}
    wl::wl_output_listener listener_{geometry, mode, done, scale, name, description};
    static void logical_position(void *data, xo::zxdg_output_v1 *, rstd::int32_t x, rstd::int32_t y) {
        static_cast<Output *>(data)->pending_position = Some(Point{Scalar(x), Scalar(y)});
    }
    static void logical_size(void *data, xo::zxdg_output_v1 *, rstd::int32_t width, rstd::int32_t height) {
        auto &output = *static_cast<Output *>(data);
        output.pending_size = width > 0 && height > 0 ? Some(Size{Scalar(width), Scalar(height)}) : None<Size>();
    }
    static void xdg_done(void *data, xo::zxdg_output_v1 *native) {
        if (xo::version(native) < 3) publish_bounds(*static_cast<Output *>(data));
    }
    static void xdg_name(void *, xo::zxdg_output_v1 *, const char *) {}
    static void xdg_description(void *, xo::zxdg_output_v1 *, const char *) {}
    xo::zxdg_output_v1_listener xdg_listener_{logical_position, logical_size, xdg_done, xdg_name, xdg_description};
    void create_xdg(Output &entry) {
        if (!manager_ || entry.xdg) return;
        entry.xdg = xo::create(manager_, entry.native);
        if (!entry.xdg || xo::add_listener(entry.xdg, &xdg_listener_, &entry)) { failed_ = true; }
    }
    static void destroy(wl::wl_output *native) {
        if (wl::wl_output_get_version(native) >= wl::output_release_version) wl::wl_output_release(native);
        else wl::wl_output_destroy(native);
    }
  public:
    WaylandOutputs() = default;
    WaylandOutputs(const WaylandOutputs &) = delete;
    auto operator=(const WaylandOutputs &) -> WaylandOutputs & = delete;
    ~WaylandOutputs() { close(); }
    void set_callback(void *sink, void (*callback)(void *)) { sink_ = sink; changed_ = callback; }
    void bind(wl::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) {
        if (version < 2) return;
        for (const auto &item : outputs_) if (item->name == name) return;
        auto native = static_cast<wl::wl_output *>(wl::wl_registry_bind(
            registry, name, &wl::wl_output_interface, version < 4 ? version : 4));
        if (!native) { failed_ = true; return; }
        auto entry = Box<Output>::make(); entry->owner = this; entry->native = native; entry->name = name; entry->info.id = new_session();
        if (wl::wl_output_add_listener(native, &listener_, &*entry)) { destroy(native); failed_ = true; return; }
        create_xdg(*entry);
        outputs_.push(rstd::move(entry));
    }
    void bind_manager(wl::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) {
        if (manager_ || !version) return;
        manager_ = static_cast<xo::zxdg_output_manager_v1 *>(wl::wl_registry_bind(
            registry, name, &xo::zxdg_output_manager_v1_interface, version < 3 ? version : 3));
        if (!manager_) { failed_ = true; return; }
        manager_name_ = name;
        for (auto &entry : outputs_) create_xdg(*entry);
    }
    void remove_manager(rstd::uint32_t name) {
        if (!manager_ || manager_name_ != name) return;
        xo::destroy_manager(manager_); manager_ = nullptr; manager_name_ = 0;
    }
    void remove(rstd::uint32_t name) {
        for (usize i{}; i < outputs_.len(); i += usize(1)) if (outputs_[i]->name == name) {
            auto entry = outputs_.remove(i); bool entered = entry->entered;
            if (entry->xdg) xo::destroy(entry->xdg);
            destroy(entry->native); if (entered) changed(); return;
        }
    }
    void enter(wl::wl_output *native) {
        auto entry = output(native); if (!entry || entry->entered) return;
        entry->entered = true; changed();
    }
    void leave(wl::wl_output *native) {
        auto entry = output(native); if (!entry || !entry->entered) return;
        entry->entered = false; changed();
    }
    auto scale() const -> rstd::int32_t {
        rstd::int32_t result = 1;
        for (const auto &entry : outputs_) if (entry->entered && entry->scale > result) result = entry->scale;
        return result;
    }
    auto failed() const -> bool { return failed_; }
    auto snapshot() const -> DisplaySnapshot {
        DisplaySnapshot result; result.status = DisplayStatus::Available; result.revision = revision_;
        for (const auto &entry : outputs_) if (entry->entered) result.outputs.push(DisplayInfo(entry->info));
        return result;
    }
    auto revision() const -> Serial { return revision_; }
    void close() {
        for (auto &entry : outputs_) { if (entry->xdg) xo::destroy(entry->xdg); destroy(entry->native); }
        outputs_.clear();
        if (manager_) xo::destroy_manager(manager_);
        manager_ = nullptr; manager_name_ = 0; revision_ = 1; failed_ = false;
    }
};
}
