module vui.host:platform.x11_state;
import :ffi.xcb;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
namespace xc = ffi::xcb;

class X11State {
    friend struct X11StateTest;
    struct Query { unsigned sequence{}; bool active{}, dirty{true}, ready{}; Option<bool> value; };
    xc::xcb_connection_t *connection_{};
    xc::xcb_generic_event_t *deferred_{};
    xc::xcb_window_t root_{}, window_{};
    xc::xcb_atom_t supported_{}, state_{}, fullscreen_{}, wm_state_{};
    Query supported_query_, state_query_, minimized_query_;
    Option<bool> fullscreen_supported_, fullscreen_active_, minimized_;
    bool changed_{}, failed_{};

    void query(Query &query, xc::xcb_window_t window, xc::xcb_atom_t property) {
        if (!query.dirty || query.active || query.ready || !connection_) return;
        auto cookie = xc::xcb_get_property(connection_, 0, window, property, xc::XCB_ATOM_ATOM, 0, 1024);
        query.sequence = cookie.sequence; query.active = true; query.dirty = false;
        xc::xcb_flush(connection_);
    }
    void receive(Query &query, bool supported) {
        if (!query.active || !connection_) return;
        void *raw{}; xc::xcb_generic_error_t *error{};
        if (!xc::xcb_poll_for_reply(connection_, query.sequence, &raw, &error)) return;
        query.active = false;
        Option<bool> result;
        auto reply = static_cast<xc::xcb_get_property_reply_t *>(raw);
        if (!error && reply && !reply->bytes_after &&
            ((reply->type == xc::none && reply->format == 0) ||
             (reply->type == xc::XCB_ATOM_ATOM && reply->format == 32))) {
            bool found_state{}, found_fullscreen{};
            auto length = xc::xcb_get_property_value_length(reply);
            auto values = static_cast<const xc::xcb_atom_t *>(xc::xcb_get_property_value(reply));
            if (length >= 0 && length % 4 == 0 && (!length || values)) {
                for (int i{}; i < length / 4; ++i) {
                    found_state |= values[i] == state_; found_fullscreen |= values[i] == fullscreen_;
                }
                result = Some(supported ? found_state && found_fullscreen : found_fullscreen);
            }
        }
        if (raw) xc::free(raw);
        if (error) xc::free(error);
        query.value = result; query.ready = true;
    }
    void query_minimized() {
        if (!minimized_query_.dirty || minimized_query_.active || minimized_query_.ready || !connection_) return;
        auto cookie = xc::xcb_get_property(connection_, 0, window_, wm_state_, wm_state_, 0, 2);
        minimized_query_.sequence = cookie.sequence; minimized_query_.active = true; minimized_query_.dirty = false;
        xc::xcb_flush(connection_);
    }
    void receive_minimized() {
        if (!minimized_query_.active || !connection_) return;
        void *raw{}; xc::xcb_generic_error_t *error{};
        if (!xc::xcb_poll_for_reply(connection_, minimized_query_.sequence, &raw, &error)) return;
        minimized_query_.active = false; Option<bool> result;
        auto reply = static_cast<xc::xcb_get_property_reply_t *>(raw);
        if (!error && reply && reply->type == wm_state_ && reply->format == 32 && !reply->bytes_after &&
            xc::xcb_get_property_value_length(reply) == 8) {
            auto values = static_cast<const rstd::uint32_t *>(xc::xcb_get_property_value(reply));
            if (values && (values[0] == 1 || values[0] == 3)) result = Some(values[0] == 3);
        }
        if (raw) xc::free(raw);
        if (error) xc::free(error);
        minimized_query_.value = result; minimized_query_.ready = true;
    }
    void finish(Query &query, Option<bool> &destination) {
        if (!query.ready) return;
        if (!query.dirty && destination != query.value) { destination = query.value; changed_ = true; }
        query.value = None(); query.ready = false;
    }
    void event(const xc::xcb_generic_event_t &event) {
        auto type = event.response_type & 0x7f;
        if (!type) { failed_ = true; return; }
        if (type != xc::property_notify) return;
        auto &property = *reinterpret_cast<const xc::xcb_property_notify_event_t *>(&event);
        if (property.window == root_ && property.atom == supported_) supported_query_.dirty = true;
        if (property.window == window_ && property.atom == state_) state_query_.dirty = true;
        if (property.window == window_ && property.atom == wm_state_) minimized_query_.dirty = true;
    }
    void drain_events(unsigned &count) {
        while (count < 32) {
            auto item = deferred_ ? rstd::exchange(deferred_, static_cast<xc::xcb_generic_event_t *>(nullptr))
                                  : xc::xcb_poll_for_event(connection_);
            if (!item) break;
            event(*item); xc::free(item); ++count;
        }
        if (count == 32 && !deferred_) deferred_ = xc::xcb_poll_for_event(connection_);
    }
  public:
    X11State() = default;
    X11State(const X11State &) = delete;
    auto operator=(const X11State &) -> X11State & = delete;
    ~X11State() { close(); }
    auto open(const char *display, xc::xcb_window_t root, xc::xcb_window_t window,
              xc::xcb_atom_t supported, xc::xcb_atom_t state, xc::xcb_atom_t fullscreen, xc::xcb_atom_t wm_state) -> bool {
        if (connection_ || !root || !window || !supported || !state || !fullscreen || !wm_state) return false;
        int screen{}; connection_ = xc::xcb_connect(display, &screen);
        if (!connection_ || xc::xcb_connection_has_error(connection_)) { close(); return false; }
        root_ = root; window_ = window; supported_ = supported; state_ = state; fullscreen_ = fullscreen; wm_state_ = wm_state;
        rstd::uint32_t mask = xc::XCB_EVENT_MASK_PROPERTY_CHANGE;
        xc::xcb_change_window_attributes(connection_, root_, xc::XCB_CW_EVENT_MASK, &mask);
        xc::xcb_change_window_attributes(connection_, window_, xc::XCB_CW_EVENT_MASK, &mask);
        query(supported_query_, root_, supported_); query(state_query_, window_, state_);
        query_minimized();
        return true;
    }
    void dispatch() {
        if (!connection_) return;
        unsigned count{}; drain_events(count);
        receive(supported_query_, true); receive(state_query_, false); receive_minimized();
        drain_events(count);
        if (!deferred_) {
            finish(supported_query_, fullscreen_supported_);
            finish(state_query_, fullscreen_active_);
            finish(minimized_query_, minimized_);
        }
        query(supported_query_, root_, supported_); query(state_query_, window_, state_);
        query_minimized();
        if (xc::xcb_connection_has_error(connection_)) failed_ = true;
        if (failed_) {
            if (fullscreen_supported_.is_some() || fullscreen_active_.is_some() || minimized_.is_some()) changed_ = true;
            fullscreen_supported_ = None(); fullscreen_active_ = None(); minimized_ = None();
        }
    }
    auto supported() const -> Option<bool> { return fullscreen_supported_; }
    auto active() const -> Option<bool> { return fullscreen_active_; }
    auto minimized() const -> Option<bool> { return minimized_; }
    auto fd() const -> int { return connection_ ? xc::xcb_get_file_descriptor(connection_) : -1; }
    auto pending() const -> bool { return deferred_ != nullptr; }
    auto failed() const -> bool { return failed_; }
    auto take_changed() -> bool { return rstd::exchange(changed_, false); }
    void close() {
        if (deferred_) xc::free(deferred_);
        if (connection_) xc::xcb_disconnect(connection_);
        deferred_ = nullptr; connection_ = nullptr; root_ = window_ = 0;
        supported_query_ = {}; state_query_ = {}; minimized_query_ = {}; fullscreen_supported_ = None(); fullscreen_active_ = None(); minimized_ = None();
        changed_ = failed_ = false;
    }
};
}
