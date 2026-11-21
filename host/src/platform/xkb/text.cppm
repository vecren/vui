module vui.host:platform.xkb.text;
import :platform.xkb.ffi;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

enum class ComposeResult { Plain, Pending, Composed };

class KeyboardCompose {
    Option<rstd::sync::Arc<ffi::XkbDispatch>> api_;
    ffi::xkb_compose_state *state_{};

  public:
    KeyboardCompose() = default;
    KeyboardCompose(const KeyboardCompose &) = delete;
    auto operator=(const KeyboardCompose &) -> KeyboardCompose & = delete;
    ~KeyboardCompose() { clear(); }
    void clear() {
        if (state_)
            (*api_)->xkb_compose_state_unref(state_);
        state_ = nullptr;
        api_ = None();
    }
    auto configure(const rstd::sync::Arc<ffi::XkbDispatch> &api, ffi::xkb_context *context, const char *locale) -> bool;
    void configure(const rstd::sync::Arc<ffi::XkbDispatch> &api, ffi::xkb_context *context);
    void reset() {
        if (state_)
            (*api_)->xkb_compose_state_reset(state_);
    }
    auto feed(rstd::uint32_t symbol) -> ComposeResult;
    auto utf8(char *bytes, unsigned long size) -> int {
        return state_ ? (*api_)->xkb_compose_state_get_utf8(state_, bytes, size) : 0;
    }
};

auto keyboard_text_from_utf8(const char *bytes, int size, bool &invalid) -> TextInput;
} // namespace vui::host::detail
