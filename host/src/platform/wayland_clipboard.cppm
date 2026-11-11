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
    struct Offer { wd::wl_data_offer *native{}; bool utf8{}, upper{}, plain{}, utf8_string{}, text{}, latin{}; };
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
    wd::wl_data_device_manager *manager_{};
    wd::wl_data_device *device_{};
    wl::wl_seat *seat_{};
    rstd::uint32_t manager_name_{};
    wd::wl_data_offer *selection_{};
    Vec<Offer> offers_;
    Vec<Box<Read>> reads_;
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
    static void mime(void *data, wd::wl_data_offer *native, const char *type) {
        auto &self = *static_cast<WaylandClipboard *>(data);
        auto entry = self.offer(native); if (!entry || !type) return;
        auto value = rstd::ffi::CStr::from_ptr(type).to_str();
        if (value.is_err()) return;
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
    }
    wd::wl_data_offer_listener offer_listener_{mime, source_actions, action};
    wd::wl_data_device_listener device_listener_{data_offer, enter, leave, motion, drop, selection};
    void create_device() {
        if (device_ || !manager_ || !seat_) return;
        device_ = wd::get_device(manager_, seat_);
        if (!device_ || wd::device_listener(device_, &device_listener_, this)) failed_ = true;
    }
    void clear_device() {
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
    void lost_focus() { remove_offer(selection_); cancel_reads(); }
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
        for (const auto &read : reads_) if (read->complete && !read->worker.is_finished())
            return milliseconds < 1 ? milliseconds : 1;
        return milliseconds;
    }
    auto pending() const -> usize { return reads_.len(); }
    auto failed() const -> bool { return failed_; }
    void close() {
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
