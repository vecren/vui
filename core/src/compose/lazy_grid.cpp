module vui.compose;
import :lazy_grid;
using namespace rstd::prelude;

namespace vui::compose {

auto GridCells::slots(Scalar available, Scalar spacing, Index max_slots) const
    -> Result<LazyGridSlots, UiError> {
    if (!valid() || !finite(available) || !finite(spacing) || available < 0)
        return Err(UiError::InvalidValue);

    Scalar count = value_;
    if (kind_ != Kind::Fixed) {
        const auto stride = value_ + spacing;
        if (!finite(stride) || stride <= 0) return Err(UiError::InvalidValue);
        count = rstd::cmp::max(Scalar(1), __builtin_floor((available + spacing) / stride));
    }
    if (!finite(count) || count > Scalar(max_slots)) return Err(UiError::Limit);

    const auto lane_count = Index(count);
    const auto size_without_spacing = available - (count - 1) * spacing;
    if (!finite(size_without_spacing)) return Err(UiError::InvalidValue);
    const auto distribute = kind_ != Kind::FixedSize;
    const auto space = rstd::cmp::max(Scalar(0), size_without_spacing);
    auto width = distribute ? space / count : rstd::cmp::min(available, value_);
    Scalar remainder{};
    if (distribute && __builtin_floor(space) == space && __builtin_floor(spacing) == spacing) {
        width = __builtin_floor(width);
        remainder = space - width * count;
    }

    LazyGridSlots result;
    result.sizes.reserve(usize(lane_count));
    result.positions.reserve(usize(lane_count));
    Scalar position{};
    for (Index lane{}; lane < lane_count; ++lane) {
        const auto lane_width = width + (Scalar(lane) < remainder ? Scalar(1) : Scalar(0));
        if (!finite(position + lane_width) || (lane + 1 < lane_count && lane_width + spacing < 0))
            return Err(UiError::InvalidValue);
        result.sizes.push(Scalar(lane_width));
        result.positions.push(Scalar(position));
        position += lane_width + spacing;
    }
    return Ok(rstd::move(result));
}

void LazyGridLines::reset(Index count, Index slots, bool has_custom_spans) {
    count_ = count;
    slots_ = slots;
    custom_ = has_custom_spans;
    // Store sparse checkpoints and keep detailed cursors for one nearby bucket.
    bucket_size_ = slots ? Index(__builtin_sqrt(Scalar(count) / Scalar(slots))) + 1 : 1;
    buckets_.clear();
    recent_.clear();
    farthest_ = {};
    measured_ = None();
    measured_spans_.clear();
    if (custom_) buckets_.push(Cursor{});
}

auto LazyGridLines::line_count_hint() const -> Index {
    if (!slots_ || !count_) return 0;
    if (!custom_ || !farthest_.line) return (count_ - 1) / slots_ + 1;
    const auto remainder = count_ - farthest_.item;
    const auto projected = Serial(remainder) * farthest_.line;
    const auto additional = projected / farthest_.item + (projected % farthest_.item != 0);
    return farthest_.line + Index(additional);
}

void LazyGridLines::remember(Cursor cursor) {
    if (cursor.line > farthest_.line) farthest_ = cursor;
    if (cursor.line % bucket_size_ == 0 && usize(cursor.line / bucket_size_) == buckets_.len())
        buckets_.push(Cursor(cursor));

    if (recent_.is_empty() || recent_[usize()].line / bucket_size_ != cursor.line / bucket_size_) {
        recent_.clear();
        recent_.push(Cursor(cursor));
        return;
    }
    const auto last = recent_[recent_.len() - usize(1)].line;
    if (last < cursor.line) recent_.push(Cursor(cursor));
}

auto LazyGridLines::preceding(slice<Cursor> cursors, Index value, Index Cursor::*field) -> Option<Cursor> {
    usize first{}, last = cursors.len();
    while (first < last) {
        const auto middle = first + (last - first) / usize(2);
        if (cursors[middle].*field <= value) first = middle + usize(1);
        else last = middle;
    }
    return first == usize() ? None<Cursor>() : Some(Cursor(cursors[first - usize(1)]));
}

auto LazyGridLines::start_for_line(Index line) const -> Cursor {
    auto position = rstd::cmp::min(usize(line / bucket_size_), buckets_.len() - usize(1));
    auto cursor = buckets_[position];
    const auto local = preceding(recent_.as_slice(), line, &Cursor::line);
    if (local && local->line > cursor.line) cursor = *local;
    if (farthest_.line <= line && farthest_.line > cursor.line) cursor = farthest_;
    return cursor;
}

auto LazyGridLines::start_for_item(Index item) const -> Cursor {
    auto cursor = preceding(buckets_.as_slice(), item, &Cursor::item).unwrap();
    const auto local = preceding(recent_.as_slice(), item, &Cursor::item);
    if (local && local->item > cursor.item) cursor = *local;
    if (farthest_.item <= item && farthest_.item > cursor.item) cursor = farthest_;
    return cursor;
}

auto LazyGridLines::measure(Cursor cursor, SpanReader span) -> Result<Cursor, UiError> {
    if (measured_ && *measured_ == cursor) return Ok(measured_next_);

    measured_ = None();
    measured_spans_.clear();
    auto next = cursor;
    ++next.line;
    Index remaining = slots_;
    while (next.item < count_ && remaining) {
        auto width = next.pending_span ? next.pending_span : span.read(span.context, next.item, remaining);
        if (!width) width = slots_;
        if (width > slots_) return Err(UiError::InvalidValue);
        if (width > remaining) {
            // The span may depend on the previous line's remaining slots.
            next.pending_span = width;
            break;
        }
        measured_spans_.push(Index(width));
        remaining -= width;
        next.pending_span = 0;
        ++next.item;
    }

    measured_ = Some(Cursor(cursor));
    measured_next_ = next;
    remember(cursor);
    remember(next);
    return Ok(next);
}

auto LazyGridLines::line_at(Index line, SpanReader span) -> Result<LazyGridLine, UiError> {
    if (!slots_) return Err(UiError::InvalidValue);
    if (line >= count_ || (farthest_.item == count_ && line >= farthest_.line))
        return Ok(LazyGridLine{count_});
    if (!custom_) {
        if (line >= line_count_hint()) return Ok(LazyGridLine{count_});
        const auto first = line * slots_;
        const auto length = rstd::cmp::min(slots_, count_ - first);
        Vec<Index> spans;
        spans.resize(usize(length), 1);
        return Ok(LazyGridLine{first, rstd::move(spans)});
    }
    auto cursor = start_for_line(line);
    while (cursor.item < count_) {
        auto next = measure(cursor, span);
        if (next.is_err()) return Err(next.unwrap_err());
        if (cursor.line == line)
            return Ok(LazyGridLine{cursor.item, Vec<Index>::from(measured_spans_.as_slice())});
        cursor = next.unwrap();
    }
    return Ok(LazyGridLine{count_});
}

auto LazyGridLines::line_for_item(Index item, SpanReader span) -> Result<Index, UiError> {
    if (!slots_ || item >= count_) return Err(UiError::InvalidValue);
    if (!custom_) return Ok(item / slots_);
    if (measured_ && measured_->item <= item && item < measured_next_.item)
        return Ok(measured_->line);
    auto cursor = start_for_item(item);
    while (cursor.item < item) {
        auto next = measure(cursor, span);
        if (next.is_err()) return Err(next.unwrap_err());
        if (item < next->item) return Ok(cursor.line);
        cursor = next.unwrap();
    }
    return Ok(cursor.line);
}
} // namespace vui::compose
