module vui.render:visibility;
import :renderer;
using namespace rstd::prelude;
namespace vui::gpu::detail {
using RasterBounds = FrameClipBounds;
namespace visibility {
// Bound float operations, including fused multiply-add and rounding near cancellation.
struct Range {
    double low{}, high{};
    Range(double value);
    Range(double low, double high) : low(low), high(high) {}
    static auto rounded(double low, double high) -> Range;
    friend auto operator+(Range a, Range b) -> Range {
        return rounded(a.low + b.low, a.high + b.high);
    }
    friend auto operator-(Range a, Range b) -> Range {
        return rounded(a.low - b.high, a.high - b.low);
    }
    friend auto operator*(Range a, Range b) -> Range;
    friend auto operator/(Range a, Range b) -> Range;
    auto absolute() const -> Range;
};

// A positive hint only retains work; rejection always uses the conservative bounds below.
auto center_on_surface(const DrawData &data) -> bool;
auto center_in(const DrawData &data, RasterBounds bounds) -> bool;
auto quad_inside(const DrawData &data, VkExtent2D extent, RasterBounds bounds) -> bool;
struct QuadMapping {
    DrawData key;
    Range a, c, b, d;
    Range mx{0}, my{0};
    bool valid{true};
    explicit QuadMapping(const DrawData &data);
    auto matches(const DrawData &data) const -> bool;
    auto quad(const DrawData &data, VkExtent2D extent) const -> Option<RasterBounds>;
};
auto outside(RasterBounds a, RasterBounds b) -> bool;
auto intersect(RasterBounds a, RasterBounds b) -> RasterBounds;
struct ClipTolerance {
    double aa_margin{}, shape_magnitude{};
};
struct ClipCoverage {
    RasterBounds outer;
    Option<RasterBounds> keep;
    Index parent{no_clip};
    ClipTolerance tolerance;
    bool empty{};
    Option<RasterBounds> inner;
};
void prepare_clip(ClipCoverage &coverage, const GpuClip &clip, float scale_x, float scale_y,
                  bool restrict_outer);
auto prepare_clips(slice<GpuClip> clips, slice<ClipInstance> instances, RasterBounds surface,
                   float scale_x, float scale_y, bool build_inner)
    -> Vec<ClipCoverage>;
auto clip_tolerance(const GpuClip &clip, float scale_x, float scale_y) -> ClipTolerance;
auto clipped(RasterBounds pixels, const GpuClip &clip, ClipTolerance tolerance,
             float scale_x, float scale_y) -> bool;
} // namespace visibility

} // namespace vui::gpu::detail
