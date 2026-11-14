module vui.host;
import :platform.x11_clipboard;
import :ffi.x11;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;
namespace x11 = ffi::x11;

void X11Clipboard::finish(usize index, Option<ClipboardError> error) {
    auto request = requests_.remove(index);
    InputEvent event; event.type = EventType::ClipboardResult; event.clipboard_request = request->id;
    if (error.is_none()) {
        if (request->target == x11::string_atom) {
            String converted;
            for (auto byte : request->bytes) {
                auto value = u8(byte).to_primitive();
                if (!value) { error = Some(ClipboardError::InvalidText); break; }
                converted.push(char32_t(value));
                if (converted.len() > usize(limit)) { error = Some(ClipboardError::Limit); break; }
            }
            if (error.is_none()) event.text = TextInput::create(converted.as_str()).unwrap();
        } else {
            auto decoded = rstd::str_::from_utf8(request->bytes.as_slice());
            if (decoded.is_err()) error = Some(ClipboardError::InvalidText);
            else {
                auto chars = decoded.unwrap()->chars();
                for (auto cp = chars.next(); cp.is_some(); cp = chars.next())
                    if (!cp->to_primitive()) { error = Some(ClipboardError::InvalidText); break; }
                if (error.is_none()) event.text = TextInput::create(decoded.unwrap()).unwrap();
            }
        }
    }
    event.clipboard_error = error;
    x11::XDestroyWindow(display_, request->window);
    emit_(sink_, rstd::move(event));
}

void X11Clipboard::property(usize index, bool initial) {
    auto &request = *requests_[index];
    x11::Atom type{}; int format{}; unsigned long count{}, after{}; unsigned char *data{};
    auto result = x11::XGetWindowProperty(display_, request.window, property_, 0,
        long((limit + 3) / 4 + 1), 1, x11::any_property_type, &type, &format, &count, &after, &data);
    if (result != x11::success) { if (data) x11::XFree(data); finish(index, Some(ClipboardError::Failed)); return; }
    if (initial && type == incr_) {
        bool valid = format == 32 && count <= 1 && !after && (!count || data);
        if (data) x11::XFree(data);
        if (!valid) { finish(index, Some(ClipboardError::Failed)); return; }
        request.incremental = true;
        x11::XFlush(display_);
        return;
    }
    if (type != request.target || format != 8 || (count && !data)) {
        if (data) x11::XFree(data);
        finish(index, Some(ClipboardError::InvalidText));
        return;
    }
    if (after || count > limit || request.bytes.len() > usize(limit - count)) {
        if (data) x11::XFree(data);
        if (!request.incremental) { finish(index, Some(ClipboardError::Limit)); return; }
        request.bytes.clear(); request.overflow = true;
        if (after) x11::XDeleteProperty(display_, request.window, property_);
        return;
    }
    if (request.overflow) {
        if (data) x11::XFree(data);
        if (!count) finish(index, Some(ClipboardError::Limit));
        return;
    }
    for (unsigned long i{}; i < count; ++i) request.bytes.push(u8(data[i]));
    if (data) x11::XFree(data);
    if (!request.incremental || !count) finish(index);
}

void X11Clipboard::open(x11::Display *display, x11::Window root) {
    display_ = display; root_ = root;
    clipboard_ = x11::XInternAtom(display, "CLIPBOARD", 0);
    utf8_ = x11::XInternAtom(display, "UTF8_STRING", 0);
    property_ = x11::XInternAtom(display, "_VUI_CLIPBOARD", 0);
    incr_ = x11::XInternAtom(display, "INCR", 0);
}

auto X11Clipboard::request(Serial id) -> Result<empty, ClipboardError> {
    if (!display_ || !root_) return Err(ClipboardError::Unavailable);
    if (!id) return Err(ClipboardError::Failed);
    if (!clipboard_ || !utf8_ || !property_ || !incr_) return Err(ClipboardError::Unsupported);
    if (requests_.len() >= usize(32)) return Err(ClipboardError::Limit);
    for (const auto &pending : requests_) if (pending->id == id) return Err(ClipboardError::Failed);
    auto window = x11::XCreateSimpleWindow(display_, root_, 0, 0, 1, 1, 0, 0, 0);
    if (!window) return Err(ClipboardError::Failed);
    x11::XSelectInput(display_, window, x11::property_change_mask);
    auto request = Box<Request>::make();
    request->id = id; request->window = window; request->target = utf8_;
    auto time = now(); request->deadline = time > ~Serial{} - timeout_ns ? ~Serial{} : time + timeout_ns;
    convert(*request);
    requests_.push(rstd::move(request));
    return Ok(empty{});
}

auto X11Clipboard::event(const x11::XEvent &event) -> bool {
    if (event.type != x11::selection_notify && event.type != x11::property_notify) return false;
    for (usize i{}; i < requests_.len(); i += usize(1)) {
        auto &request = *requests_[i];
        if (event.xany.window != request.window) continue;
        if (event.type == x11::selection_notify && event.xselection.selection == clipboard_ &&
            event.xselection.target == request.target && !request.incremental) {
            if (!event.xselection.property) {
                if (request.target == utf8_) { request.target = x11::string_atom; convert(request); }
                else finish(i, Some(ClipboardError::Unsupported));
            } else if (event.xselection.property == property_) property(i, true);
            else finish(i, Some(ClipboardError::Failed));
        } else if (event.type == x11::property_notify && request.incremental &&
                   event.xproperty.atom == property_ && event.xproperty.state == x11::property_new_value)
            property(i, false);
        return true;
    }
    return false;
}

void X11Clipboard::expire() {
    auto time = now();
    for (usize i{}; i < requests_.len();)
        if (requests_[i]->deadline <= time)
            finish(i, Some(requests_[i]->overflow ? ClipboardError::Limit : ClipboardError::Unavailable));
        else i += usize(1);
}

auto X11Clipboard::wait(Index milliseconds) const -> Index {
    auto time = now();
    for (const auto &request : requests_) {
        if (request->deadline <= time) return 0;
        auto delta = request->deadline - time;
        auto delay = delta / 1'000'000 + (delta % 1'000'000 != 0);
        if (delay < milliseconds) milliseconds = Index(delay);
    }
    return milliseconds;
}
} // namespace vui::host::detail
