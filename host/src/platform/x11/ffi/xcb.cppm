module;
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

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

module vui.host:platform.x11.ffi.xcb;
import rstd.dlopn;
namespace ffi {
using ::free;
using ::XCB_ATOM_ATOM;
using ::XCB_ATOM_CARDINAL;
using ::XCB_ATOM_INTEGER;
using ::XCB_ATOM_STRING;
using ::xcb_atom_t;
using ::xcb_connection_t;
using ::XCB_CW_EVENT_MASK;
using ::xcb_destroy_notify_event_t;
using ::XCB_EVENT_MASK_PROPERTY_CHANGE;
using ::XCB_EVENT_MASK_STRUCTURE_NOTIFY;
using ::xcb_generic_error_t;
using ::xcb_generic_event_t;
using ::xcb_get_geometry_reply_t;
using ::xcb_get_property_reply_t;
using ::XCB_PROP_MODE_REPLACE;
using ::XCB_PROPERTY_DELETE;
using ::XCB_PROPERTY_NEW_VALUE;
using ::xcb_property_notify_event_t;
using ::xcb_selection_clear_event_t;
using ::xcb_selection_notify_event_t;
using ::xcb_selection_request_event_t;
using ::xcb_timestamp_t;
using ::xcb_translate_coordinates_reply_t;
using ::XCB_WINDOW_CLASS_INPUT_OUTPUT;
using ::xcb_window_t;

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
class XcbDispatch {
    rstd::dlopn::Library library_;
    explicit XcbDispatch(rstd::dlopn::Library library) : library_(rstd::move(library)) {}

  public:
    XcbDispatch(const XcbDispatch &) = delete;
    XcbDispatch(XcbDispatch &&) = default;
    auto operator=(const XcbDispatch &) -> XcbDispatch & = delete;
    auto operator=(XcbDispatch &&) -> XcbDispatch & = default;
    static auto open() -> rstd::Option<XcbDispatch>;
    decltype(&::xcb_connect) xcb_connect{};
    decltype(&::xcb_disconnect) xcb_disconnect{};
    decltype(&::xcb_connection_has_error) xcb_connection_has_error{};
    decltype(&::xcb_get_file_descriptor) xcb_get_file_descriptor{};
    decltype(&::xcb_get_setup) xcb_get_setup{};
    decltype(&::xcb_setup_roots_iterator) xcb_setup_roots_iterator{};
    decltype(&::xcb_screen_next) xcb_screen_next{};
    decltype(&::xcb_generate_id) xcb_generate_id{};
    decltype(&::xcb_create_window) xcb_create_window{};
    decltype(&::xcb_destroy_window) xcb_destroy_window{};
    decltype(&::xcb_change_window_attributes) xcb_change_window_attributes{};
    decltype(&::xcb_intern_atom) xcb_intern_atom{};
    decltype(&::xcb_intern_atom_reply) xcb_intern_atom_reply{};
    decltype(&::xcb_set_selection_owner) xcb_set_selection_owner{};
    decltype(&::xcb_get_selection_owner) xcb_get_selection_owner{};
    decltype(&::xcb_get_selection_owner_reply) xcb_get_selection_owner_reply{};
    decltype(&::xcb_change_property) xcb_change_property{};
    decltype(&::xcb_get_property) xcb_get_property{};
    decltype(&::xcb_get_property_reply) xcb_get_property_reply{};
    decltype(&::xcb_get_property_value) xcb_get_property_value{};
    decltype(&::xcb_get_property_value_length) xcb_get_property_value_length{};
    decltype(&::xcb_get_geometry) xcb_get_geometry{};
    decltype(&::xcb_translate_coordinates) xcb_translate_coordinates{};
    decltype(&::xcb_get_extension_data) xcb_get_extension_data{};
    decltype(&::xcb_send_event) xcb_send_event{};
    decltype(&::xcb_get_input_focus) xcb_get_input_focus{};
    decltype(&::xcb_get_input_focus_reply) xcb_get_input_focus_reply{};
    decltype(&::xcb_poll_for_event) xcb_poll_for_event{};
    decltype(&::xcb_poll_for_reply) xcb_poll_for_reply{};
    decltype(&::xcb_flush) xcb_flush{};
};

} // namespace ffi
