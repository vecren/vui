export module vui:document.text;
import :text.types;
import :text.images;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
struct DocumentTextContent {
    String value, family{String::make("sans"_str)};
    String language{String::make("und"_str)};
    Scalar size{16};
    TextDirection direction{TextDirection::Auto};
    TextWrap wrap{TextWrap::WordOrCharacter};
    TextAlignment alignment{TextAlignment::Natural};
    Scalar line_spacing{};
    Scalar character_spacing{};
    TextSpacing spacing;
    Index max_lines{};
    TextOverflow overflow{TextOverflow::Clip};
    Option<TextAutoSize> auto_size;
    Option<FontSource> source;
    TextLineBreaks line_breaks;
    TextWhitespace whitespace{TextWhitespace::Preserve};
    bool bold{}, italic{}, dirty{true};
    Color color{0, 0, 0, 1};
    TextColors colors;
    TextDecorations decorations;
    TextInlineImages inline_images;
    TextFontSpans font_spans;
    TextEffects effects;
    Option<Scalar> width;
    Option<Scalar> height;
    Option<TextLayout> layout;
    Option<TextError> error;
    TextBoundaries boundaries;
};
} // namespace vui
