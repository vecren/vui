module vui.render;
import :content;
using namespace rstd::prelude;

namespace vui::gpu {
auto NativeTextureFrame::wrap(vrento::texture::TextureViewRef view,
                 TextureFrameInfo info)
    -> Result<TextureFrame, ContentError> {
    if (!view.valid() || !view.permits(false))
        return Err(ContentError::InvalidValue);
    auto shared = view.share_read();
    if (shared.is_err()) return Err(shared.unwrap_err().kind == vrento::texture::ErrorKind::Busy
                                        ? ContentError::Busy : ContentError::InvalidValue);
    view = rstd::move(shared).unwrap();
    const auto &desc = view.texture().description();
    const auto range = view.description().range;
    if (view.description().dimension != vrento::texture::Dimension::D2 ||
        range.layer_count != 1 || range.mip_count != 1)
        return Err(ContentError::InvalidValue);
    auto extent = vrento::texture::mip_extent(desc, range.first_mip);
    info.width = extent.width;
    info.height = extent.height;
    info.device_epoch = view.texture().identity().epoch;
    switch (desc.format) {
    case vrento::TextureFormat::RGBA8:
        info.format = ContentFormat::Rgba8Unorm;
        break;
    case vrento::TextureFormat::RGBA8_SRGB:
        info.format = ContentFormat::Rgba8Srgb;
        break;
    case vrento::TextureFormat::BGRA8:
        info.format = ContentFormat::Bgra8Unorm;
        break;
    case vrento::TextureFormat::BGRA8_SRGB:
        info.format = ContentFormat::Bgra8Srgb;
        break;
    case vrento::TextureFormat::RGBA16F:
        info.format = ContentFormat::Rgba16Float;
        break;
    case vrento::TextureFormat::R11G11B10F:
        info.format = ContentFormat::R11G11B10Float;
        break;
    default:
        return Err(ContentError::InvalidValue);
    }
    if ((info.format == ContentFormat::Rgba8Srgb ||
         info.format == ContentFormat::Bgra8Srgb) &&
        info.color_space != ContentColorSpace::Srgb)
        return Err(ContentError::InvalidValue);
    return TextureFrame::make(info,
                              NativeTextureFrame(rstd::move(view), info), true);
}
} // namespace vui::gpu
