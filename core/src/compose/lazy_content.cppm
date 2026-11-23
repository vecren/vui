export module vui.compose:lazy_content;
import :element;
import :lifecycle;

export namespace vui::compose {
using namespace rstd::prelude;

struct LazyItemAnimationOptions {
    Option<AnimationOptions> fade_in{Some(AnimationOptions{180'000'000, AnimationEasing::EaseInOut})};
    Option<AnimationOptions> placement{Some(AnimationOptions{250'000'000, AnimationEasing::EaseInOut})};
    Option<AnimationOptions> fade_out{Some(AnimationOptions{180'000'000, AnimationEasing::EaseInOut})};

    auto valid() const -> bool {
        return (!fade_in || fade_in->valid()) && (!placement || placement->valid()) &&
               (!fade_out || fade_out->valid());
    }
};

struct LazyItemInfo {
    Option<Key> key;
    Option<Key> content_type;
    Option<LazyItemAnimationOptions> animation;
    Option<Scalar> fill_parent_max_width, fill_parent_max_height;
};

struct LazyGridSpanScope {
    Index max_line_span{1};
    Index max_current_line_span{1};
};

namespace [[clang::annotate("vui.compose.runtime.v1")]] compiler {
using LazyItemBuilder = rstd::dyn<rstd::FnMut<ComposeResult<Element>(Index)>>;

template <class F>
auto lazy_item_builder(F make) -> Box<LazyItemBuilder> {
    return Box<LazyItemBuilder>::make([make = rstd::move(make)](Index index) mutable -> ComposeResult<Element> {
        if constexpr (rstd::mtp::same_as<decltype(make(index)), Element>)
            return Ok(make(index));
        else
            return make(index);
    });
}

template <class T, class F>
auto lazy_item_builder(state::ValueSnapshot<Vec<T>> data, F make) -> Box<LazyItemBuilder> {
    return lazy_item_builder([data, make = rstd::move(make)](Index index) mutable {
        return make(data.value()[usize(index)]);
    });
}

template <class F>
auto lazy_single_item_builder(F make, Index index) -> Box<LazyItemBuilder> {
    return lazy_item_builder([make = rstd::move(make), index](Index) mutable {
        if constexpr (requires { make(index); }) return make(index);
        else return make();
    });
}
}

class LazyContent {
    using Builder = compiler::LazyItemBuilder;
    using Metadata = rstd::dyn<rstd::FnMut<LazyItemInfo(Index)>>;
    using Span = rstd::dyn<rstd::FnMut<Index(Index, LazyGridSpanScope)>>;

    struct Provider {
        Box<Builder> build;
        Option<Box<Metadata>> describe;
        Option<Box<Span>> span;
    };
    struct Interval {
        Index start, count;
        rstd::rc::Rc<Provider> provider;
    };
    struct Data {
        Vec<Interval> intervals;
        Vec<Index> headers;
        Index count{};
        bool has_spans{};
    };
    rstd::rc::Rc<Data> data_{rstd::rc::make_rc<Data>()};

    auto append(Index count, Box<Builder>, Option<Box<Metadata>>, Option<Box<Span>> = {}, bool sticky = false)
        -> ComposeResult<empty>;
    auto append_item(Box<Builder>, Option<Key>, Option<Key>, Index span, bool sticky,
                     Option<LazyItemAnimationOptions>) -> ComposeResult<empty>;
    auto interval(Index index) const -> const Interval *;

  public:
    auto count() const -> Index { return data_->count; }
    auto identity() const -> const void * { return data_.get(); }
    auto has_spans() const -> bool { return data_->has_spans; }
    auto describe(Index index) const -> LazyItemInfo;
    // A span of zero occupies the entire line.
    auto span(Index index, LazyGridSpanScope scope) const -> Index;
    auto build(Index index) const -> ComposeResult<Element>;
    auto header_before(Index index) const -> Option<Index>;
    auto header_after(Index index) const -> Option<Index>;

    auto item(Element element, Option<Key> key = {}, Option<Key> content_type = {}, Index span = 1,
              Option<LazyItemAnimationOptions> animation = {}) -> ComposeResult<empty>;
    auto sticky_header(Element element, Option<Key> key = {}, Option<Key> content_type = {},
                       Option<LazyItemAnimationOptions> animation = {}) -> ComposeResult<empty>;

