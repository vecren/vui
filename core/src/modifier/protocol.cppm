export module vui:modifier.protocol;
import :layout.protocol;
import :paint.protocol;
import :node.state;
using namespace rstd::prelude;

export namespace vui {
class ModifierMeasureContext {
  public:
    virtual ~ModifierMeasureContext() = default;
    virtual auto node_id() const -> UiId = 0;
    virtual auto role() const -> NodeStateKey = 0;
    virtual auto measure_content(LayoutConstraints) -> Result<Size, UiError> = 0;
};
class ModifierArrangeContext {
  public:
    virtual ~ModifierArrangeContext() = default;
    virtual auto node_id() const -> UiId = 0;
    virtual auto role() const -> NodeStateKey = 0;
    virtual auto desired_content() const -> Size = 0;
    virtual auto place_content(LayoutPlacement) -> Result<empty, UiError> = 0;
};
class ModifierDrawContext {
  public:
    virtual ~ModifierDrawContext() = default;
    virtual auto role() const -> NodeStateKey = 0;
    virtual auto paint() -> PaintContext & = 0;
    virtual auto draw_content() -> Result<empty, UiError> = 0;
};
struct LayoutModifier {
    template <class Self, class Delegate = void> struct Api {
        using Trait = LayoutModifier;
        auto measure(ModifierMeasureContext &context, LayoutConstraints constraints)
            -> Result<Size, UiError> { return rstd::trait_call<0>(this, context, constraints); }
        auto arrange(ModifierArrangeContext &context, Size size) -> Result<empty, UiError> {
            return rstd::trait_call<1>(this, context, size);
        }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::measure, &T::arrange>;
};
struct DrawModifier {
    template <class Self, class Delegate = void> struct Api {
        using Trait = DrawModifier;
        auto draw(ModifierDrawContext &context) -> Result<empty, UiError> {
            return rstd::trait_call<0>(this, context);
        }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::draw>;
};
struct ModifierGeometry {
    Size size;
    Transform local_to_node;
};
}

namespace vui {
class ModifierReceiver {
  public:
    virtual ~ModifierReceiver() = default;
    virtual auto layout() const -> bool = 0;
    virtual auto drawing() const -> bool = 0;
    virtual auto measure(ModifierMeasureContext &, LayoutConstraints) -> Result<Size, UiError> = 0;
    virtual auto arrange(ModifierArrangeContext &, Size) -> Result<empty, UiError> = 0;
    virtual auto draw(ModifierDrawContext &) -> Result<empty, UiError> = 0;
};
}
