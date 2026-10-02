export module vui.render:content;
import vui;
import vrento.sampled_frame;
using namespace rstd::prelude;

export namespace vui::gpu {
class NativeTextureFrame {
    vrento::resource::SampledFrame frame_;
    TextureFrameInfo info_;
    NativeTextureFrame(vrento::resource::SampledFrame frame, TextureFrameInfo info)
        : frame_(rstd::move(frame)), info_(info) {}

  public:
    auto native() const -> const vrento::resource::SampledFrame & { return frame_; }
    auto info() const -> TextureFrameInfo { return info_; }
    static auto wrap(vrento::resource::SampledFrame frame) -> Result<TextureFrame, ContentError> {
        if (!frame.valid())
            return Err(ContentError::InvalidValue);
        const auto &d = frame.description();
        TextureFrameInfo info;
        info.producer_session = d.producer_session;
        info.frame_version = d.frame_version;
        info.device_epoch = d.device_epoch;
        info.width = d.extent.width;
        info.height = d.extent.height;
        switch (d.format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
            info.format = ContentFormat::Rgba8Unorm;
            break;
        case VK_FORMAT_R8G8B8A8_SRGB:
            info.format = ContentFormat::Rgba8Srgb;
            break;
        case VK_FORMAT_B8G8R8A8_UNORM:
            info.format = ContentFormat::Bgra8Unorm;
            break;
        case VK_FORMAT_B8G8R8A8_SRGB:
            info.format = ContentFormat::Bgra8Srgb;
            break;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            info.format = ContentFormat::Rgba16Float;
            break;
        default:
            return Err(ContentError::InvalidValue);
        }
        using namespace vrento::resource;
        info.color_space = d.color_space == FrameColorSpace::Srgb ? ContentColorSpace::Srgb
                                                                  : ContentColorSpace::LinearSrgb;
        info.alpha = d.alpha == FrameAlpha::Straight ? ContentAlpha::Straight
                     : d.alpha == FrameAlpha::Opaque ? ContentAlpha::Opaque
                                                     : ContentAlpha::Premultiplied;
        info.origin = d.origin == FrameOrigin::BottomLeft ? ContentOrigin::BottomLeft
                                                          : ContentOrigin::TopLeft;
        return TextureFrame::make(info, NativeTextureFrame(rstd::move(frame), info));
    }
};
} // namespace vui::gpu
