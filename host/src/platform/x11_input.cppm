module vui.host:platform.x11_input;
import :ffi.x11;
import :ffi.xi2;
import :platform.input_clock;
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
    NativeInputClock &clock_;
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
    void stamp(InputEvent &event, rstd::uint32_t value);
    auto device(int native) -> DeviceId;
    auto contact(int source, int native) -> Contact * {
        for (auto &item : contacts_) if (item.source == source && item.native == native) return &item;
        return nullptr;
    }
    void remove_device(int native);
    void touch(const xi::XIDeviceEvent &native);
    void pointer(const xi::XIDeviceEvent &native);
  public:
    X11Input(void *sink, void (*emit)(void *, InputEvent), void (*drop)(void *), NativeInputClock &clock)
        : sink_(sink), emit_(emit), drop_(drop), clock_(clock) {}
    X11Input(const X11Input &) = delete;
    auto operator=(const X11Input &) -> X11Input & = delete;
    void open(x11::Display *display, x11::Window window, x11::Window root);
    void event(int type, const void *data);
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