    template <class F> requires requires(F &f) { f(); }
    auto item([[clang::annotate("vui.compose.content.v2")]] F content,
              Option<Key> key = {}, Option<Key> content_type = {}, Index span = 1,
              Option<LazyItemAnimationOptions> animation = {}) -> ComposeResult<empty> {
        return append_item(compiler::lazy_single_item_builder(rstd::move(content), count()),
                           rstd::move(key), rstd::move(content_type), span, false, animation);
    }

    template <class F> requires (requires(F &f) { f(Index{}); } || requires(F &f) { f(); })
    auto sticky_header([[clang::annotate("vui.compose.content.v2")]] F content,
                       Option<Key> key = {}, Option<Key> content_type = {},
                       Option<LazyItemAnimationOptions> animation = {}) -> ComposeResult<empty> {
        return append_item(compiler::lazy_single_item_builder(rstd::move(content), count()),
                           rstd::move(key), rstd::move(content_type), 0, true, animation);
    }

    template <class F>
    auto items(Index count, [[clang::annotate("vui.compose.content.v2")]] F content) -> ComposeResult<empty> {
        if (!count) return Ok(empty{});
        return append(count, compiler::lazy_item_builder(rstd::move(content)), {});
    }

    template <class F, class M>
    auto items(Index count, [[clang::annotate("vui.compose.content.v2")]] F content, M metadata) -> ComposeResult<empty> {
        if (!count) return Ok(empty{});
        return append(count, compiler::lazy_item_builder(rstd::move(content)),
                      Some(Box<Metadata>::make(rstd::move(metadata))));
    }

    template <class F, class M, class S>
    auto items(Index count, [[clang::annotate("vui.compose.content.v2")]] F content, M metadata, S span)
        -> ComposeResult<empty> {
        if (!count) return Ok(empty{});
        return append(count, compiler::lazy_item_builder(rstd::move(content)), Some(Box<Metadata>::make(rstd::move(metadata))),
                      Some(Box<Span>::make(rstd::move(span))));
    }

    template <class T, class F, class M>
    auto items(state::ValueSnapshot<Vec<T>> data, [[clang::annotate("vui.compose.content.v2")]] F content, M metadata)
        -> ComposeResult<empty> {
        const auto size = data.value().len();
        if (size > usize(~Index{})) return Err(ComposeError(ComposeStage::Limit));
        if (size == usize()) return Ok(empty{});
        auto describe = Box<Metadata>::make(
            [data, metadata = rstd::move(metadata)](Index index) mutable {
                return metadata(data.value()[usize(index)]);
            });
        return append(Index(size.to_primitive()), compiler::lazy_item_builder(data, rstd::move(content)),
                      Some(rstd::move(describe)));
    }

    template <class T, class F, class M, class S>
    auto items(state::ValueSnapshot<Vec<T>> data, [[clang::annotate("vui.compose.content.v2")]] F content, M metadata, S span)
        -> ComposeResult<empty> {
        const auto size = data.value().len();
        if (size > usize(~Index{})) return Err(ComposeError(ComposeStage::Limit));
        if (size == usize()) return Ok(empty{});
        auto describe = Box<Metadata>::make(
            [data, metadata = rstd::move(metadata)](Index index) mutable {
                return metadata(data.value()[usize(index)]);
            });
        auto measure_span = Box<Span>::make(
            [data, span = rstd::move(span)](Index index, LazyGridSpanScope scope) mutable {
                return span(data.value()[usize(index)], scope);
            });
        return append(Index(size.to_primitive()), compiler::lazy_item_builder(data, rstd::move(content)),
                      Some(rstd::move(describe)), Some(rstd::move(measure_span)));
    }

    template <class T, class F>
    auto items(state::ValueSnapshot<Vec<T>> data, [[clang::annotate("vui.compose.content.v2")]] F content)
        -> ComposeResult<empty> {
        const auto size = data.value().len();
        if (size > usize(~Index{})) return Err(ComposeError(ComposeStage::Limit));
        if (size == usize()) return Ok(empty{});
        return append(Index(size.to_primitive()), compiler::lazy_item_builder(data, rstd::move(content)), {});
    }
};

} // namespace vui::compose

namespace vui::compose {
auto nearby_lazy_index(const LazyContent &content, Index index, const Option<Key> &key) -> Option<Index>;
}
