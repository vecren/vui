module vui.host;
import :platform.wayland.clipboard;
import :platform.wayland.ffi;
import :platform.linux.ffi;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

void WaylandClipboard::remove_source(ffi::wl_data_source *native, Option<ClipboardError> error) {
    for (usize i{}; i < sources_.len(); i += usize(1)) if (sources_[i]->native == native) {
        auto entry = sources_.remove(i);
        if (entry->pending) {
            pending_sources_ -= usize(1);
            emit_write_result(entry->id, error.is_some() ? error : Some(ClipboardError::Unavailable));
        }
        ffi::destroy_source(native); return;
    }
}

void WaylandClipboard::remove_offer(ffi::wl_data_offer *native) {
    if (!native) return;
    if (selection_ == native) selection_ = nullptr;
    for (usize i{}; i < offers_.len(); i += usize(1)) if (offers_[i].native == native) {
        ffi::destroy_offer(native); offers_.remove(i); return;
    }
}

void WaylandClipboard::emit_result(Serial id, ReadResult result) {
    InputEvent event; event.type = EventType::ClipboardResult; event.clipboard_request = id;
    event.text = rstd::move(result.text); event.clipboard_error = result.error;
    emit_(sink_, rstd::move(event));
}

auto WaylandClipboard::read_pipe(int fd, int cancel, bool latin) -> ReadResult {
    Vec<u8> bytes; bool overflow{}, discard{};
    auto started = now();
    Option<ClipboardError> error;
    bool done{};
    while (!done) {
        auto current = now();
        if (current - started >= timeout_ns) { error = Some(overflow ? ClipboardError::Limit : ClipboardError::Unavailable); break; }
        auto left = timeout_ns - (current - started);
        auto milliseconds = left / 1'000'000 + (left % 1'000'000 != 0);
        ffi::pollfd fds[]{{fd, ffi::POLLIN, 0}, {cancel, ffi::POLLIN, 0}};
        auto ready = ffi::poll(fds, 2, int(milliseconds));
        if (ready < 0) {
            if (ffi::error_number() == ffi::EINTR) continue;
            error = Some(ClipboardError::Failed); break;
        }
        if (!ready) continue;
        if (fds[1].revents & ffi::POLLIN) {
            rstd::uint64_t command{};
            long count;
            do { count = ffi::read(cancel, &command, sizeof(command)); }
            while (count < 0 && ffi::error_number() == ffi::EINTR);
            if (count != sizeof(command) || (command & 2)) { error = Some(ClipboardError::Unavailable); break; }
            if (command & 1) { bytes.clear(); discard = true; }
        } else if (fds[1].revents) { error = Some(ClipboardError::Failed); break; }
        if (fds[0].revents & ffi::POLLNVAL) { error = Some(ClipboardError::Failed); break; }
        if (!(fds[0].revents & (ffi::POLLIN | ffi::disconnected))) continue;
        unsigned char buffer[4096];
        for (unsigned batch{}; batch < 16; ++batch) {
            auto count = ffi::read(fd, buffer, sizeof(buffer));
            if (count == 0) { done = true; break; }
            if (count < 0) {
                auto status = ffi::error_number();
                if (status == ffi::EINTR) continue;
                if (status == ffi::EAGAIN && !(fds[0].revents & ffi::disconnected)) break;
                error = Some(ClipboardError::Failed); done = true; break;
            }
            if (!discard && !overflow && bytes.len() + usize(count) > usize(limit)) { bytes.clear(); overflow = true; }
            if (!discard && !overflow) for (long j{}; j < count; ++j) bytes.push(u8(buffer[j]));
        }
    }
    ffi::close(fd);
    ReadResult result;
    if (error.is_none() && discard) error = Some(ClipboardError::Unavailable);
    if (error.is_none() && overflow) error = Some(ClipboardError::Limit);
    if (error.is_none() && latin) {
        String converted;
        for (auto byte : bytes) {
            auto value = u8(byte).to_primitive();
            if (!value) { error = Some(ClipboardError::InvalidText); break; }
            converted.push(char32_t(value));
            if (converted.len() > usize(limit)) { error = Some(ClipboardError::Limit); break; }
        }
        if (error.is_none()) result.text = TextInput::create(converted.as_str()).unwrap();
    } else if (error.is_none()) {
        auto text = rstd::str_::from_utf8(bytes.as_slice());
        if (text.is_err()) error = Some(ClipboardError::InvalidText);
        else {
            auto chars = text.unwrap()->chars();
            for (auto cp = chars.next(); cp.is_some(); cp = chars.next())
                if (!cp->to_primitive()) { error = Some(ClipboardError::InvalidText); break; }
            if (error.is_none()) result.text = TextInput::create(text.unwrap()).unwrap();
        }
    }
    result.error = error; return result;
}

