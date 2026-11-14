module;
#include <cmath>
module vui.render;
import :images;
using namespace rstd::prelude;

namespace vui::gpu::detail {
struct SrgbPremultiplication {
    unsigned char values[256 * 256];
    SrgbPremultiplication() {
        for (unsigned alpha{}; alpha < 256; ++alpha)
            for (unsigned channel{}; channel < 256; ++channel) {
                auto value = channel / 255.0;
                auto linear = (value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4)) * alpha / 255.0;
                auto encoded = linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
                values[alpha * 256 + channel] = static_cast<unsigned char>(std::round(encoded * 255));
            }
    }
};

auto ImageCache::reserve_frame(const FramePlan &plan, Index max_extent) -> Result<empty, Error> {
    if (!limits.evict_unused) return Ok(empty{});
    Vec<ImageData> required;
    Serial bytes{};
    for (Index pass{}; pass < plan.pass_count().to_primitive(); ++pass)
        for (const auto &draw : plan.draws(pass)) {
            if (!draw.is_Image()) continue;
            const auto &image = draw.as_Image().data;
            bool known{};
            for (const auto &value : required) known |= value.id() == image.id();
            if (known) continue;
            if (image.width() > max_extent || image.height() > max_extent)
                return Err(Error{Stage::ImageCacheLimit});
            bytes += image.byte_size();
            required.push(ImageData(image));
        }
    if (required.len() > usize(limits.max_entries) || bytes > limits.max_bytes)
        return Err(Error{Stage::ImageCacheLimit});
    Serial added_bytes{};
    Index added_entries{};
    for (const auto &image : required) {
        bool cached{};
        for (const auto &entry : entries) cached |= entry.data.id() == image.id();
        if (!cached) { added_bytes += image.byte_size(); ++added_entries; }
    }
    auto total_entries = stats.entries + added_entries;
    auto total_bytes = stats.cpu_bytes + added_bytes;
    // Reserve before resolving indices; completed frames retain their GPU bindings.
    entries.retain([&](const ImageEntry &entry) {
        if (total_entries <= limits.max_entries && total_bytes <= limits.max_bytes) return true;
        for (const auto &image : required) if (image.id() == entry.data.id()) return true;
        auto size = entry.data.byte_size();
        --total_entries; total_bytes -= size;
        --stats.entries; stats.cpu_bytes -= size;
        if (entry.gpu.is_some()) stats.gpu_bytes -= size;
        ++stats.evictions;
        return false;
    });
    return Ok(empty{});
}

void ImageCache::clear() {
    entries.clear();
    stats.cpu_bytes = stats.gpu_bytes = 0;
    stats.entries = 0;
    ++stats.clears;
}

auto ImageCache::resolve(const ImageData &data, Index max_extent) -> Result<Index, Error> {
    for (usize i{}; i < entries.len(); ++i)
        if (entries[i].data.id() == data.id()) {
            ++stats.hits;
            return Ok(static_cast<Index>(i.to_primitive()));
        }
    ++stats.misses;
    auto bytes = data.byte_size();
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

auto premultiply_srgb(slice<unsigned char> pixels) -> Vec<unsigned char> {
    static const SrgbPremultiplication conversion;
    Vec<unsigned char> result;
    result.reserve(pixels.len());
    for (usize i{}; i < pixels.len(); i += usize(4)) {
        auto alpha = unsigned(pixels[i + usize(3)]);
        for (usize c{}; c < usize(3); ++c) {
            result.push(static_cast<unsigned char>(conversion.values[alpha * 256 + pixels[i + c]]));
        }
        result.push(static_cast<unsigned char>(pixels[i + usize(3)]));
    }
    return result;
}
} // namespace vui::gpu::detail
