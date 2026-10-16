module;
#include <cmath>
#include <cstring>
module vui.render:atlas;
import :renderer;
import :sampled;
using namespace rstd::prelude;
namespace vui::gpu::detail {
struct AtlasPage {
    Index width{}, height{}, x{1}, y{1}, row_height{};
    Vec<unsigned char> pixels;
    Option<rstd::sync::Arc<SampledImage>> gpu;
    bool resident{};
};
struct GlyphEntry {
    Serial font;
    Index glyph;
    Scalar scale, outline;
    Index page{~Index{}}, x{}, y{}, width{}, height{};
    Point bearing;
};
struct GlyphCache {
    TextCacheLimits limits;
    TextCacheStats stats;
    Vec<AtlasPage> pages;
    Vec<GlyphEntry> entries;
    void clear() {
        pages.clear();
        entries.clear();
        stats.cpu_bytes = stats.gpu_bytes = 0;
        stats.pages = stats.entries = 0;
        ++stats.clears;
    }
    auto resolve(const FontFace &font, Index glyph, Scalar scale, Index max_extent, Scalar outline = 0)
        -> Result<GlyphEntry, Error> {
        for (const auto &entry : entries)
            if (entry.font == font.id() && entry.glyph == glyph && entry.scale == scale && entry.outline == outline) {
                ++stats.hits;
                return Ok(GlyphEntry(entry));
            }
        ++stats.misses;
        if (entries.len() >= usize(limits.max_entries))
            return Err(Error{Stage::TextCacheLimit});
        GlyphBitmap bitmap;
        if (outline > 0) {
            auto original = resolve(font, glyph, scale, max_extent);
            if (original.is_err()) return Err(original.unwrap_err());
            auto base = original.unwrap();
            if (entries.len() >= usize(limits.max_entries)) return Err(Error{Stage::TextCacheLimit});
            if (base.width && base.height) {
                if (!finite(outline) || outline > 64) return Err(Error{Stage::TextCacheLimit});
                auto radius = Index(std::ceil(outline));
                if (max_extent < 2 * radius + 3 ||
                    base.width > max_extent - 2 * radius - 2 ||
                    base.height > max_extent - 2 * radius - 2)
                    return Err(Error{Stage::TextCacheLimit});
                bitmap.width = base.width + radius * 2;
                bitmap.height = base.height + radius * 2;
                bitmap.origin = {base.bearing.x - radius, base.bearing.y - radius};
                bitmap.coverage.resize(usize(bitmap.width) * usize(bitmap.height), 0);
                struct Neighbor { int x, y; Scalar weight; };
                Vec<Neighbor> neighbors;
                for (int y = -int(radius); y <= int(radius); ++y)
                    for (int x = -int(radius); x <= int(radius); ++x) {
                        auto weight = outline + 1 - std::hypot(Scalar(x), Scalar(y));
                        if (weight > 0) neighbors.push({x, y, weight > 1 ? 1 : weight});
                    }
                const auto &page = pages[usize(base.page)];
                const auto *source = page.pixels.data();
                auto *target = bitmap.coverage.data();
                for (Index y{}; y < base.height; ++y)
                    for (Index x{}; x < base.width; ++x) {
                        auto coverage = source[(base.y + y) * page.width + base.x + x];
                        if (!coverage) continue;
                        for (const auto &n : neighbors) {
                            auto offset = (y + radius + n.y) * bitmap.width + x + radius + n.x;
                            auto value = static_cast<unsigned char>(std::round(coverage * n.weight));
                            if (value > target[offset]) target[offset] = value;
                        }
                    }
            }
        } else {
            ++stats.rasterizations;
            auto raster = font.rasterize(glyph, scale);
            if (raster.is_err())
                return Err(Error{Stage::TextRaster, VK_SUCCESS, Some(raster.unwrap_err())});
            bitmap = rstd::move(raster).unwrap();
        }
        GlyphEntry entry{font.id(), glyph, scale, outline};
        entry.width = bitmap.width;
        entry.height = bitmap.height;
        entry.bearing = {bitmap.origin.x, bitmap.origin.y};
        if (bitmap.width && bitmap.height) {
            if (max_extent < 3 || bitmap.width > max_extent - 2 || bitmap.height > max_extent - 2)
                return Err(Error{Stage::TextCacheLimit});
            Index page_index{};
            Index x{}, y{}, row{};
            for (; page_index < pages.len().to_primitive(); ++page_index) {
                auto &page = pages[usize(page_index)];
                x = page.x;
                y = page.y;
                row = page.row_height;
                if (bitmap.width + 2 > page.width || bitmap.height + 2 > page.height)
                    continue;
                if (x + bitmap.width + 1 > page.width) {
                    x = 1;
                    y += row + 2;
                    row = 0;
                }
                if (y + bitmap.height + 1 <= page.height)
                    break;
            }
            if (page_index == pages.len().to_primitive()) {
                auto width =
                    bitmap.width + 2 > limits.page_extent ? bitmap.width + 2 : limits.page_extent;
                auto height =
                    bitmap.height + 2 > limits.page_extent ? bitmap.height + 2 : limits.page_extent;
                auto bytes = Serial(width) * height;
                if (pages.len() >= usize(limits.max_pages) || width > max_extent ||
                    height > max_extent || bytes > limits.max_bytes ||
                    stats.cpu_bytes > limits.max_bytes - bytes)
                    return Err(Error{Stage::TextCacheLimit});
                AtlasPage page;
                page.width = width;
                page.height = height;
                page.pixels.reserve(usize(bytes));
                for (Serial i{}; i < bytes; ++i)
                    page.pixels.push(0);
                pages.push(rstd::move(page));
                stats.cpu_bytes += bytes;
                stats.pages = static_cast<Index>(pages.len().to_primitive());
                x = y = 1;
                row = 0;
            }
            auto &page = pages[usize(page_index)];
            if (page.gpu.is_some()) {
                stats.gpu_bytes -= Serial(page.width) * page.height;
                page.gpu = None();
            }
            page.resident = false;
            for (Index i{}; i < bitmap.height; ++i)
                std::memcpy(page.pixels.as_mut_slice().as_raw_ptr() + (y + i) * page.width + x,
                            bitmap.coverage.as_slice().as_raw_ptr() + i * bitmap.width,
                            bitmap.width);
            page.x = x + bitmap.width + 2;
            page.y = y;
            page.row_height = bitmap.height > row ? bitmap.height : row;
            entry.page = page_index;
            entry.x = x;
            entry.y = y;
        }
        entries.push(GlyphEntry(entry));
        stats.entries = static_cast<Index>(entries.len().to_primitive());
        return Ok(rstd::move(entry));
    }
};
auto glyph_scale(Transform transform, Scalar surface_scale) -> Result<Scalar, Error> {
    auto aa = transform.a * transform.a + transform.b * transform.b;
    auto bb = transform.c * transform.c + transform.d * transform.d;
    auto ab = transform.a * transform.c + transform.b * transform.d;
    auto magnitude = std::sqrt((aa + bb + std::hypot(aa - bb, 2 * ab)) / 2);
    auto scale = std::ceil(magnitude * surface_scale * 8) / 8;
    if (!finite(scale) || scale > 64)
        return Err(Error{Stage::Unsupported});
    return Ok(scale < 0.125 ? 0.125 : scale);
}

} // namespace vui::gpu::detail
