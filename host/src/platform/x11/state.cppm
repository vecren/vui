module vui.host:platform.x11.state;
import :ffi.xcb;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;

class X11State {
    friend struct X11StateTest;
    struct Query { unsigned sequence{}; bool active{}, dirty{true}, ready{}; Option<bool> value; };
    ffi::xcb_connection_t *connection_{};
    ffi::xcb_generic_event_t *deferred_{};
    ffi::xcb_window_t root_{}, window_{};
    ffi::xcb_atom_t supported_{}, state_{}, fullscreen_{}, wm_state_{};
    Query supported_query_, state_query_, minimized_query_;
    Option<bool> fullscreen_supported_, fullscreen_active_, minimized_;
    bool changed_{}, failed_{};

    void query(Query &query, ffi::xcb_window_t window, ffi::xcb_atom_t property);
    void receive(Query &query, bool supported);
    void query_minimized();
    void receive_minimized();
    void finish(Query &query, Option<bool> &destination);
    void event(const ffi::xcb_generic_event_t &event);
    void drain_events(unsigned &count);
  public:
    X11State() = default;
    X11State(const X11State &) = delete;
    auto operator=(const X11State &) -> X11State & = delete;
    ~X11State() { close(); }
    auto open(const char *display, ffi::xcb_window_t root, ffi::xcb_window_t window,
              ffi::xcb_atom_t supported, ffi::xcb_atom_t state, ffi::xcb_atom_t fullscreen, ffi::xcb_atom_t wm_state) -> bool;
    void dispatch();
    auto supported() const -> Option<bool> { return fullscreen_supported_; }
    auto active() const -> Option<bool> { return fullscreen_active_; }
    auto minimized() const -> Option<bool> { return minimized_; }
    auto fd() const -> int { return connection_ ? ffi::xcb_get_file_descriptor(connection_) : -1; }
    auto pending() const -> bool { return deferred_ != nullptr; }
    auto failed() const -> bool { return failed_; }
    auto take_changed() -> bool { return rstd::exchange(changed_, false); }
    void close();
};
}