auto WaylandClipboard::start(int fd, Serial id, bool latin) -> Result<empty, ClipboardError> {
    if (waker_.is_none()) { ffi::close(fd); return Err(ClipboardError::Unavailable); }
    auto cancel = ffi::eventfd(0, ffi::event_flags);
    if (cancel < 0) { ffi::close(fd); return Err(ClipboardError::Failed); }
    auto state = rstd::sync::Arc<ReadState>::make(cancel);
    auto task = rstd::thread::spawn([fd, latin, shared = state.clone(), wake = FrameWaker(*waker_)]() mutable {
        auto result = read_pipe(fd, shared->cancel_fd, latin);
        {
            auto lock = shared->result.lock().unwrap();
            *lock = Some(rstd::move(result));
        }
        (void)wake.request();
    });
    if (task.is_err()) { ffi::close(fd); return Err(ClipboardError::Failed); }
    reads_.push(Box<Read>::make(id, rstd::move(state), rstd::move(task).unwrap()));
    return Ok(empty{});
}

void WaylandClipboard::cancel(Read &read, rstd::uint64_t value) {
    long written;
    do { written = ffi::write(read.state->cancel_fd, &value, sizeof(value)); }
    while (written < 0 && ffi::error_number() == ffi::EINTR);
}

void WaylandClipboard::cancel_reads() {
    for (auto &read : reads_) {
        if (!read->reported) {
            cancel(*read, 1);
            read->reported = true;
            emit_result(read->id, ReadResult{TextInput{}, Some(ClipboardError::Unavailable)});
        }
    }
}

void WaylandClipboard::write_pipe(int fd, int cancel, Vec<u8> bytes) {
    if (!ffi::block_sigpipe()) { ffi::close(fd); return; }
    auto started = now(); usize offset{};
    while (offset < bytes.len()) {
        auto current = now();
        if (current - started >= timeout_ns) break;
        auto left = timeout_ns - (current - started);
        auto milliseconds = left / 1'000'000 + (left % 1'000'000 != 0);
        ffi::pollfd fds[]{{fd, ffi::POLLOUT, 0}, {cancel, ffi::POLLIN, 0}};
        auto ready = ffi::poll(fds, 2, int(milliseconds));
        if (ready < 0) { if (ffi::error_number() == ffi::EINTR) continue; break; }
        if (!ready) continue;
        if (fds[1].revents || (fds[0].revents & ffi::error)) break;
        if (!(fds[0].revents & ffi::POLLOUT)) continue;
        auto data = bytes.as_slice().as_raw_ptr() + offset.to_primitive();
        auto count = ffi::write(fd, data, (bytes.len() - offset).to_primitive());
        if (count < 0) {
            if (ffi::error_number() == ffi::EINTR || ffi::error_number() == ffi::EAGAIN) continue;
            break;
        }
        if (!count) break;
        offset += usize(count);
    }
    ffi::close(fd);
}

