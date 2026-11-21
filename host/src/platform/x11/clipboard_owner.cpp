module vui.host;
import :platform.x11.clipboard_owner;
import :platform.x11.ffi.xcb;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;

auto X11ClipboardOwner::atom(const char *name) -> ffi::xcb_atom_t {
    unsigned length{}; while (name[length]) ++length;
    auto cookie = xcb_->xcb_intern_atom(connection_, 0, length, name);
    ffi::xcb_generic_error_t *error{};
    auto reply = xcb_->xcb_intern_atom_reply(connection_, cookie, &error);
    auto result = reply ? reply->atom : ffi::xcb_atom_t{};
    if (reply) ffi::free(reply);
    if (error) ffi::free(error);
    return result;
}

auto X11ClipboardOwner::valid_text(ref<str> text) const -> Option<ClipboardError> {
    if (text->len() > usize(limit)) return Some(ClipboardError::Limit);
    if (rstd::str_::from_utf8(text->as_bytes()).is_err()) return Some(ClipboardError::InvalidText);
    auto chars = text->chars();
    for (auto cp = chars.next(); cp.is_some(); cp = chars.next())
        if (!cp->to_primitive()) return Some(ClipboardError::InvalidText);
    return None();
}

auto X11ClipboardOwner::latin() const -> bool {
    auto chars = text_.as_str()->chars();
    for (auto cp = chars.next(); cp.is_some(); cp = chars.next()) if (cp->to_primitive() > 255) return false;
    return true;
}

auto X11ClipboardOwner::bytes(ffi::xcb_atom_t target) const -> Vec<u8> {
    if (target == utf8_) return Vec<u8>::from(text_.as_str()->as_bytes());
    Vec<u8> result;
    auto chars = text_.as_str()->chars();
    for (auto cp = chars.next(); cp.is_some(); cp = chars.next()) result.push(u8(cp->to_primitive()));
    return result;
}

auto X11ClipboardOwner::convert(ffi::xcb_window_t window, ffi::xcb_atom_t target, ffi::xcb_atom_t property) -> bool {
    if (!property) return false;
    if (target == targets_) {
        ffi::xcb_atom_t values[]{targets_, multiple_, timestamp_, utf8_, ffi::XCB_ATOM_STRING};
        change(window, property, ffi::XCB_ATOM_ATOM, 32, latin() ? 5 : 4, values);
        return true;
    }
    if (target == timestamp_) {
        auto value = ownership_time_;
        change(window, property, ffi::XCB_ATOM_INTEGER, 32, 1, &value);
        return true;
    }
    if (target != utf8_ && (target != ffi::XCB_ATOM_STRING || !latin())) return false;
    auto data = bytes(target);
    auto size = data.len().to_primitive();
    if (size <= chunk_size) {
        change(window, property, target, 8, size, data.as_slice().as_raw_ptr());
        return true;
    }
    if (transfers_.len() >= usize(32)) return false;
    for (const auto &transfer : transfers_)
        if (transfer->window == window && transfer->property == property) return false;
    rstd::uint32_t mask = ffi::XCB_EVENT_MASK_PROPERTY_CHANGE | ffi::XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_->xcb_change_window_attributes(connection_, window, ffi::XCB_CW_EVENT_MASK, &mask);
    auto transfer = Box<Transfer>::make();
    transfer->window = window; transfer->property = property; transfer->target = target;
    transfer->data = rstd::move(data);
    auto time = now(); transfer->deadline = time > ~Serial{} - timeout_ns ? ~Serial{} : time + timeout_ns;
    transfers_.push(rstd::move(transfer));
    auto total = rstd::uint32_t(size);
    change(window, property, incr_, 32, 1, &total);
    return true;
}

