module vui.host:platform.keyboard_text;
import :ffi.xkb;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;
namespace xkb = ffi::xkb;

enum class ComposeResult { Plain, Pending, Composed };

class KeyboardCompose {
    xkb::xkb_compose_state *state_{};
  public:
    KeyboardCompose() = default;
    KeyboardCompose(const KeyboardCompose &) = delete;
    auto operator=(const KeyboardCompose &) -> KeyboardCompose & = delete;
    ~KeyboardCompose() { clear(); }
    void clear() {
        if (state_) xkb::xkb_compose_state_unref(state_);
        state_ = nullptr;
    }
    auto configure(xkb::xkb_context *context, const char *locale) -> bool;
    void configure(xkb::xkb_context *context);
    void reset() { if (state_) xkb::xkb_compose_state_reset(state_); }
    auto feed(rstd::uint32_t symbol) -> ComposeResult;
    auto utf8(char *bytes, unsigned long size) -> int {
        return state_ ? xkb::xkb_compose_state_get_utf8(state_, bytes, size) : 0;
    }
};

auto keyboard_text_from_utf8(const char *bytes, int size, bool &invalid) -> TextInput;
}
