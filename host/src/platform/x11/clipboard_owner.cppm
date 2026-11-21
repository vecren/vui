module vui.host:platform.x11.clipboard_owner;
import :platform.x11.ffi.xcb;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;

class X11ClipboardOwner {
    static constexpr Index limit = 1'048'576, chunk_size = 32'768;
    static constexpr Serial timeout_ns = 5'000'000'000;
    struct Transfer {
        ffi::xcb_window_t window{};
        ffi::xcb_atom_t property{}, target{};
        Vec<u8> data;
        usize offset{};
        Serial deadline{};
    };
    Option<ffi::XcbDispatch> xcb_;
    ffi::xcb_connection_t *connection_{};
    ffi::xcb_generic_event_t *deferred_{};
    ffi::xcb_window_t window_{};
    ffi::xcb_atom_t clipboard_{}, utf8_{}, targets_{}, multiple_{}, timestamp_{}, atom_pair_{}, incr_{}, clock_{};
    ffi::xcb_timestamp_t server_time_{}, ownership_time_{};
    rstd::uint32_t ownership_sequence_{}, clock_sequence_{};
    bool owned_{};
    String text_;
    Vec<Box<Transfer>> transfers_;

    static auto now() -> Serial {
        auto value = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
        return value > ~Serial{} ? ~Serial{} : Serial(value);
    }
    auto atom(const char *name) -> ffi::xcb_atom_t;
    auto valid_text(ref<str> text) const -> Option<ClipboardError>;
    auto latin() const -> bool;
    auto bytes(ffi::xcb_atom_t target) const -> Vec<u8>;
    void change(ffi::xcb_window_t window, ffi::xcb_atom_t property, ffi::xcb_atom_t type,
                rstd::uint8_t format, rstd::uint32_t length, const void *data) {
        xcb_->xcb_change_property(connection_, ffi::XCB_PROP_MODE_REPLACE, window, property, type, format, length, data);
    }
    auto convert(ffi::xcb_window_t window, ffi::xcb_atom_t target, ffi::xcb_atom_t property) -> bool;
    auto convert_multiple(ffi::xcb_window_t window, ffi::xcb_atom_t property) -> bool;
    void selection(const ffi::xcb_selection_request_event_t &event);
    void erase_transfer(usize index);
    void send_next(usize index);
    void process(const ffi::xcb_generic_event_t &generic);
    auto server_timestamp() -> bool;
  public:
    X11ClipboardOwner() = default;
    X11ClipboardOwner(const X11ClipboardOwner &) = delete;
    auto operator=(const X11ClipboardOwner &) -> X11ClipboardOwner & = delete;
    ~X11ClipboardOwner() { close(); }
    auto open(const char *display) -> bool;
    auto write(ref<str> value) -> Result<empty, ClipboardError>;
    void dispatch();
    void expire();
    auto wait(Index milliseconds) const -> Index;
    auto fd() const -> int { return connection_ ? xcb_->xcb_get_file_descriptor(connection_) : -1; }
    auto pending() const -> bool { return deferred_ != nullptr; }
    auto failed() const -> bool { return connection_ && xcb_->xcb_connection_has_error(connection_); }
    void close();
};
}
