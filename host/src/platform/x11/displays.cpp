module vui.host;
import :platform.x11.displays;
import :platform.x11.ffi.xcb;
import :platform.x11.ffi.xcb_randr;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

auto X11Displays::intersect(Rect a, Rect b) -> Option<Rect> {
    auto left = rstd::cmp::max(a.x, b.x), top = rstd::cmp::max(a.y, b.y);
    auto right = rstd::cmp::min(a.x + a.width, b.x + b.width);
    auto bottom = rstd::cmp::min(a.y + a.height, b.y + b.height);
    if (right <= left || bottom <= top) return None();
    return Some(Rect{left, top, right - left, bottom - top});
}

auto X11Displays::refresh(const Mode &mode) -> Option<Scalar> {
    if (!mode.dot_clock || !mode.htotal || !mode.vtotal) return None();
    Scalar rate = Scalar(mode.dot_clock) / (Scalar(mode.htotal) * Scalar(mode.vtotal));
    if (mode.flags & ffi::XCB_RANDR_MODE_FLAG_INTERLACE) rate *= 2;
    if (mode.flags & ffi::XCB_RANDR_MODE_FLAG_DOUBLE_SCAN) rate /= 2;
    return finite(rate) && rate > 0 ? Some(Scalar(rate)) : None<Scalar>();
}

void X11Displays::start_topology() {
    if (!connection_ || !randr_ || !topology_dirty_ || resource_active_ || topology_collecting_ || topology_complete_) return;
    resource_sequence_ = current_resources_ ? randr_api_->xcb_randr_get_screen_resources_current(connection_, root_).sequence
                                           : randr_api_->xcb_randr_get_screen_resources(connection_, root_).sequence;
    resource_active_ = true; topology_dirty_ = false;
}

void X11Displays::receive_resources() {
    if (!resource_active_) return;
    void *raw{}; ffi::xcb_generic_error_t *error{};
    if (!xcb_->xcb_poll_for_reply(connection_, resource_sequence_, &raw, &error)) return;
    resource_active_ = false;
    if (error || !raw) {
        if (error) ffi::free(error); if (raw) ffi::free(raw);
        topology_ready_ = false; ++revision_; return;
    }
    rstd::uint16_t output_count{}, mode_count{};
    const rstd::uint32_t *ids{}; const ffi::xcb_randr_mode_info_t *modes{};
    if (current_resources_) {
        auto reply = static_cast<ffi::xcb_randr_get_screen_resources_current_reply_t *>(raw);
        output_count = reply->num_outputs; mode_count = reply->num_modes; config_timestamp_ = reply->config_timestamp;
        ids = randr_api_->xcb_randr_get_screen_resources_current_outputs(reply);
        modes = randr_api_->xcb_randr_get_screen_resources_current_modes(reply);
    } else {
        auto reply = static_cast<ffi::xcb_randr_get_screen_resources_reply_t *>(raw);
        output_count = reply->num_outputs; mode_count = reply->num_modes; config_timestamp_ = reply->config_timestamp;
        ids = randr_api_->xcb_randr_get_screen_resources_outputs(reply); modes = randr_api_->xcb_randr_get_screen_resources_modes(reply);
    }
    if (output_count > 128 || mode_count > 4096 || (output_count && !ids) || (mode_count && !modes)) {
        ffi::free(raw); topology_ready_ = false; ++revision_; return;
    }
    modes_.clear(); pending_outputs_.clear();
    for (unsigned i{}; i < mode_count; ++i) {
        const auto &m = modes[i]; modes_.push({m.id, m.width, m.height, m.mode_flags, m.dot_clock, m.htotal, m.vtotal});
    }
    for (unsigned i{}; i < output_count; ++i) {
        Output output; output.native = ids[i]; output.info.id = id(ids[i]);
        output.output_query = randr_api_->xcb_randr_get_output_info(connection_, ids[i], config_timestamp_).sequence;
        output.output_pending = true; pending_outputs_.push(rstd::move(output));
    }
    ffi::free(raw); topology_collecting_ = true; topology_invalid_ = false;
}

