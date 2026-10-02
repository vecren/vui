module;
#include <rstd/enum.hpp>
export module vui:animation;
import :types;
using namespace rstd::prelude;

export namespace vui {
enum class AnimationProperty { Transform, Opacity, Width, Height, Background };
enum class AnimationEasing { Linear, EaseInOut };
enum class AnimationEndPolicy { RestoreBase, CommitFinal };
enum class AnimationEndReason { Finished, Cancelled, Replaced, TargetDetached };
enum class AnimationError {
    InvalidTarget,
    InvalidValue,
    InvalidHandle,
    LayoutPending,
    Limit,
    Reentrant,
    Closed
};
struct AnimationId {
    Serial owner{}, value{};
    friend auto operator==(const AnimationId &, const AnimationId &) -> bool = default;
};
struct AnimationOptions {
    Serial duration_ns{250'000'000};
    AnimationEasing easing{AnimationEasing::Linear};
    AnimationEndPolicy end{AnimationEndPolicy::RestoreBase};
    auto valid() const -> bool {
        return (easing == AnimationEasing::Linear || easing == AnimationEasing::EaseInOut) &&
               (end == AnimationEndPolicy::RestoreBase || end == AnimationEndPolicy::CommitFinal);
    }
};
struct AnimationEvent {
    AnimationId id;
    UiId target;
    AnimationProperty property;
    AnimationEndReason reason;
    Serial sample_time_ns{}, mount_generation{};
};
class AnimationValue {
    RSTD_ENUM(AnimationValue, (Number, (Scalar value;)), (Matrix, (Transform value;)),
              (Tint, (Color value;)))
  public:
    auto valid(AnimationProperty property) const -> bool {
        if (property == AnimationProperty::Transform)
            return is_Matrix() && as_Matrix().value.valid();
        if (property == AnimationProperty::Background)
            return is_Tint() && as_Tint().value.valid();
        return is_Number() && finite(as_Number().value) && as_Number().value >= 0 &&
               ((property == AnimationProperty::Opacity && as_Number().value <= 1) ||
                property == AnimationProperty::Width || property == AnimationProperty::Height);
    }
    auto interpolate(const AnimationValue &to, Scalar t) const
        -> Result<AnimationValue, AnimationError> {
        if (!finite(t) || t < 0 || t > 1 || is_Number() != to.is_Number() ||
            is_Matrix() != to.is_Matrix() || is_Tint() != to.is_Tint())
            return Err(AnimationError::InvalidValue);
        auto well_formed = [](const AnimationValue &v) {
            return v.is_Number()   ? finite(v.as_Number().value)
                   : v.is_Matrix() ? v.as_Matrix().value.valid()
                                   : v.as_Tint().value.valid();
        };
        if (!well_formed(*this) || !well_formed(to))
            return Err(AnimationError::InvalidValue);
        if (t == 0)
            return Ok(AnimationValue(*this));
        if (t == 1)
            return Ok(AnimationValue(to));
        auto mix = [=](Scalar a, Scalar b) {
            if ((a <= 0 && b >= 0) || (a >= 0 && b <= 0))
                return (1 - t) * a + t * b;
            auto value = a + t * (b - a);
            auto lower = a < b ? a : b, upper = a < b ? b : a;
            return value < lower ? lower : value > upper ? upper : value;
        };
        if (is_Number())
            return Ok(Number(mix(as_Number().value, to.as_Number().value)));
        if (is_Matrix()) {
            auto a = as_Matrix().value, b = to.as_Matrix().value;
            return Ok(Matrix(Transform{mix(a.a, b.a), mix(a.b, b.b), mix(a.c, b.c), mix(a.d, b.d),
                                       mix(a.tx, b.tx), mix(a.ty, b.ty)}));
        }
        auto a = as_Tint().value, b = to.as_Tint().value;
        return Ok(Tint(Color{float(mix(a.r, b.r)), float(mix(a.g, b.g)), float(mix(a.b, b.b)),
                             float(mix(a.a, b.a))}));
    }
};
struct AnimationState {
    AnimationId id;
    UiId target;
    AnimationProperty property;
    AnimationValue value, target_value;
    AnimationOptions options;
    Option<Serial> started_ns;
    Serial mount_generation{};
};
} // namespace vui
