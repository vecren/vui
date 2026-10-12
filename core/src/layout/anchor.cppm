export module vui:layout.anchor;
import :layout.parameters;
using namespace rstd::prelude;

export namespace vui {
struct AnchorSpec {
    Point anchor_min, anchor_max, offset_min, offset_max, pivot;
    Option<Scalar> width, height;
    auto valid() const -> bool {
        auto axis = [](Scalar min, Scalar max, Scalar offset_min, Scalar offset_max, Scalar pivot,
                       Option<Scalar> size) {
            return finite(min) && finite(max) && min >= 0 && max <= 1 && min <= max &&
                   finite(offset_min) && finite(offset_max) && finite(pivot) && pivot >= 0 &&
                   pivot <= 1 &&
                   (min == max
                        ? offset_max == 0 && (size.is_none() || (finite(*size) && *size >= 0))
                        : size.is_none());
        };
        return axis(anchor_min.x, anchor_max.x, offset_min.x, offset_max.x, pivot.x, width) &&
               axis(anchor_min.y, anchor_max.y, offset_min.y, offset_max.y, pivot.y, height);
    }
};
class AnchorLayout {
    LayoutParameters<AnchorSpec> parameters_;
    static auto placement(AnchorSpec spec, Size parent, Size desired) -> Result<Rect, UiError> {
        if (!spec.valid())
            return Err(UiError::InvalidValue);
        auto axis = [](Scalar min, Scalar max, Scalar first, Scalar last, Scalar pivot,
                       Option<Scalar> explicit_size, Scalar parent, Scalar desired) -> Point {
            if (min == max) {
                auto size = explicit_size.unwrap_or(desired);
                return {parent * min + first - pivot * size, size};
            }
            auto start = parent * min + first;
            return {start, parent * max + last - start};
        };
        auto x = axis(spec.anchor_min.x, spec.anchor_max.x, spec.offset_min.x, spec.offset_max.x,
                      spec.pivot.x, spec.width, parent.width, desired.width);
        auto y = axis(spec.anchor_min.y, spec.anchor_max.y, spec.offset_min.y, spec.offset_max.y,
                      spec.pivot.y, spec.height, parent.height, desired.height);
        Rect rect{x.x, y.x, x.y, y.y};
        return rect.valid() ? Result<Rect, UiError>(Ok(rect))
                            : Result<Rect, UiError>(Err(UiError::InvalidValue));
    }

  public:
    auto set(UiRelation relation, AnchorSpec spec) -> Result<empty, UiError> {
        if (!spec.valid())
            return Err(UiError::InvalidValue);
        parameters_.set(relation, spec);
        return Ok(empty{});
    }
    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints)
        -> Result<Size, UiError> {
        parameters_.retain(context);
        Size desired;
        for (usize i{}; i < context.child_count(); ++i) {
            auto result = context.measure_child(i, {});
            if (result.is_err())
                return Err(result.unwrap_err());
            auto child = result.unwrap();
            desired.width = rstd::cmp::max(desired.width, child.width);
            desired.height = rstd::cmp::max(desired.height, child.height);
        }
        return Ok(Size{constraints.max_width.unwrap_or(desired.width),
                       constraints.max_height.unwrap_or(desired.height)});
    }
    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        for (usize i{}; i < context.child_count(); ++i) {
            auto desired = context.desired_child(i);
            if (desired.is_err())
                return Err(desired.unwrap_err());
            auto spec = parameters_.get(context.child_relation(i).unwrap());
            auto rect = placement(spec ? *spec : AnchorSpec{}, size, desired.unwrap());
            if (rect.is_err())
                return Err(rect.unwrap_err());
            auto slot = rect.unwrap();
            auto arranged =
                context.place_child(i, LayoutPlacement{slot, {slot.width, slot.height}, {}});
            if (arranged.is_err())
                return arranged;
        }
        return Ok(empty{});
    }
};
} // namespace vui
