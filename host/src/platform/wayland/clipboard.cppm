module vui.host:platform.wayland.clipboard;
import :ffi.wayland;
import :ffi.wayland_data;
import :ffi.linux;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

class WaylandClipboard {
    friend struct WaylandClipboardTest;
    static constexpr Index limit = 1'048'576;
    static constexpr Serial timeout_ns = 5'000'000'000;
    struct Offer { ffi::wl_data_offer *native{}; bool utf8{}, upper{}, plain{}, utf8_string{}, text{}, latin{}; Serial marker{}; };
    struct Source { ffi::wl_data_source *native{}; Serial id{}, deadline{}; String marker, text; bool pending{}, latin{}; };
    struct ReadResult { TextInput text; Option<ClipboardError> error; };
    struct ReadState {
        int cancel_fd;
        rstd::sync::Mutex<Option<ReadResult>> result;
        explicit ReadState(int fd) : cancel_fd(fd), result(None<ReadResult>()) {}
        ~ReadState() { ffi::close(cancel_fd); }
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
        ~SendState() { ffi::close(cancel_fd); }
    };
    struct Send {
        rstd::sync::Arc<SendState> state;
        rstd::thread::JoinHandle<void> worker;
        Send(rstd::sync::Arc<SendState> shared, rstd::thread::JoinHandle<void> handle)
            : state(rstd::move(shared)), worker(rstd::move(handle)) {}
    };
    ffi::wl_data_device_manager *manager_{};
    ffi::wl_data_device *device_{};
    ffi::wl_seat *seat_{};
    rstd::uint32_t manager_name_{};
    ffi::wl_data_offer *selection_{};
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
    auto offer(ffi::wl_data_offer *native) -> Offer * {
        for (auto &entry : offers_) if (entry.native == native) return &entry;
        return nullptr;
    }
    auto source(ffi::wl_data_source *native) -> Source * {
        for (auto &entry : sources_) if (entry->native == native) return &*entry;
        return nullptr;
    }
    void emit_write_result(Serial id, Option<ClipboardError> error = None()) {
        InputEvent event; event.type = EventType::ClipboardWriteResult; event.clipboard_request = id;
        event.clipboard_error = error; emit_(sink_, rstd::move(event));
    }
    void remove_source(ffi::wl_data_source *native, Option<ClipboardError> error = None());
    void remove_offer(ffi::wl_data_offer *native);
    void emit_result(Serial id, ReadResult result);
    static auto read_pipe(int fd, int cancel, bool latin) -> ReadResult;
    auto start(int fd, Serial id, bool latin = false) -> Result<empty, ClipboardError>;
    static void cancel(Read &read, rstd::uint64_t value);
    void cancel_reads();
    static void write_pipe(int fd, int cancel, Vec<u8> bytes);
    void start_send(int fd, Vec<u8> bytes);
    static void source_target(void *, ffi::wl_data_source *, const char *) {}
    static void source_send(void *data, ffi::wl_data_source *native, const char *type, rstd::int32_t fd);
    static void source_cancelled(void *data, ffi::wl_data_source *native) {
        static_cast<WaylandClipboard *>(data)->remove_source(native);
    }
    static void source_drop(void *, ffi::wl_data_source *) {}
    static void source_finished(void *, ffi::wl_data_source *) {}
    static void source_action(void *, ffi::wl_data_source *, rstd::uint32_t) {}
    static void mime(void *data, ffi::wl_data_offer *native, const char *type);
    static void source_actions(void *, ffi::wl_data_offer *, rstd::uint32_t) {}
    static void action(void *, ffi::wl_data_offer *, rstd::uint32_t) {}
    static void data_offer(void *data, ffi::wl_data_device *, ffi::wl_data_offer *native);
    static void enter(void *data, ffi::wl_data_device *, rstd::uint32_t serial, ffi::wl_surface *,
                      ffi::wl_fixed_t, ffi::wl_fixed_t, ffi::wl_data_offer *native) {
        auto &self = *static_cast<WaylandClipboard *>(data);
        if (native) { ffi::offer_accept(native, serial, nullptr); self.remove_offer(native); }
    }
    static void leave(void *, ffi::wl_data_device *) {}
    static void motion(void *, ffi::wl_data_device *, rstd::uint32_t, ffi::wl_fixed_t, ffi::wl_fixed_t) {}
    static void drop(void *, ffi::wl_data_device *) {}
    static void selection(void *data, ffi::wl_data_device *, ffi::wl_data_offer *native);
    ffi::wl_data_offer_listener offer_listener_{mime, source_actions, action};
    ffi::wl_data_device_listener device_listener_{data_offer, enter, leave, motion, drop, selection};
    ffi::wl_data_source_listener source_listener_{source_target, source_send, source_cancelled, source_drop, source_finished, source_action};
    void create_device();
    void clear_device();
  public:
    WaylandClipboard(void *sink, void (*emit)(void *, InputEvent)) : sink_(sink), emit_(emit) {}
    WaylandClipboard(const WaylandClipboard &) = delete;
    auto operator=(const WaylandClipboard &) -> WaylandClipboard & = delete;
    ~WaylandClipboard() { close(); }
    void set_waker(FrameWaker waker) { waker_ = Some(rstd::move(waker)); }
    void bind(ffi::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version);
    void set_seat(ffi::wl_seat *seat) {
        if (seat_ == seat) return;
        clear_device(); cancel_reads(); seat_ = seat; create_device();
    }
    void remove(rstd::uint32_t name);
    void lost_focus();
    auto write(Serial id, rstd::uint32_t serial, ref<str> text) -> Result<empty, ClipboardError>;
    auto request(Serial id) -> Result<empty, ClipboardError>;
    void drain();
    auto wait(Index milliseconds) const -> Index;
    auto pending() const -> usize { return reads_.len() + pending_sources_; }
    auto failed() const -> bool { return failed_; }
    void close();
};
}