void WaylandClipboard::start_send(int fd, Vec<u8> bytes) {
    drain();
    if (sends_.len() >= usize(32) || waker_.is_none() || !ffi::nonblocking(fd)) { ffi::close(fd); return; }
    auto cancel = ffi::eventfd(0, ffi::event_flags);
    if (cancel < 0) { ffi::close(fd); return; }
    auto state = rstd::sync::Arc<SendState>::make(cancel);
    auto task = rstd::thread::spawn([fd, bytes = rstd::move(bytes), shared = state.clone(), wake = FrameWaker(*waker_)]() mutable {
        write_pipe(fd, shared->cancel_fd, rstd::move(bytes));
        { auto lock = shared->complete.lock().unwrap(); *lock = true; }
        (void)wake.request();
    });
    if (task.is_err()) { ffi::close(fd); return; }
    sends_.push(Box<Send>::make(rstd::move(state), rstd::move(task).unwrap()));
}

void WaylandClipboard::source_send(void *data, ffi::wl_data_source *native, const char *type, rstd::int32_t fd) {
    auto &self = *static_cast<WaylandClipboard *>(data);
    auto entry = self.source(native);
    if (!entry || !type) { ffi::close(fd); return; }
    auto mime = rstd::ffi::CStr::from_ptr(type).to_str();
    if (mime.is_err()) { ffi::close(fd); return; }
    Vec<u8> bytes;
    if (*mime == entry->marker.as_str()) { ffi::close(fd); return; }
    if (*mime == "STRING"_str && entry->latin) {
        auto chars = entry->text.as_str()->chars();
        for (auto cp = chars.next(); cp.is_some(); cp = chars.next()) bytes.push(u8(cp->to_primitive()));
    } else if (*mime == "text/plain;charset=utf-8"_str || *mime == "text/plain"_str ||
               *mime == "UTF8_STRING"_str || *mime == "TEXT"_str)
        bytes = Vec<u8>::from(entry->text.as_str()->as_bytes());
    else { ffi::close(fd); return; }
    self.start_send(fd, rstd::move(bytes));
}

void WaylandClipboard::mime(void *data, ffi::wl_data_offer *native, const char *type) {
    auto &self = *static_cast<WaylandClipboard *>(data);
    auto entry = self.offer(native); if (!entry || !type) return;
    auto value = rstd::ffi::CStr::from_ptr(type).to_str();
    if (value.is_err()) return;
    for (const auto &source : self.sources_) if (*value == source->marker.as_str()) { entry->marker = source->id; break; }
    if (*value == "text/plain;charset=utf-8"_str) entry->utf8 = true;
    else if (*value == "text/plain;charset=UTF-8"_str) entry->upper = true;
    else if (*value == "text/plain"_str) entry->plain = true;
    else if (*value == "UTF8_STRING"_str) entry->utf8_string = true;
    else if (*value == "TEXT"_str) entry->text = true;
    else if (*value == "STRING"_str) entry->latin = true;
}

void WaylandClipboard::data_offer(void *data, ffi::wl_data_device *, ffi::wl_data_offer *native) {
    auto &self = *static_cast<WaylandClipboard *>(data);
    if (!native || self.offers_.len() >= usize(32) || self.offer(native)) { self.failed_ = true; return; }
    self.offers_.push({native});
    if (ffi::offer_listener(native, &self.offer_listener_, &self)) self.failed_ = true;
}

void WaylandClipboard::selection(void *data, ffi::wl_data_device *, ffi::wl_data_offer *native) {
    auto &self = *static_cast<WaylandClipboard *>(data);
    if (self.selection_ && self.selection_ != native) self.remove_offer(self.selection_);
    self.selection_ = native && self.offer(native) ? native : nullptr;
    auto entry = self.offer(self.selection_);
    if (entry) for (auto &source : self.sources_) if (source->pending && source->id == entry->marker) {
        source->pending = false; self.pending_sources_ -= usize(1); self.emit_write_result(source->id); break;
    }
}

void WaylandClipboard::create_device() {
    if (device_ || !manager_ || !seat_) return;
    device_ = ffi::get_device(manager_, seat_);
    if (!device_ || ffi::device_listener(device_, &device_listener_, this)) failed_ = true;
}

