module;
#include <cmath>
module vui.render:images;
import :renderer;
import :sampled;
using namespace rstd::prelude;
namespace vui::gpu::detail {
struct ImageEntry {
    ImageData data;
    Option<rstd::sync::Arc<SampledImage>> gpu;
    bool resident{};
};
struct ImageCache {
    ImageCacheLimits limits;
    ImageCacheStats stats;
    Vec<ImageEntry> entries;
    void clear() {
        entries.clear();
        stats.cpu_bytes = stats.gpu_bytes = 0;
        stats.entries = 0;
        ++stats.clears;
    }
    auto resolve(const ImageData &data, Index max_extent) -> Result<Index, Error> {
        for (usize i{}; i < entries.len(); ++i)
            if (entries[i].data.id() == data.id()) {
                ++stats.hits;
                return Ok(static_cast<Index>(i.to_primitive()));
            }
        ++stats.misses;
        auto bytes = data.pixels().len().to_primitive();
        if (entries.len() >= usize(limits.max_entries) || data.width() > max_extent ||
            data.height() > max_extent || bytes > limits.max_bytes ||
            stats.cpu_bytes > limits.max_bytes - bytes)
            return Err(Error{Stage::ImageCacheLimit});
        auto index = static_cast<Index>(entries.len().to_primitive());
        entries.push(ImageEntry{ImageData(data), {}, false});
        stats.cpu_bytes += bytes;
        stats.entries = index + 1;
        return Ok(index);
    }
};
auto premultiply_srgb(slice<unsigned char> pixels) -> Vec<unsigned char> {
    Vec<unsigned char> result;
    result.reserve(pixels.len());
    for (usize i{}; i < pixels.len(); i += usize(4)) {
        auto alpha = pixels[i + usize(3)] / 255.0;
        for (usize c{}; c < usize(3); ++c) {
            auto value = pixels[i + c] / 255.0;
            auto linear =
                (value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4)) * alpha;
            auto encoded =
                linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
            result.push(static_cast<unsigned char>(std::round(encoded * 255)));
        }
        result.push(static_cast<unsigned char>(pixels[i + usize(3)]));
    }
    return result;
}

} // namespace vui::gpu::detail
