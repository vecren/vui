module vui.compose;
import :lazy;
import :controls;
import :runtime;

using namespace rstd::prelude;

namespace vui::compose {
namespace {
using rstd::rc::Rc;

auto smaller(Scalar a, Scalar b) -> Scalar { return a < b ? a : b; }
auto larger(Scalar a, Scalar b) -> Scalar { return a > b ? a : b; }

auto copy_key(const Option<Key> &key) -> Option<Key> { return key ? Some(Key(*key)) : None<Key>(); }

struct LazyRootTag {};
struct LazyItemTag {};

struct LazyItemParameters final : ComponentParameters {
    LazyContent content;
    Index index;
    LazyItemParameters(LazyContent source, Index item) : content(rstd::move(source)), index(item) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<LazyItemParameters>(); }
    auto equal(const ComponentParameters &other) const -> bool override {
        if (other.type() != type()) return false;
        const auto &item = static_cast<const LazyItemParameters &>(other);
        return index == item.index && content.identity() == item.content.identity();
    }
};

struct MeasuredLine {
    Index index;
    Scalar extent;
};

class LazyGeometry {
    struct Node {
        Index left{}, right{};
        Scalar adjustment{}, extent{};
    };
    Vec<Node> nodes_;
    static constexpr Index highest_bit = Index(1) << 31;
    Scalar estimate_{48}, spacing_{};

    auto step(Scalar extent) const -> Scalar { return larger(0, extent + spacing_); }

    auto write(Index node, Index line, Index bit, Scalar extent, bool &changed) -> Scalar {
        if (!bit) {
            auto &leaf = nodes_[usize(node)];
            changed |= leaf.extent != extent;
            const auto delta = step(extent) - step(leaf.extent);
            leaf.extent = extent;
            leaf.adjustment += delta;
            return delta;
        }
        const auto branch = line & bit ? &Node::right : &Node::left;
        auto child = nodes_[usize(node)].*branch;
        if (!child) {
            child = Index(nodes_.len().to_primitive());
            nodes_.push(Node{.extent = estimate_});
            nodes_[usize(node)].*branch = child;
        }
        const auto delta = write(child, line, bit >> 1, extent, changed);
        nodes_[usize(node)].adjustment += delta;
        return delta;
    }

  public:
    void reset(Scalar estimate, Scalar spacing) {
        nodes_.clear();
        nodes_.push(Node{});
        estimate_ = estimate;
        spacing_ = spacing;
    }

    auto record(const Vec<MeasuredLine> &lines) -> bool {
        bool changed{};
        if (nodes_.len() == usize(1) && !lines.is_empty()) {
            Scalar mean{};
            Index n{};
            for (const auto &line : lines) mean += (line.extent - mean) / Scalar(++n);
            if (mean * 2 < estimate_ || mean > estimate_ * 2) {
                estimate_ = larger(1, mean);
                changed = true;
            }
        }
        for (const auto &line : lines) write(0, line.index, highest_bit, line.extent, changed);
        return changed;
    }

