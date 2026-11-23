export module vui.compose:lazy;
export import :lazy_content;
import :controls;
export import :lazy_grid;

export namespace vui::compose {
using namespace rstd::prelude;

struct LazyOptions {
    Insets padding;
    Scalar spacing{}, cross_spacing{};
    bool reverse_layout{}, user_scroll_enabled{true};
    ScrollMotionOptions motion{.enabled = true};
    Scalar estimated_extent{48};
    Alignment cross_alignment{Alignment::Stretch};
    Index overscan{2}, max_materialized{1024};
    friend auto operator==(const LazyOptions &, const LazyOptions &) -> bool = default;
};

struct LazyVisibleItemInfo {
    Index index{}, line{}, lane{}, span{1};
    Option<Key> key;
    Rect bounds;
};

struct LazyLayoutInfo {
    Index first_visible_index{};
    Scalar first_visible_offset{};
    Index item_count{}, materialized_count{};
    Size viewport_size;
    Scalar before_content_padding{}, after_content_padding{};
    bool reverse_layout{};
    rstd::rc::Rc<const Vec<LazyVisibleItemInfo>> visible;
    auto visible_items() const & -> slice<LazyVisibleItemInfo> {
        return visible ? visible->as_slice() : slice<LazyVisibleItemInfo>();
    }
    auto visible_items() const && -> slice<LazyVisibleItemInfo> = delete;
};

class LazyState;
}

namespace vui::compose {
struct LazyStateData {
    LazyLayoutInfo info;
    Option<Key> anchor_key;
    const void *owner{};
    Option<Index> requested;
    Scalar request_offset{};
    Option<Box<rstd::dyn<rstd::FnMut<void()>>>> wake;
};

auto make_lazy_layout(LazyContent content, LazyOptions options, bool horizontal, Option<GridCells> grid,
                      LazyState state, rstd::source_location source) -> Element;
}

export namespace vui::compose {
class LazyState {
    friend auto make_lazy_layout(LazyContent, LazyOptions, bool, Option<GridCells>, LazyState, rstd::source_location) -> Element;
    rstd::rc::Rc<LazyStateData> data_{rstd::rc::make_rc<LazyStateData>()};
    auto share() const -> rstd::rc::Rc<LazyStateData> { return data_.clone(); }

  public:
    auto layout_info() const -> LazyLayoutInfo { return data_->info; }
    auto request_scroll_to_item(Index index, Scalar offset = 0) -> Result<empty, UiError> {
        if (!finite(offset)) return Err(UiError::InvalidValue);
        data_.get()->requested = Some(index);
        data_.get()->request_offset = offset;
        if (data_->wake) (**data_.get()->wake)();
        return Ok(empty{});
    }
};

inline auto lazy_column(LazyContent content, LazyOptions options = {}, LazyState state = {},
                        rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, false, {}, rstd::move(state), source);
}

inline auto lazy_row(LazyContent content, LazyOptions options = {}, LazyState state = {},
                     rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, true, {}, rstd::move(state), source);
}

inline auto lazy_vertical_grid(GridCells cells, LazyContent content, LazyOptions options = {}, LazyState state = {},
                               rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, false, Some(cells), rstd::move(state), source);
}

inline auto lazy_horizontal_grid(GridCells cells, LazyContent content, LazyOptions options = {}, LazyState state = {},
                                 rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, true, Some(cells), rstd::move(state), source);
}
} // namespace vui::compose
