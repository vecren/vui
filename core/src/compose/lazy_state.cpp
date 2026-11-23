module vui.compose;
import :lazy_state;
using namespace rstd::prelude;

namespace vui::compose {
namespace {
auto same_layout(const LazyLayoutInfo &a, const LazyLayoutInfo &b) -> bool {
    if (a.first_visible_index != b.first_visible_index || a.first_visible_offset != b.first_visible_offset ||
        a.item_count != b.item_count || a.materialized_count != b.materialized_count ||
        a.viewport_size != b.viewport_size || a.before_content_padding != b.before_content_padding ||
        a.after_content_padding != b.after_content_padding || a.reverse_layout != b.reverse_layout ||
        a.can_scroll_forward != b.can_scroll_forward || a.can_scroll_backward != b.can_scroll_backward ||
        a.prefetched_count != b.prefetched_count || a.retained_slot_count != b.retained_slot_count)
        return false;
    if (a.visible.get() == b.visible.get()) return true;
    if (!a.visible || !b.visible) return false;
    const auto before = a.visible_items(), after = b.visible_items();
    if (before.len() != after.len()) return false;
    for (usize i{}; i < before.len(); ++i)
        if (before[i] != after[i]) return false;
    return true;
}

auto ui_error(state::StateError error) -> UiError {
    if (error == state::StateError::Capacity) return UiError::Limit;
    if (error == state::StateError::Reentrant || error == state::StateError::WrongThread) return UiError::Routing;
    return UiError::InvalidValue;
}

auto prepare_observation(LazyStateData &data) -> Result<empty, state::StateError> {
    if (data.observation) return Ok(empty{});
    auto observation = rstd::rc::make_rc<LazyStateObservation>();
    auto layout = observation.get()->graph.cell(data.info, same_layout);
    if (layout.is_err()) return Err(layout.unwrap_err());
    auto index = observation.get()->graph.cell(data.info.first_visible_index);
    if (index.is_err()) return Err(index.unwrap_err());
    observation.get()->layout = Some(layout.unwrap());
    observation.get()->first_index = Some(index.unwrap());
    data.observation = rstd::move(observation);
    return Ok(empty{});
}
}

auto LazyState::layout_info_value() const -> Result<state::Value<LazyLayoutInfo>, state::StateError> {
    auto data = data_.clone();
    auto prepared = prepare_observation(*data.get());
    if (prepared.is_err()) return Err(prepared.unwrap_err());
    return Ok(data->observation->layout->value());
}

auto LazyState::first_visible_index_value() const -> Result<state::Value<Index>, state::StateError> {
    auto data = data_.clone();
    auto prepared = prepare_observation(*data.get());
    if (prepared.is_err()) return Err(prepared.unwrap_err());
    return Ok(data->observation->first_index->value());
}

auto LazyStateData::publish_layout(LazyLayoutInfo next) -> Result<empty, UiError> {
    if (!observation) {
        info = rstd::move(next);
        return Ok(empty{});
    }
    if (same_layout(info, next)) return Ok(empty{});
    auto started = state::begin_update(observation->graph.view());
    if (started.is_err()) return Err(ui_error(started.unwrap_err()));
    auto update = started.unwrap();
    auto layout = update.set(*observation->layout, LazyLayoutInfo(next));
    if (layout.is_err()) return Err(ui_error(layout.unwrap_err()));
    if (info.first_visible_index != next.first_visible_index) {
        auto written = update.set(*observation->first_index, next.first_visible_index);
        if (written.is_err()) return Err(ui_error(written.unwrap_err()));
    }
    auto previous = rstd::exchange(info, rstd::move(next));
    auto committed = update.commit();
    if (committed.is_err()) {
        info = rstd::move(previous);
        return Err(ui_error(committed.unwrap_err()));
    }
    return Ok(empty{});
}

auto LazyState::pin_item(Index index) -> Result<LazyItemPin, UiError> {
    if (!data_->pin) return Err(UiError::InvalidId);
    return (**data_.get()->pin)(index);
}

auto LazyStateData::request(Index index, Scalar offset, Serial duration) -> Result<empty, UiError> {
    if (!finite(offset)) return Err(UiError::InvalidValue);
    requested = Some(index);
    request_offset = offset;
    request_duration = duration;
    ++request_revision;
    scroll_status = LazyScrollStatus::Requested;
    if (wake) (**wake)();
    return Ok(empty{});
}

auto LazyState::restore(LazyScrollPosition position) -> Result<LazyState, UiError> {
    LazyState state;
    auto request = state.request_scroll_to_item(position.index, position.offset);
    if (request.is_err()) return Err(request.unwrap_err());
    auto data = LazyStateAccess::share(state);
    data.get()->info.first_visible_index = position.index;
    data.get()->info.first_visible_offset = position.offset;
    return Ok(rstd::move(state));
}
} // namespace vui::compose