    auto position(Index line) const -> Scalar {
        auto position = Scalar(line) * step(estimate_);
        Index node{};
        for (auto bit = highest_bit; bit; bit >>= 1) {
            const auto &branch = nodes_[usize(node)];
            if (line & bit) {
                if (branch.left) position += nodes_[usize(branch.left)].adjustment;
                node = branch.right;
            } else {
                node = branch.left;
            }
            if (!node) break;
        }
        return position;
    }
    auto size(Index line) const -> Scalar {
        Index node{};
        for (auto bit = highest_bit; bit; bit >>= 1) {
            const auto &branch = nodes_[usize(node)];
            node = line & bit ? branch.right : branch.left;
            if (!node) return estimate_;
        }
        return nodes_[usize(node)].extent;
    }
    auto extent(Index count) const -> Scalar {
        return count ? position(count - 1) + size(count - 1) : 0;
    }
    auto line_at(Scalar offset, Index count) const -> Index {
        auto first = Index(), last = count ? count - 1 : 0;
        while (first < last) {
            const auto previous = last - first;
            const auto middle = first + previous / 2 + previous % 2;
            if (position(middle) <= offset) first = middle;
            else last = middle - 1;
        }
        return first;
    }
};

struct Cell {
    Index line{}, lane{}, span{1};
};

struct GridMeasureCursor {
    Index line{};
    LazyGridLine row;
    Vec<Index> lanes;
};

struct LazyAnchor {
    Index index;
    Option<Key> key;
    Scalar offset;
};

struct LazySlot {
    Key key;
    Option<Key> content_type;
    Index index;
    Serial id{};
};

class LazyRuntime {
    LazyContent content_;
    LazyOptions options_;
    bool horizontal_{};
    Option<GridCells> grid_;
    Rc<LazyStateData> state_;
    Option<State<Serial>> signal_;
    Serial revision_{};

    LazyGeometry geometry_;
    LazyGridSlots slots_;
    LazyGridLines lines_;
    Scalar cross_{};
    bool geometry_ready_{}, pending_{}, rebase_{true};
    Option<LazyAnchor> anchor_;
    Option<Scalar> pending_scroll_;
    Scalar extent_{};
    Vec<Index> requested_, rendered_;
    Vec<LazySlot> slots_in_use_;
    Serial next_slot_{1};
    struct Placement {
        Cell cell;
        Scalar extent, cross;
    };
    Vec<Placement> placements_;
    Option<Index> sticky_, next_sticky_line_;
    Scalar sticky_offset_{};

    UiDocument *document_{};
    UiId node_, viewport_, scroll_;
    Option<Binding> metrics_;

    auto main(Size value) const -> Scalar { return horizontal_ ? value.width : value.height; }
    auto cross(Size value) const -> Scalar { return horizontal_ ? value.height : value.width; }
    auto point(Scalar value) const -> Point { return horizontal_ ? Point{value, 0} : Point{0, value}; }
    auto axis(Point value) const -> Scalar { return horizontal_ ? value.x : value.y; }
    auto start_padding() const -> Scalar {
        auto padding = options_.padding;
        return options_.reverse_layout ? (horizontal_ ? padding.right : padding.bottom)
                                       : (horizontal_ ? padding.left : padding.top);
    }
    auto end_padding() const -> Scalar {
        auto padding = options_.padding;
        return options_.reverse_layout ? (horizontal_ ? padding.left : padding.top)
                                       : (horizontal_ ? padding.right : padding.bottom);
    }
    auto cross_start() const -> Scalar { return horizontal_ ? options_.padding.top : options_.padding.left; }
    auto cross_end() const -> Scalar { return horizontal_ ? options_.padding.bottom : options_.padding.right; }
    auto size(Scalar along, Scalar across) const -> Size { return horizontal_ ? Size{along, across} : Size{across, along}; }
    auto line_count() const -> Index { return grid_ ? lines_.line_count_hint() : content_.count(); }
    auto span() {
        return [this](Index item, Index remaining) {
            return content_.span(item, {slots_.count(), remaining});
        };
    }
    auto line_for(Index index) -> Result<Index, UiError> {
        return grid_ ? lines_.line_for_item(index, span()) : Result<Index, UiError>(Ok(index));
    }
    auto first_in(Index line) -> Result<Index, UiError> {
        if (!grid_) return Ok(line);
        auto resolved = lines_.line_at(line, span());
        if (resolved.is_err()) return Err(resolved.unwrap_err());
        return Ok(resolved->first);
    }
    auto cell_for(Index index, GridMeasureCursor &cursor) -> Result<Cell, UiError> {
        if (!grid_) return Ok(Cell{.line = index});
        auto contains = [&] { return index >= cursor.row.first && usize(index - cursor.row.first) < cursor.row.spans.len(); };
        if (!contains()) {
            auto line = line_for(index);
            if (line.is_err()) return Err(line.unwrap_err());
            auto row = lines_.line_at(*line, span());
            if (row.is_err()) return Err(row.unwrap_err());
            cursor.line = *line;
            cursor.row = row.unwrap();
            if (!contains()) return Err(UiError::InvalidValue);
            cursor.lanes.clear();
            Index lane{};
            for (const auto span : cursor.row.spans) {
                cursor.lanes.push(Index(lane));
                lane += span;
            }
        }
        const auto item = usize(index - cursor.row.first);
        return Ok(Cell{cursor.line, cursor.lanes[item], cursor.row.spans[item]});
    }
    auto anchor_index(const LazyContent &content) const -> Index {
        if (!anchor_ || !content.count()) return 0;
        const auto index = anchor_->index < content.count() ? anchor_->index : content.count() - 1;
        if (!anchor_->key || content.describe(index).key == anchor_->key) return index;
        constexpr Index neighborhood = 128;
        for (Index distance = 1; distance <= neighborhood; ++distance) {
            if (index >= distance && content.describe(index - distance).key == anchor_->key) return index - distance;
            if (distance < content.count() - index &&
                content.describe(index + distance).key == anchor_->key) return index + distance;
        }
        return index;
    }

