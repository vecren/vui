module;
#include <xcb/randr.h>

module vui.host:platform.x11.ffi.xcb_randr;
import rstd.dlopn;
namespace ffi {
using ::XCB_RANDR_CONNECTION_CONNECTED;
using ::xcb_randr_get_crtc_info_reply_t;
using ::xcb_randr_get_output_info_reply_t;
using ::xcb_randr_get_screen_resources_current_reply_t;
using ::xcb_randr_get_screen_resources_reply_t;
using ::XCB_RANDR_MODE_FLAG_DOUBLE_SCAN;
using ::XCB_RANDR_MODE_FLAG_INTERLACE;
using ::xcb_randr_mode_info_t;
using ::XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE;
using ::XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE;
using ::XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE;
using ::XCB_RANDR_ROTATION_ROTATE_270;
using ::XCB_RANDR_ROTATION_ROTATE_90;
using ::XCB_RANDR_SET_CONFIG_SUCCESS;
class RandrDispatch {
    rstd::dlopn::Library library_;
    explicit RandrDispatch(rstd::dlopn::Library library) : library_(rstd::move(library)) {}

  public:
    RandrDispatch(const RandrDispatch &) = delete;
    RandrDispatch(RandrDispatch &&) = default;
    auto operator=(const RandrDispatch &) -> RandrDispatch & = delete;
    auto operator=(RandrDispatch &&) -> RandrDispatch & = default;
    static auto open() -> rstd::Option<RandrDispatch>;
    decltype(&::xcb_randr_id) xcb_randr_id{};
    decltype(&::xcb_randr_query_version) xcb_randr_query_version{};
    decltype(&::xcb_randr_query_version_reply) xcb_randr_query_version_reply{};
    decltype(&::xcb_randr_select_input) xcb_randr_select_input{};
    decltype(&::xcb_randr_get_screen_resources) xcb_randr_get_screen_resources{};
    decltype(&::xcb_randr_get_screen_resources_outputs) xcb_randr_get_screen_resources_outputs{};
    decltype(&::xcb_randr_get_screen_resources_modes) xcb_randr_get_screen_resources_modes{};
    decltype(&::xcb_randr_get_screen_resources_current) xcb_randr_get_screen_resources_current{};
    decltype(&::xcb_randr_get_screen_resources_current_outputs) xcb_randr_get_screen_resources_current_outputs{};
    decltype(&::xcb_randr_get_screen_resources_current_modes) xcb_randr_get_screen_resources_current_modes{};
    decltype(&::xcb_randr_get_output_info) xcb_randr_get_output_info{};
    decltype(&::xcb_randr_get_crtc_info) xcb_randr_get_crtc_info{};
};

} // namespace ffi
