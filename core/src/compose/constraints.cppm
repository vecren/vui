export module vui.compose:constraints;
import :runtime;
import :element;
using namespace rstd::prelude;

export namespace vui::compose {
namespace [[clang::annotate("vui.compose.runtime.v1")]] compiler {
struct ConstraintBody {
    Box<rstd::dyn<rstd::FnMut<ComposeResult<Element>(CompositionContext &, LayoutConstraints)>>> invoke;
    template <class F> explicit ConstraintBody(F body)
        : invoke(Box<rstd::dyn<rstd::FnMut<ComposeResult<Element>(CompositionContext &, LayoutConstraints)>>>::make(
            [body = rstd::move(body)](CompositionContext &context, LayoutConstraints constraints) mutable -> ComposeResult<Element> {
                if constexpr (rstd::mtp::same_as<decltype(body(context, constraints)), Element>)
                    return Ok(body(context, constraints));
                else return body(context, constraints);
            })) {}
};
auto constraint_element(Key, rstd::rc::Rc<ConstraintBody>, rstd::source_location) -> Element;
}

template <class F>
auto with_constraints(Key key, [[clang::annotate("vui.compose.content.v2")]] F body,
                      rstd::source_location source = rstd::source_location::current()) -> Element {
    return compiler::constraint_element(rstd::move(key),
        rstd::rc::make_rc<compiler::ConstraintBody>(rstd::move(body)), source);
}
}
