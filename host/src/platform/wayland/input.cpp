module vui.host;
import :platform.input;
import :platform.wayland.input;
import :platform.wayland.ffi;
import :platform.linux.ffi;
import :platform.xkb.ffi;
import :platform.xkb;
import :platform.wayland.text;
import :platform.wayland.clipboard;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

void WaylandInput::emit(InputEvent event) {
    dropped_ += input_.push(rstd::move(event), clipboard_.pending(), [&](InputEvent &queued) {
        if (!queued.sample_time.clock)
            queued.sample_time = {clock_, now_ns(), 1, TimeOrigin::Received};
        queued.modifiers = modifiers_;
        queued.coordinate_generation = generation_;
        if (trace_input_)
            queued.stamp = input_trace_.stamp();
    });
}

auto WaylandInput::native_time(rstd::uint32_t value) -> SampleTime {
    auto sample = native_clock_.sample(value, now_ns());
    if (sample.discontinuity) {
        InputEvent reset;
        reset.type = EventType::InputReset;
        emit(rstd::move(reset));
    }
    return sample.time;
}

void WaylandInput::flush_axis() {
    if (axis_pending_ && pointer_focused_) {
        InputEvent event;
        event.type = EventType::Wheel;
        event.device = pointer_device_;
        event.position = position_;
        if (axis_time_.is_some())
            event.sample_time = *axis_time_;
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
        if (event.scroll_delta.x || event.scroll_delta.y)
            emit(rstd::move(event));
    }
    axis_pixels_ = {};
    axis_steps_ = {};
    axis_time_ = None();
    axis_pending_ = axis_discrete_ = axis_value120_ = axis_wheel_ = false;
}

