export module vui.compose:lazy_grid;
import :types;
using namespace rstd::prelude;

namespace vui::compose {
struct LazyGridSlots {
    Vec<Scalar> sizes, positions;

    auto count() const -> Index { return Index(sizes.len().to_primitive()); }
    auto extent(Index slot, Index span) const -> Scalar {
        const auto end = usize(slot + span - 1);
        return positions[end] + sizes[end] - positions[usize(slot)];
    }
};
}

export namespace vui::compose {
class GridCells {
    enum class Kind { Fixed, Adaptive, FixedSize };
    Kind kind_{Kind::Fixed};
    Scalar value_{1};

    GridCells(Kind kind, Scalar value) : kind_(kind), value_(value) {}

  public:
    GridCells() = default;
    static auto fixed(Index count) -> GridCells { return {Kind::Fixed, Scalar(count)}; }
    static auto adaptive(Scalar minimum) -> GridCells { return {Kind::Adaptive, minimum}; }
    static auto fixed_size(Scalar size) -> GridCells { return {Kind::FixedSize, size}; }
    auto valid() const -> bool { return finite(value_) && value_ > 0; }
    auto slots(Scalar available, Scalar spacing, Index max_slots = 4096) const
        -> Result<LazyGridSlots, UiError>;
    friend auto operator==(const GridCells &, const GridCells &) -> bool = default;
};
}

namespace vui::compose {
struct LazyGridLine {
    Index first{};
    Vec<Index> spans;
};

class LazyGridLines {
    struct Cursor {
        Index line{}, item{}, pending_span{};
        friend auto operator==(const Cursor &, const Cursor &) -> bool = default;
    };
    struct SpanReader {
        void *context;
        Index (*read)(void *, Index item, Index remaining);
    };
    Index count_{}, slots_{1}, bucket_size_{1};
    bool custom_{};
    Vec<Cursor> buckets_, recent_;
    Cursor farthest_{};
    Option<Cursor> measured_;
    Cursor measured_next_{};
    Vec<Index> measured_spans_;

    static auto preceding(slice<Cursor> cursors, Index value, Index Cursor::*field) -> Option<Cursor>;
    void remember(Cursor cursor);
    auto start_for_line(Index line) const -> Cursor;
    auto start_for_item(Index item) const -> Cursor;
    auto measure(Cursor cursor, SpanReader span) -> Result<Cursor, UiError>;
    auto line_at(Index line, SpanReader span) -> Result<LazyGridLine, UiError>;
    auto line_for_item(Index item, SpanReader span) -> Result<Index, UiError>;

  public:
    void reset(Index count, Index slots, bool has_custom_spans);
    auto line_count_hint() const -> Index;

    template <class F> auto line_at(Index line, F &&span) -> Result<LazyGridLine, UiError> {
        auto callback = [&](Index item, Index remaining) { return Index(span(item, remaining)); };
        return line_at(line, SpanReader{&callback, [](void *context, Index item, Index remaining) {
            return (*static_cast<decltype(callback) *>(context))(item, remaining);
        }});
    }
    template <class F> auto line_for_item(Index item, F &&span) -> Result<Index, UiError> {
        auto callback = [&](Index item, Index remaining) { return Index(span(item, remaining)); };
        return line_for_item(item, SpanReader{&callback, [](void *context, Index item, Index remaining) {
            return (*static_cast<decltype(callback) *>(context))(item, remaining);
        }});
    }
};
} // namespace vui::compose
