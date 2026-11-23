export module vui.compose:lifecycle;
import :compiler;
import :runtime;
import :saveable;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui::compose {
namespace [[clang::annotate("vui.compose.runtime.v1")]] compiler {
inline auto context(CallSite site) -> ComposeResult<CompositionContext *> {
    if (!site.id || !current()) {
        ComposeError error(ComposeStage::Evaluation);
        error.operation = String::make(!site.id ? "compose hook requires compiler site"_str : "compose hook requires active evaluation"_str);
        error.source = site.source;
        return Err(rstd::move(error));
    }
    return Ok(current());
}
template <class T> auto located(ComposeResult<T> result, CallSite site) -> ComposeResult<T> {
    if (result.is_ok()) return result;
    auto error = result.unwrap_err();
    if (!error.source.line()) error.source = site.source;
    return Err(rstd::move(error));
}
template <class T, class F> auto initializer(F make) {
    return [make = rstd::move(make)]() mutable -> ComposeResult<T> {
        if constexpr (rstd::mtp::same_as<decltype(make()), T>) return Ok(make());
        else return make();
    };
}
}
template <class T>
[[clang::annotate("vui.compose.hook.v2")]]
auto state(T initial, CallSite site = {}) -> ComposeResult<State<T>> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(scope->state(scope->slot(site.id), rstd::move(initial)), site);
}
template <class T>
[[clang::annotate("vui.compose.hook.v2")]]
auto state(Key key, T initial, CallSite site = {}) -> ComposeResult<State<T>> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(scope->state(scope->slot(rstd::move(key)), rstd::move(initial)), site);
}
template <SaveableStateValue T>
[[clang::annotate("vui.compose.hook.v2")]]
auto saved_state(Key key, T initial, CallSite site = {}) -> ComposeResult<State<T>> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(scope->saved_state(scope->slot(rstd::move(key)), rstd::move(initial)), site);
}
template <class T, class F>
[[clang::annotate("vui.compose.hook.v2")]]
auto remember(EffectKeys keys, F make, CallSite site = {}) -> ComposeResult<Remembered<T>> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(scope->template remember<T>(scope->slot(site.id), rstd::move(keys), compiler::initializer<T>(rstd::move(make))), site);
}
template <class T, class F>
[[clang::annotate("vui.compose.hook.v2")]]
auto remember(F make, CallSite site = {}) -> ComposeResult<Remembered<T>> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(scope->template remember<T>(scope->slot(site.id), compiler::initializer<T>(rstd::move(make))), site);
}
template <class T, class F>
[[clang::annotate("vui.compose.hook.v2")]]
auto remember(Key key, EffectKeys keys, F make, CallSite site = {}) -> ComposeResult<Remembered<T>> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(scope->template remember<T>(scope->slot(rstd::move(key)), rstd::move(keys), compiler::initializer<T>(rstd::move(make))), site);
}
template <class T, class F>
[[clang::annotate("vui.compose.hook.v2")]]
auto remember(Key key, F make, CallSite site = {}) -> ComposeResult<Remembered<T>> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(scope->template remember<T>(scope->slot(rstd::move(key)), compiler::initializer<T>(rstd::move(make))), site);
}
template <class T>
[[clang::annotate("vui.compose.hook.v2")]]
auto read(vui::state::Value<T> value, CallSite site = {}) -> ComposeResult<vui::state::ValueSnapshot<T>> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    return compiler::located(opened.unwrap()->read(value), site);
}
template <class T>
[[clang::annotate("vui.compose.hook.v2")]]
auto read(const CompositionLocal<T> &local, CallSite site = {}) -> ComposeResult<LocalSnapshot<T>> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    return compiler::located(opened.unwrap()->read(local), site);
}
[[clang::annotate("vui.compose.hook.v2")]]
inline auto node(CallSite site = {}) -> ComposeResult<NodeRef> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(scope->node(scope->slot(site.id)), site);
}
[[clang::annotate("vui.compose.hook.v2")]]
inline auto node(Key key, CallSite site = {}) -> ComposeResult<NodeRef> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(scope->node(scope->slot(rstd::move(key))), site);
}
[[clang::annotate("vui.compose.hook.v2")]]
inline auto group(Key key, CallSite site = {}) -> ComposeResult<CompositionContext::GroupGuard> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    return Ok(opened.unwrap()->group(rstd::move(key), site.id, site.source));
}
template <class F>
[[clang::annotate("vui.compose.hook.v2")]]
auto effect(EffectKeys keys, [[clang::annotate("vui.compose.deferred.v2")]] F start, CallSite site = {}) -> ComposeResult<empty> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(scope->effect(scope->slot(site.id), rstd::move(keys),
        [start = rstd::move(start)](CommittedContext &context) mutable -> ComposeResult<EffectLease> {
            if constexpr (requires { start(context); }) {
                if constexpr (rstd::mtp::same_as<decltype(start(context)), EffectLease>) return Ok(start(context));
                else return start(context);
            } else {
                if constexpr (rstd::mtp::same_as<decltype(start()), EffectLease>) return Ok(start());
                else return start();
            }
        }, site.source), site);
}
} // namespace vui::compose
