module vui.host:platform.keys;
import :ffi.xkb;
import vui.render;

namespace vui::host::detail {
inline auto key_from_symbol(rstd::uint32_t symbol) -> Key {
    namespace xkb = ffi::xkb;
    if (symbol >= 'a' && symbol <= 'z') return Key(unsigned(Key::A) + symbol - 'a');
    if (symbol >= 'A' && symbol <= 'Z') return Key(unsigned(Key::A) + symbol - 'A');
    switch (symbol) {
    case xkb::tab: case xkb::iso_tab: return Key::Tab;
    case xkb::enter: case xkb::kp_enter: return Key::Enter;
    case xkb::space: return Key::Space;
    case xkb::escape: return Key::Escape;
    case xkb::left: return Key::Left;
    case xkb::right: return Key::Right;
    case xkb::up: return Key::Up;
    case xkb::down: return Key::Down;
    case xkb::home: return Key::Home;
    case xkb::end: return Key::End;
    case xkb::page_up: return Key::PageUp;
    case xkb::page_down: return Key::PageDown;
    case xkb::backspace: return Key::Backspace;
    case xkb::del: return Key::Delete;
    default: return Key::Unknown;
    }
}
} // namespace vui::host::detail
