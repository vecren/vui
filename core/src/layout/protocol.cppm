export module vui:layout.protocol;
import :layout;
import :text;
using namespace rstd::prelude;

export namespace vui {
struct LayoutPlacement {
    Rect slot;
    Size local_size;
    Transform local_to_slot;
    auto valid() const -> bool {
        return slot.valid() && local_size.valid() && local_to_slot.valid();
    }
};
class LayoutMeasureContext {
  public:
    virtual ~LayoutMeasureContext() = default;
    virtual auto node_id() const -> UiId = 0;
    virtual auto style() const -> LayoutStyle = 0;
    virtual auto child_count() const -> usize = 0;
    virtual auto child_id(usize) const -> Result<UiId, UiError> = 0;
    virtual auto child_relation(usize) const -> Result<UiRelation, UiError> = 0;
    virtual auto measure_child(usize, LayoutConstraints) -> Result<Size, UiError> = 0;
    virtual void request_layout() = 0;
};
class LayoutArrangeContext {
  public:
    virtual ~LayoutArrangeContext() = default;
    virtual auto node_id() const -> UiId = 0;
    virtual auto style() const -> LayoutStyle = 0;
    virtual auto child_count() const -> usize = 0;
    virtual auto child_id(usize) const -> Result<UiId, UiError> = 0;
    virtual auto child_relation(usize) const -> Result<UiRelation, UiError> = 0;
    virtual auto desired_child(usize) -> Result<Size, UiError> = 0;
    virtual auto child_text(usize) -> Result<TextPlacement, UiError> { return Err(UiError::InvalidValue); }
    virtual auto set_text_input_area(Option<Rect>) -> Result<empty, UiError> { return Err(UiError::InvalidValue); }
    virtual void request_paint() {}
    virtual auto arrange_child(usize, Rect) -> Result<empty, UiError> = 0;
    virtual auto place_child(usize, LayoutPlacement) -> Result<empty, UiError> {
        return Err(UiError::InvalidValue);
    }
    virtual auto set_clip(Clip) -> Result<empty, UiError> { return Err(UiError::InvalidValue); }
    virtual void request_layout() = 0;
};
class LayoutStrategy {
  public:
    virtual ~LayoutStrategy() = default;
    virtual auto measure(LayoutMeasureContext &, LayoutConstraints) -> Result<Size, UiError> = 0;
    virtual auto arrange(LayoutArrangeContext &, Size) -> Result<empty, UiError> = 0;
};
struct Layout {
    template <class Self, class Delegate = void> struct Api {
        using Trait = Layout;
        auto measure(LayoutMeasureContext &context, LayoutConstraints constraints)
            -> Result<Size, UiError> {
            return rstd::trait_call<0>(this, context, constraints);
        }
        auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
            return rstd::trait_call<1>(this, context, size);
        }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::measure, &T::arrange>;
};
} // namespace vui
