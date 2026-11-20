module;
#include "../../../ffi/wayland_private.hpp"
extern "C" {
#include <text-input-v3-client-protocol.h>
}

#include "../../../ffi/wayland_undef.hpp"

module vui.host:platform.wayland.protocol.text_input;
namespace ffi {
using ::zwp_text_input_manager_v3;
inline const auto &zwp_text_input_manager_v3_interface = ::vui_zwp_text_input_manager_v3_interface;
using ::zwp_text_input_v3;
using ::ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD;
using ::ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER;
using ::zwp_text_input_v3_listener;

auto get_text_input(::zwp_text_input_manager_v3 *m, ::wl_seat *s)
    -> decltype(::zwp_text_input_manager_v3_get_text_input(m, s));
void manager_destroy(::zwp_text_input_manager_v3 *m);
auto add_listener(::zwp_text_input_v3 *t, const ::zwp_text_input_v3_listener *l, void *p)
    -> decltype(::zwp_text_input_v3_add_listener(t, l, p));
void destroy(::zwp_text_input_v3 *t);
void enable(::zwp_text_input_v3 *t);
void disable(::zwp_text_input_v3 *t);
void surrounding(::zwp_text_input_v3 *t, const char *s, int32_t cursor, int32_t anchor);
void cause(::zwp_text_input_v3 *t, uint32_t value);
void rectangle(::zwp_text_input_v3 *t, int32_t x, int32_t y, int32_t w, int32_t h);
void commit(::zwp_text_input_v3 *t);
} // namespace ffi