void WaylandClipboard::clear_device() {
    while (!sources_.is_empty()) remove_source(sources_[usize(0)]->native);
    for (auto entry : offers_) ffi::destroy_offer(entry.native);
    offers_.clear(); selection_ = nullptr;
    if (device_) {
        if (ffi::device_version(device_) >= ffi::WL_DATA_DEVICE_RELEASE_SINCE_VERSION) ffi::release_device(device_);
        else ffi::destroy_device(device_);
    }
    device_ = nullptr;
}

void WaylandClipboard::bind(ffi::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) {
    if (manager_) return;
    auto selected = version < 3 ? version : 3;
    manager_ = static_cast<ffi::wl_data_device_manager *>(ffi::wl_registry_bind(
        registry, name, &ffi::wl_data_device_manager_interface, selected));
    if (!manager_) { failed_ = true; return; }
    manager_name_ = name; create_device();
}

void WaylandClipboard::remove(rstd::uint32_t name) {
    if (manager_ && manager_name_ == name) {
        clear_device(); cancel_reads(); ffi::destroy_manager(manager_); manager_ = nullptr; manager_name_ = 0;
    }
}

void WaylandClipboard::lost_focus() {
    remove_offer(selection_); cancel_reads();
    for (usize i{}; i < sources_.len();) {
        if (sources_[i]->pending) remove_source(sources_[i]->native);
        else i += usize(1);
    }
}

auto WaylandClipboard::write(Serial id, rstd::uint32_t serial, ref<str> text) -> Result<empty, ClipboardError> {
    if (!id) return Err(ClipboardError::Failed);
    if (!device_ || !manager_) return Err(ClipboardError::Unsupported);
    if (text->len() > usize(limit)) return Err(ClipboardError::Limit);
    if (rstd::str_::from_utf8(text->as_bytes()).is_err()) return Err(ClipboardError::InvalidText);
    bool latin = true; auto chars = text->chars();
    for (auto cp = chars.next(); cp.is_some(); cp = chars.next()) {
        if (!cp->to_primitive()) return Err(ClipboardError::InvalidText);
        if (cp->to_primitive() > 255) latin = false;
    }
    drain();
    if (sources_.len() >= usize(32)) return Err(ClipboardError::Limit);
    for (const auto &entry : sources_) if (entry->id == id) return Err(ClipboardError::Failed);
    auto native = ffi::create_source(manager_);
    if (!native) return Err(ClipboardError::Failed);
    if (ffi::source_listener(native, &source_listener_, this)) { ffi::destroy_source(native); return Err(ClipboardError::Failed); }
    auto entry = Box<Source>::make();
    entry->native = native; entry->id = id; entry->text = String::make(text); entry->pending = true; entry->latin = latin;
    entry->marker = String::make("application/x-vui-clipboard-"_str);
    constexpr char digits[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) entry->marker.push(char32_t(digits[(id >> shift) & 15]));
    auto started = now(); entry->deadline = started > ~Serial{} - timeout_ns ? ~Serial{} : started + timeout_ns;
    auto marker = String::make(entry->marker.as_str()); marker.push(U'\0');
    ffi::source_offer(native, "text/plain;charset=utf-8"); ffi::source_offer(native, "text/plain");
    ffi::source_offer(native, "UTF8_STRING"); ffi::source_offer(native, "TEXT");
    if (latin) ffi::source_offer(native, "STRING");
    ffi::source_offer(native, reinterpret_cast<const char *>(marker.as_str()->as_bytes().as_raw_ptr()));
    for (usize i{}; i < sources_.len();) {
        if (sources_[i]->pending) remove_source(sources_[i]->native);
        else i += usize(1);
    }
    sources_.push(rstd::move(entry));
    pending_sources_ += usize(1);
    ffi::set_selection(device_, native, serial);
    return Ok(empty{});
}

