export module vui.controls:virtual_list;
import vui;
import :types;
import :scroll_view;
using namespace rstd::prelude;

export namespace vui {
struct VirtualItem { Serial key; Index index; };
struct VirtualListItem { VirtualItem item; UiId node; };
struct VirtualListOptions {
    bool horizontal{};
    Scalar item_extent{40}, spacing{};
    Insets padding;
    Option<Scalar> item_cross_extent;
    Index overscan{1}, max_materialized{4096};
    Scalar line_step{40};
    auto valid() const -> bool {
        return finite(item_extent) && item_extent > 0 && finite(spacing) && spacing >= 0 && finite(item_extent + spacing) &&
            finite(padding.left) && padding.left >= 0 && finite(padding.top) && padding.top >= 0 &&
            finite(padding.right) && padding.right >= 0 && finite(padding.bottom) && padding.bottom >= 0 &&
            finite(padding.left + padding.right) && finite(padding.top + padding.bottom) &&
            (item_cross_extent.is_none() || (finite(*item_cross_extent) && *item_cross_extent >= 0 &&
                finite(*item_cross_extent + padding.left + padding.right) && finite(*item_cross_extent + padding.top + padding.bottom))) &&
            max_materialized && overscan <= max_materialized && finite(line_step) && line_step > 0;
    }
};
struct VirtualItemFactory {
    template <class Self, class Delegate = void> struct Api {
        using Trait = VirtualItemFactory;
        auto make(UiSession &session, VirtualItem item) -> Result<UiId, UiError> { return rstd::trait_call<0>(this, session, item); }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::make>;
};
struct VirtualListStatus { Index count{}, materialized{}; Option<UiError> error; };
class VirtualListState {
    UiSession &session_;
    Box<rstd::dyn<VirtualItemFactory>> factory_;
    Vec<Serial> keys_;
    Vec<VirtualListItem> cells_;
    UiId content_, scroll_;
    VirtualListOptions options_;
    ViewportMetrics metrics_;
    Binding lifetime_;
    Binding metrics_binding_;
    rstd::rc::Weak<VirtualListState> self_;
    Option<UiError> error_;
    bool scheduled_{}, closed_{}, reset_{}, refreshing_{};
    auto ui() -> UiDocument & { return session_.document(); }
    auto axis(Point value) const -> Scalar { return options_.horizontal ? value.x : value.y; }
    auto axis(Size value) const -> Scalar { return options_.horizontal ? value.width : value.height; }
    auto start() const -> Scalar { return options_.horizontal ? options_.padding.left : options_.padding.top; }
    auto end() const -> Scalar { return options_.horizontal ? options_.padding.right : options_.padding.bottom; }
    struct Range { Index first{}, last{}; auto operator==(const Range &) const -> bool = default; };
    auto range() const -> Range {
        if (keys_.is_empty() || axis(metrics_.viewport) <= 0) return {};
        auto count = Scalar(keys_.len().to_primitive()); auto step = options_.item_extent + options_.spacing;
        auto first = __builtin_floor((axis(metrics_.offset) - start()) / step);
        auto last = __builtin_ceil((axis(metrics_.offset) + axis(metrics_.viewport) - start()) / step);
        first = rstd::cmp::max(Scalar(0), rstd::cmp::min(count, first - options_.overscan));
        last = rstd::cmp::max(first, rstd::cmp::min(count, last + options_.overscan));
        return {Index(first), Index(last)};
    }
    void remove_cells() {
        for (auto cell : cells_) if (ui().valid(cell.node)) ui().destroy(cell.node).unwrap();
        cells_.clear();
    }
    auto refresh() -> Result<empty, UiError> {
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
  public:
    VirtualListState(UiSession &session, Box<rstd::dyn<VirtualItemFactory>> factory, UiId content, VirtualListOptions options)
        : session_(session), factory_(rstd::move(factory)), content_(content), options_(options), lifetime_(session.document().bindings().make()) {}
    void connect(rstd::rc::Weak<VirtualListState> self, UiId scroll) {
        self_ = self; scroll_ = scroll;
        metrics_binding_ = ScrollView::on_metrics_changed(ui(), scroll_, [self](ViewportMetrics metrics) {
            auto owner = self.upgrade(); if (owner.is_none()) return;
            auto previous = owner->get()->range(); owner->get()->metrics_ = metrics;
            if (previous != owner->get()->range()) {
                auto result = owner->get()->request(); if (result.is_err()) owner->get()->error_ = Some(result.unwrap_err());
            }
        }).unwrap();
    }
    auto reset() -> Result<empty, UiError> {
        if (refreshing_) return Err(UiError::Routing);
        reset_ = true; return request();
    }
    auto request() -> Result<empty, UiError> {
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
    auto set_items(slice<Serial> keys) -> Result<empty, UiError> {
        if (refreshing_) return Err(UiError::Routing);
        if (closed_) return Err(UiError::Cancelled);
        if (keys.len().to_primitive() > ~Index{} || !finite(extent(keys.len()))) return Err(UiError::InvalidValue);
        bool same = keys.len() == keys_.len(); if (same) for (usize i{}; i < keys.len(); ++i) same &= keys[i] == keys_[i];
        if (same) return Ok(empty{});
        auto sorted = Vec<Serial>::from(keys); rstd::slice_::sort_unstable(sorted.as_mut_slice().as_mut_ref());
        for (usize i = usize(1); i < sorted.len(); ++i) if (sorted[i] == sorted[i - usize(1)]) return Err(UiError::InvalidValue);
        keys_ = Vec<Serial>::from(keys); ui().invalidate_layout(content_).unwrap(); return request();
    }
    auto extent(usize count) const -> Scalar { return start() + end() + (count == usize() ? 0 : Scalar(count.to_primitive()) * options_.item_extent + Scalar(count.to_primitive() - 1) * options_.spacing); }
    auto items() const -> slice<VirtualListItem> { return cells_.as_slice(); }
    auto status() const -> VirtualListStatus { return {Index(keys_.len().to_primitive()), Index(cells_.len().to_primitive()), error_}; }
    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError> {
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
    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
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
};
struct VirtualListContentLayout {
    rstd::rc::Rc<VirtualListState> state;
    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError> { return state.get()->measure(context, constraints); }
    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> { return state.get()->arrange(context, size); }
};
struct VirtualListHandle { rstd::rc::Rc<VirtualListState> state; };
class VirtualList {
    static auto handle(UiDocument &ui, UiId id) -> VirtualListHandle * {
        auto content = ScrollView::content(ui, id);
        return content.is_ok() && content.unwrap().is_some() ? ui.state<VirtualListHandle>(*content.unwrap()) : nullptr;
    }
  public:
    static auto reset(UiDocument &ui, UiId id) -> Result<empty, UiError> {
        auto value = handle(ui, id); return value ? value->state.get()->reset() : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    static auto refresh(UiDocument &ui, UiId id) -> Result<empty, UiError> {
        auto handle = VirtualList::handle(ui, id); return handle ? handle->state.get()->request() : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    template <class T> requires rstd::Impled<T, VirtualItemFactory>
    static auto create(UiSession &session, NodeStyle style, VirtualListOptions options, rstd::rc::Weak<T> factory) -> Result<UiId, ControlError> {
        auto &ui = session.document(); if (!options.valid()) return Err(ControlError{Some(UiError::InvalidValue), {}});
        struct Adapter {
            rstd::rc::Weak<T> owner;
            auto make(UiSession &session, VirtualItem item) -> Result<UiId, UiError> {
                auto value = owner.upgrade(); if (value.is_none()) return Err(UiError::Cancelled);
                return rstd::as<VirtualItemFactory>(*value->get()).make(session, item);
            }
        };
        NodeStyle content_style; content_style.layout.horizontal = options.horizontal ? Alignment::Start : Alignment::Stretch;
        content_style.layout.vertical = options.horizontal ? Alignment::Stretch : Alignment::Start;
        if (options.item_cross_extent.is_some()) {
            if (options.horizontal) content_style.layout.vertical = Alignment::Start; else content_style.layout.horizontal = Alignment::Start;
        }
        auto made = ui.create<Stack>(content_style); if (made.is_err()) return Err(ControlError{Some(made.unwrap_err()), {}});
        auto content = made.unwrap();
        auto state = rstd::rc::make_rc<VirtualListState>(session, Box<rstd::dyn<VirtualItemFactory>>::make(Adapter{rstd::move(factory)}), content, options);
        auto scroll = ScrollView::create(session, content, style, {{options.horizontal, !options.horizontal}, options.line_step});
        if (scroll.is_err()) { ui.destroy(content); return Err(scroll.unwrap_err()); }
        auto id = scroll.unwrap();
        ui.install_layout(content, VirtualListContentLayout{state}).unwrap(); ui.attach_state(content, VirtualListHandle{state}).unwrap(); state.get()->connect(state.downgrade(), id);
        return Ok(id);
    }
    static auto set_items(UiDocument &ui, UiId id, slice<Serial> keys) -> Result<empty, UiError> {
        auto handle = VirtualList::handle(ui, id); return handle ? handle->state.get()->set_items(keys) : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    static auto items(UiDocument &ui, UiId id) -> Result<slice<VirtualListItem>, UiError> {
        auto handle = VirtualList::handle(ui, id); return handle ? Result<slice<VirtualListItem>, UiError>(Ok(handle->state->items())) : Result<slice<VirtualListItem>, UiError>(Err(UiError::InvalidId));
    }
    static auto status(UiDocument &ui, UiId id) -> Result<VirtualListStatus, UiError> {
        auto handle = VirtualList::handle(ui, id); return handle ? Result<VirtualListStatus, UiError>(Ok(handle->state->status())) : Result<VirtualListStatus, UiError>(Err(UiError::InvalidId));
    }
    static auto item(UiDocument &ui, UiId id, Serial key) -> Result<Option<UiId>, UiError> {
        auto active = items(ui, id); if (active.is_err()) return Err(active.unwrap_err());
        for (auto cell : active.unwrap()) if (cell.item.key == key) return Ok(Some(UiId(cell.node))); return Ok(None<UiId>());
    }
};
} // namespace vui
