module vui.render:timestamps;
import vui;
using namespace rstd::prelude;
namespace vui::gpu::detail {
auto timestamp_duration(Index bits, double period, Serial observation_nanoseconds, Serial begin,
                        Serial end) -> Option<double>;

} // namespace vui::gpu::detail
