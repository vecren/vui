module;
#include <cstring>
module vui.render:sampled;
import :renderer;
using namespace rstd::prelude;
namespace vui::gpu::detail {
namespace vk = ::vrento::vulkan;
struct SampledImage {
    vrento::texture::TextureRef image;
    vrento::texture::TextureViewRef view;
    VkExtent2D extent;
};
auto make_sampled(vk::TextureContext &textures, VkExtent2D extent, VkFormat format)
    -> Result<rstd::sync::Arc<SampledImage>, Error> {
    const auto &device = textures.session().device();
    auto features = device.gpu().GetFormatProperties(format).optimalTilingFeatures;
    constexpr auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                              VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((features & required) != required)
        return Err(Error{Stage::Unsupported});
    SampledImage gpu;
    gpu.extent = extent;
    vrento::texture::TextureDesc info;
    info.extent = {extent.width, extent.height, 1};
    info.format = format == VK_FORMAT_R8_UNORM ? vrento::TextureFormat::R8
                                               : vrento::TextureFormat::RGBA8_SRGB;
    auto image = textures.CreateTexture(info);
    if (image.is_err())
        return Err(Error{Stage::Allocation, VkResult(image.unwrap_err().native)});
    gpu.image = image.unwrap();
    auto view = textures.CreateView(gpu.image, {});
    if (view.is_err())
        return Err(Error{Stage::Allocation, VkResult(view.unwrap_err().native)});
    gpu.view = view.unwrap();
    return Ok(rstd::sync::Arc<SampledImage>::make(rstd::move(gpu)));
}
auto make_upload(rstd::sync::Arc<SampledImage> image, slice<unsigned char> pixels)
    -> Result<vrento::texture::TextureUpload, Error> {
    vrento::texture::TextureUpload upload;
    upload.target = image->view;
    upload.width = image->extent.width;
    upload.height = image->extent.height;
    upload.bytes.reserve(pixels.len());
    for (auto byte : pixels)
        upload.bytes.push(u8(byte));
    return Ok(rstd::move(upload));
}

} // namespace vui::gpu::detail
