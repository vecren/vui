module vui.host;
import :window;
import :platform;
using namespace rstd::prelude;

namespace vui::host {
auto Window::timing() const -> FrameTiming {
    auto now = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
    return {now > ~Serial{} ? ~Serial{} : Serial(now), {}};
}
} // namespace vui::host
