export module vui:text;
export import vectext;
import :types;

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
using vectext::TextQueries;
using vectext::TextLayoutData;
using vectext::TextLayout;
using vectext::TextService;
using FontFace = vectext::FontInstance;
} // namespace vui
