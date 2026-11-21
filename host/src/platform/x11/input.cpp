module vui.host;
import :platform.x11.input;
import :platform.x11.ffi.xlib;
import :platform.x11.ffi.xi2;
import :platform.input.clock;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;

void X11Input::stamp(InputEvent &event, rstd::uint32_t value) {
    auto sample = clock_.sample(value);
    if (sample.discontinuity) { InputEvent reset; reset.type = EventType::InputReset; emit_(sink_, rstd::move(reset)); }
    event.sample_time = sample.time;
}

auto X11Input::device(int native) -> DeviceId {
    for (auto item : devices_) if (item.native == native) return item.id;
    if (native <= 0 || devices_.len() >= usize(256)) return 0;
    auto id = new_session(); devices_.push(Device{native, id}); return id;
}

void X11Input::remove_device(int native) {
    for (auto item : devices_) if (item.native == native) {
        InputEvent event; event.type = EventType::DeviceLost; event.device = item.id;
        emit_(sink_, rstd::move(event));
        break;
    }
    contacts_.retain([&](const Contact &item) { return item.source != native; });
    devices_.retain([&](const Device &item) { return item.native != native; });
}

void X11Input::touch(const ffi::XIDeviceEvent &native) {
    if (native.event != window_ || !finite(native.event_x) || !finite(native.event_y)) return;
    auto current = contact(native.sourceid, native.detail);
    if (native.evtype == ffi::XI_TouchBegin) {
        if (current || contacts_.len() >= usize(32) || next_contact_ == ~Index{}) { drop_(sink_); return; }
        auto id = device(native.sourceid);
        if (!id) { drop_(sink_); return; }
        contacts_.push(Contact{native.sourceid, native.detail, id, next_contact_++});
        current = &contacts_[contacts_.len() - usize(1)];
    } else if (!current) return;
    InputEvent event;
    event.type = native.evtype == ffi::XI_TouchBegin ? EventType::PointerDown
                 : native.evtype == ffi::XI_TouchEnd ? EventType::PointerUp : EventType::PointerMove;
    event.pointer_kind = PointerKind::Touch; event.device = current->device; event.pointer = current->pointer;
    event.position = {native.event_x, native.event_y}; event.modifiers = modifiers(native.mods.effective);
    stamp(event, rstd::uint32_t(native.time));
    emit_(sink_, rstd::move(event));
    if (native.evtype == ffi::XI_TouchEnd)
        contacts_.retain([&](const Contact &item) { return item.source != native.sourceid || item.native != native.detail; });
}

void X11Input::pointer(const ffi::XIDeviceEvent &native) {
    if (native.event != window_ || !finite(native.event_x) || !finite(native.event_y)) return;
    if ((native.flags & ffi::XIPointerEmulated) &&
        (native.evtype == ffi::XI_Motion || (native.detail >= 1 && native.detail <= 3))) return;
    InputEvent event; event.position = {native.event_x, native.event_y};
    event.modifiers = modifiers(native.mods.effective);
    if (native.evtype == ffi::XI_Motion) event.type = EventType::PointerMove;
    else if (native.detail >= 4 && native.detail <= 7) {
        if (native.evtype == ffi::XI_ButtonRelease) return;
        event.type = EventType::Wheel; event.scroll_unit = ScrollUnit::Lines;
        event.scroll_delta = native.detail == 4 ? Point{0, -1} : native.detail == 5 ? Point{0, 1}
                             : native.detail == 6 ? Point{-1, 0} : Point{1, 0};
    } else {
        event.button = native.detail == 1 ? PointerButton::Primary : native.detail == 2 ? PointerButton::Middle
                       : native.detail == 3 ? PointerButton::Secondary : native.detail == 8 ? PointerButton::Back
                       : native.detail == 9 ? PointerButton::Forward : PointerButton::None;
        if (event.button == PointerButton::None) return;
        event.type = native.evtype == ffi::XI_ButtonPress ? EventType::PointerDown : EventType::PointerUp;
        if (!native.send_event && native.deviceid > 0) {
            PointerButtons *buttons{};
            for (auto &item : pointer_buttons_)
                if (item.master == native.deviceid) { buttons = &item; break; }
            if (!buttons && native.evtype == ffi::XI_ButtonPress) {
                if (pointer_buttons_.len() >= usize(32)) pointer_buttons_unknown_ = true;
                else {
                    pointer_buttons_.push({native.deviceid, 0});
                    buttons = &pointer_buttons_[pointer_buttons_.len() - usize(1)];
                }
            }
            if (buttons) {
                auto mask = 1u << unsigned(native.detail);
                if (native.evtype == ffi::XI_ButtonPress) buttons->buttons |= mask;
                else buttons->buttons &= ~mask;
            }
            pointer_buttons_.retain([](const PointerButtons &item) { return item.buttons != 0; });
        }
    }
    stamp(event, rstd::uint32_t(native.time));
    emit_(sink_, rstd::move(event));
}

