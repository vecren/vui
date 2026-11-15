module;
#include <xcb/randr.h>

module vui.host:ffi.xcb_randr;
namespace ffi {
using ::xcb_randr_id;
using ::xcb_randr_mode_info_t;
using ::xcb_randr_get_screen_resources_reply_t;
using ::xcb_randr_get_screen_resources_current_reply_t;
using ::xcb_randr_get_output_info_reply_t;
using ::xcb_randr_get_crtc_info_reply_t;
using ::xcb_randr_query_version;
using ::xcb_randr_query_version_reply;
using ::xcb_randr_select_input;
using ::xcb_randr_get_screen_resources;
using ::xcb_randr_get_screen_resources_outputs;
using ::xcb_randr_get_screen_resources_modes;
using ::xcb_randr_get_screen_resources_current;
using ::xcb_randr_get_screen_resources_current_outputs;
using ::xcb_randr_get_screen_resources_current_modes;
using ::xcb_randr_get_output_info;
using ::xcb_randr_get_crtc_info;
using ::XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE;
using ::XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE;
using ::XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE;
using ::XCB_RANDR_CONNECTION_CONNECTED;
using ::XCB_RANDR_SET_CONFIG_SUCCESS;
using ::XCB_RANDR_ROTATION_ROTATE_90;
using ::XCB_RANDR_ROTATION_ROTATE_270;
using ::XCB_RANDR_MODE_FLAG_INTERLACE;
using ::XCB_RANDR_MODE_FLAG_DOUBLE_SCAN;
}
