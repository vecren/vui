module vui.compose;
import :lazy_content;

using namespace rstd::prelude;

namespace vui::compose {
auto LazyContent::append(Index count, Box<Builder> build, Option<Box<Metadata>> describe,
                         Option<Box<Span>> span, bool sticky) -> ComposeResult<empty> {
    if (count > ~Index{} - data_->count)
        return Err(ComposeError(ComposeStage::Limit, Some(UiError::Limit)));
    if (!data_.is_unique()) {
        auto old = data_.clone();
        data_ = rstd::rc::make_rc<Data>(Data{Vec<Interval>::from(old->intervals.as_slice()),
            Vec<Index>::from(old->headers.as_slice()), old->count, old->has_spans});
    }
    auto &data = *data_.get();
    if (sticky) data.headers.push(Index(data.count));
    data.has_spans |= span.is_some();
    auto provider = rstd::rc::make_rc<Provider>(Provider{rstd::move(build), rstd::move(describe), rstd::move(span)});
    data.intervals.push({data.count, count, rstd::move(provider)});
    data.count += count;
    return Ok(empty{});
}

auto LazyContent::interval(Index index) const -> const Interval * {
    if (index >= count()) return nullptr;
    const auto &intervals = data_->intervals;
    auto first = usize(), last = intervals.len();
    while (first < last) {
        const auto middle = first + (last - first) / usize(2);
        const auto &entry = intervals[middle];
        if (index < entry.start) last = middle;
        else if (index >= entry.start + entry.count) first = middle + usize(1);
        else return &entry;
    }
    return nullptr;
}

auto LazyContent::describe(Index index) const -> LazyItemInfo {
    const auto *entry = interval(index);
    if (!entry || !entry->provider->describe) return {};
    auto provider = entry->provider.clone();
    return (**provider.get()->describe)(index - entry->start);
}

auto LazyContent::span(Index index, LazyGridSpanScope scope) const -> Index {
    const auto *entry = interval(index);
    if (!entry || !entry->provider->span) return 1;
    auto provider = entry->provider.clone();
    return (**provider.get()->span)(index - entry->start, scope);
}

auto LazyContent::build(Index index) const -> ComposeResult<Element> {
    const auto *entry = interval(index);
    if (!entry) return Err(ComposeError(ComposeStage::Evaluation, Some(UiError::InvalidValue)));
    auto provider = entry->provider.clone();
    return (*provider.get()->build)(index - entry->start);
}

auto LazyContent::header_after(Index index) const -> Option<Index> {
    const auto &headers = data_->headers;
    auto first = usize(), last = headers.len();
    while (first < last) {
        auto middle = first + (last - first) / usize(2);
        if (headers[middle] <= index) first = middle + usize(1);
        else last = middle;
    }
    return first < headers.len() ? Some(Index(headers[first])) : None<Index>();
}

auto LazyContent::header_before(Index index) const -> Option<Index> {
    const auto &headers = data_->headers;
    auto first = usize(), last = headers.len();
    while (first < last) {
        auto middle = first + (last - first) / usize(2);
        if (headers[middle] <= index) first = middle + usize(1);
        else last = middle;
    }
    return first != usize() ? Some(Index(headers[first - usize(1)])) : None<Index>();
}

auto LazyContent::append_item(Box<Builder> build, Option<Key> key, Option<Key> content_type, Index span, bool sticky)
    -> ComposeResult<empty> {
    auto describe = Box<Metadata>::make(
        [key = rstd::move(key), type = rstd::move(content_type)](Index) -> LazyItemInfo {
            return {key ? Some(Key(*key)) : None<Key>(), type ? Some(Key(*type)) : None<Key>()};
        });
    Option<Box<Span>> grid_span;
    if (span != 1) grid_span = Some(Box<Span>::make([span](Index, LazyGridSpanScope) { return span; }));
    return append(1, rstd::move(build), Some(rstd::move(describe)), rstd::move(grid_span), sticky);
}

auto LazyContent::item(Element element, Option<Key> key, Option<Key> content_type, Index span) -> ComposeResult<empty> {
    auto build = compiler::lazy_item_builder([element = rstd::move(element)](Index) { return Element(element); });
    return append_item(rstd::move(build), rstd::move(key), rstd::move(content_type), span, false);
}

auto LazyContent::sticky_header(Element element, Option<Key> key, Option<Key> content_type)
    -> ComposeResult<empty> {
    auto build = compiler::lazy_item_builder([element = rstd::move(element)](Index) { return Element(element); });
    return append_item(rstd::move(build), rstd::move(key), rstd::move(content_type), 0, true);
}
} // namespace vui::compose