void X11Displays::receive_outputs() {
    if (!topology_collecting_) return;
    bool waiting{};
    for (auto &output : pending_outputs_) {
        if (output.output_pending) {
            void *raw{}; ffi::xcb_generic_error_t *error{};
            if (!xcb_->xcb_poll_for_reply(connection_, output.output_query, &raw, &error)) { waiting = true; continue; }
            output.output_pending = false;
            auto reply = static_cast<ffi::xcb_randr_get_output_info_reply_t *>(raw);
            if (error || !reply || reply->status != ffi::XCB_RANDR_SET_CONFIG_SUCCESS) topology_invalid_ = true;
            else if (reply->connection == ffi::XCB_RANDR_CONNECTION_CONNECTED && reply->crtc) {
                output.connected = true;
                output.crtc_query = randr_api_->xcb_randr_get_crtc_info(connection_, reply->crtc, config_timestamp_).sequence;
                output.crtc_pending = true;
            }
            if (raw) ffi::free(raw); if (error) ffi::free(error);
        }
        if (output.crtc_pending) {
            void *raw{}; ffi::xcb_generic_error_t *error{};
            if (!xcb_->xcb_poll_for_reply(connection_, output.crtc_query, &raw, &error)) { waiting = true; continue; }
            output.crtc_pending = false;
            auto reply = static_cast<ffi::xcb_randr_get_crtc_info_reply_t *>(raw);
            if (error || !reply || reply->status != ffi::XCB_RANDR_SET_CONFIG_SUCCESS) topology_invalid_ = true;
            else if (!reply->mode || !reply->width || !reply->height) output.connected = false;
            else {
                output.info.bounds = Some(Rect{Scalar(reply->x), Scalar(reply->y), Scalar(reply->width), Scalar(reply->height)});
                for (const auto &mode : modes_) if (mode.id == reply->mode) {
                    if (mode.width && mode.height) output.info.pixel_size = Some(mode_size(mode, reply->rotation));
                    output.info.refresh_hz = refresh(mode); break;
                }
            }
            if (raw) ffi::free(raw); if (error) ffi::free(error);
        }
        waiting |= output.output_pending || output.crtc_pending;
    }
    if (waiting) return;
    topology_collecting_ = false; topology_complete_ = true;
}

void X11Displays::finish_topology() {
    if (!topology_complete_) return;
    topology_complete_ = false;
    if (topology_invalid_ || topology_dirty_) {
        if (topology_invalid_ && ++retries_ <= 3) topology_dirty_ = true;
        else if (topology_invalid_) { topology_ready_ = false; ++revision_; }
        pending_outputs_.clear(); return;
    }
    pending_outputs_.retain([](const Output &output) { return output.connected && output.info.bounds.is_some(); });
    outputs_ = rstd::exchange(pending_outputs_, Vec<Output>{}); topology_ready_ = true; retries_ = 0; ++revision_;
}

void X11Displays::start_geometry() {
    if (window_geometry_.dirty && !window_batch_ && !window_geometry_.active && !window_position_.active) {
        window_geometry_.sequence = xcb_->xcb_get_geometry(connection_, window_).sequence;
        window_position_.sequence = xcb_->xcb_translate_coordinates(connection_, window_, root_, 0, 0).sequence;
        window_geometry_.active = window_position_.active = true;
        window_batch_ = true;
        window_geometry_.dirty = false; pending_window_size_ = None(); pending_window_position_ = None();
    }
    if (!randr_ && root_geometry_.dirty && !root_batch_ && !root_geometry_.active) {
        root_geometry_.sequence = xcb_->xcb_get_geometry(connection_, root_).sequence;
        root_geometry_.active = true; root_geometry_.dirty = false; root_batch_ = true; pending_root_size_ = None();
    }
}

void X11Displays::receive_geometry() {
    if (window_geometry_.active) {
        void *raw{}; ffi::xcb_generic_error_t *error{};
        if (xcb_->xcb_poll_for_reply(connection_, window_geometry_.sequence, &raw, &error)) {
            window_geometry_.active = false; auto reply = static_cast<ffi::xcb_get_geometry_reply_t *>(raw);
            if (!error && reply && reply->width && reply->height) pending_window_size_ = Some(Size{Scalar(reply->width), Scalar(reply->height)});
            if (raw) ffi::free(raw); if (error) ffi::free(error);
        }
    }
    if (window_position_.active) {
        void *raw{}; ffi::xcb_generic_error_t *error{};
        if (xcb_->xcb_poll_for_reply(connection_, window_position_.sequence, &raw, &error)) {
            window_position_.active = false; auto reply = static_cast<ffi::xcb_translate_coordinates_reply_t *>(raw);
            if (!error && reply && reply->same_screen) pending_window_position_ = Some(Point{Scalar(reply->dst_x), Scalar(reply->dst_y)});
            if (raw) ffi::free(raw); if (error) ffi::free(error);
        }
    }
    if (root_geometry_.active) {
        void *raw{}; ffi::xcb_generic_error_t *error{};
        if (xcb_->xcb_poll_for_reply(connection_, root_geometry_.sequence, &raw, &error)) {
            root_geometry_.active = false; auto reply = static_cast<ffi::xcb_get_geometry_reply_t *>(raw);
            if (!error && reply && reply->width && reply->height) pending_root_size_ = Some(Size{Scalar(reply->width), Scalar(reply->height)});
            if (raw) ffi::free(raw); if (error) ffi::free(error);
        }
    }
}

