module vui.host:platform.wayland_input;
import :ffi.wayland;
import :ffi.linux;
import :ffi.xkb;
import :platform.keys;
import :platform.keyboard_text;
import :platform.input_clock;
import :platform.input_queue;
import :platform.wayland_text;
import :platform.wayland_clipboard;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;
namespace wl = ffi::wayland;
namespace xkb = ffi::xkb;

class WaylandInput {
    friend struct WaylandInputTest;
    wl::wl_seat *seat_{};
    wl::wl_pointer *pointer_{};
    wl::wl_touch *touch_{};
    wl::wl_keyboard *keyboard_{};
    xkb::xkb_context *key_context_{};
    xkb::xkb_keymap *keymap_{};
    xkb::xkb_state *key_state_{};
    KeyboardCompose compose_;
    wl::wl_surface *surface_{};
    rstd::uint32_t seat_name_{};
    bool failed_{}, pointer_focused_{}, keyboard_focused_{};
    rstd::uint32_t pointer_buttons_{};
    Modifiers modifiers_{};
    rstd::int32_t repeat_rate_{4}, repeat_delay_{500};
    rstd::uint32_t repeat_key_{};
    Option<Serial> repeat_at_;
    TextInput repeat_text_;
    Option<Serial> repeat_text_session_;
    bool repeat_composed_{};
    rstd::uint32_t key_group_{};
    Point position_{};
    Serial generation_{1}, clock_{new_session()}, pointer_device_{new_session()}, touch_device_{new_session()};
    NativeInputClock native_clock_;
    InputQueue input_;
    InputTraceSource input_trace_;
    bool trace_input_{};
    struct Activation { Serial token; rstd::uint32_t serial; };
    Vec<Activation> queued_activations_, delivered_activations_;
    Serial dropped_{};
    struct Contact { rstd::int32_t native; Index pointer; Point position; };
    Vec<Contact> contacts_;
    Index next_contact_{1};
    Point axis_pixels_{}, axis_steps_{};
    Option<SampleTime> axis_time_;
    bool axis_pending_{}, axis_discrete_{}, axis_value120_{}, axis_wheel_{};
    WaylandText text_{this,
        [](void *data, InputEvent event) { static_cast<WaylandInput *>(data)->emit(rstd::move(event)); },
        [](void *data) { ++static_cast<WaylandInput *>(data)->dropped_; }};
    WaylandClipboard clipboard_{this,
        [](void *data, InputEvent event) { static_cast<WaylandInput *>(data)->emit(rstd::move(event)); }};

