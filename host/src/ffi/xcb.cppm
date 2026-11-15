module;
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <stdlib.h>

inline constexpr auto alias_XCB_CONFIGURE_NOTIFY = XCB_CONFIGURE_NOTIFY;
inline constexpr auto alias_XCB_COPY_FROM_PARENT = XCB_COPY_FROM_PARENT;
inline constexpr auto alias_XCB_CURRENT_TIME = XCB_CURRENT_TIME;
inline constexpr auto alias_XCB_DESTROY_NOTIFY = XCB_DESTROY_NOTIFY;
inline constexpr auto alias_XCB_NONE = XCB_NONE;
inline constexpr auto alias_XCB_PROPERTY_NOTIFY = XCB_PROPERTY_NOTIFY;
inline constexpr auto alias_XCB_REPARENT_NOTIFY = XCB_REPARENT_NOTIFY;
inline constexpr auto alias_XCB_SELECTION_CLEAR = XCB_SELECTION_CLEAR;
inline constexpr auto alias_XCB_SELECTION_NOTIFY = XCB_SELECTION_NOTIFY;
inline constexpr auto alias_XCB_SELECTION_REQUEST = XCB_SELECTION_REQUEST;

#undef XCB_CONFIGURE_NOTIFY
#undef XCB_COPY_FROM_PARENT
#undef XCB_CURRENT_TIME
#undef XCB_DESTROY_NOTIFY
#undef XCB_NONE
#undef XCB_PROPERTY_NOTIFY
#undef XCB_REPARENT_NOTIFY
#undef XCB_SELECTION_CLEAR
#undef XCB_SELECTION_NOTIFY
#undef XCB_SELECTION_REQUEST

module vui.host:ffi.xcb;
namespace ffi {
using ::xcb_connection_t;
using ::xcb_window_t;
using ::xcb_atom_t;
using ::xcb_timestamp_t;
using ::xcb_generic_event_t;
using ::xcb_generic_error_t;
using ::xcb_get_property_reply_t;
using ::xcb_get_geometry_reply_t;
using ::xcb_translate_coordinates_reply_t;
using ::xcb_selection_clear_event_t;
using ::xcb_selection_request_event_t;
using ::xcb_selection_notify_event_t;
using ::xcb_property_notify_event_t;
using ::xcb_destroy_notify_event_t;
using ::xcb_connect;
using ::xcb_disconnect;
using ::xcb_connection_has_error;
using ::xcb_get_file_descriptor;
using ::xcb_get_setup;
using ::xcb_setup_roots_iterator;
using ::xcb_screen_next;
using ::xcb_generate_id;
using ::xcb_create_window;
using ::xcb_destroy_window;
using ::xcb_change_window_attributes;
using ::xcb_intern_atom;
using ::xcb_intern_atom_reply;
using ::xcb_set_selection_owner;
using ::xcb_get_selection_owner;
using ::xcb_get_selection_owner_reply;
using ::xcb_change_property;
using ::xcb_get_property;
using ::xcb_get_property_reply;
using ::xcb_get_property_value;
using ::xcb_get_property_value_length;
using ::xcb_get_geometry;
using ::xcb_translate_coordinates;
using ::xcb_get_extension_data;
using ::xcb_send_event;
using ::xcb_get_input_focus;
using ::xcb_get_input_focus_reply;
using ::xcb_poll_for_event;
using ::xcb_poll_for_reply;
using ::xcb_flush;
using ::free;
using ::XCB_WINDOW_CLASS_INPUT_OUTPUT;
using ::XCB_CW_EVENT_MASK;
using ::XCB_EVENT_MASK_PROPERTY_CHANGE;
using ::XCB_EVENT_MASK_STRUCTURE_NOTIFY;
using ::XCB_ATOM_ATOM;
using ::XCB_ATOM_INTEGER;
using ::XCB_ATOM_CARDINAL;
using ::XCB_ATOM_STRING;
using ::XCB_PROP_MODE_REPLACE;
using ::XCB_PROPERTY_NEW_VALUE;
using ::XCB_PROPERTY_DELETE;

inline constexpr auto XCB_CONFIGURE_NOTIFY = alias_XCB_CONFIGURE_NOTIFY;
inline constexpr auto XCB_COPY_FROM_PARENT = alias_XCB_COPY_FROM_PARENT;
inline constexpr auto XCB_CURRENT_TIME = alias_XCB_CURRENT_TIME;
inline constexpr auto XCB_DESTROY_NOTIFY = alias_XCB_DESTROY_NOTIFY;
inline constexpr auto XCB_NONE = alias_XCB_NONE;
inline constexpr auto XCB_PROPERTY_NOTIFY = alias_XCB_PROPERTY_NOTIFY;
inline constexpr auto XCB_REPARENT_NOTIFY = alias_XCB_REPARENT_NOTIFY;
inline constexpr auto XCB_SELECTION_CLEAR = alias_XCB_SELECTION_CLEAR;
inline constexpr auto XCB_SELECTION_NOTIFY = alias_XCB_SELECTION_NOTIFY;
inline constexpr auto XCB_SELECTION_REQUEST = alias_XCB_SELECTION_REQUEST;
}
