module;
#include "../../../ffi/wayland_private.hpp"
extern "C" {
#include <text-input-v3-client-protocol.h>
#define types vui_text_input_v3_types
#include <text-input-v3-protocol.inc>
#undef types
}
#include "../../../ffi/wayland_undef.hpp"

module vui.host;
import :platform.wayland.protocol.text_input;

namespace ffi {
auto get_text_input(::zwp_text_input_manager_v3 *m, ::wl_seat *s)
    -> decltype(::zwp_text_input_manager_v3_get_text_input(m, s)) {
    return ::zwp_text_input_manager_v3_get_text_input(m, s);
}
void manager_destroy(::zwp_text_input_manager_v3 *m) { ::zwp_text_input_manager_v3_destroy(m); }
auto add_listener(::zwp_text_input_v3 *t, const ::zwp_text_input_v3_listener *l, void *p)
    -> decltype(::zwp_text_input_v3_add_listener(t, l, p)) {
    return ::zwp_text_input_v3_add_listener(t, l, p);
}
void destroy(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_destroy(t); }
void enable(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_enable(t); }
void disable(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_disable(t); }
void surrounding(::zwp_text_input_v3 *t, const char *s, int32_t cursor, int32_t anchor) {
    ::zwp_text_input_v3_set_surrounding_text(t, s, cursor, anchor);
}
void cause(::zwp_text_input_v3 *t, uint32_t value) { ::zwp_text_input_v3_set_text_change_cause(t, value); }
void rectangle(::zwp_text_input_v3 *t, int32_t x, int32_t y, int32_t w, int32_t h) {
    ::zwp_text_input_v3_set_cursor_rectangle(t, x, y, w, h);
}
void commit(::zwp_text_input_v3 *t) { ::zwp_text_input_v3_commit(t); }
} // namespace ffi