auto WaylandClipboard::request(Serial id) -> Result<empty, ClipboardError> {
    if (!id) return Err(ClipboardError::Failed);
    if (!device_) return Err(ClipboardError::Unsupported);
    auto entry = offer(selection_);
    if (!entry) return Err(ClipboardError::Unavailable);
    if (!entry->utf8 && !entry->upper && !entry->plain && !entry->utf8_string && !entry->text && !entry->latin)
        return Err(ClipboardError::Unsupported);
    drain();
    if (reads_.len() >= usize(32)) return Err(ClipboardError::Limit);
    for (const auto &read : reads_) if (read->id == id) return Err(ClipboardError::Failed);
    int fds[2];
    if (ffi::make_pipe(fds) < 0) return Err(ClipboardError::Failed);
    if (!ffi::nonblocking(fds[0])) { ffi::close(fds[0]); ffi::close(fds[1]); return Err(ClipboardError::Failed); }
    auto latin = !entry->utf8 && !entry->upper && !entry->plain && !entry->utf8_string && !entry->text;
    auto started = start(fds[0], id, latin);
    if (started.is_err()) { ffi::close(fds[1]); return started; }
    auto mime = entry->utf8 ? "text/plain;charset=utf-8" : entry->upper ? "text/plain;charset=UTF-8"
                : entry->plain ? "text/plain" : entry->utf8_string ? "UTF8_STRING" : entry->text ? "TEXT" : "STRING";
    ffi::offer_receive(entry->native, mime, fds[1]);
    ffi::close(fds[1]);
    return Ok(empty{});
}

void WaylandClipboard::drain() {
    for (usize i{}; i < sources_.len();) {
        if (sources_[i]->pending && now() >= sources_[i]->deadline) remove_source(sources_[i]->native);
        else i += usize(1);
    }
    for (usize i{}; i < sends_.len();) {
        auto &send = *sends_[i]; bool complete;
        { auto lock = send.state->complete.lock().unwrap(); complete = *lock; }
        if (complete && send.worker.is_finished()) {
            auto finished = sends_.remove(i); (void)rstd::move(finished->worker).join();
        } else i += usize(1);
    }
    for (usize i{}; i < reads_.len();) {
        auto &read = *reads_[i];
        Option<ReadResult> result;
        {
            auto lock = read.state->result.lock().unwrap();
            result = rstd::exchange(*lock, None<ReadResult>());
        }
        if (result.is_some()) {
            read.complete = true;
            if (!read.reported) { read.reported = true; emit_result(read.id, rstd::move(result).unwrap()); }
        }
        if (read.complete && read.worker.is_finished()) {
            auto finished = reads_.remove(i);
            (void)rstd::move(finished->worker).join();
        } else i += usize(1);
    }
}

auto WaylandClipboard::wait(Index milliseconds) const -> Index {
    for (const auto &source : sources_) if (source->pending) {
        auto current = now();
        if (current >= source->deadline) return 0;
        auto left = source->deadline - current;
        auto delay = left / 1'000'000 + (left % 1'000'000 != 0);
        if (delay < milliseconds) milliseconds = Index(delay);
    }
    for (const auto &send : sends_) {
        auto lock = send->state->complete.lock().unwrap();
        if (*lock && !send->worker.is_finished()) return milliseconds < 1 ? milliseconds : 1;
    }
    for (const auto &read : reads_) if (read->complete && !read->worker.is_finished())
        return milliseconds < 1 ? milliseconds : 1;
    return milliseconds;
}

void WaylandClipboard::close() {
    for (auto &send : sends_) {
        rstd::uint64_t value = 2; long written;
        do { written = ffi::write(send->state->cancel_fd, &value, sizeof(value)); }
        while (written < 0 && ffi::error_number() == ffi::EINTR);
    }
    while (!sends_.is_empty()) {
        auto send = sends_.remove(usize(0)); (void)rstd::move(send->worker).join();
    }
    for (auto &read : reads_) cancel(*read, 2);
    while (!reads_.is_empty()) {
        auto read = reads_.remove(usize(0));
        (void)rstd::move(read->worker).join();
    }
    clear_device();
    if (manager_) ffi::destroy_manager(manager_);
    manager_ = nullptr; manager_name_ = 0; seat_ = nullptr; failed_ = false; waker_ = None();
}
} // namespace vui::host::detail