    auto request_window(Index first, Scalar logical, Scalar viewport) -> Result<empty, UiError> {
        Vec<Index> wanted;
        const auto visible_begin = geometry_.line_at(larger(0, logical - start_padding()), line_count());
        const auto begin = visible_begin > options_.overscan ? visible_begin - options_.overscan : 0;
        const auto end = geometry_.line_at(larger(0, logical + viewport - start_padding()), line_count());
        const auto overscan_end = Serial(end) + options_.overscan;
        const auto limit = usize(options_.max_materialized);
        auto first_item = first_in(first);
        if (first_item.is_err()) return Err(first_item.unwrap_err());
        auto active_header = *first_item < content_.count() ? content_.header_before(*first_item) : None<Index>();
        auto previous_header = sticky_;
        sticky_ = active_header;
        next_sticky_line_ = None();
        const auto regular_limit = active_header && limit != usize() ? limit - usize(1) : limit;
        const auto add_line = [&](Index line) -> Result<empty, UiError> {
            if (!grid_) {
                if (active_header != Some(line)) wanted.push(Index(line));
                return Ok(empty{});
            }
            auto row = lines_.line_at(line, span());
            if (row.is_err()) return Err(row.unwrap_err());
            for (usize i{}; i < row->spans.len() && wanted.len() < regular_limit; ++i) {
                const auto item = row->first + Index(i.to_primitive());
                if (active_header != Some(item)) wanted.push(Index(item));
            }
            return Ok(empty{});
        };
        for (auto line = visible_begin; line < line_count() && Serial(line) <= overscan_end && wanted.len() < regular_limit; ++line) {
            auto added = add_line(line);
            if (added.is_err()) return Err(added.unwrap_err());
        }
        for (auto line = visible_begin; line > begin && wanted.len() < regular_limit;) {
            auto added = add_line(--line);
            if (added.is_err()) return Err(added.unwrap_err());
        }
        rstd::slice_::sort_unstable_by(wanted.as_mut_slice().as_mut_ref(), [](Index a, Index b) { return a < b; });
        if (active_header) {
            auto next = content_.header_after(*active_header);
            if (next && (!grid_ || (!wanted.is_empty() && *next <= wanted[wanted.len() - usize(1)]))) {
                auto line = line_for(*next);
                if (line.is_err()) return Err(line.unwrap_err());
                next_sticky_line_ = Some(*line);
            }
            wanted.push(Index(*active_header));
        }
        bool changed = wanted.len() != requested_.len();
        for (usize i{}; !changed && i < wanted.len(); ++i) changed = wanted[i] != requested_[i];
        if (changed) {
            requested_ = rstd::move(wanted);
            auto notified = signal_->set(++revision_);
            if (notified.is_err()) return Err(UiError::InvalidValue);
        }
        if ((active_header && sticky_offset_ != logical) || previous_header != active_header) {
            sticky_offset_ = logical;
            auto dirty = document_->invalidate_placement(node_);
            if (dirty.is_err()) return Err(dirty.unwrap_err());
        }
        const auto visible_index = *first_item < content_.count() ? *first_item : Index();
        const auto line_offset = content_.count() ? larger(0, logical - geometry_.position(first)) : 0;
        anchor_ = content_.count() ? Some(LazyAnchor{visible_index, content_.describe(visible_index).key, line_offset}) : None<LazyAnchor>();
        state_.get()->anchor_key = anchor_ ? copy_key(anchor_->key) : None<Key>();
        return Ok(empty{});
    }

