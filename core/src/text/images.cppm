export module vui:text.images;
import :text.types;
import :image.types;
using namespace rstd::prelude;

export namespace vui {
struct TextInlineImage {
    ImageData image;
    Rect source;
    Color tint{1, 1, 1, 1};
    ImageSampling sampling{ImageSampling::Linear};
    ImageBlend blend{ImageBlend::SourceOver};
    auto valid() const -> bool {
        return image.valid_source(source) && source.width > 0 && source.height > 0 && tint.valid() &&
            (sampling == ImageSampling::Nearest || sampling == ImageSampling::Linear) &&
            vui::valid(blend);
    }
    auto operator==(const TextInlineImage &other) const -> bool {
        return image.id() == other.image.id() && source == other.source && tint == other.tint && sampling == other.sampling && blend == other.blend;
    }
};
struct TextInlineImagePlacement { Index image; Rect destination; };
class TextInlineImages {
    struct Data {
        TextInlineObjects objects;
        Vec<TextInlineImage> images;
        Data(TextInlineObjects objects, Vec<TextInlineImage> images) : objects(rstd::move(objects)), images(rstd::move(images)) {}
    };
    Option<rstd::sync::Arc<Data>> data_;
    TextInlineImages(TextInlineObjects objects, Vec<TextInlineImage> images)
        : data_(Some(rstd::sync::Arc<Data>::make(rstd::move(objects), rstd::move(images)))) {}
  public:
    TextInlineImages() = default;
    TextInlineImages(const TextInlineImages &other) : data_(other.data_.is_some() ? Some(other.data_->clone()) : None<rstd::sync::Arc<Data>>()) {}
    auto operator=(const TextInlineImages &other) -> TextInlineImages & {
        if (this != &other) data_ = other.data_.is_some() ? Some(other.data_->clone()) : None<rstd::sync::Arc<Data>>();
        return *this;
    }
    TextInlineImages(TextInlineImages &&) = default;
    auto operator=(TextInlineImages &&) -> TextInlineImages & = default;
    static auto create(TextInlineObjects objects, slice<TextInlineImage> images) -> Result<TextInlineImages, TextError> {
        if (objects.objects().len() != images.len()) return Err(TextError::InvalidValue);
        Vec<TextInlineImage> checked;
        for (const auto &image : images) {
            if (!image.valid()) return Err(TextError::InvalidValue);
            checked.push(TextInlineImage(image));
        }
        if (images.is_empty()) return Ok(TextInlineImages{});
        return Ok(TextInlineImages(rstd::move(objects), rstd::move(checked)));
    }
    auto images() const -> slice<TextInlineImage> { return data_.is_some() ? (*data_)->images.as_slice() : slice<TextInlineImage>{}; }
    auto objects() const -> TextInlineObjects { return data_.is_some() ? TextInlineObjects((*data_)->objects) : TextInlineObjects{}; }
    auto matches(ref<str> text) const -> bool { return data_.is_none() || (*data_)->objects.matches(text); }
    auto placements(const TextLayout &layout, Point origin = {}) const -> Result<Vec<TextInlineImagePlacement>, TextError> {
        if (!matches(layout.text()) || objects() != layout.inline_objects() || !finite(origin.x) || !finite(origin.y)) return Err(TextError::InvalidValue);
        Vec<TextInlineImagePlacement> result;
        for (const auto &box : layout.inline_boxes()) {
            Rect bounds{box.ink.x + origin.x, box.ink.y + origin.y, box.ink.width, box.ink.height};
            if (!bounds.valid()) return Err(TextError::InvalidValue);
            result.push(TextInlineImagePlacement{box.object, bounds});
        }
        return Ok(rstd::move(result));
    }
    auto operator==(const TextInlineImages &other) const -> bool {
        if (data_.is_none() || other.data_.is_none()) return data_.is_none() && other.data_.is_none();
        return rstd::sync::Arc<Data>::ptr_eq(*data_, *other.data_) ||
            (objects() == other.objects() && images() == other.images());
    }
};
} // namespace vui
