module vui.host:platform.x11_displays;
import :ffi.xcb;
import :ffi.xcb_randr;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;
namespace xc = ffi::xcb;
namespace rr = ffi::xcb_randr;

class X11Displays {
    friend struct X11DisplaysTest;
    struct Query { unsigned sequence{}; bool active{}, dirty{true}; };
    struct Output {
        rstd::uint32_t native{};
        DisplayInfo info;
        unsigned output_query{}, crtc_query{};
        bool output_pending{}, crtc_pending{}, connected{};
    };
    struct Mode { rstd::uint32_t id{}, width{}, height{}, flags{}, dot_clock{}, htotal{}, vtotal{}; };
    xc::xcb_connection_t *connection_{};
    xc::xcb_generic_event_t *deferred_{};
    xc::xcb_window_t root_{}, window_{};
    xc::xcb_atom_t desktop_atom_{}, workarea_atom_{};
    rstd::uint8_t randr_event_{};
    bool randr_{}, current_resources_{}, failed_{}, topology_ready_{}, topology_dirty_{true}, resource_active_{};
    bool topology_collecting_{}, topology_invalid_{};
    bool window_batch_{}, root_batch_{}, workspace_batch_{}, topology_complete_{};
    unsigned resource_sequence_{}, retries_{};
    rstd::uint32_t config_timestamp_{};
    Vec<Mode> modes_;
    Vec<Output> outputs_, pending_outputs_;
    Query window_geometry_, window_position_, root_geometry_, desktop_, workarea_;
    Option<Rect> window_rect_, workspace_;
    Option<Size> pending_window_size_;
    Option<Size> pending_root_size_;
    Option<Point> pending_window_position_;
    Option<Vec<rstd::uint32_t>> pending_desktop_, pending_workarea_;
    Serial fallback_id_{new_session()}, revision_{1};

