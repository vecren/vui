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
    auto reserve_frame(const FramePlan &plan, Index max_extent) -> Result<empty, Error>;
    void clear();
    auto resolve(const ImageData &data, Index max_extent) -> Result<Index, Error>;
};

auto premultiply_srgb(slice<unsigned char> pixels) -> Vec<unsigned char>;

} // namespace vui::gpu::detail