    auto item_bounds(usize child) const -> Rect {
        const auto &place = placements_[child];
        auto logical = start_padding() + geometry_.position(place.cell.line);
        if (sticky_ == Some(rendered_[child])) {
            logical = larger(logical, sticky_offset_);
            if (next_sticky_line_) logical = smaller(logical,
                start_padding() + geometry_.position(*next_sticky_line_) - place.extent);
        }
        const auto along = options_.reverse_layout ? extent_ - logical - place.extent : logical;
        auto across = cross_start();
        if (grid_) across += slots_.positions[usize(place.cell.lane)];
        else if (options_.cross_alignment == Alignment::Center) across += (cross_ - place.cross) * .5;
        else if (options_.cross_alignment == Alignment::End) across += cross_ - place.cross;
        return horizontal_ ? Rect{along, across, place.extent, place.cross}
                           : Rect{across, along, place.cross, place.extent};
    }

    void publish_geometry(Size viewport, Scalar physical) {
        auto visible = rstd::rc::make_rc<Vec<LazyVisibleItemInfo>>();
        const auto viewport_extent = main(viewport);
        const auto alignment = options_.reverse_layout ? larger(0, viewport_extent - extent_) : 0;
        for (usize child{}; child < placements_.len(); ++child) {
            auto bounds = item_bounds(child);
            auto &position = horizontal_ ? bounds.x : bounds.y;
            position += alignment - physical;
            if (position >= viewport_extent || position + placements_[child].extent <= 0) continue;
            const auto index = rendered_[child];
            auto cell = placements_[child].cell;
            visible.get()->push({index, cell.line, cell.lane, cell.span, content_.describe(index).key, bounds});
        }
        state_.get()->info = {
            anchor_ ? anchor_->index : Index(), anchor_ ? anchor_->offset : 0, content_.count(),
            Index(rendered_.len().to_primitive()), viewport, start_padding(), end_padding(),
            options_.reverse_layout, visible.to_const()
        };
    }

