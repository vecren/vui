module vui.compose;
import :lazy;
import :lazy_item_motion;
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
    enum class Kind { Active, Probe, Pinned, Prefetched, Spare };
    Key key;
    Option<Key> content_type;
    Index index;
    Serial id{};
    Kind kind{Kind::Active};
    Option<LayoutConstraints> premeasure;
    auto placed() const -> bool { return kind == Kind::Active || kind == Kind::Probe || kind == Kind::Pinned; }
};

class LazyRuntime {
    LazyContent content_;
    LazyOptions options_;
    bool horizontal_{};
    Option<GridCells> grid_;
    Rc<LazyStateData> state_;
    Rc<LazyStateData> default_state_;
    Option<State<Serial>> signal_;
    Serial revision_{};

    LazyGeometry geometry_;
    LazyGridSlots slots_;
    LazyGridLines lines_;
    Scalar cross_{};
    bool cross_bounded_{};
    Option<LayoutConstraints> parent_bounds_;
    bool geometry_ready_{}, pending_{}, rebase_{true};
    Option<LazyAnchor> anchor_;
    Option<ScrollAnimation> scroll_animation_;
    Serial animation_request_{};
    Scalar extent_{}, arranged_extent_{};
    Vec<Index> requested_, rendered_;
    Vec<LazySlot> slots_in_use_;
    struct Pin {
        Index index;
        Option<Key> key;
        rstd::rc::Weak<LazyPinLease> lease;
    };
    Vec<Pin> pins_;
    Vec<Index> focus_items_;
    Option<Index> focus_line_;
    bool focus_invalidated_{};
    Option<Binding> focus_search_;
    Serial next_slot_{1};
    struct Prefetch {
        Index index;
        Option<LayoutConstraints> constraints;
    };
    Vec<Prefetch> prefetched_;
    Option<Index> prefetch_line_;
    Option<Scalar> prefetch_offset_;
    Option<bool> prefetch_forward_;
    Index prefetch_item_start_{}, prefetch_item_count_{};
    Index demand_begin_{}, demand_end_{};
    Serial prefetch_generation_{1};
    bool prefetch_queued_{};
    bool prefetch_constraints_changed_{};
    rstd::rc::Weak<LazyRuntime> prefetch_owner_;
    UiSession *session_{};
    struct Placement {
        Cell cell;
        Scalar extent, cross;
        UiId node;
    };
    Vec<Placement> placements_;
    Option<Index> sticky_, next_sticky_line_;
    Scalar sticky_offset_{};
    LazyItemAnimator item_animator_;

    UiDocument *document_{};
    UiId node_, viewport_, scroll_;
    Option<Binding> metrics_;

    auto main(Size value) const -> Scalar { return horizontal_ ? value.width : value.height; }
    auto cross(Size value) const -> Scalar { return horizontal_ ? value.height : value.width; }
    auto point(Scalar value) const -> Point { return horizontal_ ? Point{value, 0} : Point{0, value}; }
    auto axis(Point value) const -> Scalar { return horizontal_ ? value.x : value.y; }
    auto physical_offset(Scalar logical, Scalar maximum) const -> Point {
        return point(options_.reverse_layout ? maximum - logical : logical);
    }
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
    auto nearby_index(const LazyContent &content, Index index, const Option<Key> &key) const -> Option<Index> {
        if (!key) return index < content.count() ? Some(Index(index)) : None<Index>();
        if (!content.count()) return None();
        if (index >= content.count()) index = content.count() - 1;
        if (content.describe(index).key == key) return Some(Index(index));
        constexpr Index neighborhood = 128;
        for (Index distance = 1; distance <= neighborhood; ++distance) {
            if (distance <= index && content.describe(index - distance).key == key) return Some(index - distance);
            if (distance < content.count() - index && content.describe(index + distance).key == key)
                return Some(index + distance);
        }
        return None();
    }

    auto anchor_index(const LazyContent &content) const -> Index {
        if (!anchor_ || !content.count()) return 0;
        auto located = nearby_index(content, anchor_->index, anchor_->key);
        return located ? *located : rstd::cmp::min(anchor_->index, content.count() - 1);
    }

    void clear_pins() {
        for (const auto &pin : pins_)
            if (auto lease = pin.lease.upgrade(); lease) lease->get()->active = false;
        pins_.clear();
    }

    auto refresh_slots() -> bool { return signal_ && signal_->set(++revision_).is_ok(); }

    static auto pin_limit(const LazyOptions &options) -> usize {
        return usize(rstd::cmp::min(Index(64), options.max_materialized / 2));
    }

