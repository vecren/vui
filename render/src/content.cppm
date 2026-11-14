export module vui.render:content;
import vui;
export import vrento;
using namespace rstd::prelude;

export namespace vui::gpu {
class NativeTextureFrame {
    vrento::texture::TextureViewRef view_;
    TextureFrameInfo info_;
    NativeTextureFrame(vrento::texture::TextureViewRef view,
                       TextureFrameInfo info)
        : view_(rstd::move(view)), info_(info) {}

  public:
    auto native() const -> const vrento::texture::TextureViewRef & {
        return view_;
    }
    auto info() const -> TextureFrameInfo { return info_; }
    static auto wrap(vrento::texture::TextureViewRef view,
                     TextureFrameInfo info)
        -> Result<TextureFrame, ContentError>;
};
} // namespace vui::gpu