void X11Input::open(X11Connection &connection, ffi::Window window, ffi::Window root) {
    window_ = window; opcode_ = 0; enabled_ = false;
    auto display = connection.display();
    auto dispatch = connection.xi_dispatch();
    if (!dispatch) return;
    int event_base{}, error_base{};
    if (!connection.api().XQueryExtension(display, "XInputExtension", &opcode_, &event_base, &error_base)) return;
    int major = 2, minor = 2;
    if (dispatch->XIQueryVersion(display, &major, &minor) != ffi::Success || major < 2 || (major == 2 && minor < 2)) return;
    constexpr int mask_size = ffi::XIMaskLen(ffi::XI_TouchEnd);
    unsigned char bits[mask_size]{};
    const int events[]{ffi::XI_Motion, ffi::XI_ButtonPress, ffi::XI_ButtonRelease, ffi::XI_TouchBegin, ffi::XI_TouchUpdate, ffi::XI_TouchEnd};
    for (auto event : events) ffi::XISetMask(bits, event);
    ffi::XIEventMask selected{ffi::XIAllMasterDevices, int(sizeof(bits)), bits};
    if (dispatch->XISelectEvents(display, window, &selected, 1) != ffi::Success) return;
    unsigned char hierarchy[mask_size]{};
    ffi::XISetMask(hierarchy, ffi::XI_HierarchyChanged);
    ffi::XIEventMask changes{ffi::XIAllDevices, int(sizeof(hierarchy)), hierarchy};
    if (dispatch->XISelectEvents(display, root, &changes, 1) != ffi::Success) {
        unsigned char none[mask_size]{}; selected.mask = none;
        (void)dispatch->XISelectEvents(display, window, &selected, 1);
        return;
    }
    enabled_ = true;
}

void X11Input::event(int type, const void *data) {
    if (!enabled_ || !data) return;
    if (type == ffi::XI_HierarchyChanged) {
        auto &change = *static_cast<const ffi::XIHierarchyEvent *>(data);
        if (!change.info) return;
        for (int i{}; i < change.num_info; ++i)
            if (change.info[i].flags & (ffi::XISlaveRemoved | ffi::XIMasterRemoved | ffi::XISlaveDetached | ffi::XIDeviceDisabled)) {
                remove_device(change.info[i].deviceid);
                clear_pointer_buttons();
            }
    } else if (type == ffi::XI_TouchBegin || type == ffi::XI_TouchUpdate || type == ffi::XI_TouchEnd)
        touch(*static_cast<const ffi::XIDeviceEvent *>(data));
    else if (type == ffi::XI_Motion || type == ffi::XI_ButtonPress || type == ffi::XI_ButtonRelease)
        pointer(*static_cast<const ffi::XIDeviceEvent *>(data));
}
} // namespace vui::host::detail
