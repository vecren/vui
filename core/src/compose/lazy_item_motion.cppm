export module vui.compose:lazy_item_motion;
import :lazy_content;
import :element;
using namespace rstd::prelude;

namespace vui::compose {
struct LazyAnimatedItem {
    UiId node;
    Index index;
    Rect viewport_bounds;
    Option<Key> key;
    LazyItemAnimationOptions options;
    bool sticky{};
};

struct LazyItemGhost {
    Index index;
    Option<Key> key;
    PaintedSubtree paint;
    AnimationOptions options;
};

class LazyItemAnimator {
    struct Item {
        LazyAnimatedItem geometry;
        Option<AnimationId> move, opacity;
    };
    struct Exiting {
        UiId node;
        Index index;
        Option<Key> key;
        bool position_adjusted{};
    };
    struct Layer {
        Option<UiId> id;
    };
    struct LayerElement;

    rstd::rc::Rc<Layer> layer_{rstd::rc::make_rc<Layer>()};
    Vec<Exiting> exiting_;
    Vec<Item> items_;
    Option<LazyContent> content_;
    Point viewport_origin_;

  public:
    auto layer(rstd::source_location source) const -> Element;
    auto prepare_removals(UiDocument &document, const LazyContent &content) const -> Vec<LazyItemGhost>;
    auto start_removals(UiDocument &document, Vec<LazyItemGhost> ghosts) -> Result<empty, UiError>;
    auto update(UiDocument &document, const LazyContent &content, Vec<LazyAnimatedItem> items,
                Point viewport_origin, Point anchor_correction = {}) -> Result<empty, UiError>;
    void reset();
};
} // namespace vui::compose
