module vui.controls;
import :virtual_list;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto VirtualListOptions::valid() const -> bool {
    return finite(item_extent) && item_extent > 0 && finite(spacing) && finite(item_extent + spacing) && item_extent + spacing > 0 &&
        finite(padding.left) && padding.left >= 0 && finite(padding.top) && padding.top >= 0 &&
        finite(padding.right) && padding.right >= 0 && finite(padding.bottom) && padding.bottom >= 0 &&
        finite(padding.left + padding.right) && finite(padding.top + padding.bottom) &&
        (item_cross_extent.is_none() || (finite(*item_cross_extent) && *item_cross_extent >= 0 &&
            finite(*item_cross_extent + padding.left + padding.right) && finite(*item_cross_extent + padding.top + padding.bottom))) &&
        max_materialized && overscan <= max_materialized && ScrollStyle{{horizontal,!horizontal},line_step,wheel_mode,wheel_direction,motion}.valid();
}

auto VirtualListState::range() const -> Range {
    if (keys_.is_empty() || axis(metrics_.viewport) <= 0) return {};
    auto count = Scalar(keys_.len().to_primitive()); auto step = options_.item_extent + options_.spacing;
    auto first = __builtin_floor((axis(metrics_.offset) - start() - options_.item_extent) / step) + 1;
    auto last = __builtin_ceil((axis(metrics_.offset) + axis(metrics_.viewport) - start()) / step);
    first = rstd::cmp::max(Scalar(0), rstd::cmp::min(count, first - options_.overscan));
    last = rstd::cmp::max(first, rstd::cmp::min(count, last + options_.overscan));
    return {Index(first), Index(last)};
}

auto VirtualListState::refresh() -> Result<empty, UiError> {
    scheduled_ = false; if (closed_ || !ui().valid(content_)) return Ok(empty{});
    struct RefreshScope { bool &value; ~RefreshScope() { value = false; } } scope{refreshing_}; refreshing_ = true;
    error_ = None(); bool changed = reset_; if (reset_) { remove_cells(); reset_ = false; }
    auto window = range(); auto first = window.first, last = window.last;
    if (last - first > options_.max_materialized) { error_ = Some(UiError::Limit); return Err(UiError::Limit); }
    for (usize i{}; i < cells_.len();) {
        auto &cell = cells_[i]; Option<Index> index;
        for (Index next = first; next < last; ++next) if (keys_[usize(next)] == cell.item.key) { index = Some(Index(next)); break; }
        if (index.is_none() || !ui().valid(cell.node)) {
            if (ui().valid(cell.node)) ui().destroy(cell.node).unwrap(); cells_.remove(i); changed = true;
        } else { changed |= cell.item.index != *index; cell.item.index = *index; ++i; }
    }
    for (Index index = first; index < last; ++index) {
        auto key = keys_[usize(index)]; bool found{};
        for (auto cell : cells_) found |= cell.item.key == key;
        if (found) continue;
        auto made = factory_->make(session_, {key, index});
        if (made.is_err()) {
            error_ = Some(made.unwrap_err());
            if (made.unwrap_err() == UiError::Cancelled) { closed_ = true; keys_.clear(); remove_cells(); }
            ui().invalidate_layout(content_).unwrap(); return Ok(empty{});
        }
        auto id = made.unwrap();
        if (!ui().valid(id) || id == content_ || id == scroll_ || ui().parent_relation(id).unwrap().is_some() || ui().mount_state(id).unwrap().mounted) {
            error_ = Some(UiError::InvalidValue); return Ok(empty{});
        }
        auto attached = ui().attach(content_, id);
        if (attached.is_err()) { ui().destroy(id); error_ = Some(attached.unwrap_err()); return Ok(empty{}); }
        cells_.push({{key, index}, id});
        changed = true;
    }
    for (usize i{}; i < cells_.len(); ++i) {
        auto smallest = i;
        for (auto j = i + usize(1); j < cells_.len(); ++j) if (cells_[j].item.index < cells_[smallest].item.index) smallest = j;
        if (smallest != i) { auto swap = cells_[i]; cells_[i] = cells_[smallest]; cells_[smallest] = swap; }
        ui().reorder(content_, cells_[i].node, i).unwrap();
    }
    if (changed) ui().invalidate_layout(content_).unwrap(); return Ok(empty{});
}