void WaylandInput::cancel_contacts() {
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

void WaylandInput::release_pointer() {
    if (!pointer_)
        return;
    flush_axis();
    InputEvent event;
    event.type = EventType::DeviceLost;
    event.device = pointer_device_;
    emit(rstd::move(event));
    pointer_device_ = new_session();
    pointer_focused_ = false;
    pointer_buttons_ = 0;
    if (ffi::wl_pointer_get_version(pointer_) >= ffi::WL_POINTER_RELEASE_SINCE_VERSION)
        ffi::wl_pointer_release(pointer_);
    else
        ffi::wl_pointer_destroy(pointer_);
    pointer_ = nullptr;
}

void WaylandInput::lose_touch() {
    cancel_contacts();
    InputEvent event;
    event.type = EventType::DeviceLost;
    event.device = touch_device_;
    emit(rstd::move(event));
    touch_device_ = new_session();
}

void WaylandInput::release_touch() {
    if (!touch_)
        return;
    lose_touch();
    if (ffi::wl_touch_get_version(touch_) >= ffi::WL_TOUCH_RELEASE_SINCE_VERSION)
        ffi::wl_touch_release(touch_);
    else
        ffi::wl_touch_destroy(touch_);
    touch_ = nullptr;
}

void WaylandInput::release_keyboard() {
    repeat_at_ = None();
    repeat_text_ = TextInput{};
    repeat_text_session_ = None();
    if (keyboard_focused_) {
        InputEvent event;
        event.type = EventType::WindowFocusLost;
        emit(rstd::move(event));
    }
    clipboard_.lost_focus();
    keyboard_focused_ = false;
    queued_activations_.clear();
    delivered_activations_.clear();
    modifiers_ = {};
    repeat_rate_ = 4;
    repeat_delay_ = 500;
    if (keyboard_) {
        if (ffi::wl_keyboard_get_version(keyboard_) >= ffi::WL_KEYBOARD_RELEASE_SINCE_VERSION)
            ffi::wl_keyboard_release(keyboard_);
        else
            ffi::wl_keyboard_destroy(keyboard_);
        keyboard_ = nullptr;
    }
    compose_.clear();
    if (key_state_)
        xkb().xkb_state_unref(key_state_);
    if (keymap_)
        xkb().xkb_keymap_unref(keymap_);
    if (key_context_)
        xkb().xkb_context_unref(key_context_);
    key_state_ = nullptr;
    keymap_ = nullptr;
    key_context_ = nullptr;
}

void WaylandInput::capabilities(void *data, ffi::wl_seat *seat, rstd::uint32_t caps) {
    auto &self = *static_cast<WaylandInput *>(data);
    if ((caps & ffi::WL_SEAT_CAPABILITY_POINTER) && !self.pointer_) {
        self.pointer_ = ffi::wl_seat_get_pointer(seat);
        if (!self.pointer_ || ffi::wl_pointer_add_listener(self.pointer_, &self.pointer_listener_, &self))
            self.failed_ = true;
    } else if (!(caps & ffi::WL_SEAT_CAPABILITY_POINTER))
        self.release_pointer();
    if ((caps & ffi::WL_SEAT_CAPABILITY_TOUCH) && !self.touch_) {
        self.touch_ = ffi::wl_seat_get_touch(seat);
        if (!self.touch_ || ffi::wl_touch_add_listener(self.touch_, &self.touch_listener_, &self))
            self.failed_ = true;
    } else if (!(caps & ffi::WL_SEAT_CAPABILITY_TOUCH))
        self.release_touch();
    if ((caps & ffi::WL_SEAT_CAPABILITY_KEYBOARD) && !self.keyboard_) {
        self.keyboard_ = ffi::wl_seat_get_keyboard(seat);
        if (!self.keyboard_ || ffi::wl_keyboard_add_listener(self.keyboard_, &self.keyboard_listener_, &self))
            self.failed_ = true;
    } else if (!(caps & ffi::WL_SEAT_CAPABILITY_KEYBOARD))
        self.release_keyboard();
}

void WaylandInput::enter(void *data, ffi::wl_pointer *, rstd::uint32_t, ffi::wl_surface *surface, ffi::wl_fixed_t x,
                         ffi::wl_fixed_t y) {
    auto &self = *static_cast<WaylandInput *>(data);
    self.pointer_focused_ = surface && surface == self.surface_;
    if (!self.pointer_focused_)
        return;
    self.position_ = {ffi::wl_fixed_to_double(x), ffi::wl_fixed_to_double(y)};
    InputEvent event;
    event.type = EventType::WindowPointerEnter;
    event.device = self.pointer_device_;
    event.position = self.position_;
    self.emit(rstd::move(event));
}

void WaylandInput::leave(void *data, ffi::wl_pointer *, rstd::uint32_t, ffi::wl_surface *surface) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (!self.pointer_focused_ || surface != self.surface_)
        return;
    self.flush_axis();
    if (self.pointer_buttons_) {
        InputEvent cancel;
        cancel.type = EventType::PointerCancel;
        cancel.device = self.pointer_device_;
        cancel.position = self.position_;
        cancel.cancel_reason = CancelReason::Native;
        self.emit(rstd::move(cancel));
        self.pointer_buttons_ = 0;
    }
    InputEvent event;
    event.type = EventType::WindowPointerLeave;
    event.device = self.pointer_device_;
    event.position = self.position_;
    self.emit(rstd::move(event));
    self.pointer_focused_ = false;
}

void WaylandInput::motion(void *data, ffi::wl_pointer *, rstd::uint32_t time, ffi::wl_fixed_t x, ffi::wl_fixed_t y) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (!self.pointer_focused_)
        return;
    self.position_ = {ffi::wl_fixed_to_double(x), ffi::wl_fixed_to_double(y)};
    InputEvent event;
    event.type = EventType::PointerMove;
    event.device = self.pointer_device_;
    event.position = self.position_;
    event.sample_time = self.native_time(time);
    self.emit(rstd::move(event));
}

void WaylandInput::button(void *data, ffi::wl_pointer *, rstd::uint32_t serial, rstd::uint32_t time,
                          rstd::uint32_t button, rstd::uint32_t state) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (!self.pointer_focused_ ||
        (state != ffi::WL_POINTER_BUTTON_STATE_PRESSED && state != ffi::WL_POINTER_BUTTON_STATE_RELEASED))
        return;
    InputEvent event;
    event.button = button == ffi::BTN_LEFT     ? PointerButton::Primary
                   : button == ffi::BTN_RIGHT  ? PointerButton::Secondary
                   : button == ffi::BTN_MIDDLE ? PointerButton::Middle
                   : button == ffi::BTN_SIDE   ? PointerButton::Back
                   : button == ffi::BTN_EXTRA  ? PointerButton::Forward
                                               : PointerButton::None;
    if (event.button == PointerButton::None)
        return;
    auto mask = 1u << (unsigned(event.button) - 1);
    if (state == ffi::WL_POINTER_BUTTON_STATE_PRESSED)
        self.pointer_buttons_ |= mask;
    else
        self.pointer_buttons_ &= ~mask;
    event.type = state == ffi::WL_POINTER_BUTTON_STATE_PRESSED ? EventType::PointerDown : EventType::PointerUp;
    event.device = self.pointer_device_;
    event.position = self.position_;
    event.sample_time = self.native_time(time);
    event.activation = self.activation(serial);
    self.emit(rstd::move(event));
}

