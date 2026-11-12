module vui.host:platform.input_clock;
import vui;

namespace vui::host::detail {
using namespace rstd::prelude;

struct NativeClockSample {
    SampleTime time;
    bool discontinuity{};
};

class NativeInputClock {
    Serial id_{new_session()}, milliseconds_{}, received_{};
    rstd::uint32_t last_{};
    bool initialized_{};
    static constexpr Serial unit = 1'000'000;
    static constexpr Serial wrap = Serial(1) << 32;
    static constexpr Serial half = wrap / 2;

    auto start(rstd::uint32_t value, Serial received, bool discontinuity) -> NativeClockSample {
        if (discontinuity) id_ = new_session();
        milliseconds_ = wrap + value; last_ = value; received_ = received; initialized_ = true;
        return {{id_, milliseconds_ * unit, unit, TimeOrigin::Native}, discontinuity};
    }
  public:
    auto sample(rstd::uint32_t value) -> NativeClockSample {
        auto now = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
        return sample(value, now > ~Serial{} ? ~Serial{} : Serial(now));
    }
    auto sample(rstd::uint32_t value, Serial received) -> NativeClockSample {
        if (!initialized_) return start(value, received, false);
        if (received < received_ || received - received_ >= half * unit) return start(value, received, true);
        received_ = received;
        auto forward = rstd::uint32_t(value - last_);
        if (Serial(forward) == half) return start(value, received, true);
        Serial result = milliseconds_;
        if (Serial(forward) < half) {
            if (milliseconds_ > (~Serial{} / unit) - forward) return start(value, received, true);
            milliseconds_ += forward; last_ = value; result = milliseconds_;
        } else {
            auto backward = rstd::uint32_t(last_ - value);
            if (milliseconds_ < backward) return start(value, received, true);
            result -= backward;
        }
        return {{id_, result * unit, unit, TimeOrigin::Native}, false};
    }
    void reset() { id_ = new_session(); initialized_ = false; }
};
} // namespace vui::host::detail
