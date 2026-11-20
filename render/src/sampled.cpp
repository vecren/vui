module;
#include <cstring>
module vui.render;
import :sampled;
using namespace rstd::prelude;

namespace vui::gpu::detail {
auto make_sampled(vk::TextureContext &textures, VkExtent2D extent, VkFormat format, Index mip_levels)
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
    info.mip_levels=mip_levels;
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

auto make_upload(rstd::sync::Arc<SampledImage> image, slice<unsigned char> pixels, Index mip)
    -> Result<vrento::texture::TextureUpload, Error> {
    vrento::texture::TextureUpload upload;
    upload.target = image->view;
    upload.mip=mip;
    upload.width = image->extent.width>>mip; if(!upload.width)upload.width=1;
    upload.height = image->extent.height>>mip; if(!upload.height)upload.height=1;
    Vec<u8> bytes;
    bytes.resize(pixels.len(), u8(0));
    std::memcpy(bytes.as_mut_slice().as_raw_ptr(), pixels.as_raw_ptr(), pixels.len().to_primitive());
    upload.bytes = rstd::move(bytes);
    return Ok(rstd::move(upload));
}

auto make_image_sampler(vk::TextureContext &textures,const ImageData &image,bool linear)
    -> Result<vrento::texture::SamplerRef,Error> {
    auto options=image.texture_options();
    auto wrap=[](ImageWrap value){return value==ImageWrap::Repeat?vrento::TextureWrap::REPEAT:
        value==ImageWrap::Mirror?vrento::TextureWrap::MIRRORED_REPEAT:vrento::TextureWrap::CLAMP_TO_EDGE;};
    vrento::texture::SamplerDesc desc;
    desc.sample.wrapS=wrap(options.wrap_u);desc.sample.wrapT=wrap(options.wrap_v);
    desc.sample.magFilter=desc.sample.minFilter=linear?vrento::TextureFilter::LINEAR:vrento::TextureFilter::NEAREST;
    desc.sample.mipFilter=options.mipmaps==ImageMipmapSampling::None?vrento::TextureMipmapFilter::None:
        options.mipmaps==ImageMipmapSampling::Nearest?vrento::TextureMipmapFilter::Nearest:vrento::TextureMipmapFilter::Linear;
    desc.max_lod=options.mipmaps==ImageMipmapSampling::None?0:float(image.mip_count()-1);desc.lod_bias=float(options.mip_bias);
    auto limit=textures.session().device().capabilities().max_sampler_anisotropy;
    desc.max_anisotropy=rstd::min(float(options.max_anisotropy),limit);
    auto sampler=textures.CreateSampler(desc);
    if(sampler.is_err())return Err(Error::texture(Stage::Allocation,sampler.unwrap_err()));
    return Ok(rstd::move(sampler).unwrap());
}
} // namespace vui::gpu::detail
