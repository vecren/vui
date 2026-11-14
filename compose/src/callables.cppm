export module vui.compose:callables;
import :compiler;
import :runtime;
import :element;
import :types;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui::compose {
namespace [[clang::annotate("vui.compose.runtime.v1")]] compiler {
struct ContentTag {};
struct ContentFunction {
    Box<rstd::dyn<rstd::FnMut<ComposeResult<Element>()>>> invoke;
    template <class F> explicit ContentFunction(F body)
        : invoke(Box<rstd::dyn<rstd::FnMut<ComposeResult<Element>()>>>::make(
            [body = rstd::move(body)]() mutable -> ComposeResult<Element> {
                using R = decltype(body());
                static_assert(rstd::mtp::same_as<R, Element> || rstd::mtp::same_as<R, ComposeResult<Element>>,
                    "Content must return Element or ComposeResult<Element>");
                return evaluated(body());
            })) {}
};
struct ContentParameters final : ComponentParameters {
    rstd::rc::Rc<ContentFunction> function;
    explicit ContentParameters(rstd::rc::Rc<ContentFunction> value) : function(rstd::move(value)) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<ContentParameters>(); }
    auto equal(const ComponentParameters &other) const -> bool override {
        return other.type() == type() && function.get() == static_cast<const ContentParameters &>(other).function.get();
    }
};
template <class Signature> struct ActionFunction {
    Box<rstd::dyn<rstd::FnMut<Signature>>> invoke;
    template <class F> explicit ActionFunction(F callback)
        : invoke(Box<rstd::dyn<rstd::FnMut<Signature>>>::make(rstd::move(callback))) {}
};
}

class [[clang::annotate("vui.compose.content.type.v2")]] Content {
    rstd::rc::Rc<compiler::ContentFunction> function_;
  public:
    template <class F> requires (!rstd::mtp::same_as<rstd::mtp::decay<F>, Content>)
    explicit Content([[clang::annotate("vui.compose.content.v2")]] F body)
        : function_(rstd::rc::make_rc<compiler::ContentFunction>(rstd::move(body))) {}
    Content(const Content &) = default;
    Content(Content &&) = default;
    auto operator=(const Content &) -> Content & = default;
    auto operator=(Content &&) -> Content & = default;
    [[clang::annotate("vui.compose.content.call.v2")]]
    auto operator()(CallSite site = {}) const -> ComposeResult<Element> {
        if (!site.id) {
            auto error = ComposeError(ComposeStage::Evaluation);
            error.source = site.source;
            error.operation = String::make("Content invocation requires a compose compiler site"_str);
            return Err(rstd::move(error));
        }
        auto parameters = rstd::rc::Rc<ComponentParameters>(new compiler::ContentParameters(function_));
        auto evaluate = [parameters](CompositionContext &) mutable -> ComposeResult<Element> {
            auto &value = static_cast<compiler::ContentParameters &>(*parameters.get());
            return value.function.get()->invoke->operator()();
        };
        return Ok(component<compiler::ContentTag>(compiler::identity(site), rstd::move(evaluate), site.source, rstd::move(parameters)));
    }
    auto same_instance(const Content &other) const -> bool { return function_.get() == other.function_.get(); }
};
template <class F> auto make_content([[clang::annotate("vui.compose.content.v2")]] F body) -> Content {
    return Content(rstd::move(body));
}

template <class Signature> class Action;
template <class R, class... A>
class [[clang::annotate("vui.compose.action.v2")]] Action<R(A...)> {
    rstd::rc::Rc<compiler::ActionFunction<R(A...)>> function_;
  public:
    template <class F> requires (!rstd::mtp::same_as<rstd::mtp::decay<F>, Action>)
    explicit Action(F callback)
        : function_(rstd::rc::make_rc<compiler::ActionFunction<R(A...)>>(rstd::move(callback))) {}
    Action(const Action &) = default;
    Action(Action &&) = default;
    auto operator=(const Action &) -> Action & = default;
    auto operator=(Action &&) -> Action & = default;
    auto operator()(A... args) const -> R {
        auto function = function_;
        return function.get()->invoke->operator()(rstd::forward<A>(args)...);
    }
    auto same_instance(const Action &other) const -> bool { return function_.get() == other.function_.get(); }
};
template <> struct InputTraits<Content> {
    using Stored = Content;
    static constexpr bool comparable = true;
    static auto capture(Content value) -> Stored { return value; }
    static auto replay(const Stored &value) -> const Content & { return value; }
    static auto equal(const Stored &a, const Stored &b) -> bool { return a.same_instance(b); }
};
template <class S> struct InputTraits<Action<S>> {
    using Stored = Action<S>;
    static constexpr bool comparable = true;
    static auto capture(Action<S> value) -> Stored { return value; }
    static auto replay(const Stored &value) -> const Action<S> & { return value; }
    static auto equal(const Stored &a, const Stored &b) -> bool { return a.same_instance(b); }
};
}