    auto make_pin(const Rc<LazyRuntime> &owner, Index index) -> Result<LazyItemPin, UiError> {
        if (!document_ || !state_) return Err(UiError::InvalidId);
        if (index >= content_.count()) return Err(UiError::InvalidValue);
        pins_.retain([](const Pin &pin) { return !pin.lease.expired(); });
        if (pins_.len() >= pin_limit(options_)) return Err(UiError::Limit);
        auto wake = Box<rstd::dyn<rstd::FnMut<void()>>>::make([weak = owner.downgrade()] {
            if (auto live = weak.upgrade(); live) (void)live->get()->refresh_slots();
        });
        auto lease = rstd::rc::make_rc<LazyPinLease>(rstd::move(wake));
        pins_.push({index, content_.describe(index).key, lease.downgrade()});
        if (!refresh_slots()) return Err(UiError::Cancelled);
        return Ok(LazyStateAccess::pin(rstd::move(lease)));
    }

    auto extend_focus(bool backwards) -> bool {
        if (focus_invalidated_ || !geometry_ready_ || requested_.is_empty()) return false;
        const auto line = focus_line_ ? *focus_line_ : backwards ? demand_begin_ : demand_end_;
        if (backwards ? line == 0 : line + 1 >= line_count()) return false;
        const auto next = backwards ? line - 1 : line + 1;
        auto first = next;
        Index count = 1;
        if (grid_) {
            auto row = lines_.line_at(next, span());
            if (row.is_err() || row->spans.is_empty()) return false;
            first = row->first;
            count = Index(row->spans.len().to_primitive());
        }
        if (requested_.len() + focus_items_.len() + usize(count) >= usize(options_.max_materialized)) return false;
        focus_line_ = Some(Index(next));
        for (Index i{}; i < count; ++i) focus_items_.push(first + i);
        return refresh_slots();
    }

    auto release_focus() -> bool {
        focus_line_ = None();
        focus_invalidated_ = false;
        if (focus_items_.is_empty()) return false;
        focus_items_.clear();
        return refresh_slots();
    }

    auto focus_candidate(const InputSnapshot &snapshot, UiId target) const -> bool {
        for (usize i{}; i < placements_.len() && i < slots_in_use_.len(); ++i) {
            const auto &slot = slots_in_use_[i];
            if (slot.kind == LazySlot::Kind::Active || !input_descendant(snapshot, placements_[i].node, target))
                continue;
            if (slot.kind == LazySlot::Kind::Pinned) return false;
            for (const auto index : focus_items_)
                if (index == slot.index) return true;
            return false;
        }
        return true;
    }

