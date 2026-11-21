module;
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

inline constexpr auto alias_XKB_KEY_BackSpace = XKB_KEY_BackSpace;
inline constexpr auto alias_XKB_KEY_Delete = XKB_KEY_Delete;
inline constexpr auto alias_XKB_KEY_Down = XKB_KEY_Down;
inline constexpr auto alias_XKB_KEY_End = XKB_KEY_End;
inline constexpr auto alias_XKB_KEY_Escape = XKB_KEY_Escape;
inline constexpr auto alias_XKB_KEY_Home = XKB_KEY_Home;
inline constexpr auto alias_XKB_KEY_ISO_Left_Tab = XKB_KEY_ISO_Left_Tab;
inline constexpr auto alias_XKB_KEY_KP_Enter = XKB_KEY_KP_Enter;
inline constexpr auto alias_XKB_KEY_Left = XKB_KEY_Left;
inline constexpr auto alias_XKB_KEY_Page_Down = XKB_KEY_Page_Down;
inline constexpr auto alias_XKB_KEY_Page_Up = XKB_KEY_Page_Up;
inline constexpr auto alias_XKB_KEY_Return = XKB_KEY_Return;
inline constexpr auto alias_XKB_KEY_Right = XKB_KEY_Right;
inline constexpr auto alias_XKB_KEY_Tab = XKB_KEY_Tab;
inline constexpr auto alias_XKB_KEY_Up = XKB_KEY_Up;
inline constexpr auto alias_XKB_KEY_dead_acute = XKB_KEY_dead_acute;
inline constexpr auto alias_XKB_KEY_space = XKB_KEY_space;
inline constexpr auto alias_XKB_MOD_NAME_ALT = XKB_MOD_NAME_ALT;
inline constexpr auto alias_XKB_MOD_NAME_CTRL = XKB_MOD_NAME_CTRL;
inline constexpr auto alias_XKB_MOD_NAME_LOGO = XKB_MOD_NAME_LOGO;
inline constexpr auto alias_XKB_MOD_NAME_SHIFT = XKB_MOD_NAME_SHIFT;

#undef XKB_KEY_BackSpace
#undef XKB_KEY_Delete
#undef XKB_KEY_Down
#undef XKB_KEY_End
#undef XKB_KEY_Escape
#undef XKB_KEY_Home
#undef XKB_KEY_ISO_Left_Tab
#undef XKB_KEY_KP_Enter
#undef XKB_KEY_Left
#undef XKB_KEY_Page_Down
#undef XKB_KEY_Page_Up
#undef XKB_KEY_Return
#undef XKB_KEY_Right
#undef XKB_KEY_Tab
#undef XKB_KEY_Up
#undef XKB_KEY_dead_acute
#undef XKB_KEY_space
#undef XKB_MOD_NAME_ALT
#undef XKB_MOD_NAME_CTRL
#undef XKB_MOD_NAME_LOGO
#undef XKB_MOD_NAME_SHIFT

