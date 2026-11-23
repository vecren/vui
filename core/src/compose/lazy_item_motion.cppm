export module vui.compose:lazy_item_motion;
import :lazy_content;
import :lazy_state;
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
    bool leaving{};
};

using LazyMoveCompletion = rstd::dyn<rstd::FnMut<void(UiId)>>;

struct LazyItemGhost {
    Index index;
    Option<Key> key;
    PaintedSubtree paint;
    AnimationOptions options;
    Rect bounds;
    Point visual_position;
    Option<Index> destination;
};

struct LazyItemViewport {
    Size size;
    Point origin;
    Option<Index> first, last;
    bool horizontal{}, reverse{};
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
        Option<Point> moving_origin;
    };
    struct Layer {
        Option<UiId> id;
    };
    struct LayerElement;

    rstd::rc::Rc<Layer> layer_{rstd::rc::make_rc<Layer>()};
    Vec<Exiting> exiting_;
    Vec<Item> items_;
    Vec<LazyItemGhost> pending_;
    Option<LazyContent> content_;
    LazyItemViewport viewport_;

    auto start_exiting(UiDocument &document, LazyItemGhost ghost, Point offset,
                       Option<Scalar> edge, bool horizontal) -> Result<empty, UiError>;

  public:
    auto layer(rstd::source_location source) const -> Element;
    auto moved_items(const LazyContent &content) const -> Vec<Index>;
    auto capture_changes(UiDocument &document, const LazyContent &content, const Vec<Index> &retained) const
        -> Vec<LazyItemGhost>;
    void stage_changes(Vec<LazyItemGhost> ghosts) { pending_ = rstd::move(ghosts); }
    auto update(UiDocument &document, const LazyContent &content, Vec<LazyAnimatedItem> items,
                const Vec<LazyVisibleItemInfo> &visible, LazyItemViewport viewport,
                Point anchor_correction, const rstd::rc::Rc<Box<LazyMoveCompletion>> &completion)
        -> Result<empty, UiError>;
    void reset();
};
} // namespace vui::compose
