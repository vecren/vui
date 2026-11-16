export module vui:document.modifier;
import :modifier.protocol;
import :binding.scope;
using namespace rstd::prelude;

namespace vui {
struct DocumentModifier {
    NodeStateKey role;
    BindingGuard binding;
    Box<ModifierReceiver> receiver;
    Size desired, content_desired, measured;
    Transform local_to_node;
    LayoutPlacement content_placement;
};
}
