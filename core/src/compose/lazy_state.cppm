export module vui.compose:lazy_state;
import :types;

export namespace vui::compose {
using namespace rstd::prelude;

struct LazyVisibleItemInfo {
    Index index{}, line{}, lane{}, span{1};
    Option<Key> key;
    Rect bounds;
    friend auto operator==(const LazyVisibleItemInfo &, const LazyVisibleItemInfo &) -> bool = default;
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
    bool can_scroll_forward{}, can_scroll_backward{};
    Index prefetched_count{}, retained_slot_count{};
};

struct LazyScrollPosition {
    Index index{};
    Scalar offset{};
};

enum class LazyScrollStatus { Idle, Requested, Animating, Finished, Cancelled };

class LazyItemPin;
}

namespace vui::compose {
struct LazyPinLease {
    bool active{true};
    Box<rstd::dyn<rstd::FnMut<void()>>> wake;
    explicit LazyPinLease(Box<rstd::dyn<rstd::FnMut<void()>>> callback) : wake(rstd::move(callback)) {}
    ~LazyPinLease() { if (active) (*wake)(); }
};

struct LazyStateObservation {
    state::Graph graph;
    Option<state::Cell<LazyLayoutInfo>> layout;
    Option<state::Cell<Index>> first_index;
};

struct LazyStateData {
    LazyLayoutInfo info;
    rstd::rc::Rc<LazyStateObservation> observation;
    Option<Key> anchor_key;
    const void *owner{};
    Option<Index> requested;
    Scalar request_offset{};
    Serial request_duration{};
    Serial request_revision{};
    LazyScrollStatus scroll_status{};
    Option<Box<rstd::dyn<rstd::FnMut<void()>>>> wake;
    Option<Box<rstd::dyn<rstd::FnMut<Result<LazyItemPin, UiError>(Index)>>>> pin;
    auto request(Index index, Scalar offset, Serial duration) -> Result<empty, UiError>;
    auto publish_layout(LazyLayoutInfo next) -> Result<empty, UiError>;
};

struct LazyStateAccess;
}

export namespace vui::compose {
class LazyItemPin {
    friend struct LazyStateAccess;
    rstd::rc::Rc<LazyPinLease> lease_;
    explicit LazyItemPin(rstd::rc::Rc<LazyPinLease> lease) : lease_(rstd::move(lease)) {}
  public:
    LazyItemPin(const LazyItemPin &) = delete;
    auto operator=(const LazyItemPin &) -> LazyItemPin & = delete;
    LazyItemPin(LazyItemPin &&) = default;
    auto operator=(LazyItemPin &&) -> LazyItemPin & = default;
    auto active() const -> bool { return lease_ && lease_->active; }
    void release() { lease_.reset(); }
};

class LazyState {
    friend struct LazyStateAccess;
    rstd::rc::Rc<LazyStateData> data_{rstd::rc::make_rc<LazyStateData>()};

  public:
    auto layout_info() const -> LazyLayoutInfo { return data_->info; }
    auto layout_info_value() const -> Result<state::Value<LazyLayoutInfo>, state::StateError>;
    auto first_visible_index_value() const -> Result<state::Value<Index>, state::StateError>;
    auto scroll_position() const -> LazyScrollPosition {
        return {data_->info.first_visible_index, data_->info.first_visible_offset};
    }
    auto scroll_status() const -> LazyScrollStatus { return data_->scroll_status; }
    auto pin_item(Index index) -> Result<LazyItemPin, UiError>;
    static auto restore(LazyScrollPosition position) -> Result<LazyState, UiError>;
    auto request_scroll_to_item(Index index, Scalar offset = 0) -> Result<empty, UiError> {
        return data_.get()->request(index, offset, 0);
    }
    auto animate_scroll_to_item(Index index, Scalar offset = 0, Serial duration_ns = 250'000'000)
        -> Result<empty, UiError> {
        return data_.get()->request(index, offset, duration_ns);
    }
};

} // namespace vui::compose

namespace vui::compose {
struct LazyStateAccess {
    static auto share(const LazyState &state) -> rstd::rc::Rc<LazyStateData> {
        return state.data_.clone();
    }
    static auto pin(rstd::rc::Rc<LazyPinLease> lease) -> LazyItemPin { return LazyItemPin(rstd::move(lease)); }
};
}
