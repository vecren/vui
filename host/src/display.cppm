export module vui.host:display;
import vui.render;
using namespace rstd::prelude;

export namespace vui::host {
struct DisplayInfo {
    Serial id{};
    Option<Rect> bounds;
    Option<Rect> usable_bounds;
    Option<PixelSize> pixel_size;
    Option<Scalar> refresh_hz;
    friend auto operator==(const DisplayInfo &, const DisplayInfo &) -> bool = default;
};
enum class DisplayStatus { Unavailable, Available, Unsupported };
struct DisplaySnapshot {
    DisplayStatus status{DisplayStatus::Unavailable};
    Serial revision{};
    Vec<DisplayInfo> outputs;
};
}
