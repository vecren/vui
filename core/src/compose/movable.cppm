export module vui.compose:movable;
import :compiler;
import :runtime;
import :element;
using namespace rstd::prelude;

export namespace vui::compose {
class [[clang::annotate("vui.compose.content.type.v2")]] MovableContent {
    rstd::rc::Rc<MovableIdentity> identity_;
  public:
    template <class F> requires (!rstd::mtp::same_as<rstd::mtp::decay<F>, MovableContent>)
    explicit MovableContent([[clang::annotate("vui.compose.content.v2")]] F body,
                            rstd::source_location source = rstd::source_location::current())
        : identity_(rstd::rc::make_rc<MovableIdentity>(rstd::rc::make_rc<ViewFunction>(
            [body = rstd::move(body)](CompositionContext &context) mutable -> ComposeResult<Element> {
                if constexpr (requires { body(context); }) return compiler::evaluated(body(context));
                else return compiler::evaluated(body());
            }), source)) {}
    MovableContent(const MovableContent &) = default;
    MovableContent(MovableContent &&) = default;
    auto operator=(const MovableContent &) -> MovableContent & = default;
    auto operator=(MovableContent &&) -> MovableContent & = default;
    [[clang::annotate("vui.compose.content.call.v2")]]
    auto operator()(CallSite site = {}) const -> Element;
    auto same_instance(const MovableContent &other) const -> bool { return identity_.get() == other.identity_.get(); }
};
template <> struct InputTraits<MovableContent> {
    using Stored = MovableContent;
    static constexpr bool comparable = true;
    static auto capture(MovableContent value) -> Stored { return value; }
    static auto replay(const Stored &value) -> const Stored & { return value; }
    static auto equal(const Stored &a, const Stored &b) -> bool { return a.same_instance(b); }
};
}
