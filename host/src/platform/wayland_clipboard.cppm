module vui.host:platform.wayland_clipboard;
import :ffi.wayland;
import :ffi.wayland_data;
import :ffi.linux;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;
namespace wl = ffi::wayland;
namespace wd = ffi::wayland_data;
namespace sys = ffi::linux;

class WaylandClipboard {
    friend struct WaylandClipboardTest;
    static constexpr Index limit = 1'048'576;
    static constexpr Serial timeout_ns = 5'000'000'000;
    struct Offer { wd::wl_data_offer *native{}; bool utf8{}, upper{}, plain{}, utf8_string{}, text{}, latin{}; Serial marker{}; };
    struct Source { wd::wl_data_source *native{}; Serial id{}, deadline{}; String marker, text; bool pending{}, latin{}; };
    struct ReadResult { TextInput text; Option<ClipboardError> error; };
    struct ReadState {
        int cancel_fd;
        rstd::sync::Mutex<Option<ReadResult>> result;
        explicit ReadState(int fd) : cancel_fd(fd), result(None<ReadResult>()) {}
        ~ReadState() { sys::close(cancel_fd); }
    };
    struct Read {
        Serial id{};
        bool reported{}, complete{};
        rstd::sync::Arc<ReadState> state;
        rstd::thread::JoinHandle<void> worker;
        Read(Serial value, rstd::sync::Arc<ReadState> shared, rstd::thread::JoinHandle<void> handle)
            : id(value), state(rstd::move(shared)), worker(rstd::move(handle)) {}
    };
    struct SendState {
        int cancel_fd;
        rstd::sync::Mutex<bool> complete{false};
        explicit SendState(int fd) : cancel_fd(fd) {}
        ~SendState() { sys::close(cancel_fd); }
    };
    struct Send {
        rstd::sync::Arc<SendState> state;
        rstd::thread::JoinHandle<void> worker;
        Send(rstd::sync::Arc<SendState> shared, rstd::thread::JoinHandle<void> handle)
            : state(rstd::move(shared)), worker(rstd::move(handle)) {}
    };
    wd::wl_data_device_manager *manager_{};
    wd::wl_data_device *device_{};
    wl::wl_seat *seat_{};
    rstd::uint32_t manager_name_{};
    wd::wl_data_offer *selection_{};
    Vec<Offer> offers_;
    Vec<Box<Read>> reads_;
    Vec<Box<Source>> sources_;
    Vec<Box<Send>> sends_;
    usize pending_sources_{};
    Option<FrameWaker> waker_;
    bool failed_{};
    void *sink_{};
    void (*emit_)(void *, InputEvent){};

