module vui.compose;
import :lazy_state;
using namespace rstd::prelude;

namespace vui::compose {
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