    auto refresh() -> Result<empty, UiError> {
        if (!document_ || !state_ || !geometry_ready_) return Ok(empty{});
        auto metrics = ScrollView::metrics(*document_, scroll_);
        if (metrics.is_err()) {
            const auto why = metrics.unwrap_err();
            if (why == UiError::LayoutPending) return document_->invalidate_viewport_observation(viewport_);
            return Err(why);
        }
        const Scalar viewport = main(metrics->viewport);
        if (viewport <= 0) return Ok(empty{});
        const Scalar maximum = axis(metrics->maximum);
        const Scalar current = axis(metrics->offset);
        auto logical = options_.reverse_layout ? maximum - current : current;
        const auto old_line_count = line_count();
        const auto requested = state_->requested;
        const bool requested_jump = requested && content_.count();
        if (requested_jump) {
            const auto index = *requested < content_.count() ? *requested : content_.count() - 1;
            auto line = line_for(index);
            if (line.is_err()) return Err(line.unwrap_err());
            if (old_line_count != line_count()) return document_->invalidate_layout(node_);
            pending_scroll_ = Some(geometry_.position(*line) + state_->request_offset);
        } else if (rebase_ && anchor_ && content_.count()) {
            auto line = line_for(anchor_index(content_));
            if (line.is_err()) return Err(line.unwrap_err());
            if (old_line_count != line_count()) return document_->invalidate_layout(node_);
            pending_scroll_ = Some(geometry_.position(*line) + anchor_->offset);
        } else if (rebase_ && options_.reverse_layout && !anchor_) {
            pending_scroll_ = Some(Scalar(0));
        }
        if (pending_scroll_) {
            logical = smaller(larger(0, *pending_scroll_), maximum);
            const auto corrected = options_.reverse_layout ? maximum - logical : logical;
            pending_scroll_ = None();
            if (requested_jump || corrected != current) {
                auto rebased = requested_jump ? ScrollView::set_offset(*document_, scroll_, point(corrected))
                                              : ScrollView::rebase_offset(*document_, scroll_, point(corrected));
                if (rebased.is_err()) return Err(rebased.unwrap_err());
            }
            if (requested_jump) state_.get()->requested = None();
        }
        rebase_ = false;
        const auto first = geometry_.line_at(larger(0, logical), line_count());
        auto window = request_window(first, logical, viewport);
        if (window.is_err()) return window;
        publish_geometry(metrics->viewport, options_.reverse_layout ? maximum - logical : logical);
        return old_line_count != line_count() ? document_->invalidate_layout(node_) : Result<empty, UiError>(Ok(empty{}));
    }

  public:
    ~LazyRuntime() { unbind_state(); }

    auto compose_slots(const LazyContent &content) const -> ComposeResult<Vec<LazySlot>> {
        const auto shift = anchor_ && content.identity() != content_.identity()
            ? rstd::int64_t(anchor_index(content)) - anchor_->index : 0;
        Vec<LazySlot> slots;
        Vec<bool> claimed;
        for (const auto &slot : slots_in_use_) {
            (void)slot;
            claimed.push(false);
        }
        for (const auto requested : requested_) {
            const auto shifted = rstd::int64_t(requested) + shift;
            if (shifted < 0 || shifted >= content.count()) continue;
            const auto index = Index(shifted);
            auto metadata = content.describe(index);
            auto encoded = metadata.key ? rstd::format("k{}", metadata.key->encoded().as_str()) : rstd::format("p{}", index);
            Key key(encoded.as_str());
            for (const auto &slot : slots)
                if (slot.key == key) return Err(ComposeError(ComposeStage::Key));
            Serial id{};
            for (usize i{}; i < slots_in_use_.len(); ++i) {
                const auto &previous = slots_in_use_[i];
                if (previous.key != key) continue;
                id = previous.id;
                claimed[i] = true;
                break;
            }
            slots.push({rstd::move(key), rstd::move(metadata.content_type), index, id});
        }
        auto next = next_slot_;
        for (usize i{}; i < slots.len(); ++i) {
            auto &slot = slots[i];
            if (slot.id) continue;
            for (usize i{}; i < slots_in_use_.len(); ++i) {
                const auto &previous = slots_in_use_[i];
                if (claimed[i] || previous.content_type != slot.content_type) continue;
                slot.id = previous.id;
                claimed[i] = true;
                break;
            }
            if (!slot.id) {
                if (next == ~Serial{}) return Err(ComposeError(ComposeStage::Limit));
                slot.id = next++;
            }
        }
        return Ok(rstd::move(slots));
    }