module vui.host:platform.xkb.ffi;
import rstd.dlopn;
namespace ffi {
using ::XKB_COMPOSE_CANCELLED;
using ::XKB_COMPOSE_COMPILE_NO_FLAGS;
using ::XKB_COMPOSE_COMPOSED;
using ::XKB_COMPOSE_COMPOSING;
using ::XKB_COMPOSE_FEED_ACCEPTED;
using ::xkb_compose_state;
using ::XKB_COMPOSE_STATE_NO_FLAGS;
using ::xkb_compose_table;
using ::xkb_context;
using ::xkb_context_flags;
using ::XKB_CONTEXT_NO_FLAGS;
using ::xkb_keymap;
using ::XKB_KEYMAP_COMPILE_NO_FLAGS;
using ::XKB_KEYMAP_FORMAT_TEXT_V1;
using ::xkb_rule_names;
using ::xkb_state;
using ::XKB_STATE_MODS_EFFECTIVE;

inline constexpr auto XKB_KEY_BackSpace = alias_XKB_KEY_BackSpace;
inline constexpr auto XKB_KEY_Delete = alias_XKB_KEY_Delete;
inline constexpr auto XKB_KEY_Down = alias_XKB_KEY_Down;
inline constexpr auto XKB_KEY_End = alias_XKB_KEY_End;
inline constexpr auto XKB_KEY_Escape = alias_XKB_KEY_Escape;
inline constexpr auto XKB_KEY_Home = alias_XKB_KEY_Home;
inline constexpr auto XKB_KEY_ISO_Left_Tab = alias_XKB_KEY_ISO_Left_Tab;
inline constexpr auto XKB_KEY_KP_Enter = alias_XKB_KEY_KP_Enter;
inline constexpr auto XKB_KEY_Left = alias_XKB_KEY_Left;
inline constexpr auto XKB_KEY_Page_Down = alias_XKB_KEY_Page_Down;
inline constexpr auto XKB_KEY_Page_Up = alias_XKB_KEY_Page_Up;
inline constexpr auto XKB_KEY_Return = alias_XKB_KEY_Return;
inline constexpr auto XKB_KEY_Right = alias_XKB_KEY_Right;
inline constexpr auto XKB_KEY_Tab = alias_XKB_KEY_Tab;
inline constexpr auto XKB_KEY_Up = alias_XKB_KEY_Up;
inline constexpr auto XKB_KEY_dead_acute = alias_XKB_KEY_dead_acute;
inline constexpr auto XKB_KEY_space = alias_XKB_KEY_space;
inline constexpr auto XKB_MOD_NAME_ALT = alias_XKB_MOD_NAME_ALT;
inline constexpr auto XKB_MOD_NAME_CTRL = alias_XKB_MOD_NAME_CTRL;
inline constexpr auto XKB_MOD_NAME_LOGO = alias_XKB_MOD_NAME_LOGO;
inline constexpr auto XKB_MOD_NAME_SHIFT = alias_XKB_MOD_NAME_SHIFT;
class XkbDispatch {
    rstd::dlopn::Library library_;
    explicit XkbDispatch(rstd::dlopn::Library library) : library_(rstd::move(library)) {}

  public:
    XkbDispatch(const XkbDispatch &) = delete;
    auto operator=(const XkbDispatch &) -> XkbDispatch & = delete;
    XkbDispatch(XkbDispatch &&) = default;
    auto operator=(XkbDispatch &&) -> XkbDispatch & = default;
    static auto open() -> rstd::Result<rstd::sync::Arc<XkbDispatch>, rstd::dlopn::Error>;
    decltype(&::xkb_context_new) xkb_context_new{};
    decltype(&::xkb_context_unref) xkb_context_unref{};
    decltype(&::xkb_keymap_new_from_string) xkb_keymap_new_from_string{};
    decltype(&::xkb_keymap_new_from_names) xkb_keymap_new_from_names{};
    decltype(&::xkb_keymap_new_from_buffer) xkb_keymap_new_from_buffer{};
    decltype(&::xkb_keymap_unref) xkb_keymap_unref{};
    decltype(&::xkb_keymap_key_repeats) xkb_keymap_key_repeats{};
    decltype(&::xkb_keymap_mod_get_index) xkb_keymap_mod_get_index{};
    decltype(&::xkb_state_new) xkb_state_new{};
    decltype(&::xkb_state_unref) xkb_state_unref{};
    decltype(&::xkb_state_update_mask) xkb_state_update_mask{};
    decltype(&::xkb_state_key_get_one_sym) xkb_state_key_get_one_sym{};
    decltype(&::xkb_state_key_get_utf8) xkb_state_key_get_utf8{};
    decltype(&::xkb_keysym_to_utf8) xkb_keysym_to_utf8{};
    decltype(&::xkb_state_mod_name_is_active) xkb_state_mod_name_is_active{};
    decltype(&::xkb_compose_table_new_from_locale) xkb_compose_table_new_from_locale{};
    decltype(&::xkb_compose_table_unref) xkb_compose_table_unref{};
    decltype(&::xkb_compose_state_new) xkb_compose_state_new{};
    decltype(&::xkb_compose_state_unref) xkb_compose_state_unref{};
    decltype(&::xkb_compose_state_feed) xkb_compose_state_feed{};
    decltype(&::xkb_compose_state_get_status) xkb_compose_state_get_status{};
    decltype(&::xkb_compose_state_get_utf8) xkb_compose_state_get_utf8{};
    decltype(&::xkb_compose_state_reset) xkb_compose_state_reset{};
};

} // namespace ffi
