module vui.host;
import :platform.wayland.outputs;
import :ffi.wayland;
import :platform.wayland.protocol.xdg_output;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

void WaylandOutputs::mode(void *data, ffi::wl_output *, rstd::uint32_t flags, rstd::int32_t width, rstd::int32_t height, rstd::int32_t refresh) {
    if (!(flags & ffi::WL_OUTPUT_MODE_CURRENT)) return;
    auto &output = *static_cast<Output *>(data);
    output.pending_pixels = width > 0 && height > 0 ? Some(PixelSize{Index(width), Index(height)}) : None<PixelSize>();
    output.pending_refresh = refresh > 0 ? Some(Scalar(refresh) / 1000) : None<Scalar>();
}

auto WaylandOutputs::pending_bounds(const Output &output) -> Option<Rect> {
    if (output.pending_position.is_none() || output.pending_size.is_none()) return None();
    return Some(Rect{output.pending_position->x, output.pending_position->y,
                     output.pending_size->width, output.pending_size->height});
}

void WaylandOutputs::done(void *data, ffi::wl_output *) {
    auto &output = *static_cast<Output *>(data);
    bool updated = output.info.pixel_size != output.pending_pixels || output.info.refresh_hz != output.pending_refresh;
    output.info.pixel_size = output.pending_pixels; output.info.refresh_hz = output.pending_refresh;
    if (output.scale != output.pending_scale) {
        output.scale = output.pending_scale; updated = true;
    }
    if (output.xdg && ffi::version(output.xdg) >= 3) {
        auto bounds = pending_bounds(output);
        if (output.info.bounds != bounds) { output.info.bounds = bounds; updated = true; }
    }
    if (updated && output.entered) output.owner->changed();
}

void WaylandOutputs::scale(void *data, ffi::wl_output *, rstd::int32_t value) {
    auto &output = *static_cast<Output *>(data);
    if (value <= 0) { output.owner->failed_ = true; return; }
    output.pending_scale = value;
}

void WaylandOutputs::create_xdg(Output &entry) {
    if (!manager_ || entry.xdg) return;
    entry.xdg = ffi::create(manager_, entry.native);
    if (!entry.xdg || ffi::add_listener(entry.xdg, &xdg_listener_, &entry)) { failed_ = true; }
}

void WaylandOutputs::bind(ffi::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) {
    if (version < 2) return;
    for (const auto &item : outputs_) if (item->name == name) return;
    auto native = static_cast<ffi::wl_output *>(ffi::wl_registry_bind(
        registry, name, &ffi::wl_output_interface, version < 4 ? version : 4));
    if (!native) { failed_ = true; return; }
    auto entry = Box<Output>::make(); entry->owner = this; entry->native = native; entry->name = name; entry->info.id = new_session();
    if (ffi::wl_output_add_listener(native, &listener_, &*entry)) { destroy(native); failed_ = true; return; }
    create_xdg(*entry);
    outputs_.push(rstd::move(entry));
}

void WaylandOutputs::bind_manager(ffi::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) {
    if (manager_ || !version) return;
    manager_ = static_cast<ffi::zxdg_output_manager_v1 *>(ffi::wl_registry_bind(
        registry, name, &ffi::zxdg_output_manager_v1_interface, version < 3 ? version : 3));
    if (!manager_) { failed_ = true; return; }
    manager_name_ = name;
    for (auto &entry : outputs_) create_xdg(*entry);
}

void WaylandOutputs::remove(rstd::uint32_t name) {
    for (usize i{}; i < outputs_.len(); i += usize(1)) if (outputs_[i]->name == name) {
        auto entry = outputs_.remove(i); bool entered = entry->entered;
        if (entry->xdg) ffi::destroy(entry->xdg);
        destroy(entry->native); if (entered) changed(); return;
    }
}

auto WaylandOutputs::scale() const -> rstd::int32_t {
    rstd::int32_t result = 1;
    for (const auto &entry : outputs_) if (entry->entered && entry->scale > result) result = entry->scale;
    return result;
}

auto WaylandOutputs::snapshot() const -> DisplaySnapshot {
    DisplaySnapshot result; result.status = DisplayStatus::Available; result.revision = revision_;
    for (const auto &entry : outputs_) if (entry->entered) result.outputs.push(DisplayInfo(entry->info));
    return result;
}

void WaylandOutputs::close() {
    for (auto &entry : outputs_) { if (entry->xdg) ffi::destroy(entry->xdg); destroy(entry->native); }
    outputs_.clear();
    if (manager_) ffi::destroy_manager(manager_);
    manager_ = nullptr; manager_name_ = 0; revision_ = 1; failed_ = false;
}
} // namespace vui::host::detail