void X11Displays::finish_geometry() {
    if (window_batch_ && !window_geometry_.active && !window_position_.active) {
        window_batch_ = false;
        Option<Rect> result;
        if (pending_window_size_.is_some() && pending_window_position_.is_some())
            result = Some(Rect{pending_window_position_->x, pending_window_position_->y, pending_window_size_->width, pending_window_size_->height});
        if (!window_geometry_.dirty && window_rect_ != result) { window_rect_ = result; ++revision_; }
        pending_window_size_ = None(); pending_window_position_ = None();
    }
    if (root_batch_ && !root_geometry_.active) {
        root_batch_ = false;
        if (!root_geometry_.dirty && pending_root_size_.is_some()) {
            outputs_.clear(); Output output; output.info.id = fallback_id_;
            output.info.bounds = Some(Rect{0, 0, pending_root_size_->width, pending_root_size_->height});
            output.info.pixel_size = Some(PixelSize{Index(pending_root_size_->width), Index(pending_root_size_->height)});
            outputs_.push(rstd::move(output)); topology_ready_ = true; ++revision_;
        } else if (!root_geometry_.dirty) { topology_ready_ = false; ++revision_; }
        pending_root_size_ = None();
    }
}

void X11Displays::start_workarea() {
    if (!desktop_.dirty || workspace_batch_ || desktop_.active || workarea_.active) return;
    desktop_.sequence = xcb_->xcb_get_property(connection_, 0, root_, desktop_atom_, ffi::XCB_ATOM_CARDINAL, 0, 1).sequence;
    workarea_.sequence = xcb_->xcb_get_property(connection_, 0, root_, workarea_atom_, ffi::XCB_ATOM_CARDINAL, 0, 4096).sequence;
    desktop_.active = workarea_.active = true; desktop_.dirty = false;
    workspace_batch_ = true;
    pending_desktop_ = None(); pending_workarea_ = None();
}

void X11Displays::receive_property(Query &query, Option<Vec<rstd::uint32_t>> &result) {
    if (!query.active) return;
    void *raw{}; ffi::xcb_generic_error_t *error{};
    if (!xcb_->xcb_poll_for_reply(connection_, query.sequence, &raw, &error)) return;
    query.active = false; auto reply = static_cast<ffi::xcb_get_property_reply_t *>(raw);
    if (!error && reply && reply->type == ffi::XCB_ATOM_CARDINAL && reply->format == 32 && !reply->bytes_after) {
        auto length = xcb_->xcb_get_property_value_length(reply);
        auto data = static_cast<const rstd::uint32_t *>(xcb_->xcb_get_property_value(reply));
        if (length >= 0 && length % 4 == 0 && (!length || data)) {
            Vec<rstd::uint32_t> values; for (int i{}; i < length / 4; ++i) values.push(rstd::uint32_t(data[i]));
            result = Some(rstd::move(values));
        }
    }
    if (raw) ffi::free(raw); if (error) ffi::free(error);
}

void X11Displays::finish_workarea() {
    if (!workspace_batch_ || desktop_.active || workarea_.active) return;
    workspace_batch_ = false;
    if (desktop_.dirty) { pending_desktop_ = None(); pending_workarea_ = None(); return; }
    Option<Rect> result;
    if (pending_desktop_.is_some() && pending_desktop_->len() == usize(1) && pending_workarea_.is_some()) {
        auto desktop = (*pending_desktop_)[usize(0)]; auto &values = *pending_workarea_;
        if (desktop < values.len().to_primitive() / 4) {
            auto index = usize(desktop * 4); auto width = values[index + usize(2)], height = values[index + usize(3)];
            if (width && height && width <= 0x7fffffff && height <= 0x7fffffff)
                result = Some(Rect{Scalar(rstd::int32_t(values[index])), Scalar(rstd::int32_t(values[index + usize(1)])), Scalar(width), Scalar(height)});
        }
    }
    if (workspace_ != result) { workspace_ = result; ++revision_; }
    pending_desktop_ = None(); pending_workarea_ = None();
}

void X11Displays::event(const ffi::xcb_generic_event_t &event) {
    auto type = event.response_type & 0x7f;
    if (!type) { failed_ = true; return; }
    if (randr_ && (type == randr_event_ || type == randr_event_ + 1)) { topology_dirty_ = true; retries_ = 0; }
    if (type == ffi::XCB_PROPERTY_NOTIFY) {
        auto &property = *reinterpret_cast<const ffi::xcb_property_notify_event_t *>(&event);
        if (property.window == root_ && (property.atom == desktop_atom_ || property.atom == workarea_atom_)) desktop_.dirty = true;
    }
    if (type == ffi::XCB_CONFIGURE_NOTIFY || type == ffi::XCB_REPARENT_NOTIFY) {
        window_geometry_.dirty = true;
        if (!randr_) root_geometry_.dirty = true;
    }
}

