export module vui:types;
import rstd;

export namespace vui {
using Scalar = double;
using Serial = rstd::uint64_t;
using Index = rstd::uint32_t;

struct FrameTiming {
    Serial now_ns{};
    rstd::Option<Serial> expected_present_ns;
    rstd::Option<Serial> sample_ns;

    auto sample_time_ns() const -> Serial { return sample_ns.is_some() ? *sample_ns : now_ns; }
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
enum class ShapeKind { Rectangle, RoundedRectangle, Ellipse };

struct CornerRadii {
    Scalar top_left{}, top_right{}, bottom_right{}, bottom_left{};
    auto valid() const -> bool {
        return finite(top_left) && finite(top_right) && finite(bottom_right) && finite(bottom_left) &&
               top_left >= 0 && top_right >= 0 && bottom_right >= 0 && bottom_left >= 0;
    }
    friend auto operator==(const CornerRadii &, const CornerRadii &) -> bool = default;
};

struct Shape {
    Rect bounds;
    CornerRadii radii;
    ShapeKind kind{ShapeKind::Rectangle};

    static constexpr auto rectangle(Rect bounds) -> Shape { return {bounds, {}, ShapeKind::Rectangle}; }
    static constexpr auto rounded_rect(Rect bounds, CornerRadii radii) -> Shape {
        return {bounds, radii, ShapeKind::RoundedRectangle};
    }
    static constexpr auto rounded_rect(Rect bounds, Scalar radius) -> Shape {
        return rounded_rect(bounds, CornerRadii{radius, radius, radius, radius});
    }
    static constexpr auto ellipse(Rect bounds) -> Shape { return {bounds, {}, ShapeKind::Ellipse}; }
    static constexpr auto circle(Point center, Scalar radius) -> Shape {
        return ellipse({center.x - radius, center.y - radius, 2 * radius, 2 * radius});
    }
    static constexpr auto capsule(Rect bounds) -> Shape {
        return rounded_rect(bounds, rstd::cmp::min(bounds.width, bounds.height) / 2);
    }
    auto empty() const -> bool { return bounds.width <= 0 || bounds.height <= 0; }
    auto valid() const -> bool;
    auto normalized() const -> Shape;
    auto inset(Scalar amount) const -> Shape;
    auto contains(Point point) const -> bool;
    friend auto operator==(const Shape &, const Shape &) -> bool = default;
};

struct Clip {
    bool enabled{};
    Shape shape;
    constexpr Clip() = default;
    constexpr Clip(bool enabled, Shape shape) : enabled(enabled), shape(shape) {}
    constexpr Clip(bool enabled, Rect bounds, Scalar radius = 0)
        : Clip(enabled, Shape::rounded_rect(bounds, radius)) {}
    auto bounds() const -> Rect { return shape.bounds; }
    auto valid() const -> bool { return shape.valid(); }
    auto contains(Point point) const -> bool { return !enabled || shape.contains(point); }
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
