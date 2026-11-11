module;
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace vui_xkb_constants {
inline constexpr auto tab = XKB_KEY_Tab, enter = XKB_KEY_Return, kp_enter = XKB_KEY_KP_Enter,
    space = XKB_KEY_space, escape = XKB_KEY_Escape, left = XKB_KEY_Left, right = XKB_KEY_Right,
    up = XKB_KEY_Up, down = XKB_KEY_Down, home = XKB_KEY_Home, end = XKB_KEY_End,
    page_up = XKB_KEY_Page_Up, page_down = XKB_KEY_Page_Down, backspace = XKB_KEY_BackSpace,
    del = XKB_KEY_Delete, iso_tab = XKB_KEY_ISO_Left_Tab;
inline constexpr auto shift = XKB_MOD_NAME_SHIFT, control = XKB_MOD_NAME_CTRL,
    alt = XKB_MOD_NAME_ALT, meta = XKB_MOD_NAME_LOGO;
}

export module vui.host:ffi.xkb;

export namespace vui::host::ffi::xkb {
using ::xkb_context;
using ::xkb_context_flags;
using ::xkb_rule_names;
using ::xkb_keymap;
using ::xkb_state;
using ::xkb_compose_table;
using ::xkb_compose_state;
using ::xkb_context_new;
using ::xkb_context_unref;
using ::xkb_keymap_new_from_string;
using ::xkb_keymap_new_from_names;
using ::xkb_keymap_new_from_buffer;
using ::xkb_keymap_unref;
using ::xkb_keymap_key_repeats;
using ::xkb_keymap_mod_get_index;
using ::xkb_state_new;
using ::xkb_state_unref;
using ::xkb_state_update_mask;
using ::xkb_state_key_get_one_sym;
using ::xkb_state_key_get_utf8;
using ::xkb_keysym_to_utf8;
using ::xkb_state_mod_name_is_active;
using ::XKB_CONTEXT_NO_FLAGS;
using ::XKB_KEYMAP_FORMAT_TEXT_V1;
using ::XKB_KEYMAP_COMPILE_NO_FLAGS;
using ::XKB_STATE_MODS_EFFECTIVE;
using ::XKB_COMPOSE_COMPILE_NO_FLAGS;
using ::XKB_COMPOSE_STATE_NO_FLAGS;
using ::XKB_COMPOSE_FEED_ACCEPTED;
using ::XKB_COMPOSE_COMPOSING;
using ::XKB_COMPOSE_COMPOSED;
using ::XKB_COMPOSE_CANCELLED;
using ::vui_xkb_constants::tab;
using ::vui_xkb_constants::iso_tab;
using ::vui_xkb_constants::enter;
using ::vui_xkb_constants::kp_enter;
using ::vui_xkb_constants::space;
using ::vui_xkb_constants::escape;
using ::vui_xkb_constants::left;
using ::vui_xkb_constants::right;
using ::vui_xkb_constants::up;
using ::vui_xkb_constants::down;
using ::vui_xkb_constants::home;
using ::vui_xkb_constants::end;
using ::vui_xkb_constants::page_up;
using ::vui_xkb_constants::page_down;
using ::vui_xkb_constants::backspace;
using ::vui_xkb_constants::del;
using ::vui_xkb_constants::shift;
using ::vui_xkb_constants::control;
using ::vui_xkb_constants::alt;
using ::vui_xkb_constants::meta;
using ::xkb_compose_table_new_from_locale;
using ::xkb_compose_table_unref;
using ::xkb_compose_state_new;
using ::xkb_compose_state_unref;
using ::xkb_compose_state_feed;
using ::xkb_compose_state_get_status;
using ::xkb_compose_state_get_utf8;
using ::xkb_compose_state_reset;
} // namespace vui::host::ffi::xkb