    static auto now() -> Serial {
        auto value = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
        return value > ~Serial{} ? ~Serial{} : Serial(value);
    }
    auto offer(wd::wl_data_offer *native) -> Offer * {
        for (auto &entry : offers_) if (entry.native == native) return &entry;
        return nullptr;
    }
    auto source(wd::wl_data_source *native) -> Source * {
        for (auto &entry : sources_) if (entry->native == native) return &*entry;
        return nullptr;
    }
    void emit_write_result(Serial id, Option<ClipboardError> error = None()) {
        InputEvent event; event.type = EventType::ClipboardWriteResult; event.clipboard_request = id;
        event.clipboard_error = error; emit_(sink_, rstd::move(event));
    }
    void remove_source(wd::wl_data_source *native, Option<ClipboardError> error = None()) {
        for (usize i{}; i < sources_.len(); i += usize(1)) if (sources_[i]->native == native) {
            auto entry = sources_.remove(i);
            if (entry->pending) {
                pending_sources_ -= usize(1);
                emit_write_result(entry->id, error.is_some() ? error : Some(ClipboardError::Unavailable));
            }
            wd::destroy_source(native); return;
        }
    }
    void remove_offer(wd::wl_data_offer *native) {
        if (!native) return;
        if (selection_ == native) selection_ = nullptr;
        for (usize i{}; i < offers_.len(); i += usize(1)) if (offers_[i].native == native) {
            wd::destroy_offer(native); offers_.remove(i); return;
        }
    }
    void emit_result(Serial id, ReadResult result) {
        InputEvent event; event.type = EventType::ClipboardResult; event.clipboard_request = id;
        event.text = rstd::move(result.text); event.clipboard_error = result.error;
        emit_(sink_, rstd::move(event));
    }
    static auto read_pipe(int fd, int cancel, bool latin) -> ReadResult {
        Vec<u8> bytes; bool overflow{}, discard{};
        auto started = now();
        Option<ClipboardError> error;
        bool done{};
        while (!done) {
            auto current = now();
            if (current - started >= timeout_ns) { error = Some(overflow ? ClipboardError::Limit : ClipboardError::Unavailable); break; }
            auto left = timeout_ns - (current - started);
            auto milliseconds = left / 1'000'000 + (left % 1'000'000 != 0);
            sys::pollfd fds[]{{fd, sys::input, 0}, {cancel, sys::input, 0}};
            auto ready = sys::poll(fds, 2, int(milliseconds));
            if (ready < 0) {
                if (sys::error_number() == sys::interrupted) continue;
                error = Some(ClipboardError::Failed); break;
            }
            if (!ready) continue;
            if (fds[1].revents & sys::input) {
                rstd::uint64_t command{};
                long count;
                do { count = sys::read(cancel, &command, sizeof(command)); }
                while (count < 0 && sys::error_number() == sys::interrupted);
                if (count != sizeof(command) || (command & 2)) { error = Some(ClipboardError::Unavailable); break; }
                if (command & 1) { bytes.clear(); discard = true; }
            } else if (fds[1].revents) { error = Some(ClipboardError::Failed); break; }
            if (fds[0].revents & sys::invalid) { error = Some(ClipboardError::Failed); break; }
            if (!(fds[0].revents & (sys::input | sys::disconnected))) continue;
            unsigned char buffer[4096];
            for (unsigned batch{}; batch < 16; ++batch) {
                auto count = sys::read(fd, buffer, sizeof(buffer));
                if (count == 0) { done = true; break; }
                if (count < 0) {
                    auto status = sys::error_number();
                    if (status == sys::interrupted) continue;
                    if (status == sys::again && !(fds[0].revents & sys::disconnected)) break;
                    error = Some(ClipboardError::Failed); done = true; break;
                }
                if (!discard && !overflow && bytes.len() + usize(count) > usize(limit)) { bytes.clear(); overflow = true; }
                if (!discard && !overflow) for (long j{}; j < count; ++j) bytes.push(u8(buffer[j]));
            }
        }
        sys::close(fd);
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
    auto start(int fd, Serial id, bool latin = false) -> Result<empty, ClipboardError> {
        if (waker_.is_none()) { sys::close(fd); return Err(ClipboardError::Unavailable); }
        auto cancel = sys::eventfd(0, sys::event_flags);
        if (cancel < 0) { sys::close(fd); return Err(ClipboardError::Failed); }
        auto state = rstd::sync::Arc<ReadState>::make(cancel);
        auto task = rstd::thread::spawn([fd, latin, shared = state.clone(), wake = FrameWaker(*waker_)]() mutable {
            auto result = read_pipe(fd, shared->cancel_fd, latin);
            {
                auto lock = shared->result.lock().unwrap();
                *lock = Some(rstd::move(result));
            }
            (void)wake.request();
        });
        if (task.is_err()) { sys::close(fd); return Err(ClipboardError::Failed); }
        reads_.push(Box<Read>::make(id, rstd::move(state), rstd::move(task).unwrap()));
        return Ok(empty{});
    }
    static void cancel(Read &read, rstd::uint64_t value) {
        long written;
        do { written = sys::write(read.state->cancel_fd, &value, sizeof(value)); }
        while (written < 0 && sys::error_number() == sys::interrupted);
    }
    void cancel_reads() {
        for (auto &read : reads_) {
            if (!read->reported) {
                cancel(*read, 1);
                read->reported = true;
                emit_result(read->id, ReadResult{TextInput{}, Some(ClipboardError::Unavailable)});
            }
        }
    }
    static void write_pipe(int fd, int cancel, Vec<u8> bytes) {
        if (!sys::block_sigpipe()) { sys::close(fd); return; }
        auto started = now(); usize offset{};
        while (offset < bytes.len()) {
            auto current = now();
            if (current - started >= timeout_ns) break;
            auto left = timeout_ns - (current - started);
            auto milliseconds = left / 1'000'000 + (left % 1'000'000 != 0);
            sys::pollfd fds[]{{fd, sys::output, 0}, {cancel, sys::input, 0}};
            auto ready = sys::poll(fds, 2, int(milliseconds));
            if (ready < 0) { if (sys::error_number() == sys::interrupted) continue; break; }
            if (!ready) continue;
            if (fds[1].revents || (fds[0].revents & sys::error)) break;
            if (!(fds[0].revents & sys::output)) continue;
            auto data = bytes.as_slice().as_raw_ptr() + offset.to_primitive();
            auto count = sys::write(fd, data, (bytes.len() - offset).to_primitive());
            if (count < 0) {
                if (sys::error_number() == sys::interrupted || sys::error_number() == sys::again) continue;
                break;
            }
            if (!count) break;
            offset += usize(count);
        }
        sys::close(fd);
    }
    void start_send(int fd, Vec<u8> bytes) {
        drain();
        if (sends_.len() >= usize(32) || waker_.is_none() || !sys::nonblocking(fd)) { sys::close(fd); return; }
        auto cancel = sys::eventfd(0, sys::event_flags);
        if (cancel < 0) { sys::close(fd); return; }
        auto state = rstd::sync::Arc<SendState>::make(cancel);
        auto task = rstd::thread::spawn([fd, bytes = rstd::move(bytes), shared = state.clone(), wake = FrameWaker(*waker_)]() mutable {
            write_pipe(fd, shared->cancel_fd, rstd::move(bytes));
            { auto lock = shared->complete.lock().unwrap(); *lock = true; }
            (void)wake.request();
        });
        if (task.is_err()) { sys::close(fd); return; }
        sends_.push(Box<Send>::make(rstd::move(state), rstd::move(task).unwrap()));
    }
    static void source_target(void *, wd::wl_data_source *, const char *) {}
    static void source_send(void *data, wd::wl_data_source *native, const char *type, rstd::int32_t fd) {
        auto &self = *static_cast<WaylandClipboard *>(data);
        auto entry = self.source(native);
        if (!entry || !type) { sys::close(fd); return; }
        auto mime = rstd::ffi::CStr::from_ptr(type).to_str();
        if (mime.is_err()) { sys::close(fd); return; }
        Vec<u8> bytes;
        if (*mime == entry->marker.as_str()) { sys::close(fd); return; }
        if (*mime == "STRING"_str && entry->latin) {
            auto chars = entry->text.as_str()->chars();
            for (auto cp = chars.next(); cp.is_some(); cp = chars.next()) bytes.push(u8(cp->to_primitive()));
        } else if (*mime == "text/plain;charset=utf-8"_str || *mime == "text/plain"_str ||
                   *mime == "UTF8_STRING"_str || *mime == "TEXT"_str)
            bytes = Vec<u8>::from(entry->text.as_str()->as_bytes());
        else { sys::close(fd); return; }
        self.start_send(fd, rstd::move(bytes));
    }
    static void source_cancelled(void *data, wd::wl_data_source *native) {
        static_cast<WaylandClipboard *>(data)->remove_source(native);
    }
    static void source_drop(void *, wd::wl_data_source *) {}
    static void source_finished(void *, wd::wl_data_source *) {}
    static void source_action(void *, wd::wl_data_source *, rstd::uint32_t) {}
    static void mime(void *data, wd::wl_data_offer *native, const char *type) {
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
    static void source_actions(void *, wd::wl_data_offer *, rstd::uint32_t) {}
    static void action(void *, wd::wl_data_offer *, rstd::uint32_t) {}
    static void data_offer(void *data, wd::wl_data_device *, wd::wl_data_offer *native) {
        auto &self = *static_cast<WaylandClipboard *>(data);
        if (!native || self.offers_.len() >= usize(32) || self.offer(native)) { self.failed_ = true; return; }
        self.offers_.push({native});
        if (wd::offer_listener(native, &self.offer_listener_, &self)) self.failed_ = true;
    }
    static void enter(void *data, wd::wl_data_device *, rstd::uint32_t serial, wl::wl_surface *,
                      wl::wl_fixed_t, wl::wl_fixed_t, wd::wl_data_offer *native) {
        auto &self = *static_cast<WaylandClipboard *>(data);
        if (native) { wd::offer_accept(native, serial, nullptr); self.remove_offer(native); }
    }
    static void leave(void *, wd::wl_data_device *) {}
    static void motion(void *, wd::wl_data_device *, rstd::uint32_t, wl::wl_fixed_t, wl::wl_fixed_t) {}
    static void drop(void *, wd::wl_data_device *) {}
    static void selection(void *data, wd::wl_data_device *, wd::wl_data_offer *native) {
        auto &self = *static_cast<WaylandClipboard *>(data);
        if (self.selection_ && self.selection_ != native) self.remove_offer(self.selection_);
        self.selection_ = native && self.offer(native) ? native : nullptr;
        auto entry = self.offer(self.selection_);
        if (entry) for (auto &source : self.sources_) if (source->pending && source->id == entry->marker) {
            source->pending = false; self.pending_sources_ -= usize(1); self.emit_write_result(source->id); break;
        }
    }
    wd::wl_data_offer_listener offer_listener_{mime, source_actions, action};
    wd::wl_data_device_listener device_listener_{data_offer, enter, leave, motion, drop, selection};
    wd::wl_data_source_listener source_listener_{source_target, source_send, source_cancelled, source_drop, source_finished, source_action};
    void create_device() {
        if (device_ || !manager_ || !seat_) return;
        device_ = wd::get_device(manager_, seat_);
        if (!device_ || wd::device_listener(device_, &device_listener_, this)) failed_ = true;
    }
    void clear_device() {
        while (!sources_.is_empty()) remove_source(sources_[usize(0)]->native);
        for (auto entry : offers_) wd::destroy_offer(entry.native);
        offers_.clear(); selection_ = nullptr;
        if (device_) {
            if (wd::device_version(device_) >= wd::device_release_version) wd::release_device(device_);
            else wd::destroy_device(device_);
        }
        device_ = nullptr;
    }
  public:
    WaylandClipboard(void *sink, void (*emit)(void *, InputEvent)) : sink_(sink), emit_(emit) {}
    WaylandClipboard(const WaylandClipboard &) = delete;
    auto operator=(const WaylandClipboard &) -> WaylandClipboard & = delete;
    ~WaylandClipboard() { close(); }
    void set_waker(FrameWaker waker) { waker_ = Some(rstd::move(waker)); }
    void bind(wl::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) {
        if (manager_) return;
        auto selected = version < 3 ? version : 3;
        manager_ = static_cast<wd::wl_data_device_manager *>(wl::wl_registry_bind(
            registry, name, &wd::wl_data_device_manager_interface, selected));
        if (!manager_) { failed_ = true; return; }
        manager_name_ = name; create_device();
    }
    void set_seat(wl::wl_seat *seat) {
        if (seat_ == seat) return;
        clear_device(); cancel_reads(); seat_ = seat; create_device();
    }
    void remove(rstd::uint32_t name) {
        if (manager_ && manager_name_ == name) {
            clear_device(); cancel_reads(); wd::destroy_manager(manager_); manager_ = nullptr; manager_name_ = 0;
        }
    }
    void lost_focus() {
        remove_offer(selection_); cancel_reads();
        for (usize i{}; i < sources_.len();) {
            if (sources_[i]->pending) remove_source(sources_[i]->native);
            else i += usize(1);
        }
    }
    auto write(Serial id, rstd::uint32_t serial, ref<str> text) -> Result<empty, ClipboardError> {
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
        auto native = wd::create_source(manager_);
        if (!native) return Err(ClipboardError::Failed);
        if (wd::source_listener(native, &source_listener_, this)) { wd::destroy_source(native); return Err(ClipboardError::Failed); }
        auto entry = Box<Source>::make();
        entry->native = native; entry->id = id; entry->text = String::make(text); entry->pending = true; entry->latin = latin;
        entry->marker = String::make("application/x-vui-clipboard-"_str);
        constexpr char digits[] = "0123456789abcdef";
        for (int shift = 60; shift >= 0; shift -= 4) entry->marker.push(char32_t(digits[(id >> shift) & 15]));
        auto started = now(); entry->deadline = started > ~Serial{} - timeout_ns ? ~Serial{} : started + timeout_ns;
        auto marker = String::make(entry->marker.as_str()); marker.push(U'\0');
        wd::source_offer(native, "text/plain;charset=utf-8"); wd::source_offer(native, "text/plain");
        wd::source_offer(native, "UTF8_STRING"); wd::source_offer(native, "TEXT");
        if (latin) wd::source_offer(native, "STRING");
        wd::source_offer(native, reinterpret_cast<const char *>(marker.as_str()->as_bytes().as_raw_ptr()));
        for (usize i{}; i < sources_.len();) {
            if (sources_[i]->pending) remove_source(sources_[i]->native);
            else i += usize(1);
        }
        sources_.push(rstd::move(entry));
        pending_sources_ += usize(1);
        wd::set_selection(device_, native, serial);
        return Ok(empty{});
    }
    auto request(Serial id) -> Result<empty, ClipboardError> {
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
        if (sys::make_pipe(fds) < 0) return Err(ClipboardError::Failed);
        if (!sys::nonblocking(fds[0])) { sys::close(fds[0]); sys::close(fds[1]); return Err(ClipboardError::Failed); }
        auto latin = !entry->utf8 && !entry->upper && !entry->plain && !entry->utf8_string && !entry->text;
        auto started = start(fds[0], id, latin);
        if (started.is_err()) { sys::close(fds[1]); return started; }
        auto mime = entry->utf8 ? "text/plain;charset=utf-8" : entry->upper ? "text/plain;charset=UTF-8"
                    : entry->plain ? "text/plain" : entry->utf8_string ? "UTF8_STRING" : entry->text ? "TEXT" : "STRING";
        wd::offer_receive(entry->native, mime, fds[1]);
        sys::close(fds[1]);
        return Ok(empty{});
    }
    void drain() {
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
    auto wait(Index milliseconds) const -> Index {
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
    auto pending() const -> usize { return reads_.len() + pending_sources_; }
    auto failed() const -> bool { return failed_; }
    void close() {
        for (auto &send : sends_) {
            rstd::uint64_t value = 2; long written;
            do { written = sys::write(send->state->cancel_fd, &value, sizeof(value)); }
            while (written < 0 && sys::error_number() == sys::interrupted);
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
        if (manager_) wd::destroy_manager(manager_);
        manager_ = nullptr; manager_name_ = 0; seat_ = nullptr; failed_ = false; waker_ = None();
    }
};
}