void WaylandInput::axis(void *data, ffi::wl_pointer *pointer, rstd::uint32_t time, rstd::uint32_t axis,
                        ffi::wl_fixed_t value) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (!self.pointer_focused_)
        return;
    if (axis == ffi::WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        self.axis_pixels_.x += ffi::wl_fixed_to_double(value);
    else if (axis == ffi::WL_POINTER_AXIS_VERTICAL_SCROLL)
        self.axis_pixels_.y += ffi::wl_fixed_to_double(value);
    else
        return;
    self.axis_time_ = Some(self.native_time(time));
    self.axis_pending_ = true;
    if (ffi::wl_pointer_get_version(pointer) < ffi::WL_POINTER_FRAME_SINCE_VERSION)
        self.flush_axis();
}

void WaylandInput::axis_discrete(void *data, ffi::wl_pointer *, rstd::uint32_t axis, rstd::int32_t value) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (self.axis_value120_)
        return;
    self.axis_discrete_ = true;
    if (axis == ffi::WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        self.axis_steps_.x += value;
    else if (axis == ffi::WL_POINTER_AXIS_VERTICAL_SCROLL)
        self.axis_steps_.y += value;
}

void WaylandInput::axis_value120(void *data, ffi::wl_pointer *, rstd::uint32_t axis, rstd::int32_t value) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (!self.axis_value120_)
        self.axis_steps_ = {};
    self.axis_value120_ = self.axis_discrete_ = true;
    if (axis == ffi::WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        self.axis_steps_.x += Scalar(value) / 120;
    else if (axis == ffi::WL_POINTER_AXIS_VERTICAL_SCROLL)
        self.axis_steps_.y += Scalar(value) / 120;
}

void WaylandInput::touch_down(void *data, ffi::wl_touch *, rstd::uint32_t serial, rstd::uint32_t time,
                              ffi::wl_surface *surface, rstd::int32_t id, ffi::wl_fixed_t x, ffi::wl_fixed_t y) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (surface != self.surface_ || !surface || self.contact(id))
        return;
    if (self.contacts_.len() >= usize(32) || self.next_contact_ == ~Index{}) {
        ++self.dropped_;
        return;
    }
    Point pos{ffi::wl_fixed_to_double(x), ffi::wl_fixed_to_double(y)};
    auto pointer = self.next_contact_++;
    self.contacts_.push({id, pointer, pos});
    InputEvent event;
    event.type = EventType::PointerDown;
    event.pointer_kind = PointerKind::Touch;
    event.device = self.touch_device_;
    event.pointer = pointer;
    event.position = pos;
    event.sample_time = self.native_time(time);
    event.activation = self.activation(serial);
    self.emit(rstd::move(event));
}

void WaylandInput::touch_up(void *data, ffi::wl_touch *, rstd::uint32_t serial, rstd::uint32_t time, rstd::int32_t id) {
    auto &self = *static_cast<WaylandInput *>(data);
    auto contact = self.contact(id);
    if (!contact)
        return;
    InputEvent event;
    event.type = EventType::PointerUp;
    event.pointer_kind = PointerKind::Touch;
    event.device = self.touch_device_;
    event.pointer = contact->pointer;
    event.position = contact->position;
    event.sample_time = self.native_time(time);
    event.activation = self.activation(serial);
    self.emit(rstd::move(event));
    self.contacts_.retain([&](const Contact &item) { return item.native != id; });
}

void WaylandInput::touch_motion(void *data, ffi::wl_touch *, rstd::uint32_t time, rstd::int32_t id, ffi::wl_fixed_t x,
                                ffi::wl_fixed_t y) {
    auto &self = *static_cast<WaylandInput *>(data);
    auto contact = self.contact(id);
    if (!contact)
        return;
    contact->position = {ffi::wl_fixed_to_double(x), ffi::wl_fixed_to_double(y)};
    InputEvent event;
    event.type = EventType::PointerMove;
    event.pointer_kind = PointerKind::Touch;
    event.device = self.touch_device_;
    event.pointer = contact->pointer;
    event.position = contact->position;
    event.sample_time = self.native_time(time);
    self.emit(rstd::move(event));
}

