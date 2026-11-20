module vui.host:platform.x11.displays;
import :ffi.xcb;
import :ffi.xcb_randr;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

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
    Option<ffi::XcbDispatch> xcb_;
    Option<ffi::RandrDispatch> randr_api_;
    ffi::xcb_connection_t *connection_{};
    ffi::xcb_generic_event_t *deferred_{};
    ffi::xcb_window_t root_{}, window_{};
    ffi::xcb_atom_t desktop_atom_{}, workarea_atom_{};
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

    static auto intersect(Rect a, Rect b) -> Option<Rect>;
    static auto refresh(const Mode &mode) -> Option<Scalar>;
    static auto mode_size(const Mode &mode, rstd::uint16_t rotation) -> PixelSize {
        bool rotated = rotation & (ffi::XCB_RANDR_ROTATION_ROTATE_90 | ffi::XCB_RANDR_ROTATION_ROTATE_270);
        return {rotated ? mode.height : mode.width, rotated ? mode.width : mode.height};
    }
    auto id(rstd::uint32_t native) const -> Serial {
        for (const auto &output : outputs_) if (output.native == native) return output.info.id;
        return new_session();
    }
    void start_topology();
    void receive_resources();
    void receive_outputs();
    void finish_topology();
    void start_geometry();
    void receive_geometry();
    void finish_geometry();
    void start_workarea();
    void receive_property(Query &query, Option<Vec<rstd::uint32_t>> &result);
    void receive_workarea() {
        receive_property(desktop_, pending_desktop_); receive_property(workarea_, pending_workarea_);
    }
    void finish_workarea();
    void event(const ffi::xcb_generic_event_t &event);
    void drain_events(unsigned &count);
  public:
    X11Displays() = default;
    X11Displays(const X11Displays &) = delete;
    auto operator=(const X11Displays &) -> X11Displays & = delete;
    ~X11Displays() { close(); }
    auto open(const char *display, ffi::xcb_window_t root, ffi::xcb_window_t window, ffi::xcb_atom_t desktop, ffi::xcb_atom_t workarea) -> bool;
    void dispatch();
    void window_changed() { window_geometry_.dirty = true; }
    auto fd() const -> int { return connection_ ? xcb_->xcb_get_file_descriptor(connection_) : -1; }
    auto pending() const -> bool { return deferred_ != nullptr; }
    auto failed() const -> bool { return failed_; }
    auto revision() const -> Serial { return revision_; }
    auto snapshot() const -> DisplaySnapshot;
    void close();
};
}