    static auto intersect(Rect a, Rect b) -> Option<Rect> {
        auto left = rstd::cmp::max(a.x, b.x), top = rstd::cmp::max(a.y, b.y);
        auto right = rstd::cmp::min(a.x + a.width, b.x + b.width);
        auto bottom = rstd::cmp::min(a.y + a.height, b.y + b.height);
        if (right <= left || bottom <= top) return None();
        return Some(Rect{left, top, right - left, bottom - top});
    }
    static auto refresh(const Mode &mode) -> Option<Scalar> {
        if (!mode.dot_clock || !mode.htotal || !mode.vtotal) return None();
        Scalar rate = Scalar(mode.dot_clock) / (Scalar(mode.htotal) * Scalar(mode.vtotal));
        if (mode.flags & rr::XCB_RANDR_MODE_FLAG_INTERLACE) rate *= 2;
        if (mode.flags & rr::XCB_RANDR_MODE_FLAG_DOUBLE_SCAN) rate /= 2;
        return finite(rate) && rate > 0 ? Some(Scalar(rate)) : None<Scalar>();
    }
    static auto mode_size(const Mode &mode, rstd::uint16_t rotation) -> PixelSize {
        bool rotated = rotation & (rr::XCB_RANDR_ROTATION_ROTATE_90 | rr::XCB_RANDR_ROTATION_ROTATE_270);
        return {rotated ? mode.height : mode.width, rotated ? mode.width : mode.height};
    }
    auto id(rstd::uint32_t native) const -> Serial {
        for (const auto &output : outputs_) if (output.native == native) return output.info.id;
        return new_session();
    }
    void start_topology() {
        if (!connection_ || !randr_ || !topology_dirty_ || resource_active_ || topology_collecting_ || topology_complete_) return;
        resource_sequence_ = current_resources_ ? rr::xcb_randr_get_screen_resources_current(connection_, root_).sequence
                                               : rr::xcb_randr_get_screen_resources(connection_, root_).sequence;
        resource_active_ = true; topology_dirty_ = false;
    }
    void receive_resources() {
        if (!resource_active_) return;
        void *raw{}; xc::xcb_generic_error_t *error{};
        if (!xc::xcb_poll_for_reply(connection_, resource_sequence_, &raw, &error)) return;
        resource_active_ = false;
        if (error || !raw) {
            if (error) xc::free(error); if (raw) xc::free(raw);
            topology_ready_ = false; ++revision_; return;
        }
        rstd::uint16_t output_count{}, mode_count{};
        const rstd::uint32_t *ids{}; const rr::xcb_randr_mode_info_t *modes{};
        if (current_resources_) {
            auto reply = static_cast<rr::xcb_randr_get_screen_resources_current_reply_t *>(raw);
            output_count = reply->num_outputs; mode_count = reply->num_modes; config_timestamp_ = reply->config_timestamp;
            ids = rr::xcb_randr_get_screen_resources_current_outputs(reply);
            modes = rr::xcb_randr_get_screen_resources_current_modes(reply);
        } else {
            auto reply = static_cast<rr::xcb_randr_get_screen_resources_reply_t *>(raw);
            output_count = reply->num_outputs; mode_count = reply->num_modes; config_timestamp_ = reply->config_timestamp;
            ids = rr::xcb_randr_get_screen_resources_outputs(reply); modes = rr::xcb_randr_get_screen_resources_modes(reply);
        }
        if (output_count > 128 || mode_count > 4096 || (output_count && !ids) || (mode_count && !modes)) {
            xc::free(raw); topology_ready_ = false; ++revision_; return;
        }
        modes_.clear(); pending_outputs_.clear();
        for (unsigned i{}; i < mode_count; ++i) {
            const auto &m = modes[i]; modes_.push({m.id, m.width, m.height, m.mode_flags, m.dot_clock, m.htotal, m.vtotal});
        }
        for (unsigned i{}; i < output_count; ++i) {
            Output output; output.native = ids[i]; output.info.id = id(ids[i]);
            output.output_query = rr::xcb_randr_get_output_info(connection_, ids[i], config_timestamp_).sequence;
            output.output_pending = true; pending_outputs_.push(rstd::move(output));
        }
        xc::free(raw); topology_collecting_ = true; topology_invalid_ = false;
    }
    void receive_outputs() {
        if (!topology_collecting_) return;
        bool waiting{};
        for (auto &output : pending_outputs_) {
            if (output.output_pending) {
                void *raw{}; xc::xcb_generic_error_t *error{};
                if (!xc::xcb_poll_for_reply(connection_, output.output_query, &raw, &error)) { waiting = true; continue; }
                output.output_pending = false;
                auto reply = static_cast<rr::xcb_randr_get_output_info_reply_t *>(raw);
                if (error || !reply || reply->status != rr::XCB_RANDR_SET_CONFIG_SUCCESS) topology_invalid_ = true;
                else if (reply->connection == rr::XCB_RANDR_CONNECTION_CONNECTED && reply->crtc) {
                    output.connected = true;
                    output.crtc_query = rr::xcb_randr_get_crtc_info(connection_, reply->crtc, config_timestamp_).sequence;
                    output.crtc_pending = true;
                }
                if (raw) xc::free(raw); if (error) xc::free(error);
            }
            if (output.crtc_pending) {
                void *raw{}; xc::xcb_generic_error_t *error{};
                if (!xc::xcb_poll_for_reply(connection_, output.crtc_query, &raw, &error)) { waiting = true; continue; }
                output.crtc_pending = false;
                auto reply = static_cast<rr::xcb_randr_get_crtc_info_reply_t *>(raw);
                if (error || !reply || reply->status != rr::XCB_RANDR_SET_CONFIG_SUCCESS) topology_invalid_ = true;
                else if (!reply->mode || !reply->width || !reply->height) output.connected = false;
                else {
                    output.info.bounds = Some(Rect{Scalar(reply->x), Scalar(reply->y), Scalar(reply->width), Scalar(reply->height)});
                    for (const auto &mode : modes_) if (mode.id == reply->mode) {
                        if (mode.width && mode.height) output.info.pixel_size = Some(mode_size(mode, reply->rotation));
                        output.info.refresh_hz = refresh(mode); break;
                    }
                }
                if (raw) xc::free(raw); if (error) xc::free(error);
            }
            waiting |= output.output_pending || output.crtc_pending;
        }
        if (waiting) return;
        topology_collecting_ = false; topology_complete_ = true;
    }
    void finish_topology() {
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
    void start_geometry() {
        if (window_geometry_.dirty && !window_batch_ && !window_geometry_.active && !window_position_.active) {
            window_geometry_.sequence = xc::xcb_get_geometry(connection_, window_).sequence;
            window_position_.sequence = xc::xcb_translate_coordinates(connection_, window_, root_, 0, 0).sequence;
            window_geometry_.active = window_position_.active = true;
            window_batch_ = true;
            window_geometry_.dirty = false; pending_window_size_ = None(); pending_window_position_ = None();
        }
        if (!randr_ && root_geometry_.dirty && !root_batch_ && !root_geometry_.active) {
            root_geometry_.sequence = xc::xcb_get_geometry(connection_, root_).sequence;
            root_geometry_.active = true; root_geometry_.dirty = false; root_batch_ = true; pending_root_size_ = None();
        }
    }
    void receive_geometry() {
        if (window_geometry_.active) {
            void *raw{}; xc::xcb_generic_error_t *error{};
            if (xc::xcb_poll_for_reply(connection_, window_geometry_.sequence, &raw, &error)) {
                window_geometry_.active = false; auto reply = static_cast<xc::xcb_get_geometry_reply_t *>(raw);
                if (!error && reply && reply->width && reply->height) pending_window_size_ = Some(Size{Scalar(reply->width), Scalar(reply->height)});
                if (raw) xc::free(raw); if (error) xc::free(error);
            }
        }
        if (window_position_.active) {
            void *raw{}; xc::xcb_generic_error_t *error{};
            if (xc::xcb_poll_for_reply(connection_, window_position_.sequence, &raw, &error)) {
                window_position_.active = false; auto reply = static_cast<xc::xcb_translate_coordinates_reply_t *>(raw);
                if (!error && reply && reply->same_screen) pending_window_position_ = Some(Point{Scalar(reply->dst_x), Scalar(reply->dst_y)});
                if (raw) xc::free(raw); if (error) xc::free(error);
            }
        }
        if (root_geometry_.active) {
            void *raw{}; xc::xcb_generic_error_t *error{};
            if (xc::xcb_poll_for_reply(connection_, root_geometry_.sequence, &raw, &error)) {
                root_geometry_.active = false; auto reply = static_cast<xc::xcb_get_geometry_reply_t *>(raw);
                if (!error && reply && reply->width && reply->height) pending_root_size_ = Some(Size{Scalar(reply->width), Scalar(reply->height)});
                if (raw) xc::free(raw); if (error) xc::free(error);
            }
        }
    }
    void finish_geometry() {
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
    void start_workarea() {
        if (!desktop_.dirty || workspace_batch_ || desktop_.active || workarea_.active) return;
        desktop_.sequence = xc::xcb_get_property(connection_, 0, root_, desktop_atom_, xc::XCB_ATOM_CARDINAL, 0, 1).sequence;
        workarea_.sequence = xc::xcb_get_property(connection_, 0, root_, workarea_atom_, xc::XCB_ATOM_CARDINAL, 0, 4096).sequence;
        desktop_.active = workarea_.active = true; desktop_.dirty = false;
        workspace_batch_ = true;
        pending_desktop_ = None(); pending_workarea_ = None();
    }
    void receive_property(Query &query, Option<Vec<rstd::uint32_t>> &result) {
        if (!query.active) return;
        void *raw{}; xc::xcb_generic_error_t *error{};
        if (!xc::xcb_poll_for_reply(connection_, query.sequence, &raw, &error)) return;
        query.active = false; auto reply = static_cast<xc::xcb_get_property_reply_t *>(raw);
        if (!error && reply && reply->type == xc::XCB_ATOM_CARDINAL && reply->format == 32 && !reply->bytes_after) {
            auto length = xc::xcb_get_property_value_length(reply);
            auto data = static_cast<const rstd::uint32_t *>(xc::xcb_get_property_value(reply));
            if (length >= 0 && length % 4 == 0 && (!length || data)) {
                Vec<rstd::uint32_t> values; for (int i{}; i < length / 4; ++i) values.push(rstd::uint32_t(data[i]));
                result = Some(rstd::move(values));
            }
        }
        if (raw) xc::free(raw); if (error) xc::free(error);
    }
    void receive_workarea() {
        receive_property(desktop_, pending_desktop_); receive_property(workarea_, pending_workarea_);
    }
    void finish_workarea() {
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
    void event(const xc::xcb_generic_event_t &event) {
        auto type = event.response_type & 0x7f;
        if (!type) { failed_ = true; return; }
        if (randr_ && (type == randr_event_ || type == randr_event_ + 1)) { topology_dirty_ = true; retries_ = 0; }
        if (type == xc::property_notify) {
            auto &property = *reinterpret_cast<const xc::xcb_property_notify_event_t *>(&event);
            if (property.window == root_ && (property.atom == desktop_atom_ || property.atom == workarea_atom_)) desktop_.dirty = true;
        }
        if (type == xc::configure_notify || type == xc::reparent_notify) {
            window_geometry_.dirty = true;
            if (!randr_) root_geometry_.dirty = true;
        }
    }
    void drain_events(unsigned &count) {
        while (count < 32) {
            auto item = deferred_ ? rstd::exchange(deferred_, static_cast<xc::xcb_generic_event_t *>(nullptr)) : xc::xcb_poll_for_event(connection_);
            if (!item) break;
            event(*item); xc::free(item); ++count;
        }
        if (count == 32 && !deferred_) deferred_ = xc::xcb_poll_for_event(connection_);
    }
  public:
    X11Displays() = default;
    X11Displays(const X11Displays &) = delete;
    auto operator=(const X11Displays &) -> X11Displays & = delete;
    ~X11Displays() { close(); }
    auto open(const char *display, xc::xcb_window_t root, xc::xcb_window_t window, xc::xcb_atom_t desktop, xc::xcb_atom_t workarea) -> bool {
        if (connection_ || !root || !window || !desktop || !workarea) return false;
        int screen{}; connection_ = xc::xcb_connect(display, &screen);
        if (!connection_ || xc::xcb_connection_has_error(connection_)) { close(); return false; }
        root_ = root; window_ = window; desktop_atom_ = desktop; workarea_atom_ = workarea;
        auto extension = xc::xcb_get_extension_data(connection_, &rr::xcb_randr_id);
        if (extension && extension->present) {
            auto cookie = rr::xcb_randr_query_version(connection_, 1, 5); xc::xcb_generic_error_t *error{};
            auto version = rr::xcb_randr_query_version_reply(connection_, cookie, &error);
            if (!error && version && (version->major_version > 1 || (version->major_version == 1 && version->minor_version >= 2))) {
                randr_ = true; randr_event_ = extension->first_event;
                current_resources_ = version->major_version > 1 || version->minor_version >= 3;
                rr::xcb_randr_select_input(connection_, root_, rr::XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                    rr::XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE | rr::XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
            }
            if (version) xc::free(version); if (error) xc::free(error);
        }
        rstd::uint32_t mask = xc::XCB_EVENT_MASK_PROPERTY_CHANGE | xc::XCB_EVENT_MASK_STRUCTURE_NOTIFY;
        xc::xcb_change_window_attributes(connection_, root_, xc::XCB_CW_EVENT_MASK, &mask);
        mask = xc::XCB_EVENT_MASK_STRUCTURE_NOTIFY;
        xc::xcb_change_window_attributes(connection_, window_, xc::XCB_CW_EVENT_MASK, &mask);
        start_topology(); start_geometry(); start_workarea(); xc::xcb_flush(connection_); return true;
    }
    void dispatch() {
        if (!connection_) return;
        unsigned count{}; drain_events(count);
        receive_resources(); receive_outputs(); receive_geometry(); receive_workarea();
        drain_events(count);
        if (!deferred_) { finish_topology(); finish_geometry(); finish_workarea(); }
        start_topology(); start_geometry(); start_workarea(); xc::xcb_flush(connection_);
        if (xc::xcb_connection_has_error(connection_)) failed_ = true;
    }
    void window_changed() { window_geometry_.dirty = true; }
    auto fd() const -> int { return connection_ ? xc::xcb_get_file_descriptor(connection_) : -1; }
    auto pending() const -> bool { return deferred_ != nullptr; }
    auto failed() const -> bool { return failed_; }
    auto revision() const -> Serial { return revision_; }
    auto snapshot() const -> DisplaySnapshot {
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
    void close() {
        if (deferred_) xc::free(deferred_); if (connection_) xc::xcb_disconnect(connection_);
        deferred_ = nullptr; connection_ = nullptr; root_ = window_ = 0;
        randr_ = current_resources_ = failed_ = topology_ready_ = resource_active_ = topology_collecting_ = topology_invalid_ = false;
        window_batch_ = root_batch_ = workspace_batch_ = topology_complete_ = false;
        topology_dirty_ = true; retries_ = 0; outputs_.clear(); pending_outputs_.clear(); modes_.clear();
        window_geometry_ = {}; window_position_ = {}; root_geometry_ = {}; desktop_ = {}; workarea_ = {};
        window_rect_ = None(); workspace_ = None(); pending_window_size_ = None(); pending_root_size_ = None(); pending_window_position_ = None();
        pending_desktop_ = None(); pending_workarea_ = None(); revision_ = 1; fallback_id_ = new_session();
    }
};
}