void VirtualListState::connect(rstd::rc::Weak<VirtualListState> self, UiId scroll) {
    self_ = self; scroll_ = scroll;
    metrics_binding_ = ScrollView::on_metrics_changed(ui(), scroll_, [self](ViewportMetrics metrics) {
        auto owner = self.upgrade(); if (owner.is_none()) return;
        auto previous = owner->get()->range(); owner->get()->metrics_ = metrics;
        if (previous != owner->get()->range()) {
            auto result = owner->get()->request(); if (result.is_err()) owner->get()->error_ = Some(result.unwrap_err());
        }
    }).unwrap();
}

auto VirtualListState::request() -> Result<empty, UiError> {
    if (refreshing_) return Err(UiError::Routing);
    if (closed_) return Err(UiError::Cancelled);
    if (scheduled_) return Ok(empty{});
    scheduled_ = true;
    auto queued = ui().defer(content_, lifetime_.guard(), [self = self_](UiDocument &) -> Result<empty, UiError> {
        auto owner = self.upgrade(); return owner.is_some() ? owner->get()->refresh() : Result<empty, UiError>(Ok(empty{}));
    });
    if (queued.is_err()) { scheduled_ = false; return Err(queued.unwrap_err()); }
    return Ok(empty{});
}

auto VirtualListState::set_items(slice<Serial> keys) -> Result<empty, UiError> {
    if (refreshing_) return Err(UiError::Routing);
    if (closed_) return Err(UiError::Cancelled);
    if (keys.len().to_primitive() > ~Index{} || !finite(extent(keys.len()))) return Err(UiError::InvalidValue);
    bool same = keys.len() == keys_.len(); if (same) for (usize i{}; i < keys.len(); ++i) same &= keys[i] == keys_[i];
    if (same) return Ok(empty{});
    auto sorted = Vec<Serial>::from(keys); rstd::slice_::sort_unstable(sorted.as_mut_slice().as_mut_ref());
    for (usize i = usize(1); i < sorted.len(); ++i) if (sorted[i] == sorted[i - usize(1)]) return Err(UiError::InvalidValue);
    keys_ = Vec<Serial>::from(keys); ui().invalidate_layout(content_).unwrap(); return request();
}

auto VirtualListState::measure(LayoutMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError> {
    auto cross = options_.horizontal ? constraints.max_height : constraints.max_width;
    auto padding = options_.horizontal ? options_.padding.top + options_.padding.bottom : options_.padding.left + options_.padding.right;
    auto breadth = options_.item_cross_extent.is_some() ? *options_.item_cross_extent + padding : cross.unwrap_or(padding); if (breadth < padding) breadth = padding;
    for (usize i{}; i < context.child_count(); ++i) {
        auto measured = context.measure_child(i, options_.horizontal ? LayoutConstraints{Some(options_.item_extent), Some(breadth - padding)}
            : LayoutConstraints{Some(breadth - padding), Some(options_.item_extent)});
        if (measured.is_err()) return Err(measured.unwrap_err());
    }
    return Ok(options_.horizontal ? Size{extent(keys_.len()), breadth} : Size{breadth, extent(keys_.len())});
}

auto VirtualListState::arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
    for (usize i{}; i < context.child_count(); ++i) {
        auto id = context.child_id(i).unwrap(); const VirtualListItem *found{};
        for (const auto &cell : cells_) if (cell.node == id) { found = &cell; break; }
        if (!found) return Err(UiError::MissingChild);
        auto position = start() + found->item.index * (options_.item_extent + options_.spacing);
        auto p = options_.padding;
        Rect bounds = options_.horizontal ? Rect{position, p.top, options_.item_extent, rstd::cmp::max(Scalar(0), size.height - p.top - p.bottom)}
            : Rect{p.left, position, rstd::cmp::max(Scalar(0), size.width - p.left - p.right), options_.item_extent};
        auto placed = context.arrange_child(i, bounds); if (placed.is_err()) return placed;
    }
    return Ok(empty{});
}
} // namespace vui
