export module vui.compose:lazy;
export import :lazy_content;
export import :lazy_state;
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
    LazyArrangement main_arrangement{}, cross_arrangement{};
    bool prefetch{true};
    Index retained_slots_per_type{7};
    friend auto operator==(const LazyOptions &, const LazyOptions &) -> bool = default;
};

}

namespace vui::compose {
auto make_lazy_layout(LazyContent content, LazyOptions options, bool horizontal, Option<GridCells> grid,
                      Option<LazyState> state, rstd::source_location source) -> Element;
}

export namespace vui::compose {
inline auto lazy_column(LazyContent content, LazyOptions options, LazyState state,
                        rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, false, {}, Some(rstd::move(state)), source);
}

inline auto lazy_column(LazyContent content, LazyOptions options = {},
                        rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, false, {}, {}, source);
}

inline auto lazy_row(LazyContent content, LazyOptions options, LazyState state,
                     rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, true, {}, Some(rstd::move(state)), source);
}

inline auto lazy_row(LazyContent content, LazyOptions options = {},
                     rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, true, {}, {}, source);
}

inline auto lazy_vertical_grid(GridCells cells, LazyContent content, LazyOptions options, LazyState state,
                               rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, false, Some(cells), Some(rstd::move(state)), source);
}

inline auto lazy_vertical_grid(GridCells cells, LazyContent content, LazyOptions options = {},
                               rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, false, Some(cells), {}, source);
}

inline auto lazy_horizontal_grid(GridCells cells, LazyContent content, LazyOptions options, LazyState state,
                                 rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, true, Some(cells), Some(rstd::move(state)), source);
}

inline auto lazy_horizontal_grid(GridCells cells, LazyContent content, LazyOptions options = {},
                                 rstd::source_location source = rstd::source_location::current()) -> Element {
    return make_lazy_layout(rstd::move(content), options, true, Some(cells), {}, source);
}
} // namespace vui::compose