auto X11ClipboardOwner::convert_multiple(ffi::xcb_window_t window, ffi::xcb_atom_t property) -> bool {
    if (!property) return false;
    auto cookie = xcb_->xcb_get_property(connection_, 0, window, property, atom_pair_, 0, 64);
    ffi::xcb_generic_error_t *error{};
    auto reply = xcb_->xcb_get_property_reply(connection_, cookie, &error);
    if (error) ffi::free(error);
    if (!reply) return false;
    auto count = xcb_->xcb_get_property_value_length(reply);
    if (reply->type != atom_pair_ || reply->format != 32 || reply->bytes_after || count < 0 ||
        count % 8 || count > 256) { ffi::free(reply); return false; }
    auto values = static_cast<const ffi::xcb_atom_t *>(xcb_->xcb_get_property_value(reply));
    Vec<ffi::xcb_atom_t> pairs;
    for (int i{}; i < count / 4; ++i) pairs.push(ffi::xcb_atom_t(values[i]));
    ffi::free(reply);
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

void X11ClipboardOwner::selection(const ffi::xcb_selection_request_event_t &event) {
    alignas(ffi::xcb_selection_notify_event_t) char raw[32]{};
    auto reply = reinterpret_cast<ffi::xcb_selection_notify_event_t *>(raw);
    reply->response_type = ffi::XCB_SELECTION_NOTIFY;
    reply->time = event.time; reply->requestor = event.requestor;
    reply->selection = event.selection; reply->target = event.target;
    auto property = event.property ? event.property : event.target;
    if (owned_ && event.owner == window_ && event.selection == clipboard_ &&
        (event.target == multiple_ ? convert_multiple(event.requestor, property)
                                   : convert(event.requestor, event.target, property))) reply->property = property;
    xcb_->xcb_send_event(connection_, 0, event.requestor, 0, raw);
    xcb_->xcb_flush(connection_);
}

void X11ClipboardOwner::erase_transfer(usize index) {
    auto window = transfers_[index]->window;
    transfers_.remove(index);
    for (const auto &transfer : transfers_) if (transfer->window == window) return;
    if (window != window_) {
        rstd::uint32_t mask{};
        xcb_->xcb_change_window_attributes(connection_, window, ffi::XCB_CW_EVENT_MASK, &mask);
    }
}

void X11ClipboardOwner::send_next(usize index) {
    auto &transfer = *transfers_[index];
    auto remaining = transfer.data.len() - transfer.offset;
    auto count = rstd::cmp::min(remaining, usize(chunk_size));
    auto data = transfer.data.as_slice().as_raw_ptr();
    auto offset = transfer.offset.to_primitive();
    change(transfer.window, transfer.property, transfer.target, 8, count.to_primitive(),
           count != usize(0) ? data + offset : nullptr);
    transfer.offset += count;
    if (count == usize(0)) erase_transfer(index);
    xcb_->xcb_flush(connection_);
}

void X11ClipboardOwner::process(const ffi::xcb_generic_event_t &generic) {
    auto type = generic.response_type & 0x7f;
    if (type == ffi::XCB_SELECTION_REQUEST) selection(*reinterpret_cast<const ffi::xcb_selection_request_event_t *>(&generic));
    else if (type == ffi::XCB_SELECTION_CLEAR) {
        auto &event = *reinterpret_cast<const ffi::xcb_selection_clear_event_t *>(&generic);
        if (event.owner == window_ && event.selection == clipboard_ &&
            rstd::int32_t(generic.full_sequence - ownership_sequence_) >= 0) owned_ = false;
    } else if (type == ffi::XCB_PROPERTY_NOTIFY) {
        auto &event = *reinterpret_cast<const ffi::xcb_property_notify_event_t *>(&generic);
        if (event.window == window_ && event.atom == clock_ && event.state == ffi::XCB_PROPERTY_NEW_VALUE) {
            server_time_ = event.time; clock_sequence_ = generic.full_sequence;
        }
        if (event.state == ffi::XCB_PROPERTY_DELETE)
            for (usize i{}; i < transfers_.len(); i += usize(1))
                if (transfers_[i]->window == event.window && transfers_[i]->property == event.atom) {
                    send_next(i); break;
                }
    } else if (type == ffi::XCB_DESTROY_NOTIFY) {
        auto &event = *reinterpret_cast<const ffi::xcb_destroy_notify_event_t *>(&generic);
        for (usize i{}; i < transfers_.len();)
            if (transfers_[i]->window == event.window) erase_transfer(i); else i += usize(1);
    }
}

auto X11ClipboardOwner::server_timestamp() -> bool {
    unsigned char value{};
    auto timestamp_request = xcb_->xcb_change_property(connection_, ffi::XCB_PROP_MODE_REPLACE,
        window_, clock_, ffi::XCB_ATOM_INTEGER, 8, 1, &value);
    auto cookie = xcb_->xcb_get_input_focus(connection_);
    ffi::xcb_generic_error_t *error{};
    auto reply = xcb_->xcb_get_input_focus_reply(connection_, cookie, &error);
    auto succeeded = reply != nullptr;
    if (reply) ffi::free(reply);
    if (error) ffi::free(error);
    if (!succeeded) return false;
    dispatch();
    return server_time_ != 0 && rstd::int32_t(clock_sequence_ - timestamp_request.sequence) >= 0;
}

auto X11ClipboardOwner::open(const char *display) -> bool {
    if (connection_) return false;
    xcb_ = ffi::XcbDispatch::open();
    if (xcb_.is_none()) return false;
    int screen_index{};
    connection_ = xcb_->xcb_connect(display, &screen_index);
    if (!connection_ || xcb_->xcb_connection_has_error(connection_)) { close(); return false; }
    auto screens = xcb_->xcb_setup_roots_iterator(xcb_->xcb_get_setup(connection_));
    while (screen_index-- > 0 && screens.rem) xcb_->xcb_screen_next(&screens);
    if (!screens.rem) { close(); return false; }
    window_ = xcb_->xcb_generate_id(connection_);
    rstd::uint32_t mask = ffi::XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_->xcb_create_window(connection_, ffi::XCB_COPY_FROM_PARENT, window_, screens.data->root,
        0, 0, 1, 1, 0, ffi::XCB_WINDOW_CLASS_INPUT_OUTPUT, ffi::XCB_COPY_FROM_PARENT,
        ffi::XCB_CW_EVENT_MASK, &mask);
    clipboard_ = atom("CLIPBOARD"); utf8_ = atom("UTF8_STRING"); targets_ = atom("TARGETS");
    multiple_ = atom("MULTIPLE"); timestamp_ = atom("TIMESTAMP"); atom_pair_ = atom("ATOM_PAIR");
    incr_ = atom("INCR"); clock_ = atom("_VUI_CLIPBOARD_TIME");
    if (!clipboard_ || !utf8_ || !targets_ || !multiple_ || !timestamp_ || !atom_pair_ || !incr_ || !clock_) {
        close(); return false;
    }
    xcb_->xcb_flush(connection_);
    return true;
}

auto X11ClipboardOwner::write(ref<str> value) -> Result<empty, ClipboardError> {
    if (!connection_ || xcb_->xcb_connection_has_error(connection_)) return Err(ClipboardError::Unavailable);
    auto error = valid_text(value);
    if (error.is_some()) return Err(*error);
    auto copy = String::make(value);
    if (!server_timestamp()) return Err(ClipboardError::Failed);
    auto timestamp = server_time_;
    auto ownership = xcb_->xcb_set_selection_owner(connection_, window_, clipboard_, timestamp);
    auto cookie = xcb_->xcb_get_selection_owner(connection_, clipboard_);
    ffi::xcb_generic_error_t *failure{};
    auto reply = xcb_->xcb_get_selection_owner_reply(connection_, cookie, &failure);
    auto established = reply && reply->owner == window_;
    if (reply) ffi::free(reply);
    if (failure) ffi::free(failure);
    if (!established) return Err(ClipboardError::Failed);
    text_ = rstd::move(copy); ownership_time_ = timestamp; ownership_sequence_ = ownership.sequence; owned_ = true;
    return Ok(empty{});
}

void X11ClipboardOwner::dispatch() {
    if (!connection_) return;
    unsigned handled{};
    while (handled < 32) {
        auto event = deferred_ ? rstd::exchange(deferred_, static_cast<ffi::xcb_generic_event_t *>(nullptr))
                               : xcb_->xcb_poll_for_event(connection_);
        if (!event) break;
        process(*event); ffi::free(event);
        ++handled;
    }
    if (handled == 32) deferred_ = xcb_->xcb_poll_for_event(connection_);
}

void X11ClipboardOwner::expire() {
    auto time = now();
    for (usize i{}; i < transfers_.len();)
        if (transfers_[i]->deadline <= time) erase_transfer(i); else i += usize(1);
    if (connection_) xcb_->xcb_flush(connection_);
}

auto X11ClipboardOwner::wait(Index milliseconds) const -> Index {
    auto time = now();
    for (const auto &transfer : transfers_) {
        if (transfer->deadline <= time) return 0;
        auto delta = transfer->deadline - time;
        auto delay = delta / 1'000'000 + (delta % 1'000'000 != 0);
        if (delay < milliseconds) milliseconds = Index(delay);
    }
    return milliseconds;
}

void X11ClipboardOwner::close() {
    transfers_.clear();
    if (deferred_) ffi::free(deferred_);
    deferred_ = nullptr;
    if (connection_) xcb_->xcb_disconnect(connection_);
    connection_ = nullptr; window_ = 0; owned_ = false; text_ = String{};
    xcb_ = None();
}
} // namespace vui::host::detail
