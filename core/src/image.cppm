export module vui:image;
import :types;
using namespace rstd::prelude;

export namespace vui {
inline constexpr Index image_max_extent = 16384;
inline constexpr Serial image_max_pixels = 16 * 1024 * 1024;
enum class ImageError { InvalidValue, Limit };
enum class ImageFit { Fill, Contain, Cover };
enum class ImageSampling { Nearest, Linear };
// Additive matches straight-alpha SrcAlpha/One for both color and alpha.
enum class ImageBlend { SourceOver, Additive, AdditiveRgb };
constexpr auto valid(ImageBlend value) -> bool {
    return value == ImageBlend::SourceOver || value == ImageBlend::Additive || value == ImageBlend::AdditiveRgb;
}
struct ImageRadialFill {
    Scalar amount{1};
    // Radians in the normalized destination rectangle, positive clockwise from the right.
    Scalar start_angle{-1.5707963267948966};
    bool clockwise{true};
    auto valid() const -> bool {
        return finite(amount) && amount >= 0 && amount <= 1 && finite(start_angle);
    }
    auto operator==(const ImageRadialFill &) const -> bool = default;
};
class ImageData {
    struct Data {
        Serial id{new_session()};
        Index width, height;
        Scalar density;
        Vec<unsigned char> pixels;
        Data(Index w, Index h, Scalar d, Vec<unsigned char> p)
            : width(w), height(h), density(d), pixels(rstd::move(p)) {}
    };
    rstd::sync::Arc<Data> data_;
    ImageData(Index w, Index h, Scalar d, Vec<unsigned char> p)
        : data_(rstd::sync::Arc<Data>::make(w, h, d, rstd::move(p))) {}

  public:
    ImageData(const ImageData &other) : data_(other.data_.clone()) {}
    auto operator=(const ImageData &other) -> ImageData & {
        if (this != &other)
            data_ = other.data_.clone();
        return *this;
    }
    ImageData(ImageData &&) = default;
    auto operator=(ImageData &&) -> ImageData & = default;
    static auto validate_dimensions(Index width, Index height, Scalar density = 1)
        -> Result<empty, ImageError> {
        if (!width || !height || !finite(density) || density <= 0 || !finite(width / density) ||
            !finite(height / density))
            return Err(ImageError::InvalidValue);
        if (width > image_max_extent || height > image_max_extent ||
            Serial(width) * height > image_max_pixels)
            return Err(ImageError::Limit);
        return Ok(empty{});
    }
    static auto rgba8_srgb(Index width, Index height, slice<unsigned char> pixels,
                           Scalar density = 1) -> Result<ImageData, ImageError> {
        auto valid = validate_dimensions(width, height, density);
        if (valid.is_err())
            return Err(valid.unwrap_err());
        if (pixels.len().to_primitive() != Serial(width) * height * 4)
            return Err(ImageError::InvalidValue);
        auto owned = Vec<unsigned char>::from(pixels);
        return Ok(ImageData(width, height, density, rstd::move(owned)));
    }
    auto id() const -> Serial { return data_->id; }
    auto width() const -> Index { return data_->width; }
    auto height() const -> Index { return data_->height; }
    auto density() const -> Scalar { return data_->density; }
    auto natural_size() const -> Size { return {width() / density(), height() / density()}; }
    auto pixels() const -> slice<unsigned char> { return data_->pixels.as_slice(); }
};
enum class ImageEdges : unsigned { None = 0, Left = 1, Top = 2, Right = 4, Bottom = 8, All = 15 };
inline constexpr auto operator|(ImageEdges left, ImageEdges right) -> ImageEdges {
    return static_cast<ImageEdges>(static_cast<unsigned>(left) | static_cast<unsigned>(right));
}
struct ImagePlacement {
    Rect destination, source;
    ImageEdges edges{ImageEdges::All};
};
inline auto place_image(Size image, Rect box, ImageFit fit) -> Result<ImagePlacement, ImageError> {
    if (!image.valid() || !image.width || !image.height || !box.valid() ||
        (fit != ImageFit::Fill && fit != ImageFit::Contain && fit != ImageFit::Cover))
        return Err(ImageError::InvalidValue);
    ImagePlacement result{box, {0, 0, Scalar(image.width), Scalar(image.height)}};
    if (!box.width || !box.height || fit == ImageFit::Fill)
        return Ok(result);
    auto sx = box.width / image.width, sy = box.height / image.height;
    if (fit == ImageFit::Contain) {
        auto scale = sx < sy ? sx : sy;
        result.destination.width = image.width * scale;
        result.destination.height = image.height * scale;
        if (result.destination.width > box.width)
            result.destination.width = box.width;
        if (result.destination.height > box.height)
            result.destination.height = box.height;
        result.destination.x += (box.width - result.destination.width) / 2;
        result.destination.y += (box.height - result.destination.height) / 2;
    } else {
        auto scale = sx > sy ? sx : sy;
        result.source.width = sx >= sy ? image.width : box.width / scale;
        result.source.height = sy >= sx ? image.height : box.height / scale;
        if (result.source.width > image.width)
            result.source.width = image.width;
        if (result.source.height > image.height)
            result.source.height = image.height;
        result.source.x = (image.width - result.source.width) / 2;
        result.source.y = (image.height - result.source.height) / 2;
    }
    if (!result.destination.valid() || !result.source.valid())
        return Err(ImageError::InvalidValue);
    return Ok(result);
}
inline auto place_image(const ImageData &image, Rect box, ImageFit fit)
    -> Result<ImagePlacement, ImageError> {
    return place_image(Size{Scalar(image.width()), Scalar(image.height())}, box, fit);
}
} // namespace vui
