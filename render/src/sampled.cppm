module vui.render:sampled;
import :renderer;
using namespace rstd::prelude;
namespace vui::gpu::detail {
namespace vk = ::vrento::vulkan;
struct SampledImage {
    vrento::texture::TextureRef image;
    vrento::texture::TextureViewRef view;
    VkExtent2D extent;
    Serial bytes{};
    Option<vrento::texture::SamplerRef> nearest,linear;
};
auto make_sampled(vk::TextureContext &textures, VkExtent2D extent, VkFormat format, Index mip_levels=1)
    -> Result<rstd::sync::Arc<SampledImage>, Error>;
auto make_upload(rstd::sync::Arc<SampledImage> image, slice<unsigned char> pixels, Index mip=0)
    -> Result<vrento::texture::TextureUpload, Error>;
auto make_image_sampler(vk::TextureContext &textures,const ImageData &image,bool linear)
    -> Result<vrento::texture::SamplerRef,Error>;

} // namespace vui::gpu::detail
