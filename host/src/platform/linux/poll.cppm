module vui.host:platform.linux.poll;
import :platform.linux.ffi;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

// The window connection and wake event occupy the first two descriptors.
inline auto poll_window(mut_ref<ffi::pollfd[]> descriptors, rstd::uint32_t timeout_ms, bool redraw) -> bool {
    auto started = rstd::time::Instant::now();
    int result;
    do {
        auto elapsed = started.elapsed().as_millis().to_primitive();
        auto remaining = elapsed >= timeout_ms ? 0 : timeout_ms - elapsed;
        auto timeout = redraw ? 0 : int(remaining > 2147483647 ? 2147483647 : remaining);
        result = ffi::poll(descriptors.as_raw_ptr(), descriptors.len().to_primitive(), timeout);
    } while (result < 0 && ffi::error_number() == ffi::EINTR);
    return result >= 0 && !(descriptors[usize(0)].revents & ffi::error) &&
           !(descriptors[usize(1)].revents & ffi::error);
}
} // namespace vui::host::detail