auto WaylandInput::keyboard_text(rstd::uint32_t code, bool feed_compose, bool &composed) -> TextInput {
    composed = false;
    if (!keyboard_focused_ || !key_state_ || text_.session().is_none())
        return {};
    if (text_.composing() || modifiers_.control || modifiers_.alt || modifiers_.meta) {
        reset_compose();
        return {};
    }
    auto symbol = xkb().xkb_state_key_get_one_sym(key_state_, code + 8);
    if (feed_compose) {
        auto status = compose_.feed(symbol);
        if (status == ComposeResult::Pending)
            return {};
        composed = status == ComposeResult::Composed;
    }
    char bytes[4096]{};
    int size = composed ? compose_.utf8(bytes, sizeof(bytes))
                        : xkb().xkb_state_key_get_utf8(key_state_, code + 8, bytes, sizeof(bytes));
    if (size <= 0 || size >= int(sizeof(bytes)))
        return {};
    bool invalid{};
    auto result = keyboard_text_from_utf8(bytes, size, invalid);
    if (invalid)
        ++dropped_;
    return result;
}

void WaylandInput::emit_keyboard_text(const TextInput &text, Option<Serial> session, Option<SampleTime> time) {
    if (!text.present() || session.is_none() || modifiers_.control || modifiers_.alt || modifiers_.meta)
        return;
    InputEvent event;
    event.type = EventType::TextInput;
    event.text = text;
    event.text_session = *session;
    if (time.is_some())
        event.sample_time = *time;
    emit(rstd::move(event));
}

void WaylandInput::keyboard_keymap(void *data, ffi::wl_keyboard *, rstd::uint32_t format, rstd::int32_t fd,
                                   rstd::uint32_t size) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (format != ffi::WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !size || size > 16 * 1024 * 1024) {
        ffi::close(fd);
        return;
    }
    if (!self.load()) {
        ffi::close(fd);
        self.failed_ = true;
        return;
    }
    void *mapping = ffi::mmap(nullptr, size, ffi::PROT_READ, ffi::MAP_PRIVATE, fd, 0);
    ffi::close(fd);
    if (ffi::map_failed(mapping)) {
        self.failed_ = true;
        return;
    }
    if (!self.key_context_)
        self.key_context_ = self.xkb().xkb_context_new(ffi::XKB_CONTEXT_NO_FLAGS);
    auto keymap =
        self.key_context_
            ? self.xkb().xkb_keymap_new_from_buffer(self.key_context_, static_cast<const char *>(mapping), size,
                                                    ffi::XKB_KEYMAP_FORMAT_TEXT_V1, ffi::XKB_KEYMAP_COMPILE_NO_FLAGS)
            : nullptr;
    ffi::munmap(mapping, size);
    if (!keymap) {
        self.failed_ = true;
        return;
    }
    auto state = self.xkb().xkb_state_new(keymap);
    if (!state) {
        self.xkb().xkb_keymap_unref(keymap);
        self.failed_ = true;
        return;
    }
    if (self.key_state_)
        self.xkb().xkb_state_unref(self.key_state_);
    if (self.keymap_)
        self.xkb().xkb_keymap_unref(self.keymap_);
    self.keymap_ = keymap;
    self.key_state_ = state;
    self.repeat_at_ = None();
    self.modifiers_ = {};
    self.repeat_text_ = TextInput{};
    self.repeat_text_session_ = None();
    self.key_group_ = 0;
    self.create_compose();
}

void WaylandInput::keyboard_enter(void *data, ffi::wl_keyboard *, rstd::uint32_t, ffi::wl_surface *surface,
                                  ffi::wl_array *) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (!surface || surface != self.surface_)
        return;
    self.keyboard_focused_ = true;
    InputEvent event;
    event.type = EventType::WindowFocusGained;
    self.emit(rstd::move(event));
}

void WaylandInput::keyboard_leave(void *data, ffi::wl_keyboard *, rstd::uint32_t, ffi::wl_surface *surface) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (!self.keyboard_focused_ || surface != self.surface_)
        return;
    self.repeat_at_ = None();
    self.keyboard_focused_ = false;
    self.queued_activations_.clear();
    self.delivered_activations_.clear();
    self.repeat_text_ = TextInput{};
    self.repeat_text_session_ = None();
    self.reset_compose();
    InputEvent event;
    event.type = EventType::WindowFocusLost;
    self.emit(rstd::move(event));
    self.clipboard_.lost_focus();
    self.modifiers_ = {};
}

