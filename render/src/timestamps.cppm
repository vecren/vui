module;
#include <cmath>
module vui.render:timestamps;
import vui;
using namespace rstd::prelude;
namespace vui::gpu::detail {
auto timestamp_duration(Index bits, double period, Serial observation_nanoseconds, Serial begin,
                        Serial end) -> Option<double> {
    if (!bits || bits > 64 || !std::isfinite(period) || period <= 0 ||
        static_cast<double>(observation_nanoseconds) >= std::ldexp(period, static_cast<int>(bits)))
        return None();
    Serial mask = bits == 64 ? ~Serial{} : (Serial{1} << bits) - 1;
    return Some(static_cast<double>((end - begin) & mask) * period);
}

} // namespace vui::gpu::detail
