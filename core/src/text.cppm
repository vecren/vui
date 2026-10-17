export module vui:text;
export import vectext;
import :types;
using namespace rstd::prelude;

export namespace vui {
struct TextEffects {
    Scalar outline_width{};
    Color outline_color{0, 0, 0, 1};
    Point shadow_offset;
    Color shadow_color{0, 0, 0, 0};
    auto valid() const -> bool {
        return finite(outline_width) && outline_width >= 0 && outline_width <= 16 &&
               outline_color.valid() && shadow_color.valid() &&
               finite(shadow_offset.x) && finite(shadow_offset.y);
    }
    auto operator==(const TextEffects &) const -> bool = default;
};
using vectext::TextError;
using vectext::TextRange;
using vectext::TextWord;
using vectext::TextBoundaries;
using vectext::TextBoundaryBias;
using vectext::TextDirection;
using vectext::TextWrap;
using vectext::TextAlignment;
using vectext::TextAffinity;
using vectext::TextRequest;
using vectext::TextFormat;
using vectext::GlyphBitmap;
using vectext::FontRasterizer;
using vectext::FontInstance;
using vectext::TextGlyph;
using vectext::TextRun;
using vectext::TextLine;
using vectext::TextHit;
using vectext::TextCaret;
using vectext::TextRangeBox;
using vectext::TextInlineUnit;
using vectext::TextInlineObject;
using vectext::TextInlineObjects;
using vectext::TextInlineBox;
using vectext::TextQueries;
using vectext::TextLayoutData;
using vectext::TextLayout;
struct TextColorSpan {
    TextRange range;
    Color color;
    auto operator==(const TextColorSpan &) const -> bool = default;
};
class TextColors {
    struct Data {
        String text;
        Vec<TextColorSpan> spans;
        Data(ref<str> text, Vec<TextColorSpan> spans) : text(String::make(text)), spans(rstd::move(spans)) {}
    };
    Option<rstd::sync::Arc<Data>> data_;
    TextColors(ref<str> text, Vec<TextColorSpan> spans)
        : data_(Some(rstd::sync::Arc<Data>::make(text, rstd::move(spans)))) {}
  public:
    TextColors() = default;
    TextColors(const TextColors &other) : data_(other.data_.is_some() ? Some(other.data_->clone()) : None<rstd::sync::Arc<Data>>()) {}
    auto operator=(const TextColors &other) -> TextColors & {
        if (this != &other) data_ = other.data_.is_some() ? Some(other.data_->clone()) : None<rstd::sync::Arc<Data>>();
        return *this;
    }
    TextColors(TextColors &&) = default;
    auto operator=(TextColors &&) -> TextColors & = default;
    static auto create(ref<str> text, slice<TextColorSpan> spans) -> Result<TextColors, TextError> {
        if (text->len() > usize(~Index{})) return Err(TextError::InvalidValue);
        Vec<TextColorSpan> checked;
        Index end{};
        for (auto span : spans) {
            if (span.range.start < end || span.range.start >= span.range.end || usize(span.range.end) > text->len() ||
                !text->is_char_boundary(usize(span.range.start)) || !text->is_char_boundary(usize(span.range.end)) || !span.color.valid())
                return Err(TextError::InvalidValue);
            end = span.range.end;
            if (!checked.is_empty() && checked[checked.len() - usize(1)].range.end == span.range.start && checked[checked.len() - usize(1)].color == span.color)
                checked[checked.len() - usize(1)].range.end = span.range.end;
            else checked.push(rstd::move(span));
        }
        if (checked.is_empty()) return Ok(TextColors{});
        return Ok(TextColors(text, rstd::move(checked)));
    }
    auto spans() const -> slice<TextColorSpan> { return data_.is_some() ? (*data_)->spans.as_slice() : slice<TextColorSpan>{}; }
    auto matches(ref<str> text) const -> bool { return data_.is_none() || (*data_)->text.as_str() == text; }
    // All glyphs in a shaping cluster use the color at its first byte.
    auto color(Index byte, Color fallback) const -> Color {
        auto values = spans(); usize first{}, last = values.len();
        while (first < last) {
            auto middle = first + (last - first) / usize(2); const auto &span = values[middle];
            if (byte < span.range.start) last = middle;
            else if (byte >= span.range.end) first = middle + usize(1);
            else return span.color;
        }
        return fallback;
    }
    auto operator==(const TextColors &other) const -> bool {
        if (data_.is_none() || other.data_.is_none()) return data_.is_none() && other.data_.is_none();
        return rstd::sync::Arc<Data>::ptr_eq(*data_, *other.data_) ||
               ((*data_)->text == (*other.data_)->text && spans() == other.spans());
    }
};
struct TextDecorationSpan {
    TextRange range;
    Color background{0, 0, 0, 0}, underline{0, 0, 0, 0};
    Scalar thickness{1}, offset{1};
    auto valid() const -> bool {
        return background.valid() && underline.valid() && finite(thickness) && thickness > 0 && finite(offset);
    }
    auto operator==(const TextDecorationSpan &) const -> bool = default;
};
struct TextDecorationRect {
    Rect bounds;
    Color color;
    bool foreground{};
};
class TextDecorations {
    struct Data {
        String text;
        Vec<TextDecorationSpan> spans;
        Data(ref<str> text, Vec<TextDecorationSpan> spans) : text(String::make(text)), spans(rstd::move(spans)) {}
    };
    Option<rstd::sync::Arc<Data>> data_;
    TextDecorations(ref<str> text, Vec<TextDecorationSpan> spans)
        : data_(Some(rstd::sync::Arc<Data>::make(text, rstd::move(spans)))) {}
  public:
    TextDecorations() = default;
    TextDecorations(const TextDecorations &other) : data_(other.data_.is_some() ? Some(other.data_->clone()) : None<rstd::sync::Arc<Data>>()) {}
    auto operator=(const TextDecorations &other) -> TextDecorations & {
        if (this != &other) data_ = other.data_.is_some() ? Some(other.data_->clone()) : None<rstd::sync::Arc<Data>>();
        return *this;
    }
    TextDecorations(TextDecorations &&) = default;
    auto operator=(TextDecorations &&) -> TextDecorations & = default;
    static auto create(ref<str> text, slice<TextDecorationSpan> spans) -> Result<TextDecorations, TextError> {
        if (text->len() > usize(~Index{})) return Err(TextError::InvalidValue);
        Vec<TextDecorationSpan> checked;
        Index end{};
        for (auto span : spans) {
            if (span.range.start < end || span.range.start >= span.range.end || usize(span.range.end) > text->len() ||
                !text->is_char_boundary(usize(span.range.start)) || !text->is_char_boundary(usize(span.range.end)) || !span.valid())
                return Err(TextError::InvalidValue);
            end = span.range.end;
            if (!checked.is_empty()) {
                auto previous = checked[checked.len() - usize(1)]; previous.range = span.range;
                if (previous == span && checked[checked.len() - usize(1)].range.end == span.range.start) {
                    checked[checked.len() - usize(1)].range.end = span.range.end;
                    continue;
                }
            }
            checked.push(rstd::move(span));
        }
        if (checked.is_empty()) return Ok(TextDecorations{});
        return Ok(TextDecorations(text, rstd::move(checked)));
    }
    auto spans() const -> slice<TextDecorationSpan> { return data_.is_some() ? (*data_)->spans.as_slice() : slice<TextDecorationSpan>{}; }
    auto matches(ref<str> text) const -> bool { return data_.is_none() || (*data_)->text.as_str() == text; }
    auto rects(const TextLayout &layout, Point origin = {}) const -> Result<Vec<TextDecorationRect>, TextError> {
        if (!matches(layout.text()) || !finite(origin.x) || !finite(origin.y)) return Err(TextError::InvalidValue);
        Vec<TextDecorationRect> result;
        for (const auto &span : spans()) {
            if (span.background.a == 0 && span.underline.a == 0) continue;
            auto boxes = layout.range_boxes(span.range);
            if (boxes.is_err()) return Err(boxes.unwrap_err());
            for (const auto &box : boxes.unwrap()) {
                Rect bounds{box.bounds.x + origin.x, box.bounds.y + origin.y, box.bounds.width, box.bounds.height};
                if (!bounds.valid()) return Err(TextError::InvalidValue);
                if (span.background.a > 0) result.push({bounds, span.background, false});
                if (span.underline.a > 0) {
                    bounds.y = box.baseline + origin.y + span.offset; bounds.height = span.thickness;
                    if (!bounds.valid()) return Err(TextError::InvalidValue);
                    result.push({bounds, span.underline, true});
                }
            }
        }
        return Ok(rstd::move(result));
    }
    auto operator==(const TextDecorations &other) const -> bool {
        if (data_.is_none() || other.data_.is_none()) return data_.is_none() && other.data_.is_none();
        return rstd::sync::Arc<Data>::ptr_eq(*data_, *other.data_) ||
               ((*data_)->text == (*other.data_)->text && spans() == other.spans());
    }
};
struct TextPlacement { TextLayout layout; Point origin; };
using vectext::TextService;
using FontFace = vectext::FontInstance;
} // namespace vui
