module vui.compose;
import :movable;
using namespace rstd::prelude;

namespace vui::compose {
namespace {
struct MovableParameters final : ComponentParameters {
    rstd::rc::Rc<MovableIdentity> identity;
    explicit MovableParameters(rstd::rc::Rc<MovableIdentity> value) : identity(rstd::move(value)) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<MovableParameters>(); }
    auto equal(const ComponentParameters &other) const -> bool override {
        return other.type() == type() && identity.get() == static_cast<const MovableParameters &>(other).identity.get();
    }
};
}
auto MovableContent::operator()(CallSite site) const -> Element {
    auto data = rstd::rc::make_rc<ElementData>();
    data.get()->kind = ElementKind::Movable;
    data.get()->key = Some(Key{identity_->identity});
    data.get()->view = identity_->view.clone();
    data.get()->movable = identity_.clone();
    data.get()->component_type = rstd::any::TypeId::of<MovableContent>();
    data.get()->parameters = rstd::rc::Rc<ComponentParameters>(new MovableParameters(identity_.clone()));
    data.get()->source = site.source.line() ? site.source : identity_->source;
    return Element(rstd::move(data));
}
}