    auto configure(const Rc<LazyRuntime> &owner, LazyContent content, LazyOptions options, bool horizontal,
                   Option<GridCells> grid, Rc<LazyStateData> state, State<Serial> signal, Vec<LazySlot> slots)
        -> Result<empty, UiError> {
        if (state->owner && state->owner != this) return Err(UiError::PropertyOwned);
        const bool changed = content_.identity() != content.identity() || options_ != options ||
                             horizontal_ != horizontal || grid_ != grid;
        bool placed_changed = rendered_.len() != slots.len();
        for (usize i{}; !placed_changed && i < slots.len(); ++i) placed_changed = rendered_[i] != slots[i].index;
        content_ = rstd::move(content);
        options_ = options;
        horizontal_ = horizontal;
        grid_ = grid;
        slots_in_use_ = rstd::move(slots);
        rendered_.clear();
        for (const auto &slot : slots_in_use_) {
            rendered_.push(Index(slot.index));
            if (next_slot_ <= slot.id) next_slot_ = slot.id + 1;
        }
        signal_ = Some(rstd::move(signal));
        if (state_.get() != state.get()) {
            unbind_state();
            state_ = rstd::move(state);
            state_.get()->owner = this;
            anchor_ = None();
            if (state_->info.item_count)
                anchor_ = Some(LazyAnchor{state_->info.first_visible_index, copy_key(state_->anchor_key), state_->info.first_visible_offset});
            rebase_ = true;
            state_.get()->wake = Some(Box<rstd::dyn<rstd::FnMut<void()>>>::make([weak = owner.downgrade()] {
                if (auto live = weak.upgrade(); live) live->get()->schedule(*live);
            }));
        }
        state_.get()->info.item_count = content_.count();
        state_.get()->info.materialized_count = Index(rendered_.len().to_primitive());
        if (changed) {
            geometry_.reset(options.estimated_extent, options.spacing);
            geometry_ready_ = false;
            rebase_ = true;
            lines_.reset(content_.count(), slots_.count(), content_.has_spans());
            if (document_) return document_->invalidate_layout(node_);
        }
        return document_ && (rebase_ || placed_changed) ? document_->invalidate_viewport_observation(viewport_)
                                                       : Result<empty, UiError>(Ok(empty{}));
    }

    void schedule(const Rc<LazyRuntime> &owner) {
        if (!document_ || pending_) return;
        auto queued = document_->defer([weak = owner.downgrade()](UiDocument &) -> Result<empty, UiError> {
            if (auto live = weak.upgrade(); live) {
                live->get()->pending_ = false;
                return live->get()->refresh();
            }
            return Ok(empty{});
        });
        pending_ = queued.is_ok();
    }

    auto attach(const Rc<LazyRuntime> &owner, UiDocument &document, UiId node, UiId viewport, UiId scroll) -> Result<empty, UiError> {
        if (!state_) return Err(UiError::PropertyOwned);
        auto observed = ScrollView::on_metrics_changed(document, scroll, [weak = owner.downgrade()](ViewportMetrics) {
            if (auto live = weak.upgrade(); live) live->get()->schedule(*live);
        });
        if (observed.is_err()) return Err(observed.unwrap_err());
        document_ = &document;
        node_ = node;
        viewport_ = viewport;
        scroll_ = scroll;
        metrics_ = Some(observed.unwrap());
        return Ok(empty{});
    }
    void detach() {
        metrics_ = None();
        unbind_state();
        state_.reset();
        document_ = nullptr;
    }

    void unbind_state() {
        if (state_ && state_->owner == this) {
            state_.get()->owner = nullptr;
            state_.get()->wake = None();
        }
    }

