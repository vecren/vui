export module vui.compose:compiler;
import :runtime;
import :element;
import :types;
export import :inputs;
using namespace rstd::prelude;

export namespace vui::compose {
namespace [[clang::annotate("vui.compose.runtime.v1")]] compiler {
template <auto Function> struct FunctionTag {};
template <class Callable> struct CallableTag {};
template <class T> struct Signature;
template <class R, class... P> struct Signature<R (*)(P...)> {
    using Result = R;
    using Parameters = rstd::tuple<typename InputTraits<P>::Stored...>;
    static constexpr bool replayable = ((!rstd::mtp::is_ref_rv<P> &&
        (!rstd::mtp::is_ref_lv<P> || rstd::mtp::is_const<rstd::mtp::rm_ref<P>>) &&
        rstd::mtp::init<P, decltype(InputTraits<P>::replay(rstd::mtp::declval<const typename InputTraits<P>::Stored &>()))>) && ...);
    static constexpr bool comparable = (InputTraits<P>::comparable && ...);
    template <class... A> static auto capture(A &&...args) -> Parameters {
        static_assert(sizeof...(P) == sizeof...(A), "compose arguments must match the selected function signature");
        return Parameters(InputTraits<P>::capture(rstd::forward<A>(args))...);
    }
    template <auto Function, rstd::size_t... I>
    static auto evaluate(const Parameters &parameters, rstd::mtp::index_sequence<I...>) -> R {
        return Function(InputTraits<P>::replay(parameters.template get<I>())...);
    }
    template <auto Function> static auto evaluate(const Parameters &parameters) -> R {
        return evaluate<Function>(parameters, rstd::mtp::make_index_sequence<sizeof...(P)>{});
    }
    template <class F, rstd::size_t... I>
    static auto evaluate(F &callable, const Parameters &parameters, rstd::mtp::index_sequence<I...>) -> R {
        return callable(InputTraits<P>::replay(parameters.template get<I>())...);
    }
    template <class F> static auto evaluate(F &callable, const Parameters &parameters) -> R {
        return evaluate(callable, parameters, rstd::mtp::make_index_sequence<sizeof...(P)>{});
    }
    template <rstd::size_t... I>
    static auto equal(const Parameters &a, const Parameters &b, rstd::mtp::index_sequence<I...>) -> bool {
        return (InputTraits<P>::equal(a.template get<I>(), b.template get<I>()) && ...);
    }
    static auto equal(const Parameters &a, const Parameters &b) -> bool {
        if constexpr (comparable) return equal(a, b, rstd::mtp::make_index_sequence<sizeof...(P)>{});
        else return false;
    }
};
template <class R, class... P> struct Signature<R (*)(P...) noexcept> : Signature<R (*)(P...)> {};
template <class R, class C, class... P> struct Signature<R (C::*)(P...)> : Signature<R (*)(P...)> {};
template <class R, class C, class... P> struct Signature<R (C::*)(P...) const> : Signature<R (*)(P...)> {};
template <class R, class C, class... P> struct Signature<R (C::*)(P...) noexcept> : Signature<R (*)(P...)> {};
template <class R, class C, class... P> struct Signature<R (C::*)(P...) const noexcept> : Signature<R (*)(P...)> {};

template <class F, class... A> struct CallableSignature
    : Signature<decltype(rstd::mtp::declval<F &>()(rstd::mtp::declval<A>()...)) (*)(rstd::mtp::decay<A>...)> {};
template <class F, class... A> requires requires { &F::operator(); }
struct CallableSignature<F, A...> : Signature<decltype(&F::operator())> {};
template <class R, class... P, class... A>
struct CallableSignature<R (*)(P...), A...> : Signature<R (*)(P...)> {};

template <auto Function> struct Arguments final : ComponentParameters {
    using FunctionSignature = Signature<decltype(Function)>;
    using Parameters = typename FunctionSignature::Parameters;
    Parameters values;
    template <class... A> explicit Arguments(A &&...args) : values(FunctionSignature::capture(rstd::forward<A>(args)...)) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<Arguments>(); }
    auto equal(const ComponentParameters &other) const -> bool override {
        return other.type() == type() && FunctionSignature::equal(values, static_cast<const Arguments &>(other).values);
    }
};
template <class R> auto evaluated(R result) -> ComposeResult<Element> {
    if constexpr (rstd::mtp::same_as<R, Element>) return Ok(rstd::move(result));
    else return result;
}
template <class R> auto described(Element value) -> R {
    static_assert(rstd::mtp::same_as<R, Element> || rstd::mtp::same_as<R, ComposeResult<Element>>,
        "compose functions must return Element or ComposeResult<Element>");
    if constexpr (rstd::mtp::same_as<R, Element>) return value;
    else return Ok(rstd::move(value));
}
[[clang::annotate("vui.compose.source.v2")]]
consteval auto source(Serial id, const char *file, const char *function, unsigned line, unsigned column) -> CallSite {
    return {id, rstd::source_location::current(file, function, line, column)};
}
inline auto identity(CallSite site) -> CallIdentity {
    return current() ? current()->identity(site.id) : CallIdentity{site.id, 0};
}
template <auto Function, class... A>
[[clang::annotate("vui.compose.call.v2")]]
auto call(CallSite site, A &&...args) -> typename Signature<decltype(Function)>::Result {
    using FunctionSignature = Signature<decltype(Function)>;
    using R = typename FunctionSignature::Result;
    static_assert(FunctionSignature::replayable,
        "compose parameters need a repeatable InputTraits::replay; mutable and rvalue references are unsupported");
    auto parameters = rstd::rc::Rc<ComponentParameters>(new Arguments<Function>(rstd::forward<A>(args)...));
    auto evaluate = [parameters](CompositionContext &) -> ComposeResult<Element> {
        const auto &values = static_cast<const Arguments<Function> &>(*parameters.get()).values;
        return evaluated(FunctionSignature::template evaluate<Function>(values));
    };
    auto value = component<FunctionTag<Function>>(identity(site), rstd::move(evaluate), site.source, rstd::move(parameters));
    if (!site.id) {
        auto data = rstd::rc::make_rc<ElementData>(value.data()); data.get()->invalid = true; value = Element(rstd::move(data));
    }
    return described<R>(rstd::move(value));
}
template <class F, class... A>
[[clang::annotate("vui.compose.invoke.v2")]]
auto invoke(CallSite site, F callable, A &&...args) -> typename CallableSignature<F, A...>::Result {
    using FunctionSignature = CallableSignature<F, A...>;
    using R = typename FunctionSignature::Result;
    static_assert(FunctionSignature::replayable,
        "compose callable parameters need repeatable owned InputTraits storage");
    auto evaluate = [callable = rstd::move(callable), values = FunctionSignature::capture(rstd::forward<A>(args)...)]
        (CompositionContext &) mutable -> ComposeResult<Element> {
        return evaluated(FunctionSignature::evaluate(callable, values));
    };
    auto value = component<CallableTag<F>>(identity(site), rstd::move(evaluate), site.source);
    if (!site.id) {
        auto data = rstd::rc::make_rc<ElementData>(value.data()); data.get()->invalid = true; value = Element(rstd::move(data));
    }
    return described<R>(rstd::move(value));
}
}
}
