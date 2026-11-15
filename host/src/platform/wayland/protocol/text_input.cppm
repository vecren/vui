module;
extern "C" {
#include <text-input-v3-client-protocol.h>
#include <text-input-v3-protocol.inc>
}

module vui.host:platform.wayland.protocol.text_input;
namespace ffi {
using ::zwp_text_input_manager_v3;
using ::zwp_text_input_manager_v3_interface;
using ::zwp_text_input_v3;
using ::zwp_text_input_v3_listener;
using ::ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD;
using ::ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER;

inline auto get_text_input(::zwp_text_input_manager_v3 *m, ::wl_seat *s) { return ::zwp_text_input_manager_v3_get_text_input(m, s); }
inline void manager_destroy(::zwp_text_input_manager_v3 *m) { ::zwp_text_input_manager_v3_destroy(m); }
inline auto add_listener(::zwp_text_input_v3 *t, const ::zwp_text_input_v3_listener *l, void *p) { return ::zwp_text_input_v3_add_listener(t, l, p); }
inline void destroy(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_destroy(t); }
inline void enable(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_enable(t); }
inline void disable(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_disable(t); }
inline void surrounding(::zwp_text_input_v3 *t, const char *s, int32_t cursor, int32_t anchor) { ::zwp_text_input_v3_set_surrounding_text(t, s, cursor, anchor); }
inline void cause(::zwp_text_input_v3 *t, uint32_t value) { ::zwp_text_input_v3_set_text_change_cause(t, value); }
inline void rectangle(::zwp_text_input_v3 *t, int32_t x, int32_t y, int32_t w, int32_t h) { ::zwp_text_input_v3_set_cursor_rectangle(t, x, y, w, h); }
inline void commit(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_commit(t); }
}
