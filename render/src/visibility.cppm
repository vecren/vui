module;
#include <cmath>
#include <cstring>
module vui.render:visibility;
import vui;
import vrento.vulkan;
import :data;
using namespace rstd::prelude;
namespace vui::gpu::detail {
struct RasterBounds {
    double left{}, top{}, right{}, bottom{};
};
namespace visibility {
// Bound float operations, including fused multiply-add and rounding near cancellation.
struct Range {
    double low{}, high{};
    Range(double value) : low(value), high(value) {
        if (std::fabs(value) < 0x1p-126) {
            low = value < 0 ? value : 0;
            high = value > 0 ? value : 0;
        }
    }
    Range(double low, double high) : low(low), high(high) {}
    static auto rounded(double low, double high) -> Range {
        if (!std::isfinite(low) || !std::isfinite(high))
            return {-__builtin_inf(), __builtin_inf()};
        auto left = std::fabs(low), right = std::fabs(high);
        auto magnitude = left > right ? left : right;
        if (magnitude > 3.0e38)
            return {-__builtin_inf(), __builtin_inf()};
        auto error = magnitude * 0x1p-20 + 0x1p-120;
        return {low - error, high + error};
    }
    friend auto operator+(Range a, Range b) -> Range {
        return rounded(a.low + b.low, a.high + b.high);
    }
    friend auto operator-(Range a, Range b) -> Range {
        return rounded(a.low - b.high, a.high - b.low);
    }
    friend auto operator*(Range a, Range b) -> Range {
        double values[]{a.low * b.low, a.low * b.high, a.high * b.low, a.high * b.high};
        double low = values[0], high = values[0];
        for (auto value : values) {
            if (!std::isfinite(value))
                return {-__builtin_inf(), __builtin_inf()};
            low = low < value ? low : value;
            high = high > value ? high : value;
        }
        return rounded(low, high);
    }
    friend auto operator/(Range a, Range b) -> Range {
        if (b.low <= 0 && b.high >= 0)
            return {-__builtin_inf(), __builtin_inf()};
        return a * rounded(1 / b.high, 1 / b.low);
    }
    auto absolute() const -> Range {
        auto left = std::fabs(low), right = std::fabs(high);
        return {low <= 0 && high >= 0 ? 0 : (left < right ? left : right),
                left > right ? left : right};
    }
};
auto finite_bounds(RasterBounds value) -> bool {
    return std::isfinite(value.left) && std::isfinite(value.top) && std::isfinite(value.right) &&
           std::isfinite(value.bottom);
}
// A positive hint only retains work; rejection always uses the conservative bounds below.
auto center_on_surface(const DrawData &data) -> bool {
    auto x = double(data.rect[0]) + double(data.rect[2]) / 2;
    auto y = double(data.rect[1]) + double(data.rect[3]) / 2;
    auto world_x = data.row0[0] * x + data.row0[1] * y + data.row0[2];
    auto world_y = data.row1[0] * x + data.row1[1] * y + data.row1[2];
    return world_x >= 0 && world_x <= data.surface[0] && world_y >= 0 && world_y <= data.surface[1];
}
struct QuadMapping {
    DrawData key;
    Range a, c, b, d;
    Range mx{0}, my{0};
    bool valid{true};
    explicit QuadMapping(const DrawData &data)
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
    auto matches(const DrawData &data) const -> bool {
        return (key.params[3] == 0) == (data.params[3] == 0) &&
               std::memcmp(key.row0, data.row0, sizeof(key.row0)) == 0 &&
               std::memcmp(key.row1, data.row1, sizeof(key.row1)) == 0 &&
               std::memcmp(key.surface, data.surface, sizeof(key.surface)) == 0;
    }
    auto quad(const DrawData &data, VkExtent2D extent) const -> Option<RasterBounds> {
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
};
auto outside(RasterBounds a, RasterBounds b) -> bool {
    return a.right < b.left || a.left > b.right || a.bottom < b.top || a.top > b.bottom;
}
auto clipped(RasterBounds pixels, const GpuClip &clip, float scale) -> bool {
    if (clip.params[2] == 0)
        return true;
    auto x = Range(pixels.left, pixels.right) / Range(scale);
    auto y = Range(pixels.top, pixels.bottom) / Range(scale);
    auto local_x = Range(clip.row0[0]) * x + Range(clip.row0[1]) * y + Range(clip.row0[2]);
    auto local_y = Range(clip.row1[0]) * x + Range(clip.row1[1]) * y + Range(clip.row1[2]);
    RasterBounds local{local_x.low, local_y.low, local_x.high, local_y.high};
    if (!finite_bounds(local))
        return false;
    // Rounded-box distance is 1-Lipschitz; bound both screen derivatives and float SDF error.
    auto derivative = (std::hypot(double(clip.row0[0]), double(clip.row1[0])) +
                       std::hypot(double(clip.row0[1]), double(clip.row1[1]))) /
                      scale;
    double magnitude = 1;
    double coordinates[]{local.left, local.top, local.right, local.bottom};
    for (auto value : coordinates)
        magnitude += std::fabs(value);
    for (auto value : clip.rect)
        magnitude += std::fabs(value);
    magnitude += std::fabs(clip.params[0]);
    if (!std::isfinite(derivative) || magnitude > 1.0e12)
        return false;
    auto margin = 4 * std::fmax(derivative, 0.0001) + magnitude * 0x1p-16;
    RasterBounds expanded{clip.rect[0] - margin, clip.rect[1] - margin,
                          double(clip.rect[0]) + clip.rect[2] + margin,
                          double(clip.rect[1]) + clip.rect[3] + margin};
    return outside(local, expanded);
}
} // namespace visibility

} // namespace vui::gpu::detail