    auto request_window(Index first, Scalar logical, Scalar viewport) -> Result<empty, UiError> {
        Vec<Index> wanted;
        const auto visible_begin = geometry_.line_at(larger(0, logical - start_padding()), line_count());
        const auto begin = visible_begin > options_.overscan ? visible_begin - options_.overscan : 0;
        const auto end = geometry_.line_at(larger(0, logical + viewport - start_padding()), line_count());
        const auto overscan_end = Serial(end) + options_.overscan;
        demand_begin_ = begin;
        demand_end_ = Index(smaller(Scalar(overscan_end), Scalar(line_count() ? line_count() - 1 : 0)));
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

    void clear_prefetch() {
        prefetch_line_ = None();
        prefetch_offset_ = None();
        prefetch_forward_ = None();
        prefetch_item_count_ = 0;
        prefetched_.clear();
    }

    void queue_prefetch() {
        if (!prefetch_line_ || prefetch_queued_) return;
        auto queued = session_->defer_idle([weak = prefetch_owner_, generation = prefetch_generation_]() mutable {
            auto owner = weak.upgrade();
            if (!owner || owner->get()->prefetch_generation_ != generation) return false;
            owner->get()->prefetch_queued_ = false;
            return owner->get()->precompose_next();
        });
        prefetch_queued_ = queued.is_ok();
    }

    auto precompose_next() -> bool {
        if (!prefetch_line_ || requested_.len() + prefetched_.len() >= usize(options_.max_materialized))
            return false;
        if (!prefetch_item_count_) {
            if (grid_) {
                auto row = lines_.line_at(*prefetch_line_, span());
                if (row.is_err() || row->spans.is_empty()) return false;
                prefetch_item_start_ = row->first;
                prefetch_item_count_ = Index(row->spans.len().to_primitive());
            } else {
                prefetch_item_start_ = *prefetch_line_;
                prefetch_item_count_ = 1;
            }
        }
        const auto index = prefetch_item_start_ + Index(prefetched_.len().to_primitive());
        prefetched_.push({index, premeasure_constraints(index)});
        if (prefetched_.len() < usize(prefetch_item_count_)) queue_prefetch();
        return signal_->set(++revision_).is_ok();
    }

    auto update_prefetch(Scalar logical) -> Result<empty, UiError> {
        if (!options_.prefetch || !session_ || requested_.is_empty()) return Ok(empty{});
        if (rstd::exchange(prefetch_constraints_changed_, false)) {
            if (!prefetched_.is_empty()) {
                auto invalidated = signal_->set(++revision_);
                if (invalidated.is_err()) return Err(UiError::InvalidValue);
            }
            clear_prefetch();
        }
        const auto previous = prefetch_offset_;
        prefetch_offset_ = Some(logical);
        if (previous && *previous != logical) prefetch_forward_ = Some(bool(logical > *previous));
        if (!prefetch_forward_) return Ok(empty{});
        const auto forward = *prefetch_forward_;
        Option<Index> next;
        if (forward && demand_end_ + 1 < line_count()) next = Some(Index(demand_end_ + 1));
        if (!forward && demand_begin_) next = Some(Index(demand_begin_ - 1));
        if (next == prefetch_line_) return Ok(empty{});
        prefetch_line_ = next;
        prefetch_item_count_ = 0;
        if (!prefetched_.is_empty()) {
            prefetched_.clear();
            auto invalidated = signal_->set(++revision_);
            if (invalidated.is_err()) return Err(UiError::InvalidValue);
        }
        queue_prefetch();
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
        auto along = options_.reverse_layout ? extent_ - logical - place.extent : logical;
        const auto count = line_count();
        const auto rank = options_.reverse_layout ? count - 1 - place.cell.line : place.cell.line;
        along += arrangement_offset(options_.main_arrangement, arranged_extent_ - extent_, rank, count,
                                    options_.reverse_layout);
        auto across = cross_start();
        if (grid_) across += slots_.positions[usize(place.cell.lane)];
        else if (options_.cross_alignment == Alignment::Center) across += (cross_ - place.cross) * .5;
        else if (options_.cross_alignment == Alignment::End) across += cross_ - place.cross;
        return horizontal_ ? Rect{along, across, place.extent, place.cross}
                           : Rect{across, along, place.cross, place.extent};
    }

    auto publish_geometry(Size viewport, Scalar physical, Scalar maximum, Scalar rebase_delta) -> Result<empty, UiError> {
        auto visible = rstd::rc::make_rc<Vec<LazyVisibleItemInfo>>();
        Vec<LazyAnimatedItem> animated;
        const auto viewport_extent = main(viewport);
        for (usize child{}; child < placements_.len(); ++child) {
            auto bounds = item_bounds(child);
            auto &position = horizontal_ ? bounds.x : bounds.y;
            position -= physical;
            if (position >= viewport_extent || position + placements_[child].extent <= 0) continue;
            const auto index = rendered_[child];
            const auto &placement = placements_[child];
            auto metadata = content_.describe(index);
            if (metadata.animation)
                animated.push({placement.node, index, bounds, copy_key(metadata.key), *metadata.animation,
                               sticky_ == Some(index)});
            const auto cell = placement.cell;
            visible.get()->push({index, cell.line, cell.lane, cell.span, rstd::move(metadata.key), bounds});
        }
        state_.get()->info = {
            anchor_ ? anchor_->index : Index(), anchor_ ? anchor_->offset : 0, content_.count(),
            Index(rendered_.len().to_primitive()), viewport, start_padding(), end_padding(),
            options_.reverse_layout, visible.to_const(),
            options_.reverse_layout ? physical > 0 : physical < maximum,
            options_.reverse_layout ? physical < maximum : physical > 0,
            state_->info.prefetched_count, state_->info.retained_slot_count
        };
        return item_animator_.update(*document_, content_, rstd::move(animated), point(-physical), point(rebase_delta));
    }

    auto scroll_to(Scalar target, Scalar maximum, Scalar viewport, Scalar &logical) -> Result<empty, UiError> {
        auto &state = *state_.get();
        if (state.request_duration) {
            if (scroll_animation_ && animation_request_ == state.request_revision) {
                switch (scroll_animation_->status()) {
                case ScrollAnimationStatus::Cancelled:
                    state.scroll_status = LazyScrollStatus::Cancelled;
                    state.requested = None();
                    scroll_animation_ = None();
                    return Ok(empty{});
                case ScrollAnimationStatus::Running: {
                    auto retargeted = ScrollView::retarget_offset(*document_, scroll_, *scroll_animation_,
                                                                 physical_offset(target, maximum));
                    if (retargeted.is_err() && retargeted.unwrap_err() == UiError::Cancelled) {
                        state.scroll_status = LazyScrollStatus::Cancelled;
                        state.requested = None();
                        scroll_animation_ = None();
                        return Ok(empty{});
                    }
                    return retargeted;
                }
                case ScrollAnimationStatus::Finished:
                    break;
                }
            } else if (target != logical) {
                const auto approach = viewport * 4;
                if (__builtin_fabs(target - logical) > approach) {
                    logical = target + (target > logical ? -approach : approach);
                    auto moved = ScrollView::set_offset(*document_, scroll_, physical_offset(logical, maximum));
                    if (moved.is_err()) return moved;
                }
                auto animated = ScrollView::animate_offset(*document_, scroll_, physical_offset(target, maximum),
                                                           state.request_duration);
                if (animated.is_err()) return Err(animated.unwrap_err());
                scroll_animation_ = Some(animated.unwrap());
                animation_request_ = state.request_revision;
                state.scroll_status = LazyScrollStatus::Animating;
                return Ok(empty{});
            }
        }
        auto moved = ScrollView::set_offset(*document_, scroll_, physical_offset(target, maximum));
        if (moved.is_err()) return moved;
        logical = target;
        state.scroll_status = LazyScrollStatus::Finished;
        state.requested = None();
        scroll_animation_ = None();
        return Ok(empty{});
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
        Scalar rebase_delta{};
        const auto old_line_count = line_count();
        const auto requested = state_->requested;
        if (requested && content_.count()) {
            const auto index = *requested < content_.count() ? *requested : content_.count() - 1;
            auto line = line_for(index);
            if (line.is_err()) return Err(line.unwrap_err());
            if (old_line_count != line_count()) return document_->invalidate_layout(node_);
            auto target = smaller(larger(0, geometry_.position(*line) + state_->request_offset), maximum);
            auto moved = scroll_to(target, maximum, viewport, logical);
            if (moved.is_err()) return moved;
        } else if (rebase_) {
            auto adjusted = logical;
            if (anchor_ && content_.count()) {
                auto line = line_for(anchor_index(content_));
                if (line.is_err()) return Err(line.unwrap_err());
                if (old_line_count != line_count()) return document_->invalidate_layout(node_);
                adjusted = smaller(larger(0, geometry_.position(*line) + anchor_->offset), maximum);
            } else if (options_.reverse_layout && !anchor_) {
                adjusted = 0;
            }
            if (adjusted != logical) {
                const auto corrected = physical_offset(adjusted, maximum);
                auto rebased = ScrollView::rebase_offset(*document_, scroll_, corrected);
                if (rebased.is_err()) return Err(rebased.unwrap_err());
                rebase_delta = axis(corrected) - current;
            }
            logical = adjusted;
        }
        rebase_ = false;
        const auto first = geometry_.line_at(larger(0, logical), line_count());
        auto window = request_window(first, logical, viewport);
        if (window.is_err()) return window;
        auto published = publish_geometry(metrics->viewport, axis(physical_offset(logical, maximum)), maximum, rebase_delta);
        if (published.is_err()) return published;
        auto prefetch = update_prefetch(logical);
        if (prefetch.is_err()) return prefetch;
        return old_line_count != line_count() ? document_->invalidate_layout(node_) : Result<empty, UiError>(Ok(empty{}));
    }

  public:
    ~LazyRuntime() { unbind_state(); }

    auto state_for(const Rc<LazyStateData> &supplied) -> Rc<LazyStateData> {
        if (supplied) return supplied.clone();
        if (!default_state_) default_state_ = rstd::rc::make_rc<LazyStateData>();
        return default_state_.clone();
    }

    auto item_animator() -> LazyItemAnimator & { return item_animator_; }

    void scroll_to_position(EventContext &context, const RoutedEvent &event) {
        if (!state_ || !options_.user_scroll_enabled ||
            event.input.semantic_action != SemanticsAction::ScrollToPosition || !event.input.semantic_position)
            return;
        const auto position = *event.input.semantic_position;
        const auto main = horizontal_ ? position.column : position.row;
        const auto cross = horizontal_ ? position.row : position.column;
        auto index = main;
        if (grid_) {
            if (cross >= slots_.count()) return;
            auto line = lines_.line_at(main, span());
            if (line.is_err()) return;
            index = line->first;
            auto lane = Index();
            bool found{};
            for (auto width : line->spans) {
                lane += width;
                if (cross < lane) { found = true; break; }
                ++index;
            }
            if (!found) return;
        } else if (cross != 0) {
            return;
        }
        if (index >= content_.count()) return;
        if (state_.get()->request(index, 0, 0).is_ok()) context.prevent_default();
    }

    auto compose_slots(const LazyContent &content, const LazyOptions &options) const -> ComposeResult<Vec<LazySlot>> {
        const auto shift = anchor_ && content.identity() != content_.identity()
            ? rstd::int64_t(anchor_index(content)) - anchor_->index : 0;
        Vec<LazySlot> slots;
        Vec<bool> claimed;
        for (const auto &slot : slots_in_use_) {
            (void)slot;
            claimed.push(false);
        }
        const auto add = [&](Index index, LazySlot::Kind kind, Option<LayoutConstraints> limits = {}) -> ComposeResult<empty> {
            for (auto &slot : slots.as_mut_slice().as_mut_ref()) {
                if (slot.index != index) continue;
                if (kind < slot.kind) slot.kind = kind;
                return Ok(empty{});
            }
            auto metadata = content.describe(index);
            auto encoded = metadata.key ? rstd::format("k{}", metadata.key->encoded().as_str()) : rstd::format("p{}", index);
            Key key(encoded.as_str());
            for (const auto &slot : slots)
                if (slot.key == key) return Err(ComposeError(ComposeStage::Key));
            Serial id{};
            for (usize i{}; i < slots_in_use_.len(); ++i) {
                const auto &previous = slots_in_use_[i];
                if (previous.key != key) continue;
                if (previous.kind == LazySlot::Kind::Spare && previous.content_type != metadata.content_type) continue;
                id = previous.id;
                claimed[i] = true;
                break;
            }
            slots.push({rstd::move(key), rstd::move(metadata.content_type), index, id, kind, limits});
            return Ok(empty{});
        };
        auto kept_pins = usize();
        for (const auto &pin : pins_) {
            if (kept_pins >= pin_limit(options)) break;
            const auto lease = pin.lease.upgrade();
            if (!lease || !(*lease)->active) continue;
            auto index = nearby_index(content, pin.index, pin.key);
            if (!index) continue;
            auto added = add(*index, LazySlot::Kind::Pinned);
            if (added.is_err()) return Err(added.unwrap_err());
            ++kept_pins;
        }
        for (const auto requested : requested_) {
            if (slots.len() >= usize(options.max_materialized)) break;
            const auto shifted = rstd::int64_t(requested) + shift;
            if (shifted < 0 || shifted >= content.count()) continue;
            auto added = add(Index(shifted), LazySlot::Kind::Active);
            if (added.is_err()) return Err(added.unwrap_err());
        }
        if (content.identity() == content_.identity()) {
            for (const auto index : focus_items_) {
                if (slots.len() >= usize(options.max_materialized)) break;
                auto added = add(index, LazySlot::Kind::Probe);
                if (added.is_err()) return Err(added.unwrap_err());
            }
        }
        const auto sticky = sticky_ ? rstd::int64_t(*sticky_) + shift : -1;
        rstd::slice_::sort_unstable_by(slots.as_mut_slice().as_mut_ref(), [sticky](const LazySlot &a, const LazySlot &b) {
            if (a.index == sticky) return false;
            if (b.index == sticky) return true;
            return a.index < b.index;
        });
        if (options.prefetch && content.identity() == content_.identity()) {
            for (const auto &item : prefetched_) {
                if (slots.len() >= usize(options.max_materialized)) break;
                auto added = add(item.index, LazySlot::Kind::Prefetched, item.constraints);
                if (added.is_err()) continue;
            }
        }
        auto next = next_slot_;
        for (usize i{}; i < slots.len(); ++i) {
            auto &slot = slots[i];
            if (slot.id) continue;
            for (usize i{}; i < slots_in_use_.len(); ++i) {
                const auto &previous = slots_in_use_[i];
                if (claimed[i] || previous.kind != LazySlot::Kind::Spare ||
                    previous.content_type != slot.content_type) continue;
                slot.id = previous.id;
                claimed[i] = true;
                break;
            }
            if (!slot.id) {
                if (next == ~Serial{}) return Err(ComposeError(ComposeStage::Limit));
                slot.id = next++;
            }
        }
        const auto active_and_prefetched = slots.len();
        for (auto i = slots_in_use_.len(); i > usize() && slots.len() < usize(options.max_materialized);) {
            --i;
            if (claimed[i]) continue;
            const auto &previous = slots_in_use_[i];
            Index retained{};
            for (auto slot = active_and_prefetched; slot < slots.len(); ++slot)
                if (slots[slot].content_type == previous.content_type) ++retained;
            if (retained >= options.retained_slots_per_type) continue;
            slots.push({Key(previous.key), copy_key(previous.content_type), previous.index, previous.id, LazySlot::Kind::Spare});
        }
        return Ok(rstd::move(slots));
    }

    auto configure(const Rc<LazyRuntime> &owner, LazyContent content, LazyOptions options, bool horizontal,
                   Option<GridCells> grid, Rc<LazyStateData> state, State<Serial> signal, Vec<LazySlot> slots)
        -> Result<empty, UiError> {
        if (state->owner && state->owner != this) return Err(UiError::PropertyOwned);
        const bool changed = content_.identity() != content.identity() || options_ != options ||
                             horizontal_ != horizontal || grid_ != grid;
        auto previous_rendered = rstd::move(rendered_);
        if (changed) {
            clear_prefetch();
            if (focus_line_) {
                focus_invalidated_ = true;
                focus_items_.clear();
            }
            auto kept_pins = usize();
            for (auto &pin : pins_.as_mut_slice().as_mut_ref()) {
                auto lease = pin.lease.upgrade();
                if (!lease || !(*lease)->active) continue;
                auto located = nearby_index(content, pin.index, pin.key);
                if (located && kept_pins < pin_limit(options)) {
                    pin.index = *located;
                    ++kept_pins;
                } else {
                    lease->get()->active = false;
                }
            }
            pins_.retain([](const Pin &pin) {
                auto lease = pin.lease.upgrade();
                return lease && (*lease)->active;
            });
        }
        content_ = rstd::move(content);
        options_ = options;
        horizontal_ = horizontal;
        grid_ = grid;
        slots_in_use_ = rstd::move(slots);
        rendered_ = Vec<Index>{};
        Index spares{}, prefetched{};
        for (const auto &slot : slots_in_use_) {
            switch (slot.kind) {
            case LazySlot::Kind::Active:
            case LazySlot::Kind::Probe:
            case LazySlot::Kind::Pinned: rendered_.push(Index(slot.index)); break;
            case LazySlot::Kind::Spare: ++spares; break;
            case LazySlot::Kind::Prefetched: ++prefetched; break;
            }
            if (next_slot_ <= slot.id) next_slot_ = slot.id + 1;
        }
        bool placed_changed = previous_rendered.len() != rendered_.len();
        for (usize i{}; !placed_changed && i < rendered_.len(); ++i) placed_changed = previous_rendered[i] != rendered_[i];
        signal_ = Some(rstd::move(signal));
        if (state_.get() != state.get()) {
            if (document_ && scroll_animation_) (void)ScrollView::stop(*document_, scroll_);
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
            state_.get()->pin = Some(Box<rstd::dyn<rstd::FnMut<Result<LazyItemPin, UiError>(Index)>>>::make(
                [weak = owner.downgrade()](Index index) -> Result<LazyItemPin, UiError> {
                    auto live = weak.upgrade();
                    if (!live) return Err(UiError::Cancelled);
                    return live->get()->make_pin(*live, index);
                }));
        }
        state_.get()->info.item_count = content_.count();
        state_.get()->info.materialized_count = Index(rendered_.len().to_primitive());
        state_.get()->info.prefetched_count = prefetched;
        state_.get()->info.retained_slot_count = spares;
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

    auto attach(const Rc<LazyRuntime> &owner, UiSession &session, UiId node, UiId viewport, UiId scroll) -> Result<empty, UiError> {
        if (!state_) return Err(UiError::PropertyOwned);
        auto &document = session.document();
        auto observed = ScrollView::on_metrics_changed(document, scroll, [weak = owner.downgrade()](ViewportMetrics) {
            if (auto live = weak.upgrade(); live) live->get()->schedule(*live);
        });
        if (observed.is_err()) return Err(observed.unwrap_err());
        const auto weak = owner.downgrade();
        auto focus = session.input().on_focus_search(scroll,
            [weak](bool backwards) {
                auto live = weak.upgrade();
                return live && live->get()->extend_focus(backwards);
            },
            [weak]() {
                auto live = weak.upgrade();
                return live && live->get()->release_focus();
            },
            [weak](const InputSnapshot &snapshot, UiId target) {
                auto live = weak.upgrade();
                return live && (*live)->focus_candidate(snapshot, target);
            });
        if (focus.is_err()) return Err(UiError::Routing);
        document_ = &document;
        session_ = &session;
        prefetch_owner_ = owner.downgrade();
        node_ = node;
        viewport_ = viewport;
        scroll_ = scroll;
        metrics_ = Some(observed.unwrap());
        focus_search_ = Some(focus.unwrap());
        return Ok(empty{});
    }
    void detach() {
        ++prefetch_generation_;
        prefetch_queued_ = false;
        clear_prefetch();
        focus_search_ = None();
        focus_items_.clear();
        focus_line_ = None();
        focus_invalidated_ = false;
        session_ = nullptr;
        metrics_ = None();
        unbind_state();
        state_.reset();
        item_animator_.reset();
        document_ = nullptr;
    }

    void unbind_state() {
        if (state_ && state_->owner == this) {
            if (scroll_animation_) {
                state_.get()->requested = None();
                state_.get()->scroll_status = LazyScrollStatus::Cancelled;
                scroll_animation_ = None();
            }
            state_.get()->owner = nullptr;
            state_.get()->wake = None();
            state_.get()->pin = None();
            if (!pins_.is_empty()) {
                clear_pins();
                if (document_ && session_) (void)refresh_slots();
            }
        }
    }

    auto item_constraints(Index index, Cell cell) const -> Result<LayoutConstraints, UiError> {
        const auto span_size = grid_ ? Some(slots_.extent(cell.lane, cell.span)) :
                                      cross_bounded_ ? Some(Scalar(cross_)) : None<Scalar>();
        const auto span_min = span_size && (grid_ || options_.cross_alignment == Alignment::Stretch) ? *span_size : 0;
        auto constraints = horizontal_ ? LayoutConstraints{{}, span_size, 0, span_min}
                                       : LayoutConstraints{span_size, {}, span_min, 0};
        if (grid_) return Ok(constraints);
        const auto metadata = content_.describe(index);
        auto fill = [](Option<Scalar> fraction, Option<Scalar> bound, Scalar padding,
                       Scalar &min, Option<Scalar> &max) -> bool {
            if (!fraction) return true;
            if (!finite(*fraction) || *fraction < 0 || *fraction > 1) return false;
            if (bound) {
                min = larger(0, *bound - padding) * *fraction;
                max = Some(Scalar(min));
            }
            return true;
        };
        const auto width = parent_bounds_ ? parent_bounds_->max_width : None<Scalar>();
        const auto height = parent_bounds_ ? parent_bounds_->max_height : None<Scalar>();
        if (!fill(metadata.fill_parent_max_width, width, options_.padding.left + options_.padding.right,
                  constraints.min_width, constraints.max_width) ||
            !fill(metadata.fill_parent_max_height, height, options_.padding.top + options_.padding.bottom,
                  constraints.min_height, constraints.max_height))
            return Err(UiError::InvalidValue);
        return Ok(constraints);
    }

    auto premeasure_constraints(Index index) -> Option<LayoutConstraints> {
        if (!geometry_ready_) return None();
        GridMeasureCursor cursor;
        auto cell = cell_for(index, cursor);
        if (cell.is_err()) return None();
        auto constraints = item_constraints(index, *cell);
        if (constraints.is_err()) return None();
        return Some(LayoutConstraints{constraints->max_width, constraints->max_height});
    }

    auto measure(LayoutMeasureContext &context, LayoutConstraints limits) -> Result<Size, UiError> {
        const auto cross_limit = horizontal_ ? limits.max_height : limits.max_width;
        const auto minimum = horizontal_ ? limits.min_height : limits.min_width;
        const auto requested_cross = cross_limit ? *cross_limit : minimum;
        const auto available = larger(0, requested_cross - cross_start() - cross_end());
        const auto parent_bounds = grid_ ? None<LayoutConstraints>() : context.parent_viewport_constraints();
        if (!geometry_ready_ || cross_ != available || cross_bounded_ != cross_limit.is_some() ||
            parent_bounds_ != parent_bounds) {
            prefetch_constraints_changed_ = geometry_ready_;
            if (grid_) {
                if (!cross_limit) return Err(UiError::InvalidValue);
                auto slots = grid_->slots(available, options_.cross_spacing, options_.max_materialized,
                                          options_.cross_arrangement);
                if (slots.is_err()) return Err(slots.unwrap_err());
                if (slots_.count() != slots->count())
                    lines_.reset(content_.count(), slots->count(), content_.has_spans());
                slots_ = slots.unwrap();
            }
            cross_ = available;
            cross_bounded_ = cross_limit.is_some();
            parent_bounds_ = parent_bounds;
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
            auto constraints = item_constraints(rendered_[child], *cell);
            if (constraints.is_err()) return Err(constraints.unwrap_err());
            auto child_size = context.measure_child(child, *constraints);
            if (child_size.is_err()) return Err(child_size.unwrap_err());
            auto child_id = context.child_id(child);
            if (child_id.is_err()) return Err(child_id.unwrap_err());
            const auto extent = main(*child_size);
            placements_.push({*cell, extent, cross(*child_size), *child_id});
            auto position = lines.len();
            while (position != usize() && lines[position - usize(1)].index >= cell->line) --position;
            if (position < lines.len() && lines[position].index == cell->line)
                lines[position].extent = larger(lines[position].extent, extent);
            else lines.push({cell->line, extent});
        }
        for (auto child = rendered_.len(); child < context.child_count(); ++child) {
            auto measured = context.measure_child(child, {Some(Scalar(0)), Some(Scalar(0))});
            if (measured.is_err()) return Err(measured.unwrap_err());
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

    auto arrange_inactive(LayoutArrangeContext &context) -> Result<empty, UiError> {
        for (auto child = rendered_.len(); child < context.child_count(); ++child) {
            auto placed = context.arrange_child(child, {});
            if (placed.is_err()) return placed;
        }
        return Ok(empty{});
    }

    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        arranged_extent_ = main(size);
        for (usize child{}; child < placements_.len(); ++child) {
            auto placed = context.arrange_child(child, item_bounds(child));
            if (placed.is_err()) return Err(placed.unwrap_err());
        }
        return arrange_inactive(context);
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
            auto action = document.bindings().make();
            auto registered = committed.session().input().on(*scroll, EventType::SemanticAction, action.guard(),
                [weak = runtime.downgrade()](EventContext &context, const RoutedEvent &event) {
                    if (auto live = weak.upgrade(); live) live->get()->scroll_to_position(context, event);
                });
            if (registered.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(UiError::Routing)));
            auto bounded = document.set_viewport_requires_bounds(*viewport, true);
            if (bounded.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(bounded.unwrap_err())));
            auto installed = document.install_layout(*node, LazyLayout{runtime.clone()});
            if (installed.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(installed.unwrap_err())));
            auto attached = runtime.get()->attach(runtime, committed.session(), *node, *viewport, *scroll);
            if (attached.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(attached.unwrap_err())));
            return Ok(EffectLease([runtime, action = rstd::move(action)]() mutable {
                action = {};
                runtime.get()->detach();
            }));
        });
        return Ok(node);
    }
};
} // namespace

