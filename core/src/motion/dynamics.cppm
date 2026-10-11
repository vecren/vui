export module vui:motion.dynamics;
import :types;
using namespace rstd::prelude;

export namespace vui {
struct ScalarMotion {
    Scalar value{}, velocity{};
};
struct Spring {
    ScalarMotion initial;
    Scalar target{}, stiffness{250}, damping{30}, mass{1};
    auto valid() const -> bool {
        return finite(initial.value) && finite(initial.velocity) && finite(target) &&
               finite(stiffness) && stiffness > 0 && finite(damping) && damping >= 0 &&
               finite(mass) && mass > 0;
    }
    auto sample(Serial elapsed_ns) const -> ScalarMotion {
        auto t = Scalar(elapsed_ns) / 1'000'000'000;
        auto x = initial.value - target, v = initial.velocity;
        auto a = damping / (2 * mass), w2 = stiffness / mass, discriminant = a * a - w2;
        if (__builtin_fabs(discriminant) < 1e-9) {
            auto b = v + a * x, decay = __builtin_exp(-a * t);
            return {target + decay * (x + b * t), decay * (v - a * b * t)};
        }
        if (discriminant < 0) {
            auto w = __builtin_sqrt(-discriminant), b = (v + a * x) / w;
            auto decay = __builtin_exp(-a * t), c = __builtin_cos(w * t), s = __builtin_sin(w * t);
            return {target + decay * (x * c + b * s),
                    decay * ((b * w - a * x) * c - (x * w + a * b) * s)};
        }
        auto root = __builtin_sqrt(discriminant), r1 = -a + root, r2 = -a - root;
        auto c1 = (v - r2 * x) / (r1 - r2), c2 = x - c1;
        auto e1 = __builtin_exp(r1 * t), e2 = __builtin_exp(r2 * t);
        return {target + c1 * e1 + c2 * e2, r1 * c1 * e1 + r2 * c2 * e2};
    }
    auto settled(ScalarMotion value, Scalar position_tolerance = .001,
                 Scalar velocity_tolerance = .001) const -> bool {
        return __builtin_fabs(value.value - target) <= position_tolerance &&
               __builtin_fabs(value.velocity) <= velocity_tolerance;
    }
};
struct Decay {
    ScalarMotion initial;
    Scalar drag{8};
    auto valid() const -> bool {
        return finite(initial.value) && finite(initial.velocity) && finite(drag) && drag > 0;
    }
    auto sample(Serial elapsed_ns) const -> ScalarMotion {
        auto decay = __builtin_exp(-drag * Scalar(elapsed_ns) / 1'000'000'000);
        return {initial.value + initial.velocity * (1 - decay) / drag, initial.velocity * decay};
    }
};
} // namespace vui