void WaylandInput::emit_key(rstd::uint32_t code, bool pressed, bool repeat, Serial token, Option<SampleTime> time) {
    InputEvent event;
    event.type = pressed ? EventType::KeyDown : EventType::KeyUp;
    event.repeat = repeat;
    event.activation = token;
    if (time.is_some())
        event.sample_time = *time;
    if (key_state_)
        event.key = key_from_symbol(xkb().xkb_state_key_get_one_sym(key_state_, code + 8));
    emit(rstd::move(event));
}

void WaylandInput::keyboard_key(void *data, ffi::wl_keyboard *, rstd::uint32_t serial, rstd::uint32_t time,
                                rstd::uint32_t code, rstd::uint32_t state) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (!self.keyboard_focused_ || code > ~rstd::uint32_t{} - 8 ||
        (state != ffi::WL_KEYBOARD_KEY_STATE_PRESSED && state != ffi::WL_KEYBOARD_KEY_STATE_RELEASED))
        return;
    bool pressed = state == ffi::WL_KEYBOARD_KEY_STATE_PRESSED;
    auto sample = self.native_time(time);
    self.emit_key(code, pressed, false, self.activation(serial), Some(sample));
    bool composed{};
    TextInput text;
    auto text_session = self.text_.session();
    if (pressed) {
        text = self.keyboard_text(code, true, composed);
        self.emit_keyboard_text(text, text_session, Some(sample));
    }
    if (pressed && self.keymap_ && self.repeat_rate_ > 0 && self.xkb().xkb_keymap_key_repeats(self.keymap_, code + 8)) {
        self.repeat_key_ = code;
        self.repeat_text_ = rstd::move(text);
        self.repeat_text_session_ = text_session;
        self.repeat_composed_ = composed;
        auto now = now_ns();
        auto delay = Serial(self.repeat_delay_ > 0 ? self.repeat_delay_ : 0) * 1'000'000;
        self.repeat_at_ = Some(delay > ~Serial{} - now ? ~Serial{} : now + delay);
    } else if (!pressed && self.repeat_at_.is_some() && self.repeat_key_ == code) {
        self.repeat_at_ = None();
        self.repeat_text_ = TextInput{};
        self.repeat_text_session_ = None();
    }
}

void WaylandInput::keyboard_modifiers(void *data, ffi::wl_keyboard *, rstd::uint32_t, rstd::uint32_t depressed,
                                      rstd::uint32_t latched, rstd::uint32_t locked, rstd::uint32_t group) {
    auto &self = *static_cast<WaylandInput *>(data);
    if (!self.key_state_)
        return;
    self.xkb().xkb_state_update_mask(self.key_state_, depressed, latched, locked, 0, 0, group);
    auto active = [&](const char *name) {
        return self.xkb().xkb_state_mod_name_is_active(self.key_state_, name, ffi::XKB_STATE_MODS_EFFECTIVE) > 0;
    };
    self.modifiers_ = {active(ffi::XKB_MOD_NAME_SHIFT), active(ffi::XKB_MOD_NAME_CTRL), active(ffi::XKB_MOD_NAME_ALT),
                       active(ffi::XKB_MOD_NAME_LOGO)};
    if (group != self.key_group_) {
        self.key_group_ = group;
        self.reset_compose();
    }
    if (self.repeat_at_.is_some() && !self.repeat_composed_ && self.repeat_text_session_ == self.text_.session()) {
        bool composed{};
        self.repeat_text_ = self.keyboard_text(self.repeat_key_, false, composed);
    }
}

void WaylandInput::keyboard_repeat_info(void *data, ffi::wl_keyboard *, rstd::int32_t rate, rstd::int32_t delay) {
    auto &self = *static_cast<WaylandInput *>(data);
    self.repeat_rate_ = rate > 0 ? rate : 0;
    self.repeat_delay_ = delay > 0 ? delay : 0;
    if (!self.repeat_rate_)
        self.repeat_at_ = None();
}

auto WaylandInput::load() -> bool {
    if (xkb_api_.is_some())
        return true;
    auto loaded = ffi::XkbDispatch::open();
    if (loaded.is_err())
        return false;
    xkb_api_ = Some(rstd::move(loaded).unwrap());
    return true;
}

