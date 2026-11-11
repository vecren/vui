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
    auto atom(const char *name) -> xc::xcb_atom_t {
        unsigned length{}; while (name[length]) ++length;
        auto cookie = xc::xcb_intern_atom(connection_, 0, length, name);
        xc::xcb_generic_error_t *error{};
        auto reply = xc::xcb_intern_atom_reply(connection_, cookie, &error);
        auto result = reply ? reply->atom : xc::xcb_atom_t{};
        if (reply) xc::free(reply);
        if (error) xc::free(error);
        return result;
    }
    auto valid_text(ref<str> text) const -> Option<ClipboardError> {
        if (text->len() > usize(limit)) return Some(ClipboardError::Limit);
        if (rstd::str_::from_utf8(text->as_bytes()).is_err()) return Some(ClipboardError::InvalidText);
        auto chars = text->chars();
        for (auto cp = chars.next(); cp.is_some(); cp = chars.next())
            if (!cp->to_primitive()) return Some(ClipboardError::InvalidText);
        return None();
    }
    auto latin() const -> bool {
        auto chars = text_.as_str()->chars();
        for (auto cp = chars.next(); cp.is_some(); cp = chars.next()) if (cp->to_primitive() > 255) return false;
        return true;
    }
    auto bytes(xc::xcb_atom_t target) const -> Vec<u8> {
        if (target == utf8_) return Vec<u8>::from(text_.as_str()->as_bytes());
        Vec<u8> result;
        auto chars = text_.as_str()->chars();
        for (auto cp = chars.next(); cp.is_some(); cp = chars.next()) result.push(u8(cp->to_primitive()));
        return result;
    }
    void change(xc::xcb_window_t window, xc::xcb_atom_t property, xc::xcb_atom_t type,
                rstd::uint8_t format, rstd::uint32_t length, const void *data) {
        xc::xcb_change_property(connection_, xc::XCB_PROP_MODE_REPLACE, window, property, type, format, length, data);
    }
    auto convert(xc::xcb_window_t window, xc::xcb_atom_t target, xc::xcb_atom_t property) -> bool {
        if (!property) return false;
        if (target == targets_) {
            xc::xcb_atom_t values[]{targets_, multiple_, timestamp_, utf8_, xc::XCB_ATOM_STRING};
            change(window, property, xc::XCB_ATOM_ATOM, 32, latin() ? 5 : 4, values);
            return true;
        }
        if (target == timestamp_) {
            auto value = ownership_time_;
            change(window, property, xc::XCB_ATOM_INTEGER, 32, 1, &value);
            return true;
        }
        if (target != utf8_ && (target != xc::XCB_ATOM_STRING || !latin())) return false;
        auto data = bytes(target);
        auto size = data.len().to_primitive();
        if (size <= chunk_size) {
            change(window, property, target, 8, size, data.as_slice().as_raw_ptr());
            return true;
        }
        if (transfers_.len() >= usize(32)) return false;
        for (const auto &transfer : transfers_)
            if (transfer->window == window && transfer->property == property) return false;
        rstd::uint32_t mask = xc::XCB_EVENT_MASK_PROPERTY_CHANGE | xc::XCB_EVENT_MASK_STRUCTURE_NOTIFY;
        xc::xcb_change_window_attributes(connection_, window, xc::XCB_CW_EVENT_MASK, &mask);
        auto transfer = Box<Transfer>::make();
        transfer->window = window; transfer->property = property; transfer->target = target;
        transfer->data = rstd::move(data);
        auto time = now(); transfer->deadline = time > ~Serial{} - timeout_ns ? ~Serial{} : time + timeout_ns;
        transfers_.push(rstd::move(transfer));
        auto total = rstd::uint32_t(size);
        change(window, property, incr_, 32, 1, &total);
        return true;
    }
    auto convert_multiple(xc::xcb_window_t window, xc::xcb_atom_t property) -> bool {
        if (!property) return false;
        auto cookie = xc::xcb_get_property(connection_, 0, window, property, atom_pair_, 0, 64);
        xc::xcb_generic_error_t *error{};
        auto reply = xc::xcb_get_property_reply(connection_, cookie, &error);
        if (error) xc::free(error);
        if (!reply) return false;
        auto count = xc::xcb_get_property_value_length(reply);
        if (reply->type != atom_pair_ || reply->format != 32 || reply->bytes_after || count < 0 ||
            count % 8 || count > 256) { xc::free(reply); return false; }
        auto values = static_cast<const xc::xcb_atom_t *>(xc::xcb_get_property_value(reply));
        Vec<xc::xcb_atom_t> pairs;
        for (int i{}; i < count / 4; ++i) pairs.push(xc::xcb_atom_t(values[i]));
        xc::free(reply);
        for (usize i{}; i < pairs.len(); i += usize(2)) {
            auto destination = pairs[i + usize(1)];
            bool valid = destination && destination != property && pairs[i] != multiple_;
            for (usize j{}; valid && j < i; j += usize(2))
                if (pairs[j + usize(1)] == destination) valid = false;
            if (!valid || !convert(window, pairs[i], destination)) pairs[i + usize(1)] = 0;
        }
        change(window, property, atom_pair_, 32, pairs.len().to_primitive(), pairs.as_slice().as_raw_ptr());
        return true;
    }
    void selection(const xc::xcb_selection_request_event_t &event) {
        alignas(xc::xcb_selection_notify_event_t) char raw[32]{};
        auto reply = reinterpret_cast<xc::xcb_selection_notify_event_t *>(raw);
        reply->response_type = xc::selection_notify;
        reply->time = event.time; reply->requestor = event.requestor;
        reply->selection = event.selection; reply->target = event.target;
        auto property = event.property ? event.property : event.target;
        if (owned_ && event.owner == window_ && event.selection == clipboard_ &&
            (event.target == multiple_ ? convert_multiple(event.requestor, property)
                                       : convert(event.requestor, event.target, property))) reply->property = property;
        xc::xcb_send_event(connection_, 0, event.requestor, 0, raw);
        xc::xcb_flush(connection_);
    }
    void erase_transfer(usize index) {
        auto window = transfers_[index]->window;
        transfers_.remove(index);
        for (const auto &transfer : transfers_) if (transfer->window == window) return;
        if (window != window_) {
            rstd::uint32_t mask{};
            xc::xcb_change_window_attributes(connection_, window, xc::XCB_CW_EVENT_MASK, &mask);
        }
    }
    void send_next(usize index) {
        auto &transfer = *transfers_[index];
        auto remaining = transfer.data.len() - transfer.offset;
        auto count = rstd::cmp::min(remaining, usize(chunk_size));
        auto data = transfer.data.as_slice().as_raw_ptr();
        auto offset = transfer.offset.to_primitive();
        change(transfer.window, transfer.property, transfer.target, 8, count.to_primitive(),
               count != usize(0) ? data + offset : nullptr);
        transfer.offset += count;
        if (count == usize(0)) erase_transfer(index);
        xc::xcb_flush(connection_);
    }
    void process(const xc::xcb_generic_event_t &generic) {
        auto type = generic.response_type & 0x7f;
        if (type == xc::selection_request) selection(*reinterpret_cast<const xc::xcb_selection_request_event_t *>(&generic));
        else if (type == xc::selection_clear) {
            auto &event = *reinterpret_cast<const xc::xcb_selection_clear_event_t *>(&generic);
            if (event.owner == window_ && event.selection == clipboard_ &&
                rstd::int32_t(generic.full_sequence - ownership_sequence_) >= 0) owned_ = false;
        } else if (type == xc::property_notify) {
            auto &event = *reinterpret_cast<const xc::xcb_property_notify_event_t *>(&generic);
            if (event.window == window_ && event.atom == clock_ && event.state == xc::XCB_PROPERTY_NEW_VALUE) {
                server_time_ = event.time; clock_sequence_ = generic.full_sequence;
            }
            if (event.state == xc::XCB_PROPERTY_DELETE)
                for (usize i{}; i < transfers_.len(); i += usize(1))
                    if (transfers_[i]->window == event.window && transfers_[i]->property == event.atom) {
                        send_next(i); break;
                    }
        } else if (type == xc::destroy_notify) {
            auto &event = *reinterpret_cast<const xc::xcb_destroy_notify_event_t *>(&generic);
            for (usize i{}; i < transfers_.len();)
                if (transfers_[i]->window == event.window) erase_transfer(i); else i += usize(1);
        }
    }
    auto server_timestamp() -> bool {
        unsigned char value{};
        auto timestamp_request = xc::xcb_change_property(connection_, xc::XCB_PROP_MODE_REPLACE,
            window_, clock_, xc::XCB_ATOM_INTEGER, 8, 1, &value);
        auto cookie = xc::xcb_get_input_focus(connection_);
        xc::xcb_generic_error_t *error{};
        auto reply = xc::xcb_get_input_focus_reply(connection_, cookie, &error);
        auto succeeded = reply != nullptr;
        if (reply) xc::free(reply);
        if (error) xc::free(error);
        if (!succeeded) return false;
        dispatch();
        return server_time_ != 0 && rstd::int32_t(clock_sequence_ - timestamp_request.sequence) >= 0;
    }
  public:
    X11ClipboardOwner() = default;
    X11ClipboardOwner(const X11ClipboardOwner &) = delete;
    auto operator=(const X11ClipboardOwner &) -> X11ClipboardOwner & = delete;
    ~X11ClipboardOwner() { close(); }
    auto open(const char *display) -> bool {
        if (connection_) return false;
        int screen_index{};
        connection_ = xc::xcb_connect(display, &screen_index);
        if (!connection_ || xc::xcb_connection_has_error(connection_)) { close(); return false; }
        auto screens = xc::xcb_setup_roots_iterator(xc::xcb_get_setup(connection_));
        while (screen_index-- > 0 && screens.rem) xc::xcb_screen_next(&screens);
        if (!screens.rem) { close(); return false; }
        window_ = xc::xcb_generate_id(connection_);
        rstd::uint32_t mask = xc::XCB_EVENT_MASK_PROPERTY_CHANGE;
        xc::xcb_create_window(connection_, xc::copy_from_parent, window_, screens.data->root,
            0, 0, 1, 1, 0, xc::XCB_WINDOW_CLASS_INPUT_OUTPUT, xc::copy_from_parent,
            xc::XCB_CW_EVENT_MASK, &mask);
        clipboard_ = atom("CLIPBOARD"); utf8_ = atom("UTF8_STRING"); targets_ = atom("TARGETS");
        multiple_ = atom("MULTIPLE"); timestamp_ = atom("TIMESTAMP"); atom_pair_ = atom("ATOM_PAIR");
        incr_ = atom("INCR"); clock_ = atom("_VUI_CLIPBOARD_TIME");
        if (!clipboard_ || !utf8_ || !targets_ || !multiple_ || !timestamp_ || !atom_pair_ || !incr_ || !clock_) {
            close(); return false;
        }
        xc::xcb_flush(connection_);
        return true;
    }
    auto write(ref<str> value) -> Result<empty, ClipboardError> {
        if (!connection_ || xc::xcb_connection_has_error(connection_)) return Err(ClipboardError::Unavailable);
        auto error = valid_text(value);
        if (error.is_some()) return Err(*error);
        auto copy = String::make(value);
        if (!server_timestamp()) return Err(ClipboardError::Failed);
        auto timestamp = server_time_;
        auto ownership = xc::xcb_set_selection_owner(connection_, window_, clipboard_, timestamp);
        auto cookie = xc::xcb_get_selection_owner(connection_, clipboard_);
        xc::xcb_generic_error_t *failure{};
        auto reply = xc::xcb_get_selection_owner_reply(connection_, cookie, &failure);
        auto established = reply && reply->owner == window_;
        if (reply) xc::free(reply);
        if (failure) xc::free(failure);
        if (!established) return Err(ClipboardError::Failed);
        text_ = rstd::move(copy); ownership_time_ = timestamp; ownership_sequence_ = ownership.sequence; owned_ = true;
        return Ok(empty{});
    }
    void dispatch() {
        if (!connection_) return;
        unsigned handled{};
        while (handled < 32) {
            auto event = deferred_ ? rstd::exchange(deferred_, static_cast<xc::xcb_generic_event_t *>(nullptr))
                                   : xc::xcb_poll_for_event(connection_);
            if (!event) break;
            process(*event); xc::free(event);
            ++handled;
        }
        if (handled == 32) deferred_ = xc::xcb_poll_for_event(connection_);
    }
    void expire() {
        auto time = now();
        for (usize i{}; i < transfers_.len();)
            if (transfers_[i]->deadline <= time) erase_transfer(i); else i += usize(1);
        if (connection_) xc::xcb_flush(connection_);
    }
    auto wait(Index milliseconds) const -> Index {
        auto time = now();
        for (const auto &transfer : transfers_) {
            if (transfer->deadline <= time) return 0;
            auto delta = transfer->deadline - time;
            auto delay = delta / 1'000'000 + (delta % 1'000'000 != 0);
            if (delay < milliseconds) milliseconds = Index(delay);
        }
        return milliseconds;
    }
    auto fd() const -> int { return connection_ ? xc::xcb_get_file_descriptor(connection_) : -1; }
    auto pending() const -> bool { return deferred_ != nullptr; }
    auto failed() const -> bool { return connection_ && xc::xcb_connection_has_error(connection_); }
    void close() {
        transfers_.clear();
        if (deferred_) xc::free(deferred_);
        deferred_ = nullptr;
        if (connection_) xc::xcb_disconnect(connection_);
        connection_ = nullptr; window_ = 0; owned_ = false; text_ = String{};
    }
};
}
