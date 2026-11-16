export module vui:document.reveal;
import :node.types;
using namespace rstd::prelude;

namespace vui {
struct DocumentRevealRegion {
    Vec<Point> points;
    explicit DocumentRevealRegion(Rect);
    auto bounds() const -> Result<Rect, UiError>;
    auto map(Transform) -> Result<empty, UiError>;
    auto clip(Rect) -> Result<bool, UiError>;
};
}
