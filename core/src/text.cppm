export module vui:text;
export import vectext;

export namespace vui {
using vectext::TextError;
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
