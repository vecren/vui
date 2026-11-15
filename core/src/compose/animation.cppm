export module vui.compose:animation;
import :lifecycle;
using namespace rstd::prelude;

export namespace vui::compose {
[[clang::annotate("vui.compose.hook.v2")]]
inline auto animation_scope(CallSite site = {}) -> ComposeResult<rstd::rc::Rc<AnimationScope>> {
    auto opened = compiler::context(site); if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    auto saved = scope->remember<rstd::rc::Rc<AnimationScope>>(scope->slot(site.id), [&]() -> ComposeResult<rstd::rc::Rc<AnimationScope>> {
        return Ok(AnimationScope::create(scope->session().document(), false));
    });
    if (saved.is_err()) return Err(saved.unwrap_err());
    auto state = saved.unwrap().get(); if (state.is_err()) return Err(state.unwrap_err());
    auto owner = state.unwrap()->clone();
    auto registered = scope->effect(scope->slot(site.id), {}, [owner](CommittedContext &) mutable -> ComposeResult<EffectLease> {
        auto activated = owner.get()->activate();
        if (activated.is_err()) return Err(ComposeError(ComposeStage::Effect, activated.unwrap_err().ui));
        return Ok(EffectLease([owner]() mutable { owner.get()->close(); }));
    }, site.source);
    if (registered.is_err()) return Err(registered.unwrap_err());
    return Ok(rstd::move(owner));
}
} // namespace vui::compose
