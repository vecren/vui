module vui.host:platform.wayland_input;
import :ffi.wayland;
import :ffi.linux;
import :ffi.xkb;
import :platform.keys;
import :platform.keyboard_text;
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
    Vec<InputEvent> input_;
    struct Activation { Serial token; rstd::uint32_t serial; };
    Vec<Activation> queued_activations_, delivered_activations_;
    Serial dropped_{};
    struct Contact { rstd::int32_t native; Index pointer; Point position; };
    Vec<Contact> contacts_;
    Index next_contact_{1};
    Point axis_pixels_{}, axis_steps_{};
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
    void emit(InputEvent event) {
        if (input_.len() >= usize(4096)) {
            if (event.type == EventType::ClipboardResult || event.type == EventType::ClipboardWriteResult) {
                for (usize i{}; i < input_.len(); i += usize(1))
                    if (input_[i].type != EventType::ClipboardResult && input_[i].type != EventType::ClipboardWriteResult) { input_.remove(i); ++dropped_; break; }
            }
            if (input_.len() >= usize(4096)) { ++dropped_; return; }
        }
        event.sample_time = {clock_, now_ns(), 1, TimeOrigin::Received};
        event.modifiers = modifiers_;
        event.coordinate_generation = generation_;
        input_.push(rstd::move(event));
    }
    auto activation(rstd::uint32_t serial) -> Serial {
        if (input_.len() >= usize(4096)) return 0;
        auto token = new_session(); queued_activations_.push({token, serial}); return token;
    }
    void flush_axis() {
        if (axis_pending_ && pointer_focused_) {
            InputEvent event;
            event.type = EventType::Wheel;
            event.device = pointer_device_;
            event.position = position_;
            if (axis_discrete_) {
                event.scroll_unit = ScrollUnit::Lines;
                event.scroll_delta = axis_steps_;
            } else if (axis_wheel_) {
                event.scroll_unit = ScrollUnit::Lines;
                event.scroll_delta = {axis_pixels_.x / 10, axis_pixels_.y / 10};
            } else {
                event.scroll_unit = ScrollUnit::Pixels;
                event.scroll_delta = axis_pixels_;
            }
            if (event.scroll_delta.x || event.scroll_delta.y) emit(rstd::move(event));
        }
        axis_pixels_ = {}; axis_steps_ = {};
        axis_pending_ = axis_discrete_ = axis_value120_ = axis_wheel_ = false;
    }
    auto contact(rstd::int32_t id) -> Contact * {
        for (auto &item : contacts_) if (item.native == id) return &item;
        return nullptr;
    }
    void cancel_contacts() {
        for (auto &item : contacts_) {
            InputEvent event;
            event.type = EventType::PointerCancel;
            event.pointer_kind = PointerKind::Touch;
            event.pointer = item.pointer;
            event.device = touch_device_;
            event.position = item.position;
            event.cancel_reason = CancelReason::Native;
            emit(rstd::move(event));
        }
        contacts_.clear();
    }
    void release_pointer() {
        if (!pointer_) return;
        flush_axis();
        InputEvent event; event.type = EventType::DeviceLost; event.device = pointer_device_;
        emit(rstd::move(event));
        pointer_focused_ = false;
        pointer_buttons_ = 0;
        if (wl::wl_pointer_get_version(pointer_) >= wl::pointer_release_version) wl::wl_pointer_release(pointer_);
        else wl::wl_pointer_destroy(pointer_);
        pointer_ = nullptr;
    }
    void release_touch() {
        if (!touch_) return;
        cancel_contacts();
        if (wl::wl_touch_get_version(touch_) >= wl::touch_release_version) wl::wl_touch_release(touch_);
        else wl::wl_touch_destroy(touch_);
        touch_ = nullptr;
    }
    void release_keyboard() {
        if (!keyboard_) return;
        repeat_at_ = None();
        repeat_text_ = TextInput{}; repeat_text_session_ = None();
        if (keyboard_focused_) {
            InputEvent event; event.type = EventType::WindowFocusLost; emit(rstd::move(event));
        }
        clipboard_.lost_focus();
        keyboard_focused_ = false;
        queued_activations_.clear(); delivered_activations_.clear();
        modifiers_ = {};
        repeat_rate_ = 4;
        repeat_delay_ = 500;
        if (wl::wl_keyboard_get_version(keyboard_) >= wl::keyboard_release_version) wl::wl_keyboard_release(keyboard_);
        else wl::wl_keyboard_destroy(keyboard_);
        keyboard_ = nullptr;
        compose_.clear();
        if (key_state_) xkb::xkb_state_unref(key_state_);
        if (keymap_) xkb::xkb_keymap_unref(keymap_);
        if (key_context_) xkb::xkb_context_unref(key_context_);
        key_state_ = nullptr; keymap_ = nullptr; key_context_ = nullptr;
    }
    static void capabilities(void *data, wl::wl_seat *seat, rstd::uint32_t caps) {
        auto &self = *static_cast<WaylandInput *>(data);
        if ((caps & wl::WL_SEAT_CAPABILITY_POINTER) && !self.pointer_) {
            self.pointer_ = wl::wl_seat_get_pointer(seat);
            if (!self.pointer_ || wl::wl_pointer_add_listener(self.pointer_, &self.pointer_listener_, &self)) self.failed_ = true;
        } else if (!(caps & wl::WL_SEAT_CAPABILITY_POINTER)) self.release_pointer();
        if ((caps & wl::WL_SEAT_CAPABILITY_TOUCH) && !self.touch_) {
            self.touch_ = wl::wl_seat_get_touch(seat);
            if (!self.touch_ || wl::wl_touch_add_listener(self.touch_, &self.touch_listener_, &self)) self.failed_ = true;
        } else if (!(caps & wl::WL_SEAT_CAPABILITY_TOUCH)) self.release_touch();
        if ((caps & wl::WL_SEAT_CAPABILITY_KEYBOARD) && !self.keyboard_) {
            self.keyboard_ = wl::wl_seat_get_keyboard(seat);
            if (!self.keyboard_ || wl::wl_keyboard_add_listener(self.keyboard_, &self.keyboard_listener_, &self)) self.failed_ = true;
        } else if (!(caps & wl::WL_SEAT_CAPABILITY_KEYBOARD)) self.release_keyboard();
    }
    static void seat_name(void *, wl::wl_seat *, const char *) {}
    static void enter(void *data, wl::wl_pointer *, rstd::uint32_t, wl::wl_surface *surface,
                      wl::wl_fixed_t x, wl::wl_fixed_t y) {
        auto &self = *static_cast<WaylandInput *>(data);
        self.pointer_focused_ = surface && surface == self.surface_;
        if (!self.pointer_focused_) return;
        self.position_ = {wl::wl_fixed_to_double(x), wl::wl_fixed_to_double(y)};
        InputEvent event; event.type = EventType::WindowPointerEnter; event.device = self.pointer_device_; event.position = self.position_;
        self.emit(rstd::move(event));
    }
    static void leave(void *data, wl::wl_pointer *, rstd::uint32_t, wl::wl_surface *surface) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (!self.pointer_focused_ || surface != self.surface_) return;
        self.flush_axis();
        if (self.pointer_buttons_) {
            InputEvent cancel; cancel.type = EventType::PointerCancel; cancel.device = self.pointer_device_;
            cancel.position = self.position_; cancel.cancel_reason = CancelReason::Native;
            self.emit(rstd::move(cancel));
            self.pointer_buttons_ = 0;
        }
        InputEvent event; event.type = EventType::WindowPointerLeave; event.device = self.pointer_device_; event.position = self.position_;
        self.emit(rstd::move(event));
        self.pointer_focused_ = false;
    }
    static void motion(void *data, wl::wl_pointer *, rstd::uint32_t, wl::wl_fixed_t x, wl::wl_fixed_t y) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (!self.pointer_focused_) return;
        self.position_ = {wl::wl_fixed_to_double(x), wl::wl_fixed_to_double(y)};
        InputEvent event; event.type = EventType::PointerMove; event.device = self.pointer_device_; event.position = self.position_;
        self.emit(rstd::move(event));
    }
    static void button(void *data, wl::wl_pointer *, rstd::uint32_t serial, rstd::uint32_t, rstd::uint32_t button, rstd::uint32_t state) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (!self.pointer_focused_ || (state != wl::WL_POINTER_BUTTON_STATE_PRESSED && state != wl::WL_POINTER_BUTTON_STATE_RELEASED)) return;
        InputEvent event;
        event.button = button == ffi::linux::button_left ? PointerButton::Primary : button == ffi::linux::button_right ? PointerButton::Secondary
                     : button == ffi::linux::button_middle ? PointerButton::Middle : button == ffi::linux::button_back ? PointerButton::Back
                     : button == ffi::linux::button_forward ? PointerButton::Forward : PointerButton::None;
        if (event.button == PointerButton::None) return;
        auto mask = 1u << (unsigned(event.button) - 1);
        if (state == wl::WL_POINTER_BUTTON_STATE_PRESSED) self.pointer_buttons_ |= mask;
        else self.pointer_buttons_ &= ~mask;
        event.type = state == wl::WL_POINTER_BUTTON_STATE_PRESSED ? EventType::PointerDown : EventType::PointerUp;
        event.device = self.pointer_device_; event.position = self.position_;
        event.activation = self.activation(serial);
        self.emit(rstd::move(event));
    }
    static void axis(void *data, wl::wl_pointer *pointer, rstd::uint32_t, rstd::uint32_t axis, wl::wl_fixed_t value) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (!self.pointer_focused_) return;
        if (axis == wl::WL_POINTER_AXIS_HORIZONTAL_SCROLL) self.axis_pixels_.x += wl::wl_fixed_to_double(value);
        else if (axis == wl::WL_POINTER_AXIS_VERTICAL_SCROLL) self.axis_pixels_.y += wl::wl_fixed_to_double(value);
        else return;
        self.axis_pending_ = true;
        if (wl::wl_pointer_get_version(pointer) < wl::pointer_frame_version) self.flush_axis();
    }
    static void frame(void *data, wl::wl_pointer *) { static_cast<WaylandInput *>(data)->flush_axis(); }
    static void axis_source(void *data, wl::wl_pointer *, rstd::uint32_t source) {
        auto &self = *static_cast<WaylandInput *>(data);
        self.axis_wheel_ = source == wl::WL_POINTER_AXIS_SOURCE_WHEEL || source == wl::WL_POINTER_AXIS_SOURCE_WHEEL_TILT;
    }
    static void axis_stop(void *, wl::wl_pointer *, rstd::uint32_t, rstd::uint32_t) {}
    static void axis_discrete(void *data, wl::wl_pointer *, rstd::uint32_t axis, rstd::int32_t value) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (self.axis_value120_) return;
        self.axis_discrete_ = true;
        if (axis == wl::WL_POINTER_AXIS_HORIZONTAL_SCROLL) self.axis_steps_.x += value;
        else if (axis == wl::WL_POINTER_AXIS_VERTICAL_SCROLL) self.axis_steps_.y += value;
    }
    static void axis_value120(void *data, wl::wl_pointer *, rstd::uint32_t axis, rstd::int32_t value) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (!self.axis_value120_) self.axis_steps_ = {};
        self.axis_value120_ = self.axis_discrete_ = true;
        if (axis == wl::WL_POINTER_AXIS_HORIZONTAL_SCROLL) self.axis_steps_.x += Scalar(value) / 120;
        else if (axis == wl::WL_POINTER_AXIS_VERTICAL_SCROLL) self.axis_steps_.y += Scalar(value) / 120;
    }
    static void touch_down(void *data, wl::wl_touch *, rstd::uint32_t serial, rstd::uint32_t, wl::wl_surface *surface, rstd::int32_t id, wl::wl_fixed_t x, wl::wl_fixed_t y) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (surface != self.surface_ || !surface || self.contact(id)) return;
        if (self.contacts_.len() >= usize(32) || self.next_contact_ == ~Index{}) { ++self.dropped_; return; }
        Point pos{wl::wl_fixed_to_double(x), wl::wl_fixed_to_double(y)};
        auto pointer = self.next_contact_++;
        self.contacts_.push({id, pointer, pos});
        InputEvent event; event.type = EventType::PointerDown; event.pointer_kind = PointerKind::Touch;
        event.device = self.touch_device_; event.pointer = pointer; event.position = pos;
        event.activation = self.activation(serial);
        self.emit(rstd::move(event));
    }
    static void touch_up(void *data, wl::wl_touch *, rstd::uint32_t serial, rstd::uint32_t, rstd::int32_t id) {
        auto &self = *static_cast<WaylandInput *>(data);
        auto contact = self.contact(id); if (!contact) return;
        InputEvent event; event.type = EventType::PointerUp; event.pointer_kind = PointerKind::Touch;
        event.device = self.touch_device_; event.pointer = contact->pointer; event.position = contact->position;
        event.activation = self.activation(serial);
        self.emit(rstd::move(event));
        self.contacts_.retain([&](const Contact &item) { return item.native != id; });
    }
    static void touch_motion(void *data, wl::wl_touch *, rstd::uint32_t, rstd::int32_t id, wl::wl_fixed_t x, wl::wl_fixed_t y) {
        auto &self = *static_cast<WaylandInput *>(data);
        auto contact = self.contact(id); if (!contact) return;
        contact->position = {wl::wl_fixed_to_double(x), wl::wl_fixed_to_double(y)};
        InputEvent event; event.type = EventType::PointerMove; event.pointer_kind = PointerKind::Touch;
        event.device = self.touch_device_; event.pointer = contact->pointer; event.position = contact->position;
        self.emit(rstd::move(event));
    }
    static void touch_frame(void *, wl::wl_touch *) {}
    static void touch_cancel(void *data, wl::wl_touch *) { static_cast<WaylandInput *>(data)->cancel_contacts(); }
    static void touch_shape(void *, wl::wl_touch *, rstd::int32_t, wl::wl_fixed_t, wl::wl_fixed_t) {}
    static void touch_orientation(void *, wl::wl_touch *, rstd::int32_t, wl::wl_fixed_t) {}
    void reset_compose() { compose_.reset(); }
    void create_compose() { compose_.configure(key_context_); }
    auto keyboard_text(rstd::uint32_t code, bool feed_compose, bool &composed) -> TextInput {
        composed = false;
        if (!keyboard_focused_ || !key_state_ || text_.session().is_none()) return {};
        if (text_.composing() || modifiers_.control || modifiers_.alt || modifiers_.meta) { reset_compose(); return {}; }
        auto symbol = xkb::xkb_state_key_get_one_sym(key_state_, code + 8);
        if (feed_compose) {
            auto status = compose_.feed(symbol);
            if (status == ComposeResult::Pending) return {};
            composed = status == ComposeResult::Composed;
        }
        char bytes[4096]{};
        int size = composed ? compose_.utf8(bytes, sizeof(bytes))
                            : xkb::xkb_state_key_get_utf8(key_state_, code + 8, bytes, sizeof(bytes));
        if (size <= 0 || size >= int(sizeof(bytes))) return {};
        bool invalid{};
        auto result = keyboard_text_from_utf8(bytes, size, invalid);
        if (invalid) ++dropped_;
        return result;
    }
    void emit_keyboard_text(const TextInput &text, Option<Serial> session) {
        if (!text.present() || session.is_none() || modifiers_.control || modifiers_.alt || modifiers_.meta) return;
        InputEvent event; event.type = EventType::TextInput; event.text = text; event.text_session = *session;
        emit(rstd::move(event));
    }
    static void keyboard_keymap(void *data, wl::wl_keyboard *, rstd::uint32_t format, rstd::int32_t fd, rstd::uint32_t size) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (format != wl::WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !size || size > 16 * 1024 * 1024) {
            ffi::linux::close(fd);
            return;
        }
        void *mapping = ffi::linux::mmap(nullptr, size, ffi::linux::map_read, ffi::linux::map_private, fd, 0);
        ffi::linux::close(fd);
        if (ffi::linux::map_failed(mapping)) { self.failed_ = true; return; }
        if (!self.key_context_) self.key_context_ = xkb::xkb_context_new(xkb::XKB_CONTEXT_NO_FLAGS);
        auto keymap = self.key_context_ ? xkb::xkb_keymap_new_from_buffer(self.key_context_,
            static_cast<const char *>(mapping), size, xkb::XKB_KEYMAP_FORMAT_TEXT_V1,
            xkb::XKB_KEYMAP_COMPILE_NO_FLAGS) : nullptr;
        ffi::linux::munmap(mapping, size);
        if (!keymap) { self.failed_ = true; return; }
        auto state = xkb::xkb_state_new(keymap);
        if (!state) { xkb::xkb_keymap_unref(keymap); self.failed_ = true; return; }
        if (self.key_state_) xkb::xkb_state_unref(self.key_state_);
        if (self.keymap_) xkb::xkb_keymap_unref(self.keymap_);
        self.keymap_ = keymap; self.key_state_ = state; self.repeat_at_ = None(); self.modifiers_ = {};
        self.repeat_text_ = TextInput{}; self.repeat_text_session_ = None(); self.key_group_ = 0;
        self.create_compose();
    }
    static void keyboard_enter(void *data, wl::wl_keyboard *, rstd::uint32_t, wl::wl_surface *surface, wl::wl_array *) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (!surface || surface != self.surface_) return;
        self.keyboard_focused_ = true;
        InputEvent event; event.type = EventType::WindowFocusGained; self.emit(rstd::move(event));
    }
    static void keyboard_leave(void *data, wl::wl_keyboard *, rstd::uint32_t, wl::wl_surface *surface) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (!self.keyboard_focused_ || surface != self.surface_) return;
        self.repeat_at_ = None(); self.keyboard_focused_ = false;
        self.queued_activations_.clear(); self.delivered_activations_.clear();
        self.repeat_text_ = TextInput{}; self.repeat_text_session_ = None(); self.reset_compose();
        InputEvent event; event.type = EventType::WindowFocusLost; self.emit(rstd::move(event));
        self.clipboard_.lost_focus();
        self.modifiers_ = {};
    }
    void emit_key(rstd::uint32_t code, bool pressed, bool repeat = false, Serial token = 0) {
        InputEvent event;
        event.type = pressed ? EventType::KeyDown : EventType::KeyUp;
        event.repeat = repeat;
        event.activation = token;
        if (key_state_) event.key = key_from_symbol(xkb::xkb_state_key_get_one_sym(key_state_, code + 8));
        emit(rstd::move(event));
    }
    static void keyboard_key(void *data, wl::wl_keyboard *, rstd::uint32_t serial, rstd::uint32_t,
                             rstd::uint32_t code, rstd::uint32_t state) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (!self.keyboard_focused_ || code > ~rstd::uint32_t{} - 8 ||
            (state != wl::WL_KEYBOARD_KEY_STATE_PRESSED && state != wl::WL_KEYBOARD_KEY_STATE_RELEASED)) return;
        bool pressed = state == wl::WL_KEYBOARD_KEY_STATE_PRESSED;
        self.emit_key(code, pressed, false, self.activation(serial));
        bool composed{};
        TextInput text;
        auto text_session = self.text_.session();
        if (pressed) {
            text = self.keyboard_text(code, true, composed);
            self.emit_keyboard_text(text, text_session);
        }
        if (pressed && self.keymap_ && self.repeat_rate_ > 0 && xkb::xkb_keymap_key_repeats(self.keymap_, code + 8)) {
            self.repeat_key_ = code;
            self.repeat_text_ = rstd::move(text); self.repeat_text_session_ = text_session; self.repeat_composed_ = composed;
            auto now = now_ns();
            auto delay = Serial(self.repeat_delay_ > 0 ? self.repeat_delay_ : 0) * 1'000'000;
            self.repeat_at_ = Some(delay > ~Serial{} - now ? ~Serial{} : now + delay);
        } else if (!pressed && self.repeat_at_.is_some() && self.repeat_key_ == code) {
            self.repeat_at_ = None(); self.repeat_text_ = TextInput{}; self.repeat_text_session_ = None();
        }
    }
    static void keyboard_modifiers(void *data, wl::wl_keyboard *, rstd::uint32_t, rstd::uint32_t depressed,
                                   rstd::uint32_t latched, rstd::uint32_t locked, rstd::uint32_t group) {
        auto &self = *static_cast<WaylandInput *>(data);
        if (!self.key_state_) return;
        xkb::xkb_state_update_mask(self.key_state_, depressed, latched, locked, 0, 0, group);
        auto active = [&](const char *name) { return xkb::xkb_state_mod_name_is_active(self.key_state_, name, xkb::XKB_STATE_MODS_EFFECTIVE) > 0; };
        self.modifiers_ = {active(xkb::shift), active(xkb::control), active(xkb::alt), active(xkb::meta)};
        if (group != self.key_group_) { self.key_group_ = group; self.reset_compose(); }
        if (self.repeat_at_.is_some() && !self.repeat_composed_ && self.repeat_text_session_ == self.text_.session()) {
            bool composed{};
            self.repeat_text_ = self.keyboard_text(self.repeat_key_, false, composed);
        }
    }
    static void keyboard_repeat_info(void *data, wl::wl_keyboard *, rstd::int32_t rate, rstd::int32_t delay) {
        auto &self = *static_cast<WaylandInput *>(data);
        self.repeat_rate_ = rate > 0 ? rate : 0;
        self.repeat_delay_ = delay > 0 ? delay : 0;
        if (!self.repeat_rate_) self.repeat_at_ = None();
    }
    wl::wl_seat_listener seat_listener_{capabilities, seat_name};
    wl::wl_pointer_listener pointer_listener_{enter, leave, motion, button, axis, frame, axis_source, axis_stop, axis_discrete, axis_value120};
    wl::wl_touch_listener touch_listener_{touch_down, touch_up, touch_motion, touch_frame, touch_cancel, touch_shape, touch_orientation};
    wl::wl_keyboard_listener keyboard_listener_{keyboard_keymap, keyboard_enter, keyboard_leave, keyboard_key, keyboard_modifiers, keyboard_repeat_info};

  public:
    WaylandInput() = default;
    WaylandInput(const WaylandInput &) = delete;
    ~WaylandInput() { close(); }
    void bind(wl::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) {
        if (seat_) return;
        seat_ = static_cast<wl::wl_seat *>(wl::wl_registry_bind(registry, name, &wl::wl_seat_interface, version < 8 ? version : 8));
        if (!seat_ || wl::wl_seat_add_listener(seat_, &seat_listener_, this)) { failed_ = true; return; }
        seat_name_ = name;
        text_.set_seat(seat_);
        clipboard_.set_seat(seat_);
    }
    void bind_text(wl::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) { text_.bind(registry, name, version); }
    void bind_clipboard(wl::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) { clipboard_.bind(registry, name, version); }
    void remove(rstd::uint32_t name) { if (seat_ && seat_name_ == name) close_seat(); text_.remove(name); clipboard_.remove(name); }
    void close_seat() {
        text_.set_seat(nullptr);
        release_pointer(); release_touch(); release_keyboard();
        clipboard_.set_seat(nullptr);
        if (seat_) {
            if (wl::wl_seat_get_version(seat_) >= wl::seat_release_version) wl::wl_seat_release(seat_);
            else wl::wl_seat_destroy(seat_);
        }
        seat_ = nullptr; seat_name_ = 0;
    }
    void close() { close_seat(); clipboard_.close(); text_.close(); surface_ = nullptr; input_.clear(); queued_activations_.clear(); delivered_activations_.clear(); dropped_ = 0; failed_ = false; }
    void set_surface(wl::wl_surface *surface) { surface_ = surface; text_.set_surface(surface); }
    void set_generation(Serial generation) { generation_ = generation; }
    auto failed() const -> bool { return failed_ || text_.failed() || clipboard_.failed(); }
    auto request_clipboard(Serial id) -> Result<empty, ClipboardError> {
        if (!keyboard_focused_) return Err(ClipboardError::Unavailable);
        if (input_.len() + clipboard_.pending() >= usize(4096)) return Err(ClipboardError::Limit);
        return clipboard_.request(id);
    }
    auto request_clipboard_write(Serial id, Serial token, ref<str> text) -> Result<empty, ClipboardError> {
        if (!keyboard_focused_ || !token) return Err(ClipboardError::Unavailable);
        if (input_.len() + clipboard_.pending() >= usize(4096)) return Err(ClipboardError::Limit);
        for (const auto &entry : delivered_activations_)
            if (entry.token == token) return clipboard_.write(id, entry.serial, text);
        return Err(ClipboardError::Unavailable);
    }
    auto clipboard() -> WaylandClipboard & { return clipboard_; }
    auto set_text_input(Option<TextInputState> state) -> NativeTextInputResult {
        auto previous = text_.session();
        auto result = text_.set_state(rstd::move(state));
        if (previous != text_.session()) {
            reset_compose(); repeat_text_ = TextInput{}; repeat_text_session_ = None();
        }
        if (result == NativeTextInputResult::Unsupported && keyboard_ && key_state_ && text_.session().is_some())
            return NativeTextInputResult::Succeeded;
        return result;
    }
    auto text_input_available() const -> bool { return text_.available(); }
    auto capture(bool enabled) const -> NativeCaptureResult {
        if (!enabled) return NativeCaptureResult::Succeeded;
        return pointer_ && pointer_focused_ && pointer_buttons_ ? NativeCaptureResult::Succeeded
                                                                  : NativeCaptureResult::Unsupported;
    }
    auto has_input() const -> bool { return !input_.is_empty() || dropped_ != 0; }
    auto take_input() -> Vec<InputEvent> {
        delivered_activations_ = rstd::exchange(queued_activations_, Vec<Activation>{});
        return rstd::exchange(input_, Vec<InputEvent>{});
    }
    auto take_dropped_input() -> Serial { return rstd::exchange(dropped_, Serial{}); }
    auto has_seat() const -> bool { return seat_ != nullptr; }
    auto has_pointer() const -> bool { return pointer_ != nullptr; }
    auto has_touch() const -> bool { return touch_ != nullptr; }
    auto has_keyboard() const -> bool { return keyboard_ != nullptr; }
    auto repeat_wait(Index milliseconds) const -> Index {
        if (repeat_at_.is_none()) return milliseconds;
        auto now = now_ns();
        if (*repeat_at_ <= now) return 0;
        auto delta = *repeat_at_ - now;
        auto delay = delta / 1'000'000 + (delta % 1'000'000 != 0);
        return delay < milliseconds ? Index(delay) : milliseconds;
    }
    void pump_repeat() {
        if (repeat_at_.is_none() || !keyboard_focused_ || repeat_rate_ <= 0) return;
        auto now = now_ns();
        auto interval = Serial(1'000'000'000) / Serial(repeat_rate_);
        if (!interval) interval = 1;
        unsigned count{};
        while (repeat_at_.is_some() && *repeat_at_ <= now && count++ < 32) {
            emit_key(repeat_key_, true, true);
            if (text_.composing()) { reset_compose(); repeat_text_ = TextInput{}; repeat_text_session_ = None(); }
            if (repeat_text_session_ == text_.session()) emit_keyboard_text(repeat_text_, repeat_text_session_);
            auto previous = *repeat_at_;
            repeat_at_ = previous > ~Serial{} - interval ? None<Serial>() : Some(previous + interval);
        }
        if (repeat_at_.is_some() && *repeat_at_ <= now) {
            auto skipped = (now - *repeat_at_) / interval + 1;
            dropped_ += rstd::min(skipped, ~Serial{} - dropped_);
            repeat_at_ = now > ~Serial{} - interval ? None<Serial>() : Some(now + interval);
        }
    }
};
} // namespace vui::host::detail
