module;
#include <cmath>
#include <cstring>
module vui.render;
import :visibility;
using namespace rstd::prelude;

namespace vui::gpu::detail::visibility {
namespace {
constexpr double interval_error = 0x1p-20;
}

auto finite_bounds(RasterBounds value) -> bool {
    return std::isfinite(value.left) && std::isfinite(value.top) && std::isfinite(value.right) &&
           std::isfinite(value.bottom);
}

auto Range::absolute() const -> Range {
    auto left = std::fabs(low), right = std::fabs(high);
    return {low <= 0 && high >= 0 ? 0 : (left < right ? left : right),
            left > right ? left : right};
}

Range::Range(double value) : low(value), high(value) {
    if (std::fabs(value) < 0x1p-126) {
        low = value < 0 ? value : 0;
        high = value > 0 ? value : 0;
    }
}

auto Range::rounded(double low, double high) -> Range {
    if (!std::isfinite(low) || !std::isfinite(high))
        return {-__builtin_inf(), __builtin_inf()};
    auto left = std::fabs(low), right = std::fabs(high);
    auto magnitude = left > right ? left : right;
    if (magnitude > 3.0e38)
        return {-__builtin_inf(), __builtin_inf()};
    auto error = magnitude * interval_error + 0x1p-120;
    return {low - error, high + error};
}

QuadMapping::QuadMapping(const DrawData &data)
    : key(data), a(data.row0[0]), c(data.row0[1]), b(data.row1[0]), d(data.row1[1]) {
    if (data.params[3] == 0) {
        auto determinant = (a * d - c * b).absolute();
        if (determinant.low <= 0) {
            valid = false;
            return;
        }
        mx = (d.absolute() + c.absolute()) / determinant / Range(data.surface[2]);
        my = (b.absolute() + a.absolute()) / determinant / Range(data.surface[2]);
    }
}

auto QuadMapping::matches(const DrawData &data) const -> bool {
    return (key.params[3] == 0) == (data.params[3] == 0) &&
           std::memcmp(key.row0, data.row0, sizeof(key.row0)) == 0 &&
           std::memcmp(key.row1, data.row1, sizeof(key.row1)) == 0 &&
           std::memcmp(key.surface, data.surface, sizeof(key.surface)) == 0;
}

auto QuadMapping::quad(const DrawData &data, VkExtent2D extent) const -> Option<RasterBounds> {
    if (!valid)
        return None();
    auto x = Range(data.rect[0]) - mx + Range(0, 1) * (Range(data.rect[2]) + mx * Range(2));
    auto y = Range(data.rect[1]) - my + Range(0, 1) * (Range(data.rect[3]) + my * Range(2));
    auto world_x = a * x + c * y + Range(data.row0[2]);
    auto world_y = b * x + d * y + Range(data.row1[2]);
    auto screen_x = (world_x / Range(data.surface[0]) * Range(2) - Range(1) + Range(1)) *
                    Range(double(extent.width) / 2);
    auto screen_y = (world_y / Range(data.surface[1]) * Range(2) - Range(1) + Range(1)) *
                    Range(double(extent.height) / 2);
    RasterBounds bounds{screen_x.low - 2, screen_y.low - 2, screen_x.high + 2,
                        screen_y.high + 2};
    if (!finite_bounds(bounds))
        return None();
    return Some(RasterBounds(bounds));
}

auto operator*(Range a, Range b) -> Range {
    double values[]{a.low * b.low, a.low * b.high, a.high * b.low, a.high * b.high};
    double low = values[0], high = values[0];
    for (auto value : values) {
        if (!std::isfinite(value))
            return {-__builtin_inf(), __builtin_inf()};
        low = low < value ? low : value;
        high = high > value ? high : value;
    }
    return Range::rounded(low, high);
}

auto operator/(Range a, Range b) -> Range {
    if (b.low <= 0 && b.high >= 0)
        return {-__builtin_inf(), __builtin_inf()};
    return a * Range::rounded(1 / b.high, 1 / b.low);
}
namespace {
auto center(const DrawData &data) -> Point {
    auto x = double(data.rect[0]) + double(data.rect[2]) / 2;
    auto y = double(data.rect[1]) + double(data.rect[3]) / 2;
    return {data.row0[0] * x + data.row0[1] * y + data.row0[2],
            data.row1[0] * x + data.row1[1] * y + data.row1[2]};
}

auto around(Point center, double rx, double ry) -> RasterBounds {
    return {center.x - rx, center.y - ry, center.x + rx, center.y + ry};
}
} // namespace

auto center_on_surface(const DrawData &data) -> bool {
    const auto point = center(data);
    return point.x >= 0 && point.x <= data.surface[0] && point.y >= 0 && point.y <= data.surface[1];
}

auto center_in(const DrawData &data, RasterBounds bounds) -> bool {
    const auto point = center(data);
    const auto x = point.x * data.row0[3];
    const auto y = point.y * data.row1[3];
    return x >= bounds.left && x <= bounds.right && y >= bounds.top && y <= bounds.bottom;
}

auto quad_inside(const DrawData &data, VkExtent2D extent, RasterBounds bounds) -> bool {
    // The fast estimate budgets at most sixteen elementary rounding steps per expression.
    constexpr double error = 16 * interval_error;
    constexpr double working_limit = 0x1p40;
    const auto sx = double(extent.width) / data.surface[0];
    const auto sy = double(extent.height) / data.surface[1];
    const auto scale = double(data.surface[2]);
    auto usable_scale = [](double value) { return value >= 0x1p-16 && value <= 0x1p16; };
    if (!usable_scale(sx) || !usable_scale(sy) || !usable_scale(scale) ||
        data.rect[2] < 0 || data.rect[3] < 0) return false;
    const double a = data.row0[0], c = data.row0[1], b = data.row1[0], d = data.row1[1];
    const auto aa = std::fabs(a), ac = std::fabs(c), ab = std::fabs(b), ad = std::fabs(d);
    if (aa + ac + ab + ad > 0x1p16) return false;

    double mx{}, my{};
    if (data.params[3] == 0) {
        // Enclose the vertex shader determinant and both divisions, including FMA contraction.
        const auto p = a * d, q = c * b;
        const auto determinant = std::fabs(p - q) - (std::fabs(p) + std::fabs(q)) * error;
        if (determinant <= 0x1p-16) return false;
        const auto inverse = (1 + error) / (determinant * scale);
        mx = (ad + ac) * inverse;
        my = (ab + aa) * inverse;
    }

    auto local = [](double origin, double size, double margin) -> Range {
        const auto slop = (std::fabs(origin) + size + 3 * margin + 1) * error;
        return {origin - margin - slop, origin + size + margin + slop};
    };
    const auto x = local(data.rect[0], data.rect[2], mx);
    const auto y = local(data.rect[1], data.rect[3], my);
    const auto abs_x = std::fmax(std::fabs(x.low), std::fabs(x.high));
    const auto abs_y = std::fmax(std::fabs(y.low), std::fabs(y.high));
    if (abs_x > working_limit || abs_y > working_limit) return false;

    auto fits = [&](double u, double v, double t, double pixels_per_unit, Index pixels,
                    double low, double high) {
        const auto magnitude = std::fabs(u) * abs_x + std::fabs(v) * abs_y + std::fabs(t);
        if (magnitude > working_limit) return false;
        const auto lo = u * (u < 0 ? x.high : x.low) + v * (v < 0 ? y.high : y.low) + t;
        const auto hi = u * (u < 0 ? x.low : x.high) + v * (v < 0 ? y.low : y.high) + t;
        // Budget rounding for the affine dot, NDC conversion, and subpixel rasterization.
        const auto slop = ((magnitude + 1) * pixels_per_unit + pixels) * error + 2;
        return lo * pixels_per_unit - slop >= low && hi * pixels_per_unit + slop <= high;
    };
    return fits(a, c, data.row0[2], sx, extent.width, bounds.left, bounds.right) &&
           fits(b, d, data.row1[2], sy, extent.height, bounds.top, bounds.bottom);
}

auto outside(RasterBounds a, RasterBounds b) -> bool {
    return a.right < b.left || a.left > b.right || a.bottom < b.top || a.top > b.bottom;
}

auto intersect(RasterBounds a, RasterBounds b) -> RasterBounds {
    return {rstd::cmp::max(a.left, b.left), rstd::cmp::max(a.top, b.top),
            rstd::min(a.right, b.right), rstd::min(a.bottom, b.bottom)};
}

auto clip_tolerance(const GpuClip &clip, float scale_x, float scale_y) -> ClipTolerance {
    // Bound the rounded-box AA band and the ellipse shader's local footprint.
    const auto derivative = std::hypot(double(clip.row0[0]), double(clip.row1[0])) / scale_x +
                            std::hypot(double(clip.row0[1]), double(clip.row1[1])) / scale_y;
    double magnitude = 1;
    for (auto value : clip.rect) magnitude += std::fabs(value);
    for (auto value : clip.radii) magnitude += std::fabs(value);
    const auto aa_margin = std::isfinite(derivative) ? 4 * std::fmax(derivative, 0.0001) : derivative;
    return {aa_margin, magnitude};
}

namespace {
auto local_bounds(RasterBounds pixels, const GpuClip &clip, float scale_x, float scale_y) -> RasterBounds {
    auto x = Range(pixels.left, pixels.right) / Range(scale_x);
    auto y = Range(pixels.top, pixels.bottom) / Range(scale_y);
    auto local_x = Range(clip.row0[0]) * x + Range(clip.row0[1]) * y + Range(clip.row0[2]);
    auto local_y = Range(clip.row1[0]) * x + Range(clip.row1[1]) * y + Range(clip.row1[2]);
    return {local_x.low, local_y.low, local_x.high, local_y.high};
}

auto coverage_margin(RasterBounds local, ClipTolerance tolerance) -> Option<double> {
    const auto magnitude = tolerance.shape_magnitude + std::fabs(local.left) + std::fabs(local.top) +
                           std::fabs(local.right) + std::fabs(local.bottom);
    if (!finite_bounds(local) || !std::isfinite(tolerance.aa_margin) || magnitude > 1.0e12)
        return None();
    return Some(tolerance.aa_margin + magnitude * 0x1p-16);
}
} // namespace

auto clipped(RasterBounds pixels, const GpuClip &clip, ClipTolerance tolerance,
             float scale_x, float scale_y) -> bool {
    if (clip.params[2] == 0) return true;
    const auto local = local_bounds(pixels, clip, scale_x, scale_y);
    const auto margin = coverage_margin(local, tolerance);
    if (!margin) return false;
    RasterBounds expanded{clip.rect[0] - *margin, clip.rect[1] - *margin,
                          double(clip.rect[0]) + clip.rect[2] + *margin,
                          double(clip.rect[1]) + clip.rect[3] + *margin};
    return outside(local, expanded);
}

namespace {
auto solid_half_size(Size half, const GpuClip &clip, double margin) -> Size {
    constexpr double diagonal = 0.7071067811865475;
    if (clip.params[0] == static_cast<float>(ShapeKind::Ellipse)) {
        const auto factor = diagonal * (1 - std::fmax(margin / rstd::min(half.width, half.height), .0004));
        return {half.width * factor, half.height * factor};
    }
    double radius{};
    for (auto corner : clip.radii) radius = std::fmax(radius, corner);
    const auto inset = radius * (1 - diagonal) + margin;
    return {half.width - inset, half.height - inset};
}

auto certified_inner(RasterBounds parent, const GpuClip &clip, ClipTolerance tolerance,
                     float scale_x, float scale_y, Size half, auto project) -> Option<RasterBounds> {
    if (half.width <= 0 || half.height <= 0) return None();
    const RasterBounds neighborhood{parent.left - 2, parent.top - 2, parent.right + 2, parent.bottom + 2};
    const auto margin = coverage_margin(local_bounds(neighborhood, clip, scale_x, scale_y), tolerance);
    if (!margin) return None();
    const auto solid = solid_half_size(half, clip, *margin);
    if (!(solid.width > 0 && solid.height > 0)) return None();
    const auto candidate = intersect(parent, project(solid));
    const RasterBounds device{std::ceil(candidate.left) + 1, std::ceil(candidate.top) + 1,
                              std::floor(candidate.right) - 1, std::floor(candidate.bottom) - 1};
    if (!(device.left < device.right && device.top < device.bottom)) return None();
    const auto local = local_bounds(device, clip, scale_x, scale_y);
    const auto safe = around({clip.rect[0] + half.width, clip.rect[1] + half.height}, solid.width, solid.height);
    if (local.left >= safe.left && local.top >= safe.top && local.right <= safe.right && local.bottom <= safe.bottom)
        return Some(RasterBounds(device));
    return None();
}

void project_clip(ClipCoverage &coverage, const GpuClip &clip, float scale_x, float scale_y,
                  bool restrict_outer) {
    const Transform inverse{clip.row0[0], clip.row1[0], clip.row0[1], clip.row1[1],
                            clip.row0[2], clip.row1[2]};
    const auto forward = inverse.inverse();
    if (!forward || !(scale_x > 0 && scale_y > 0)) {
        coverage.keep = None();
        coverage.inner = None();
        return;
    }

    const auto hx = double(clip.rect[2]) / 2;
    const auto hy = double(clip.rect[3]) / 2;
    const auto world = forward->map({clip.rect[0] + hx, clip.rect[1] + hy});
    const Point pixel{world.x * scale_x, world.y * scale_y};
    const auto ax = std::fabs(forward->a) * scale_x, cx = std::fabs(forward->c) * scale_x;
    const auto by = std::fabs(forward->b) * scale_y, dy = std::fabs(forward->d) * scale_y;
    const auto rx = ax * hx + cx * hy, ry = by * hx + dy * hy;
    const auto estimate = around(pixel, rx + (ax + cx) * coverage.tolerance.aa_margin + 2,
                                  ry + (by + dy) * coverage.tolerance.aa_margin + 2);
    if (!finite_bounds(estimate) || rx <= 0 || ry <= 0) {
        coverage.keep = None();
        coverage.inner = None();
        return;
    }

    auto project_inside = [&](Size half) {
        const auto half_x = ax * half.width + cx * half.height;
        const auto half_y = by * half.width + dy * half.height;
        const auto x = half_x / scale_x, y = half_y / scale_y;
        const auto a = std::fabs(inverse.a) * x + std::fabs(inverse.c) * y;
        const auto b = std::fabs(inverse.b) * x + std::fabs(inverse.d) * y;
        const auto factor = rstd::min(half.width / a, half.height / b);
        return around(pixel, half_x * factor, half_y * factor);
    };
    if (coverage.inner)
        coverage.inner = certified_inner(*coverage.inner, clip, coverage.tolerance,
                                         scale_x, scale_y, {hx, hy}, project_inside);

    if (coverage.keep) {
        // Fit the hint inside the local rectangle. Only CPU retention uses it.
        coverage.keep = Some(intersect(*coverage.keep, project_inside({hx, hy})));
    }

    if (!restrict_outer) return;
    auto &outer = coverage.outer;
    auto rejects = [&](RasterBounds strip) { return clipped(strip, clip, coverage.tolerance, scale_x, scale_y); };
    if (outside(estimate, outer)) {
        coverage.empty = rejects(outer);
        return;
    }
    // A projected edge only shrinks the outer bound after the old rejection test proves the excluded strip empty.
    if (estimate.left > outer.left && rejects({outer.left, outer.top, estimate.left, outer.bottom}))
        outer.left = estimate.left;
    if (estimate.right < outer.right && rejects({estimate.right, outer.top, outer.right, outer.bottom}))
        outer.right = estimate.right;
    if (estimate.top > outer.top && rejects({outer.left, outer.top, outer.right, estimate.top}))
        outer.top = estimate.top;
    if (estimate.bottom < outer.bottom && rejects({outer.left, estimate.bottom, outer.right, outer.bottom}))
        outer.bottom = estimate.bottom;
}
} // namespace

void prepare_clip(ClipCoverage &coverage, const GpuClip &clip, float scale_x, float scale_y,
                  bool restrict_outer) {
    coverage.empty |= clip.params[2] == 0;
    if (!coverage.empty) {
        coverage.tolerance = clip_tolerance(clip, scale_x, scale_y);
        project_clip(coverage, clip, scale_x, scale_y, restrict_outer);
    }
    if (coverage.empty) {
        coverage.keep = None();
        coverage.inner = None();
    }
}

auto prepare_clips(slice<GpuClip> clips, slice<ClipInstance> instances, RasterBounds surface,
                   float scale_x, float scale_y, bool build_inner)
    -> Vec<ClipCoverage> {
    Vec<ClipCoverage> prepared;
    prepared.reserve(clips.len());
    ClipCoverage root{surface, Some(RasterBounds(surface))};
    if (build_inner) root.inner = Some(RasterBounds(surface));
    for (usize index{}; index < clips.len(); ++index) {
        const auto &clip = clips[index];
        const auto parent = clip.params[1] < 0 ? no_clip : static_cast<Index>(clip.params[1]);
        const auto &previous = parent == no_clip ? root : prepared[usize(parent)];
        ClipCoverage coverage{previous.outer, previous.keep, parent};
        coverage.empty = previous.empty;
        coverage.inner = previous.inner;
        const auto &available = instances[index].coverage;
        if (available) {
            coverage.empty |= available->empty || outside(coverage.outer, available->bounds);
            coverage.outer = intersect(coverage.outer, available->bounds);
        }
        prepare_clip(coverage, clip, scale_x, scale_y, available.is_none());
        prepared.push(rstd::move(coverage));
    }
    return prepared;
}
} // namespace vui::gpu::detail::visibility
