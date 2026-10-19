export module vui:curves;
import :types;
using namespace rstd::prelude;
export namespace vui {
struct ScalarCurveKey { Scalar time, a, b, c, d; };
struct ScalarCurveRange { Scalar minimum{},maximum{}; };
struct ScalarCurve {
    Scalar start{}, stop{}, speed{1};
    slice<ScalarCurveKey> keys;
  private:
    static auto polynomial(ScalarCurveKey key,Scalar delta) -> Scalar { return ((key.a*delta+key.b)*delta+key.c)*delta+key.d; }
    template<class F> void segments(Scalar begin,Scalar end,F &&visit) const {
        if (keys.is_empty()) return;
        auto key=keys[usize()];usize next(1);
        while (next<keys.len() && keys[next].time<=begin) key=keys[next++];
        while (begin<end) {
            auto stop=next<keys.len() ? __builtin_fmin(end,keys[next].time) : end;
            visit(key,begin-key.time,stop-key.time);begin=stop;
            if (next<keys.len()) key=keys[next++];
        }
    }
  public:
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
        return polynomial(key,time-key.time);
    }
    auto integral(Scalar begin,Scalar end) const -> Result<Scalar,UiError> {
        if (!valid() || !finite(begin) || !finite(end) || end<begin) return Err(UiError::InvalidValue);
        auto duration=(stop-start)/speed;
        if (!finite(duration)) return Err(UiError::InvalidValue);
        Scalar result{};
        if (begin<0) result+=(__builtin_fmin(end,Scalar(0))-begin)*sample(0);
        if (end>duration) result+=(end-__builtin_fmax(begin,duration))*sample(duration);
        auto lo=__builtin_fmax(begin,Scalar(0)),hi=__builtin_fmin(end,duration);
        if (hi>lo) segments(start+lo*speed,start+hi*speed,[&](ScalarCurveKey key,Scalar a,Scalar b) {
            auto primitive=[&](Scalar t){return (((key.a/4*t+key.b/3)*t+key.c/2)*t+key.d)*t;};
            result+=(primitive(b)-primitive(a))/speed;
        });
        if (!finite(result)) return Err(UiError::InvalidValue);
        return Ok(result);
    }
    auto bounds() const -> Result<ScalarCurveRange,UiError> {
        if (!valid()) return Err(UiError::InvalidValue);
        auto value=sample(0);ScalarCurveRange result{value,value};bool valid_values=finite(value);
        auto take=[&](Scalar v) {
            valid_values&=finite(v);result.minimum=__builtin_fmin(result.minimum,v);result.maximum=__builtin_fmax(result.maximum,v);
        };
        take(sample((stop-start)/speed));
        segments(start,stop,[&](ScalarCurveKey key,Scalar lo,Scalar hi) {
            take(polynomial(key,lo));take(polynomial(key,hi));
            auto scale=__builtin_fmax(__builtin_fabs(key.a),__builtin_fmax(__builtin_fabs(key.b),__builtin_fabs(key.c)));
            if (!scale) return;
            auto a=3*(key.a/scale),b=2*(key.b/scale),c=key.c/scale;
            auto root=[&](Scalar t){if (lo<=t && t<=hi) take(polynomial(key,t));};
            if (!a) {if (b) root(-c/b);return;}
            auto discriminant=b*b-4*a*c;
            if (discriminant<0) return;
            auto sqrt=__builtin_sqrt(discriminant),q=-.5*(b+(b<0 ? -sqrt : sqrt));
            if (!q) root(0); else {root(q/a);root(c/q);}
        });
        if (!valid_values) return Err(UiError::InvalidValue);
        return Ok(result);
    }
};
} // namespace vui
