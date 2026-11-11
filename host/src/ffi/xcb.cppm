module;
#include <xcb/xcb.h>
#include <stdlib.h>

namespace vui_xcb_constants {
inline constexpr auto none = XCB_NONE, current_time = XCB_CURRENT_TIME, copy_from_parent = XCB_COPY_FROM_PARENT;
inline constexpr auto selection_clear = XCB_SELECTION_CLEAR, selection_request = XCB_SELECTION_REQUEST,
                      selection_notify = XCB_SELECTION_NOTIFY, property_notify = XCB_PROPERTY_NOTIFY,
                      destroy_notify = XCB_DESTROY_NOTIFY;
}

export module vui.host:ffi.xcb;
export namespace vui::host::ffi::xcb {
using ::xcb_connection_t;
using ::xcb_window_t;
using ::xcb_atom_t;
using ::xcb_timestamp_t;
using ::xcb_generic_event_t;
using ::xcb_generic_error_t;
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
using ::xcb_send_event;
using ::xcb_get_input_focus;
using ::xcb_get_input_focus_reply;
using ::xcb_poll_for_event;
using ::xcb_flush;
using ::free;
using ::vui_xcb_constants::none;
using ::vui_xcb_constants::current_time;
using ::vui_xcb_constants::copy_from_parent;
using ::XCB_WINDOW_CLASS_INPUT_OUTPUT;
using ::XCB_CW_EVENT_MASK;
using ::XCB_EVENT_MASK_PROPERTY_CHANGE;
using ::XCB_EVENT_MASK_STRUCTURE_NOTIFY;
using ::XCB_ATOM_ATOM;
using ::XCB_ATOM_INTEGER;
using ::XCB_ATOM_STRING;
using ::XCB_PROP_MODE_REPLACE;
using ::vui_xcb_constants::selection_clear;
using ::vui_xcb_constants::selection_request;
using ::vui_xcb_constants::selection_notify;
using ::vui_xcb_constants::property_notify;
using ::vui_xcb_constants::destroy_notify;
using ::XCB_PROPERTY_NEW_VALUE;
using ::XCB_PROPERTY_DELETE;
}
