export module vui.image:reader;
export import vui;
export import rstd;
using namespace rstd::prelude;

export namespace vui {
enum class UnmarkedImageColor { Reject, AssumeSrgb };
enum class ImageReadStage { Io, InvalidInput, Unsupported, Color, Limit, Decode, Pixels };
struct ImageReadError {
    ImageReadStage stage;
    Option<rstd::io::error::Error> io;
    Option<ImageError> pixels;
};
struct ImageReadOptions {
    Scalar density{1};
    UnmarkedImageColor unmarked_color{UnmarkedImageColor::Reject};
    Serial max_encoded_bytes{32 * 1024 * 1024};
    Serial max_decoded_bytes{64 * 1024 * 1024};
    Serial max_working_bytes{128 * 1024 * 1024};
};
class ImageReader {
    ImageReadOptions options_;

  public:
    explicit ImageReader(ImageReadOptions options = {}) : options_(options) {}
    auto read(slice<unsigned char> bytes) const -> Result<ImageData, ImageReadError>;
    auto read(ref<rstd::path::Path> path) const -> Result<ImageData, ImageReadError>;
};
} // namespace vui
