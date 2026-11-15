export module vui:image.types;
import :types;
import :metadata;
using namespace rstd::prelude;

export namespace vui {
inline constexpr Index image_max_extent = 16384;
inline constexpr Serial image_max_pixels = 16 * 1024 * 1024;
enum class ImageError { InvalidValue, Limit };
enum class ImageFit { Fill, Contain, Cover };
enum class ImageSampling { Nearest, Linear };
enum class ContentBounds { RequireInside, AllowOutside };
// Additive matches straight-alpha SrcAlpha/One for both color and alpha.
enum class ImageBlend { SourceOver, Additive, AdditiveRgb, Plus };
constexpr auto valid(ImageBlend value) -> bool {
    return value == ImageBlend::SourceOver || value == ImageBlend::Additive || value == ImageBlend::AdditiveRgb || value == ImageBlend::Plus;
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
enum class ImageWrap { Clamp, Repeat, Mirror };
enum class ImageMipmapSampling { None, Nearest, Linear };
struct ImageTextureOptions {
    ImageWrap wrap_u{ImageWrap::Clamp},wrap_v{ImageWrap::Clamp};
    ImageMipmapSampling mipmaps{ImageMipmapSampling::None};
    Scalar mip_bias{};
    auto valid() const -> bool {
        return unsigned(wrap_u)<=unsigned(ImageWrap::Mirror) && unsigned(wrap_v)<=unsigned(ImageWrap::Mirror) &&
            unsigned(mipmaps)<=unsigned(ImageMipmapSampling::Linear) && finite(mip_bias) && mip_bias>=-16 && mip_bias<=16;
    }
    auto operator==(const ImageTextureOptions &) const -> bool=default;
};
struct ImageMipLevel {Index width,height;slice<unsigned char> pixels;};
class ImageData {
    struct Data {
        Serial id{new_session()};
        Index width,height;
        Scalar density;
        ImageTextureOptions options;
        Vec<Vec<unsigned char>> levels;
        Serial bytes;
        Data(Index w,Index h,Scalar d,ImageTextureOptions o,Vec<Vec<unsigned char>> p,Serial b)
            :width(w),height(h),density(d),options(o),levels(rstd::move(p)),bytes(b){}
    };
    rstd::sync::Arc<Data> data_;
    Option<detail::FrameMetadata> metadata_;
    ImageData(Index w,Index h,Scalar d,ImageTextureOptions o,Vec<Vec<unsigned char>> p,Serial b)
        :data_(rstd::sync::Arc<Data>::make(w,h,d,o,rstd::move(p),b)){}
  public:
    ImageData(const ImageData &other):data_(other.data_.clone()){if(other.metadata_)metadata_=Some(other.metadata_->clone());}
    auto operator=(const ImageData &other) -> ImageData & {
        if(this!=&other){data_=other.data_.clone();metadata_=other.metadata_ ? Some(other.metadata_->clone()):None<detail::FrameMetadata>();}
        return *this;
    }
    ImageData(ImageData &&)=default;
    auto operator=(ImageData &&) -> ImageData & =default;
    static auto validate_dimensions(Index width,Index height,Scalar density=1) -> Result<empty,ImageError> {
        if(!width || !height || !finite(density) || density<=0 || !finite(width/density) || !finite(height/density))return Err(ImageError::InvalidValue);
        if(width>image_max_extent || height>image_max_extent || Serial(width)*height>image_max_pixels)return Err(ImageError::Limit);
        return Ok(empty{});
    }
    static auto rgba8_srgb_mipmaps(slice<ImageMipLevel> levels,ImageTextureOptions options={},Scalar density=1) -> Result<ImageData,ImageError> {
        if(levels.is_empty() || levels.len()>usize(15) || !options.valid())return Err(ImageError::InvalidValue);
        auto valid=validate_dimensions(levels[usize()].width,levels[usize()].height,density);
        if(valid.is_err())return Err(valid.unwrap_err());
        Index width=levels[usize()].width,height=levels[usize()].height;Serial bytes{};
        for(usize i{};i<levels.len();++i){
            const auto &level=levels[i];
            if(level.width!=width || level.height!=height || level.pixels.len().to_primitive()!=Serial(width)*height*4)return Err(ImageError::InvalidValue);
            bytes+=level.pixels.len().to_primitive();
            if(bytes>96ull*1024*1024)return Err(ImageError::Limit);
            if(width==1 && height==1 && i+usize(1)<levels.len())return Err(ImageError::InvalidValue);
            width=width>1?width/2:1;height=height>1?height/2:1;
        }
        Vec<Vec<unsigned char>> owned;
        for(const auto &level:levels)owned.push(Vec<unsigned char>::from(level.pixels));
        return Ok(ImageData(levels[usize()].width,levels[usize()].height,density,options,rstd::move(owned),bytes));
    }
    static auto rgba8_srgb(Index width,Index height,slice<unsigned char> pixels,Scalar density=1) -> Result<ImageData,ImageError> {
        const ImageMipLevel level{width,height,pixels};
        return rgba8_srgb_mipmaps(slice<ImageMipLevel>::from_raw_parts(&level,usize(1)),{},density);
    }
    auto id() const -> Serial {return data_->id;}
    auto operator==(const ImageData &other) const -> bool;
    template<class T> auto with_metadata(T value,bool concurrent_read=false) const -> ImageData {
        auto result=ImageData(*this);result.metadata_=Some(detail::FrameMetadata::make(rstd::move(value),concurrent_read));return result;
    }
    template<class T> auto metadata() const -> const T * {return metadata_ ? metadata_->get<T>():nullptr;}
    auto concurrent_read() const -> bool;
    auto without_metadata() const -> ImageData;
    auto width() const -> Index {return data_->width;}
    auto height() const -> Index {return data_->height;}
    auto density() const -> Scalar {return data_->density;}
    auto natural_size() const -> Size {return {width()/density(),height()/density()};}
    auto pixels() const -> slice<unsigned char> {return data_->levels[usize()].as_slice();}
    auto byte_size() const -> Serial {return data_->bytes;}
    auto mip_count() const -> Index {return Index(data_->levels.len().to_primitive());}
    auto texture_options() const -> ImageTextureOptions {return data_->options;}
    auto valid_source(Rect source) const -> bool {
        if(!source.valid())return false;
        return (data_->options.wrap_u!=ImageWrap::Clamp || (source.x>=0 && source.x+source.width<=width())) &&
            (data_->options.wrap_v!=ImageWrap::Clamp || (source.y>=0 && source.y+source.height<=height()));
    }
    auto mip_level(Index index) const -> Result<ImageMipLevel,ImageError> {
        if(index>=mip_count())return Err(ImageError::InvalidValue);
        Index w=width()>>index,h=height()>>index;
        return Ok(ImageMipLevel{w?w:1,h?h:1,data_->levels[usize(index)].as_slice()});
    }
};
enum class ImageEdges : unsigned { None = 0, Left = 1, Top = 2, Right = 4, Bottom = 8, All = 15 };
inline constexpr auto operator|(ImageEdges left, ImageEdges right) -> ImageEdges {
    return static_cast<ImageEdges>(static_cast<unsigned>(left) | static_cast<unsigned>(right));
}
struct ImagePlacement {
    Rect destination, source;
    ImageEdges edges{ImageEdges::All};
    auto content_position(Point local, ContentBounds bounds = ContentBounds::RequireInside) const -> Option<Point>;
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
