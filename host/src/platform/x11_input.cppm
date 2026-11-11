module vui.host:platform.x11_input;
import :ffi.x11;
import :ffi.xi2;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
namespace xi = ffi::xi2;
namespace x11 = ffi::x11;

class X11Input {
    friend struct X11InputTest;
    void *sink_{};
    void (*emit_)(void *, InputEvent){};
    void (*drop_)(void *){};
    x11::Window window_{};
    int opcode_{};
    bool enabled_{};
    struct Device { int native{}; DeviceId id{}; };
    struct Contact { int source{}, native{}; DeviceId device{}; Index pointer{}; };
    struct PointerButtons { int master{}; unsigned buttons{}; };
    Vec<Device> devices_;
    Vec<Contact> contacts_;
    Vec<PointerButtons> pointer_buttons_;
    bool pointer_buttons_unknown_{};
    Index next_contact_{1};

    static auto modifiers(unsigned state) -> Modifiers {
        return {bool(state & x11::shift_mask), bool(state & x11::control_mask),
                bool(state & x11::alt_mask), bool(state & x11::meta_mask)};
    }
    auto device(int native) -> DeviceId {
        for (auto item : devices_) if (item.native == native) return item.id;
        if (native <= 0 || devices_.len() >= usize(256)) return 0;
        auto id = new_session(); devices_.push(Device{native, id}); return id;
    }
    auto contact(int source, int native) -> Contact * {
        for (auto &item : contacts_) if (item.source == source && item.native == native) return &item;
        return nullptr;
    }
    void remove_device(int native) {
        for (auto item : devices_) if (item.native == native) {
            InputEvent event; event.type = EventType::DeviceLost; event.device = item.id;
            emit_(sink_, rstd::move(event));
            break;
        }
        contacts_.retain([&](const Contact &item) { return item.source != native; });
        devices_.retain([&](const Device &item) { return item.native != native; });
    }
    void touch(const xi::XIDeviceEvent &native) {
        if (native.event != window_ || !finite(native.event_x) || !finite(native.event_y)) return;
        auto current = contact(native.sourceid, native.detail);
        if (native.evtype == xi::touch_begin) {
            if (current || contacts_.len() >= usize(32) || next_contact_ == ~Index{}) { drop_(sink_); return; }
            auto id = device(native.sourceid);
            if (!id) { drop_(sink_); return; }
            contacts_.push(Contact{native.sourceid, native.detail, id, next_contact_++});
            current = &contacts_[contacts_.len() - usize(1)];
        } else if (!current) return;
        InputEvent event;
        event.type = native.evtype == xi::touch_begin ? EventType::PointerDown
                     : native.evtype == xi::touch_end ? EventType::PointerUp : EventType::PointerMove;
        event.pointer_kind = PointerKind::Touch; event.device = current->device; event.pointer = current->pointer;
        event.position = {native.event_x, native.event_y}; event.modifiers = modifiers(native.mods.effective);
        emit_(sink_, rstd::move(event));
        if (native.evtype == xi::touch_end)
            contacts_.retain([&](const Contact &item) { return item.source != native.sourceid || item.native != native.detail; });
    }
    void pointer(const xi::XIDeviceEvent &native) {
        if (native.event != window_ || !finite(native.event_x) || !finite(native.event_y)) return;
        if ((native.flags & xi::pointer_emulated) &&
            (native.evtype == xi::motion || (native.detail >= 1 && native.detail <= 3))) return;
        InputEvent event; event.position = {native.event_x, native.event_y};
        event.modifiers = modifiers(native.mods.effective);
        if (native.evtype == xi::motion) event.type = EventType::PointerMove;
        else if (native.detail >= 4 && native.detail <= 7) {
            if (native.evtype == xi::button_release) return;
            event.type = EventType::Wheel; event.scroll_unit = ScrollUnit::Lines;
            event.scroll_delta = native.detail == 4 ? Point{0, -1} : native.detail == 5 ? Point{0, 1}
                                 : native.detail == 6 ? Point{-1, 0} : Point{1, 0};
        } else {
            event.button = native.detail == 1 ? PointerButton::Primary : native.detail == 2 ? PointerButton::Middle
                           : native.detail == 3 ? PointerButton::Secondary : native.detail == 8 ? PointerButton::Back
                           : native.detail == 9 ? PointerButton::Forward : PointerButton::None;
            if (event.button == PointerButton::None) return;
            event.type = native.evtype == xi::button_press ? EventType::PointerDown : EventType::PointerUp;
            if (!native.send_event && native.deviceid > 0) {
                PointerButtons *buttons{};
                for (auto &item : pointer_buttons_)
                    if (item.master == native.deviceid) { buttons = &item; break; }
                if (!buttons && native.evtype == xi::button_press) {
                    if (pointer_buttons_.len() >= usize(32)) pointer_buttons_unknown_ = true;
                    else {
                        pointer_buttons_.push({native.deviceid, 0});
                        buttons = &pointer_buttons_[pointer_buttons_.len() - usize(1)];
                    }
                }
                if (buttons) {
                    auto mask = 1u << unsigned(native.detail);
                    if (native.evtype == xi::button_press) buttons->buttons |= mask;
                    else buttons->buttons &= ~mask;
                }
                pointer_buttons_.retain([](const PointerButtons &item) { return item.buttons != 0; });
            }
        }
        emit_(sink_, rstd::move(event));
    }
  public:
    X11Input(void *sink, void (*emit)(void *, InputEvent), void (*drop)(void *)) : sink_(sink), emit_(emit), drop_(drop) {}
    X11Input(const X11Input &) = delete;
    auto operator=(const X11Input &) -> X11Input & = delete;
    void open(x11::Display *display, x11::Window window, x11::Window root) {
        window_ = window; opcode_ = 0; enabled_ = false;
        int event_base{}, error_base{};
        if (!x11::XQueryExtension(display, "XInputExtension", &opcode_, &event_base, &error_base)) return;
        int major = 2, minor = 2;
        if (xi::XIQueryVersion(display, &major, &minor) != x11::success || major < 2 || (major == 2 && minor < 2)) return;
        unsigned char bits[xi::mask_size]{};
        const int events[]{xi::motion, xi::button_press, xi::button_release, xi::touch_begin, xi::touch_update, xi::touch_end};
        for (auto event : events) xi::set_mask(bits, event);
        xi::XIEventMask selected{xi::all_masters, int(sizeof(bits)), bits};
        if (xi::XISelectEvents(display, window, &selected, 1) != x11::success) return;
        unsigned char hierarchy[xi::mask_size]{};
        xi::set_mask(hierarchy, xi::hierarchy_changed);
        xi::XIEventMask changes{xi::all_devices, int(sizeof(hierarchy)), hierarchy};
        if (xi::XISelectEvents(display, root, &changes, 1) != x11::success) {
            unsigned char none[xi::mask_size]{}; selected.mask = none;
            (void)xi::XISelectEvents(display, window, &selected, 1);
            return;
        }
        enabled_ = true;
    }
    void event(int type, const void *data) {
        if (!enabled_ || !data) return;
        if (type == xi::hierarchy_changed) {
            auto &change = *static_cast<const xi::XIHierarchyEvent *>(data);
            if (!change.info) return;
            for (int i{}; i < change.num_info; ++i)
                if (change.info[i].flags & (xi::slave_removed | xi::master_removed | xi::slave_detached | xi::device_disabled)) {
                    remove_device(change.info[i].deviceid);
                    clear_pointer_buttons();
                }
        } else if (type == xi::touch_begin || type == xi::touch_update || type == xi::touch_end)
            touch(*static_cast<const xi::XIDeviceEvent *>(data));
        else if (type == xi::motion || type == xi::button_press || type == xi::button_release)
            pointer(*static_cast<const xi::XIDeviceEvent *>(data));
    }
    void clear_contacts() { contacts_.clear(); }
    void clear_pointer_buttons() { pointer_buttons_.clear(); pointer_buttons_unknown_ = false; }
    auto has_implicit_pointer_capture() const -> bool {
        return enabled_ && !pointer_buttons_unknown_ && pointer_buttons_.len() == usize(1);
    }
    void close() { enabled_ = false; window_ = 0; contacts_.clear(); devices_.clear(); clear_pointer_buttons(); }
    auto enabled() const -> bool { return enabled_; }
    auto opcode() const -> int { return opcode_; }
};
}