auto make_lazy_layout(LazyContent content, LazyOptions options, bool horizontal, Option<GridCells> grid,
                      Option<LazyState> state, rstd::source_location source) -> Element {
    auto name = rstd::ffi::CStr::from_ptr(source.file_name()).to_str().unwrap();
    auto site = rstd::format("lazy:{}:{}:{}", name, source.line(), source.column());
    return component<LazyRootTag>(Key(site.as_str()),
        [content = rstd::move(content), options, horizontal, grid,
         supplied = state ? LazyStateAccess::share(*state) : Rc<LazyStateData>{}, source](CompositionContext &context) -> ComposeResult<Element> {
            if (!options.padding.valid() || !finite(options.spacing) || !finite(options.cross_spacing) ||
                !finite(options.estimated_extent) || options.estimated_extent <= 0 ||
                !options.max_materialized || !valid_alignment(options.cross_alignment) ||
                !valid_arrangement(options.main_arrangement) || !valid_arrangement(options.cross_arrangement))
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
            auto slots = runtime->compose_slots(content, options);
            if (slots.is_err()) return Err(slots.unwrap_err());
            for (const auto &slot : *slots) {
                Vec<Element> contents;
                if (slot.kind == LazySlot::Kind::Spare) {
                    auto item = component<LazyItemTag>(Key(slot.key), [](CompositionContext &) -> ComposeResult<Element> {
                        return Ok(Element{});
                    }, source, {}, true);
                    auto data = rstd::rc::make_rc<ElementData>(item.data());
                    data.get()->retain_native = true;
                    contents.push(Element(rstd::move(data)));
                } else {
                    const auto index = slot.index;
                    auto item = component<LazyItemTag>(Key(slot.key), [content, index](CompositionContext &) {
                        return content.build(index);
                    }, source, Rc<ComponentParameters>(new LazyItemParameters(content, index)),
                    slot.kind == LazySlot::Kind::Prefetched);
                    if (slot.kind == LazySlot::Kind::Prefetched) {
                        auto data = rstd::rc::make_rc<ElementData>(item.data());
                        data.get()->premeasure = slot.premeasure;
                        item = Element(rstd::move(data));
                    }
                    contents.push(rstd::move(item));
                }
                auto wrapper = stack(rstd::move(contents), source).key(Key(slot.id));
                if (!slot.placed())
                    wrapper = rstd::move(wrapper).style(NodeStyleBuilder{}.visible(false).hit_test_visible(false).build());
                children.push(rstd::move(wrapper));
            }
            auto &animator = runtime.get()->item_animator();
            children.push(animator.layer(source));
            auto ghosts = animator.prepare_removals(context.session().document(), content);
            context.on_commit([runtime, content, options, horizontal, grid, state = runtime.get()->state_for(supplied), epoch = *epoch,
                               slots = slots.unwrap(), ghosts = rstd::move(ghosts)](CommittedContext &committed) mutable -> ComposeResult<empty> {
                auto result = runtime.get()->configure(runtime, content, options, horizontal, grid,
                                                       rstd::move(state), epoch, rstd::move(slots));
                if (result.is_err()) return Err(ComposeError(ComposeStage::Action, Some(result.unwrap_err())));
                auto removed = runtime.get()->item_animator().start_removals(committed.session().document(), rstd::move(ghosts));
                if (removed.is_err()) return Err(ComposeError(ComposeStage::Action, Some(removed.unwrap_err())));
                return Ok(empty{});
            });
            auto motion = options.motion;
            ScrollStyle scroll{.axes = {horizontal, !horizontal}, .motion = motion,
                               .user_scroll_enabled = options.user_scroll_enabled,
                               .reverse_semantics = options.reverse_layout};
            auto body = element(LazyElement{runtime}, rstd::move(children), source);
            auto style = NodeStyleBuilder{};
            if (horizontal) style.horizontal(Alignment::Stretch);
            else style.vertical(Alignment::Stretch);
            auto actions = semantics_action_mask(SemanticsAction::Focus);
            if (options.user_scroll_enabled)
                actions |= semantics_action_mask(SemanticsAction::ScrollForward) |
                           semantics_action_mask(SemanticsAction::ScrollBackward) |
                           semantics_action_mask(SemanticsAction::ScrollToPosition);
            auto collection = SemanticsCollection{.horizontal = horizontal};
            if (!grid) {
                collection.rows = Some(horizontal ? Index(1) : content.count());
                collection.columns = Some(horizontal ? content.count() : Index(1));
            }
            return Ok(with_semantics(scroll_view(rstd::move(body).style(style.build()), scroll, source),
                {.role = SemanticsRole::ScrollArea, .actions = actions, .collection = Some(collection)}));
        }, source);
}
} // namespace vui::compose