void X11Displays::drain_events(unsigned &count) {
    while (count < 32) {
        auto item = deferred_ ? rstd::exchange(deferred_, static_cast<ffi::xcb_generic_event_t *>(nullptr)) : xcb_->xcb_poll_for_event(connection_);
        if (!item) break;
        event(*item); ffi::free(item); ++count;
    }
    if (count == 32 && !deferred_) deferred_ = xcb_->xcb_poll_for_event(connection_);
}

auto X11Displays::open(const char *display, ffi::xcb_window_t root, ffi::xcb_window_t window, ffi::xcb_atom_t desktop, ffi::xcb_atom_t workarea) -> bool {
    if (connection_ || !root || !window || !desktop || !workarea) return false;
    xcb_ = ffi::XcbDispatch::open();
    if (xcb_.is_none()) return false;
    int screen{}; connection_ = xcb_->xcb_connect(display, &screen);
    if (!connection_ || xcb_->xcb_connection_has_error(connection_)) { close(); return false; }
    root_ = root; window_ = window; desktop_atom_ = desktop; workarea_atom_ = workarea;
    randr_api_ = ffi::RandrDispatch::open();
    auto extension = randr_api_.is_some() ? xcb_->xcb_get_extension_data(connection_, randr_api_->xcb_randr_id) : nullptr;
    if (extension && extension->present) {
        auto cookie = randr_api_->xcb_randr_query_version(connection_, 1, 5); ffi::xcb_generic_error_t *error{};
        auto version = randr_api_->xcb_randr_query_version_reply(connection_, cookie, &error);
        if (!error && version && (version->major_version > 1 || (version->major_version == 1 && version->minor_version >= 2))) {
            randr_ = true; randr_event_ = extension->first_event;
            current_resources_ = version->major_version > 1 || version->minor_version >= 3;
            randr_api_->xcb_randr_select_input(connection_, root_, ffi::XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                ffi::XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE | ffi::XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
        }
        if (version) ffi::free(version); if (error) ffi::free(error);
    }
    rstd::uint32_t mask = ffi::XCB_EVENT_MASK_PROPERTY_CHANGE | ffi::XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_->xcb_change_window_attributes(connection_, root_, ffi::XCB_CW_EVENT_MASK, &mask);
    mask = ffi::XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_->xcb_change_window_attributes(connection_, window_, ffi::XCB_CW_EVENT_MASK, &mask);
    start_topology(); start_geometry(); start_workarea(); xcb_->xcb_flush(connection_); return true;
}

void X11Displays::dispatch() {
    if (!connection_) return;
    unsigned count{}; drain_events(count);
    receive_resources(); receive_outputs(); receive_geometry(); receive_workarea();
    drain_events(count);
    if (!deferred_) { finish_topology(); finish_geometry(); finish_workarea(); }
    start_topology(); start_geometry(); start_workarea(); xcb_->xcb_flush(connection_);
    if (xcb_->xcb_connection_has_error(connection_)) failed_ = true;
}

auto X11Displays::snapshot() const -> DisplaySnapshot {
    DisplaySnapshot result; result.revision = revision_;
    if (!connection_ || failed_ || !topology_ready_ || window_rect_.is_none()) return result;
    result.status = DisplayStatus::Available;
    for (const auto &output : outputs_) if (output.info.bounds.is_some() && intersect(*window_rect_, *output.info.bounds).is_some()) {
        auto info = DisplayInfo(output.info);
        if (workspace_.is_some()) info.usable_bounds = intersect(*info.bounds, *workspace_);
        result.outputs.push(rstd::move(info));
    }
    return result;
}

void X11Displays::close() {
    if (deferred_) ffi::free(deferred_); if (connection_) xcb_->xcb_disconnect(connection_);
    deferred_ = nullptr; connection_ = nullptr; root_ = window_ = 0;
    randr_api_ = None(); xcb_ = None();
    randr_ = current_resources_ = failed_ = topology_ready_ = resource_active_ = topology_collecting_ = topology_invalid_ = false;
    window_batch_ = root_batch_ = workspace_batch_ = topology_complete_ = false;
    topology_dirty_ = true; retries_ = 0; outputs_.clear(); pending_outputs_.clear(); modes_.clear();
    window_geometry_ = {}; window_position_ = {}; root_geometry_ = {}; desktop_ = {}; workarea_ = {};
    window_rect_ = None(); workspace_ = None(); pending_window_size_ = None(); pending_root_size_ = None(); pending_window_position_ = None();
    pending_desktop_ = None(); pending_workarea_ = None(); revision_ = 1; fallback_id_ = new_session();
}
} // namespace vui::host::detail
