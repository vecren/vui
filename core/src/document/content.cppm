export module vui:document.content;
import :image.types;
import :content.source;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
struct DocumentImageContent {
    Option<ImageData> data;
    ImageFit fit{ImageFit::Contain};
    ImageSampling sampling{ImageSampling::Linear};
    Color tint{1, 1, 1, 1};
    ImageRadialFill radial;
};

struct DocumentSceneContent {
    Option<ContentSource> source;
    ImageFit fit{ImageFit::Contain};
    ImageSampling sampling{ImageSampling::Linear};
    Color placeholder{0.15, 0.15, 0.15, 1};
};
} // namespace vui
