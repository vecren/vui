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
    void clear();
    auto resolve(const FontFace &font, Index glyph, Scalar scale, Index max_extent, Scalar outline = 0)
        -> Result<GlyphEntry, Error>;
};
auto glyph_scale(Transform transform, Scalar surface_scale) -> Result<Scalar, Error>;

} // namespace vui::gpu::detail