void WaylandInput::bind(ffi::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) {
    if (seat_)
        return;
    seat_ = static_cast<ffi::wl_seat *>(
        ffi::wl_registry_bind(registry, name, &ffi::wl_seat_interface, version < 8 ? version : 8));
    if (!seat_ || ffi::wl_seat_add_listener(seat_, &seat_listener_, this)) {
        failed_ = true;
        return;
    }
    seat_name_ = name;
    text_.set_seat(seat_);
    clipboard_.set_seat(seat_);
}

void WaylandInput::close_seat() {
    text_.set_seat(nullptr);
    release_pointer();
    release_touch();
    release_keyboard();
    clipboard_.set_seat(nullptr);
    if (seat_) {
        if (ffi::wl_seat_get_version(seat_) >= ffi::WL_SEAT_RELEASE_SINCE_VERSION)
            ffi::wl_seat_release(seat_);
        else
            ffi::wl_seat_destroy(seat_);
    }
    seat_ = nullptr;
    seat_name_ = 0;
}

auto WaylandInput::request_clipboard(Serial id) -> Result<empty, ClipboardError> {
    if (!keyboard_focused_)
        return Err(ClipboardError::Unavailable);
    if (!input_.can_reserve(clipboard_.pending()))
        return Err(ClipboardError::Limit);
    return clipboard_.request(id);
}

auto WaylandInput::request_clipboard_write(Serial id, Serial token, ref<str> text) -> Result<empty, ClipboardError> {
    if (!keyboard_focused_ || !token)
        return Err(ClipboardError::Unavailable);
    if (!input_.can_reserve(clipboard_.pending()))
        return Err(ClipboardError::Limit);
    for (const auto &entry : delivered_activations_)
        if (entry.token == token)
            return clipboard_.write(id, entry.serial, text);
    return Err(ClipboardError::Unavailable);
}

auto WaylandInput::set_text_input(Option<TextInputState> state) -> NativeTextInputResult {
    auto previous = text_.session();
    auto result = text_.set_state(rstd::move(state));
    if (previous != text_.session()) {
        reset_compose();
        repeat_text_ = TextInput{};
        repeat_text_session_ = None();
    }
    if (result == NativeTextInputResult::Unsupported && keyboard_ && key_state_ && text_.session().is_some())
        return NativeTextInputResult::Succeeded;
    return result;
}

auto WaylandInput::capture(bool enabled) const -> NativeCaptureResult {
    if (!enabled)
        return NativeCaptureResult::Succeeded;
    return pointer_ && pointer_focused_ && pointer_buttons_ ? NativeCaptureResult::Succeeded
                                                            : NativeCaptureResult::Unsupported;
}

auto WaylandInput::repeat_wait(Index milliseconds) const -> Index {
    if (repeat_at_.is_none())
        return milliseconds;
    auto now = now_ns();
    if (*repeat_at_ <= now)
        return 0;
    auto delta = *repeat_at_ - now;
    auto delay = delta / 1'000'000 + (delta % 1'000'000 != 0);
    return delay < milliseconds ? Index(delay) : milliseconds;
}

void WaylandInput::pump_repeat() {
    if (repeat_at_.is_none() || !keyboard_focused_ || repeat_rate_ <= 0)
        return;
    auto now = now_ns();
    auto interval = Serial(1'000'000'000) / Serial(repeat_rate_);
    if (!interval)
        interval = 1;
    unsigned count{};
    while (repeat_at_.is_some() && *repeat_at_ <= now && count++ < 32) {
        emit_key(repeat_key_, true, true);
        if (text_.composing()) {
            reset_compose();
            repeat_text_ = TextInput{};
            repeat_text_session_ = None();
        }
        if (repeat_text_session_ == text_.session())
            emit_keyboard_text(repeat_text_, repeat_text_session_);
        auto previous = *repeat_at_;
        repeat_at_ = previous > ~Serial{} - interval ? None<Serial>() : Some(previous + interval);
    }
    if (repeat_at_.is_some() && *repeat_at_ <= now) {
        auto skipped = (now - *repeat_at_) / interval + 1;
        dropped_ += rstd::min(skipped, ~Serial{} - dropped_);
        repeat_at_ = now > ~Serial{} - interval ? None<Serial>() : Some(now + interval);
    }
}
} // namespace vui::host::detail
