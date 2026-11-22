module;
#include <cmath>
module vui.render;
import :effects.backdrop_blur;

namespace vui::gpu::detail {
namespace {
auto pixels(VkExtent2D extent) -> Serial { return Serial(extent.width) * extent.height; }

void cover(DrawData &draw, VkExtent2D extent) {
    draw.rect[2] = draw.surface[0] = static_cast<float>(extent.width);
    draw.rect[3] = draw.surface[1] = static_cast<float>(extent.height);
    draw.row0[0] = draw.row1[1] = draw.row0[3] = draw.row1[3] = draw.surface[2] = 1;
    draw.params[3] = 1;
}

void trim(RasterBounds &bounds, double RasterBounds::*edge, double RasterBounds::*opposite,
          const GpuClip &clip, visibility::ClipTolerance tolerance, float scale_x, float scale_y) {
    // Retain every strip that cannot be conservatively rejected by the clip test.
    auto first = bounds.*edge;
    auto last = bounds.*opposite;
    auto strip = bounds;
    strip.*opposite = first;
    if (!visibility::clipped(strip, clip, tolerance, scale_x, scale_y))
        return;
    while (std::fabs(last - first) > 1) {
        auto middle = (first + last) * .5;
        strip.*opposite = middle;
        if (visibility::clipped(strip, clip, tolerance, scale_x, scale_y))
            first = middle;
        else
            last = middle;
    }
    bounds.*edge = first;
}
}

auto BackdropBlur::plan(const DrawData &draw, Scalar sigma, Size logical, VkExtent2D target,
                        slice<GpuClip> clips, slice<visibility::ClipCoverage> coverage, Index clip_index)
    -> Option<BackdropBlur> {
    auto bounds = visibility::QuadMapping(draw).quad(draw, target)
                      .unwrap_or(RasterBounds{0, 0, double(target.width), double(target.height)});
    bounds = {std::fmax(0, bounds.left), std::fmax(0, bounds.top),
              std::fmin(target.width, bounds.right), std::fmin(target.height, bounds.bottom)};
    if (bounds.left >= bounds.right || bounds.top >= bounds.bottom)
        return None();
    const auto scale_x = draw.row0[3];
    const auto scale_y = draw.row1[3];
    while (clip_index != no_clip) {
        const auto index = usize(clip_index);
        const auto &clip = clips[index];
        const auto tolerance = coverage.is_empty() || coverage[index].empty
                                   ? visibility::clip_tolerance(clip, scale_x, scale_y)
                                   : coverage[index].tolerance;
        if (visibility::clipped(bounds, clip, tolerance, scale_x, scale_y))
            return None();
        trim(bounds, &RasterBounds::left, &RasterBounds::right, clip, tolerance, scale_x, scale_y);
        trim(bounds, &RasterBounds::right, &RasterBounds::left, clip, tolerance, scale_x, scale_y);
        trim(bounds, &RasterBounds::top, &RasterBounds::bottom, clip, tolerance, scale_x, scale_y);
        trim(bounds, &RasterBounds::bottom, &RasterBounds::top, clip, tolerance, scale_x, scale_y);
        clip_index = clip.params[1] < 0 ? no_clip : static_cast<Index>(clip.params[1]);
    }

    BackdropBlur result;
    auto roi = [](double first, double last, double deviation, Index limit, Index &origin) {
        auto margin = std::ceil(3 * deviation) + 4;
        origin = static_cast<Index>(std::fmax(0, std::floor(first - margin)));
        return static_cast<Index>(std::fmin(limit, std::ceil(last + margin))) - origin;
    };
    const auto sigma_x = sigma * (target.width / logical.width);
    const auto sigma_y = sigma * (target.height / logical.height);
    result.capture.width = roi(bounds.left, bounds.right, sigma_x, target.width, result.x);
    result.capture.height = roi(bounds.top, bounds.bottom, sigma_y, target.height, result.y);
    result.filtered = result.capture;
    if (pixels(result.capture) >= 16 * 1024) {
        if (sigma_x >= 8)
            result.filtered.width = (result.capture.width + 1) / 2;
        if (sigma_y >= 8)
            result.filtered.height = (result.capture.height + 1) / 2;
    }
    auto kernel = [](double sigma, Index filtered, Index captured) -> Kernel {
        auto deviation = sigma * (double(filtered) / captured);
        constexpr Index max_radius = 96;
        auto radius = std::ceil(3 * deviation);
        if (radius > max_radius)
            return {0, max_radius + 1};
        auto value = static_cast<float>(deviation);
        return {value, value > 0 ? static_cast<Index>(radius) : 0};
    };
    result.horizontal = kernel(sigma_x, result.filtered.width, result.capture.width);
    result.vertical = kernel(sigma_y, result.filtered.height, result.capture.height);
    result.samples = pixels(result.capture) + pixels(result.filtered) *
        (2 + 2 * Serial(result.horizontal.radius) + 2 * Serial(result.vertical.radius));
    if ((!result.horizontal.sigma && result.horizontal.radius) ||
        (!result.vertical.sigma && result.vertical.radius))
        result.samples = backdrop_sample_budget + 1;
    return Some(result);
}

void BackdropBlur::snapshot(DrawData &draw) const {
    cover(draw, capture);
    draw.params[1] = 1;
    draw.uv[0] = static_cast<float>(x);
    draw.uv[1] = static_cast<float>(y);
}

void BackdropBlur::filter(DrawData &draw, bool vertical_pass) const {
    cover(draw, filtered);
    const auto kernel = vertical_pass ? vertical : horizontal;
    draw.uv[0] = vertical_pass ? 1 : float(capture.width) / filtered.width;
    draw.uv[1] = vertical_pass ? 1 : float(capture.height) / filtered.height;
    draw.color[0] = vertical_pass ? 0 : draw.uv[0];
    draw.color[1] = vertical_pass ? 1 : 0;
    draw.color[2] = kernel.sigma;
    draw.color[3] = static_cast<float>(kernel.radius);
}

void BackdropBlur::composite(DrawData &draw) const {
    draw.uv[0] = static_cast<float>(x);
    draw.uv[1] = static_cast<float>(y);
}
} // namespace vui::gpu::detail
