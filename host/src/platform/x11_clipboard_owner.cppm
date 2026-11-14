module vui.host:platform.x11_clipboard_owner;
import :ffi.xcb;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
namespace xc = ffi::xcb;

class X11ClipboardOwner {
    static constexpr Index limit = 1'048'576, chunk_size = 32'768;
    static constexpr Serial timeout_ns = 5'000'000'000;
    struct Transfer {
        xc::xcb_window_t window{};
        xc::xcb_atom_t property{}, target{};
        Vec<u8> data;
        usize offset{};
        Serial deadline{};
    };
    xc::xcb_connection_t *connection_{};
    xc::xcb_generic_event_t *deferred_{};
    xc::xcb_window_t window_{};
    xc::xcb_atom_t clipboard_{}, utf8_{}, targets_{}, multiple_{}, timestamp_{}, atom_pair_{}, incr_{}, clock_{};
    xc::xcb_timestamp_t server_time_{}, ownership_time_{};
    rstd::uint32_t ownership_sequence_{}, clock_sequence_{};
    bool owned_{};
    String text_;
    Vec<Box<Transfer>> transfers_;

    static auto now() -> Serial {
        auto value = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
        return value > ~Serial{} ? ~Serial{} : Serial(value);
    }
    auto atom(const char *name) -> xc::xcb_atom_t;
    auto valid_text(ref<str> text) const -> Option<ClipboardError>;
    auto latin() const -> bool;
    auto bytes(xc::xcb_atom_t target) const -> Vec<u8>;
    void change(xc::xcb_window_t window, xc::xcb_atom_t property, xc::xcb_atom_t type,
                rstd::uint8_t format, rstd::uint32_t length, const void *data) {
        xc::xcb_change_property(connection_, xc::XCB_PROP_MODE_REPLACE, window, property, type, format, length, data);
    }
    auto convert(xc::xcb_window_t window, xc::xcb_atom_t target, xc::xcb_atom_t property) -> bool;
    auto convert_multiple(xc::xcb_window_t window, xc::xcb_atom_t property) -> bool;
    void selection(const xc::xcb_selection_request_event_t &event);
    void erase_transfer(usize index);
    void send_next(usize index);
    void process(const xc::xcb_generic_event_t &generic);
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
    auto fd() const -> int { return connection_ ? xc::xcb_get_file_descriptor(connection_) : -1; }
    auto pending() const -> bool { return deferred_ != nullptr; }
    auto failed() const -> bool { return connection_ && xc::xcb_connection_has_error(connection_); }
    void close();
};
}
