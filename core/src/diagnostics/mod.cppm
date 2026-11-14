export module vui:diagnostics;
import :types;
using namespace rstd::prelude;

export namespace vui {
struct InputStamp {
    Serial source{}, sequence{};
    rstd::time::Instant received{};
    auto latency_at(rstd::time::Instant observed) const -> Option<Serial> {
        if (!source || !sequence || observed < received)
            return None();
        auto value = observed.duration_since(received).as_nanos().to_primitive();
        return Some(value > ~Serial{} ? ~Serial{} : static_cast<Serial>(value));
    }
};
class InputTraceSource {
    Serial source_{new_session()}, sequence_{};

  public:
    InputTraceSource() = default;
    InputTraceSource(const InputTraceSource &) = delete;
    auto operator=(const InputTraceSource &) -> InputTraceSource & = delete;
    auto stamp() -> Option<InputStamp> {
        if (sequence_ == ~Serial{})
            return None();
        return Some(InputStamp{source_, ++sequence_, rstd::time::Instant::now()});
    }
};
struct DiagnosticsOptions {
    bool timings{};
};
struct CpuTiming {
    Serial calls{}, nanoseconds{};
    void add(CpuTiming other) {
        calls += rstd::min(other.calls, ~Serial{} - calls);
        nanoseconds += rstd::min(other.nanoseconds, ~Serial{} - nanoseconds);
    }
};
auto elapsed_nanoseconds(rstd::time::Instant start) -> Serial {
    auto value = start.elapsed().as_nanos().to_primitive();
    return value > ~Serial{} ? ~Serial{} : static_cast<Serial>(value);
}
class CpuTimer {
    CpuTiming *output_{};
    Option<rstd::time::Instant> start_;

  public:
    explicit CpuTimer(CpuTiming *output) : output_(output) {
        if (output_) {
            output_->add({1, 0});
            resume();
        }
    }
    CpuTimer(const CpuTimer &) = delete;
    auto operator=(const CpuTimer &) -> CpuTimer & = delete;
    void pause() {
        if (start_.is_some()) {
            output_->add({0, elapsed_nanoseconds(*start_)});
            start_ = None();
        }
    }
    void resume() {
        if (output_ && start_.is_none())
            start_ = Some(rstd::time::Instant::now());
    }
    ~CpuTimer() { pause(); }
};
} // namespace vui