    static auto now_ns() -> Serial {
        auto now = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
        return now > ~Serial{} ? ~Serial{} : Serial(now);
    }
    void emit(InputEvent event);
    auto native_time(rstd::uint32_t value) -> SampleTime;
    auto activation(rstd::uint32_t serial) -> Serial {
        if (!input_.can_reserve(clipboard_.pending())) return 0;
        auto token = new_session(); queued_activations_.push({token, serial}); return token;
    }
    void flush_axis();
    auto contact(rstd::int32_t id) -> Contact * {
        for (auto &item : contacts_) if (item.native == id) return &item;
        return nullptr;
    }
    void cancel_contacts();
    void release_pointer();
    void lose_touch();
    void release_touch();
    void release_keyboard();
    static void capabilities(void *data, wl::wl_seat *seat, rstd::uint32_t caps);
    static void seat_name(void *, wl::wl_seat *, const char *) {}
    static void enter(void *data, wl::wl_pointer *, rstd::uint32_t, wl::wl_surface *surface,
                      wl::wl_fixed_t x, wl::wl_fixed_t y);
    static void leave(void *data, wl::wl_pointer *, rstd::uint32_t, wl::wl_surface *surface);
    static void motion(void *data, wl::wl_pointer *, rstd::uint32_t time, wl::wl_fixed_t x, wl::wl_fixed_t y);
    static void button(void *data, wl::wl_pointer *, rstd::uint32_t serial, rstd::uint32_t time, rstd::uint32_t button, rstd::uint32_t state);
    static void axis(void *data, wl::wl_pointer *pointer, rstd::uint32_t time, rstd::uint32_t axis, wl::wl_fixed_t value);
    static void frame(void *data, wl::wl_pointer *) { static_cast<WaylandInput *>(data)->flush_axis(); }
    static void axis_source(void *data, wl::wl_pointer *, rstd::uint32_t source) {
        auto &self = *static_cast<WaylandInput *>(data);
        self.axis_wheel_ = source == wl::WL_POINTER_AXIS_SOURCE_WHEEL || source == wl::WL_POINTER_AXIS_SOURCE_WHEEL_TILT;
    }
    static void axis_stop(void *, wl::wl_pointer *, rstd::uint32_t, rstd::uint32_t) {}
    static void axis_discrete(void *data, wl::wl_pointer *, rstd::uint32_t axis, rstd::int32_t value);
    static void axis_value120(void *data, wl::wl_pointer *, rstd::uint32_t axis, rstd::int32_t value);
    static void touch_down(void *data, wl::wl_touch *, rstd::uint32_t serial, rstd::uint32_t time, wl::wl_surface *surface, rstd::int32_t id, wl::wl_fixed_t x, wl::wl_fixed_t y);
    static void touch_up(void *data, wl::wl_touch *, rstd::uint32_t serial, rstd::uint32_t time, rstd::int32_t id);
    static void touch_motion(void *data, wl::wl_touch *, rstd::uint32_t time, rstd::int32_t id, wl::wl_fixed_t x, wl::wl_fixed_t y);
    static void touch_frame(void *, wl::wl_touch *) {}
    static void touch_cancel(void *data, wl::wl_touch *) { static_cast<WaylandInput *>(data)->cancel_contacts(); }
    static void touch_shape(void *, wl::wl_touch *, rstd::int32_t, wl::wl_fixed_t, wl::wl_fixed_t) {}
    static void touch_orientation(void *, wl::wl_touch *, rstd::int32_t, wl::wl_fixed_t) {}
    void reset_compose() { compose_.reset(); }
    void create_compose() { compose_.configure(key_context_); }
    auto keyboard_text(rstd::uint32_t code, bool feed_compose, bool &composed) -> TextInput;
    void emit_keyboard_text(const TextInput &text, Option<Serial> session, Option<SampleTime> time = {});
    static void keyboard_keymap(void *data, wl::wl_keyboard *, rstd::uint32_t format, rstd::int32_t fd, rstd::uint32_t size);
    static void keyboard_enter(void *data, wl::wl_keyboard *, rstd::uint32_t, wl::wl_surface *surface, wl::wl_array *);
    static void keyboard_leave(void *data, wl::wl_keyboard *, rstd::uint32_t, wl::wl_surface *surface);
    void emit_key(rstd::uint32_t code, bool pressed, bool repeat = false, Serial token = 0, Option<SampleTime> time = {});
    static void keyboard_key(void *data, wl::wl_keyboard *, rstd::uint32_t serial, rstd::uint32_t time,
                             rstd::uint32_t code, rstd::uint32_t state);
    static void keyboard_modifiers(void *data, wl::wl_keyboard *, rstd::uint32_t, rstd::uint32_t depressed,
                                   rstd::uint32_t latched, rstd::uint32_t locked, rstd::uint32_t group);
    static void keyboard_repeat_info(void *data, wl::wl_keyboard *, rstd::int32_t rate, rstd::int32_t delay);
    wl::wl_seat_listener seat_listener_{capabilities, seat_name};
    wl::wl_pointer_listener pointer_listener_{enter, leave, motion, button, axis, frame, axis_source, axis_stop, axis_discrete, axis_value120};
    wl::wl_touch_listener touch_listener_{touch_down, touch_up, touch_motion, touch_frame, touch_cancel, touch_shape, touch_orientation};
    wl::wl_keyboard_listener keyboard_listener_{keyboard_keymap, keyboard_enter, keyboard_leave, keyboard_key, keyboard_modifiers, keyboard_repeat_info};

  public:
    WaylandInput() = default;
    WaylandInput(const WaylandInput &) = delete;
    ~WaylandInput() { close(); }
    void bind(wl::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version);
    void bind_text(wl::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) { text_.bind(registry, name, version); }
    void bind_clipboard(wl::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) { clipboard_.bind(registry, name, version); }
    void remove(rstd::uint32_t name) { if (seat_ && seat_name_ == name) close_seat(); text_.remove(name); clipboard_.remove(name); }
    void close_seat();
    void close() { close_seat(); clipboard_.close(); text_.close(); surface_ = nullptr; input_.clear(); native_clock_.reset(); queued_activations_.clear(); delivered_activations_.clear(); dropped_ = 0; failed_ = false; }
    void set_surface(wl::wl_surface *surface) { surface_ = surface; text_.set_surface(surface); }
    void set_generation(Serial generation) { generation_ = generation; }
    auto failed() const -> bool { return failed_ || text_.failed() || clipboard_.failed(); }
    auto request_clipboard(Serial id) -> Result<empty, ClipboardError>;
    auto request_clipboard_write(Serial id, Serial token, ref<str> text) -> Result<empty, ClipboardError>;
    auto clipboard() -> WaylandClipboard & { return clipboard_; }
    auto set_text_input(Option<TextInputState> state) -> NativeTextInputResult;
    auto text_input_available() const -> bool { return text_.available(); }
    auto capture(bool enabled) const -> NativeCaptureResult;
    auto has_input() const -> bool { return !input_.is_empty() || dropped_ != 0; }
    void set_input_tracing(bool enabled) { trace_input_ = enabled; }
    auto take_input() -> Vec<InputEvent> {
        delivered_activations_ = rstd::exchange(queued_activations_, Vec<Activation>{});
        return input_.take();
    }
    auto take_dropped_input() -> Serial { return rstd::exchange(dropped_, Serial{}); }
    auto has_seat() const -> bool { return seat_ != nullptr; }
    auto has_pointer() const -> bool { return pointer_ != nullptr; }
    auto has_touch() const -> bool { return touch_ != nullptr; }
    auto has_keyboard() const -> bool { return keyboard_ != nullptr; }
    auto repeat_wait(Index milliseconds) const -> Index;
    void pump_repeat();
};
} // namespace vui::host::detail
