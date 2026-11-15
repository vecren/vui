module vui.host:platform.xkb.keys;
import :ffi.xkb;
import vui.render;

namespace vui::host::detail {
inline auto key_from_symbol(rstd::uint32_t symbol) -> Key {
    if (symbol >= 'a' && symbol <= 'z') return Key(unsigned(Key::A) + symbol - 'a');
    if (symbol >= 'A' && symbol <= 'Z') return Key(unsigned(Key::A) + symbol - 'A');
    switch (symbol) {
    case ffi::XKB_KEY_Tab: case ffi::XKB_KEY_ISO_Left_Tab: return Key::Tab;
    case ffi::XKB_KEY_Return: case ffi::XKB_KEY_KP_Enter: return Key::Enter;
    case ffi::XKB_KEY_space: return Key::Space;
    case ffi::XKB_KEY_Escape: return Key::Escape;
    case ffi::XKB_KEY_Left: return Key::Left;
    case ffi::XKB_KEY_Right: return Key::Right;
    case ffi::XKB_KEY_Up: return Key::Up;
    case ffi::XKB_KEY_Down: return Key::Down;
    case ffi::XKB_KEY_Home: return Key::Home;
    case ffi::XKB_KEY_End: return Key::End;
    case ffi::XKB_KEY_Page_Up: return Key::PageUp;
    case ffi::XKB_KEY_Page_Down: return Key::PageDown;
    case ffi::XKB_KEY_BackSpace: return Key::Backspace;
    case ffi::XKB_KEY_Delete: return Key::Delete;
    default: return Key::Unknown;
    }
}
} // namespace vui::host::detail
