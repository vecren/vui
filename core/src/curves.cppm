export module vui:curves;
import :types;
using namespace rstd::prelude;
export namespace vui {
struct ScalarCurveKey { Scalar time, a, b, c, d; };
struct ScalarCurve {
    Scalar start{}, stop{}, speed{1};
    slice<ScalarCurveKey> keys;
    auto valid() const -> bool {
        if (!finite(start) || !finite(stop) || stop<start || !finite(speed) || speed<=0) return false;
        Option<Scalar> previous;
        for (auto key:keys) {
            if (!finite(key.time) || !finite(key.a) || !finite(key.b) || !finite(key.c) || !finite(key.d) ||
                (previous.is_some() && key.time<=*previous)) return false;
            previous=Some(key.time);
        }
        return true;
    }
    auto sample(Scalar elapsed) const -> Scalar {
        if (keys.is_empty()) return 0;
        auto time=__builtin_fmax(start,__builtin_fmin(stop,start+elapsed*speed));
        auto key=keys[usize()];
        for (auto candidate:keys) {if (candidate.time>time) break;key=candidate;}
        auto delta=time-key.time;return ((key.a*delta+key.b)*delta+key.c)*delta+key.d;
    }
};
} // namespace vui