    auto measure(LayoutMeasureContext &context, LayoutConstraints limits) -> Result<Size, UiError> {
        const auto cross_limit = horizontal_ ? limits.max_height : limits.max_width;
        const auto minimum = horizontal_ ? limits.min_height : limits.min_width;
        const auto requested_cross = cross_limit ? *cross_limit : minimum;
        const auto available = larger(0, requested_cross - cross_start() - cross_end());
        if (!geometry_ready_ || cross_ != available) {
            if (grid_) {
                if (!cross_limit) return Err(UiError::InvalidValue);
                auto slots = grid_->slots(available, options_.cross_spacing, options_.max_materialized);
                if (slots.is_err()) return Err(slots.unwrap_err());
                if (slots_.count() != slots->count())
                    lines_.reset(content_.count(), slots->count(), content_.has_spans());
                slots_ = slots.unwrap();
            }
            cross_ = available;
            geometry_.reset(options_.estimated_extent, options_.spacing);
            geometry_ready_ = true;
            rebase_ = true;
        }

        placements_.clear();
        Vec<MeasuredLine> lines;
        GridMeasureCursor cursor;
        for (usize child{}; child < rendered_.len(); ++child) {
            auto cell = cell_for(rendered_[child], cursor);
            if (cell.is_err()) return Err(cell.unwrap_err());
            const auto span_size = grid_ ? Some(slots_.extent(cell->lane, cell->span)) :
                                          (cross_limit ? Some(Scalar(available)) : None<Scalar>());
            const Scalar span_min = span_size && (grid_ || options_.cross_alignment == Alignment::Stretch) ? *span_size : 0;
            auto child_size = context.measure_child(child, horizontal_ ? LayoutConstraints{{}, span_size, 0, span_min}
                                                                      : LayoutConstraints{span_size, {}, span_min, 0});
            if (child_size.is_err()) return Err(child_size.unwrap_err());
            const auto extent = main(*child_size);
            placements_.push({*cell, extent, cross(*child_size)});
            auto position = lines.len();
            while (position != usize() && lines[position - usize(1)].index >= cell->line) --position;
            if (position < lines.len() && lines[position].index == cell->line)
                lines[position].extent = larger(lines[position].extent, extent);
            else lines.push({cell->line, extent});
        }
        rebase_ = geometry_.record(lines) || rebase_;
        extent_ = geometry_.extent(line_count()) + start_padding() + end_padding();
        if (!finite(extent_)) return Err(UiError::InvalidValue);
        if (rebase_ && document_) {
            auto observed = document_->invalidate_viewport_observation(viewport_);
            if (observed.is_err()) return Err(observed.unwrap_err());
        }
        auto across = requested_cross;
        if (!cross_limit)
            for (const auto &placement : placements_) across = larger(across, placement.cross + cross_start() + cross_end());
        return Ok(size(extent_, across));
    }

    auto arrange(LayoutArrangeContext &context, Size) -> Result<empty, UiError> {
        for (usize child{}; child < placements_.len(); ++child) {
            auto placed = context.arrange_child(child, item_bounds(child));
            if (placed.is_err()) return Err(placed.unwrap_err());
        }
        return Ok(empty{});
    }
};

struct LazyLayout {
    Rc<LazyRuntime> runtime;
    auto measure(LayoutMeasureContext &context, LayoutConstraints limits) -> Result<Size, UiError> {
        return runtime.get()->measure(context, limits);
    }
    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        return runtime.get()->arrange(context, size);
    }
};

auto parent(UiDocument &document, UiId id) -> Result<UiId, UiError> {
    auto relation = document.parent_relation(id);
    if (relation.is_err()) return Err(relation.unwrap_err());
    if (relation->is_none()) return Err(UiError::InvalidId);
    return Ok((**relation).parent);
}

