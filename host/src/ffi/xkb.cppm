module;
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

export module vui.host:ffi.xkb;

export namespace vui::host::ffi::xkb {
using ::xkb_context;
using ::xkb_context_flags;
using ::xkb_keymap;
using ::xkb_state;
using ::xkb_compose_table;
using ::xkb_compose_state;
using ::xkb_context_new;
using ::xkb_context_unref;
using ::xkb_keymap_new_from_string;
using ::xkb_keymap_unref;
using ::xkb_state_new;
using ::xkb_state_unref;
using ::xkb_state_update_mask;
using ::xkb_state_key_get_one_sym;
using ::xkb_state_key_get_utf8;
using ::xkb_compose_table_new_from_locale;
using ::xkb_compose_table_unref;
using ::xkb_compose_state_new;
using ::xkb_compose_state_unref;
using ::xkb_compose_state_feed;
using ::xkb_compose_state_get_status;
using ::xkb_compose_state_get_utf8;
using ::xkb_compose_state_reset;
} // namespace vui::host::ffi::xkb
