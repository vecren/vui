export module vui:types;
import rstd;

export namespace vui {
using Scalar = double;
using Serial = rstd::uint64_t;
using Index = rstd::uint32_t;

struct FrameTiming {
    Serial now_ns{};
    rstd::Option<Serial> expected_present_ns;
};

enum class UiError {
    InvalidId,
    InvalidValue,
    Cycle,
    Root,
    MissingChild,
    Limit,
    Routing,
    Cancelled,
    LayoutPending,
    TextServiceMissing,
    TextLayoutFailed,
    LayoutFailed,
    PaintFailed,
    PropertyOwned,
    StaleBase
};

inline auto finite(Scalar value) -> bool { return __builtin_isfinite(value); }

struct Point {
    Scalar x{}, y{};
    friend auto operator==(const Point &, const Point &) -> bool = default;
};
struct Size {
    Scalar width{}, height{};
    auto valid() const -> bool {
        return finite(width) && finite(height) && width >= 0 && height >= 0;
    }
    friend auto operator==(const Size &, const Size &) -> bool = default;
};
struct Rect {
    Scalar x{}, y{}, width{}, height{};
    auto valid() const -> bool {
        return finite(x) && finite(y) && Size{width, height}.valid() && finite(x + width) &&
               finite(y + height);
    }
    auto contains(Point point) const -> bool {
        return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height;
    }
    friend auto operator==(const Rect &, const Rect &) -> bool = default;
};
struct Transform {
    Scalar a{1}, b{}, c{}, d{1}, tx{}, ty{};
    auto valid() const -> bool {
        return finite(a) && finite(b) && finite(c) && finite(d) && finite(tx) && finite(ty);
    }
    auto map(Point p) const -> Point { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    auto operator*(const Transform &rhs) const -> Transform {
        return {a * rhs.a + c * rhs.b, b * rhs.a + d * rhs.b,        a * rhs.c + c * rhs.d,
                b * rhs.c + d * rhs.d, a * rhs.tx + c * rhs.ty + tx, b * rhs.tx + d * rhs.ty + ty};
    }
    auto inverse() const -> rstd::Option<Transform> {
        Scalar det = a * d - b * c;
        if (!valid() || !finite(det) || det == 0)
            return rstd::None();
        Transform result{
            d / det, -b / det, -c / det, a / det, (c * ty - d * tx) / det, (b * tx - a * ty) / det};
        if (!result.valid())
            return rstd::None();
        return rstd::Some(result);
    }
    friend auto operator==(const Transform &, const Transform &) -> bool = default;
};
struct Color {
    float r{}, g{}, b{}, a{1};
    auto valid() const -> bool {
        return finite(r) && finite(g) && finite(b) && finite(a) && r >= 0 && g >= 0 && b >= 0 &&
               a >= 0 && r <= a && g <= a && b <= a && a <= 1;
    }
    static auto from_linear(float r, float g, float b, float a = 1)
        -> rstd::Result<Color, UiError> {
        if (!finite(r) || !finite(g) || !finite(b) || !finite(a) || r < 0 || g < 0 || b < 0 ||
            a < 0 || r > 1 || g > 1 || b > 1 || a > 1)
            return rstd::Err(UiError::InvalidValue);
        return rstd::Ok(Color{r * a, g * a, b * a, a});
    }
    static auto from_srgba(float r, float g, float b, float a = 1) -> rstd::Result<Color, UiError> {
        auto checked = from_linear(r, g, b, a);
        if (checked.is_err())
            return rstd::Err(checked.unwrap_err());
        auto linear = [](float c) {
            return c <= .04045f ? c / 12.92f : __builtin_powf((c + .055f) / 1.055f, 2.4f);
        };
        return from_linear(linear(r), linear(g), linear(b), a);
    }
    static auto from_srgba8(Index r, Index g, Index b, Index a = 255)
        -> rstd::Result<Color, UiError> {
        if (r > 255 || g > 255 || b > 255 || a > 255)
            return rstd::Err(UiError::InvalidValue);
        return from_srgba(r / 255.f, g / 255.f, b / 255.f, a / 255.f);
    }
    auto modulate(Color other) const -> Color { return {r * other.r, g * other.g, b * other.b, a * other.a}; }
    friend auto operator==(const Color &, const Color &) -> bool = default;
};
struct Clip {
    bool enabled{};
    Rect rect;
    Scalar radius{};
    auto valid() const -> bool { return rect.valid() && finite(radius) && radius >= 0; }
    auto contains(Point point) const -> bool {
        if (!enabled)
            return true;
        if (!rect.contains(point))
            return false;
        auto r = radius;
        if (r > rect.width / 2)
            r = rect.width / 2;
        if (r > rect.height / 2)
            r = rect.height / 2;
        auto cx = point.x < rect.x + r ? rect.x + r : point.x;
        if (cx > rect.x + rect.width - r)
            cx = rect.x + rect.width - r;
        auto cy = point.y < rect.y + r ? rect.y + r : point.y;
        if (cy > rect.y + rect.height - r)
            cy = rect.y + rect.height - r;
        auto dx = point.x - cx, dy = point.y - cy;
        return dx * dx + dy * dy <= r * r;
    }
    friend auto operator==(const Clip &, const Clip &) -> bool = default;
};

template <class Tag> struct Identity {
    Serial session{};
    Index slot{};
    Serial generation{};
    auto valid() const -> bool { return session != 0 && generation != 0; }
    friend auto operator==(const Identity &, const Identity &) -> bool = default;
};
struct UiTag;
struct RenderTag;
using UiId = Identity<UiTag>;
using RenderId = Identity<RenderTag>;
struct UiRelation {
    UiId parent, child;
    Serial generation{};
    friend auto operator==(const UiRelation &, const UiRelation &) -> bool = default;
};

inline auto new_session() -> Serial {
    static Serial next = 1;
    auto result = __atomic_fetch_add(&next, 1, __ATOMIC_RELAXED);
    if (!result)
        rstd::panic("vui session identity exhausted");
    return result;
}
} // namespace vui
