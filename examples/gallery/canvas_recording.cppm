export module vui.examples.gallery:canvas_recording;
import vui;

namespace examples::gallery::detail {
constexpr vui::Serial canvas_warmup = 8;

struct CanvasRecording {
    vui::Serial frames{};
    vui::CpuTiming paint;

    auto measure() -> vui::CpuTimer {
        return vui::CpuTimer(++frames > canvas_warmup ? &paint : nullptr);
    }
};
} // namespace examples::gallery::detail