struct LazyElement {
    Rc<LazyRuntime> runtime;
    auto prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef> {
        auto node = previous ? context.edit().existing(*previous) : context.edit().create(LayoutKind::Custom, style);
        context.style(node, style);
        context.on_mount([runtime = runtime, target = context.target()](CommittedContext &committed) mutable -> ComposeResult<EffectLease> {
            auto node = committed.resolve(target);
            if (node.is_err()) return Err(node.unwrap_err());
            auto &document = committed.session().document();
            auto viewport = parent(document, *node);
            if (viewport.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(viewport.unwrap_err())));
            auto scroll = parent(document, *viewport);
            if (scroll.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(scroll.unwrap_err())));
            auto bounded = document.set_viewport_requires_bounds(*viewport, true);
            if (bounded.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(bounded.unwrap_err())));
            auto installed = document.install_layout(*node, LazyLayout{runtime.clone()});
            if (installed.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(installed.unwrap_err())));
            auto attached = runtime.get()->attach(runtime, document, *node, *viewport, *scroll);
            if (attached.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(attached.unwrap_err())));
            return Ok(EffectLease([runtime]() mutable { runtime.get()->detach(); }));
        });
        return Ok(node);
    }
};
} // namespace

auto make_lazy_layout(LazyContent content, LazyOptions options, bool horizontal, Option<GridCells> grid,
                      LazyState state, rstd::source_location source) -> Element {
    auto name = rstd::ffi::CStr::from_ptr(source.file_name()).to_str().unwrap();
    auto site = rstd::format("lazy:{}:{}:{}", name, source.line(), source.column());
    return component<LazyRootTag>(Key(site.as_str()),
        [content = rstd::move(content), options, horizontal, grid, state, source](CompositionContext &context) -> ComposeResult<Element> {
            if (!options.padding.valid() || !finite(options.spacing) || !finite(options.cross_spacing) ||
                options.cross_spacing < 0 || !finite(options.estimated_extent) || options.estimated_extent <= 0 ||
                !options.max_materialized || !valid_alignment(options.cross_alignment))
                return Err(ComposeError(ComposeStage::Prepare, Some(UiError::InvalidValue)));
            auto remembered = context.remember<Rc<LazyRuntime>>(Key{1}, []() -> ComposeResult<Rc<LazyRuntime>> {
                return Ok(rstd::rc::make_rc<LazyRuntime>());
            });
            if (remembered.is_err()) return Err(remembered.unwrap_err());
            auto stored = remembered.unwrap().get();
            if (stored.is_err()) return Err(stored.unwrap_err());
            auto runtime = (*stored)->clone();
            auto epoch = context.state<Serial>(Key{2}, 0);
            if (epoch.is_err()) return Err(epoch.unwrap_err());
            auto observed = context.read(epoch->value());
            if (observed.is_err()) return Err(observed.unwrap_err());
            Vec<Element> children;
            auto slots = runtime->compose_slots(content);
            if (slots.is_err()) return Err(slots.unwrap_err());
            for (const auto &slot : *slots) {
                const auto index = slot.index;
                auto item = component<LazyItemTag>(Key(slot.key), [content, index](CompositionContext &) {
                    return content.build(index);
                }, source, Rc<ComponentParameters>(new LazyItemParameters(content, index)));
                children.push(stack({rstd::move(item)}, source).key(Key(slot.id)));
            }
            context.on_commit([runtime, content, options, horizontal, grid, state, epoch = *epoch,
                               slots = slots.unwrap()](CommittedContext &) mutable -> ComposeResult<empty> {
                auto result = runtime.get()->configure(runtime, content, options, horizontal, grid,
                                                       state.share(), epoch, rstd::move(slots));
                if (result.is_err()) return Err(ComposeError(ComposeStage::Action, Some(result.unwrap_err())));
                return Ok(empty{});
            });
            auto motion = options.motion;
            ScrollStyle scroll{.axes = {horizontal, !horizontal}, .motion = motion,
                               .user_scroll_enabled = options.user_scroll_enabled};
            auto body = element(LazyElement{runtime}, rstd::move(children), source);
            if (options.reverse_layout) {
                auto style = NodeStyleBuilder{};
                if (horizontal) style.horizontal(Alignment::End);
                else style.vertical(Alignment::End);
                body = rstd::move(body).style(style.build());
            }
            return Ok(scroll_view(rstd::move(body), scroll, source));
        }, source);
}
} // namespace vui::compose
