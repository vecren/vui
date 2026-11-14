export module vui.compose:inputs;
import :types;
using namespace rstd::prelude;

export namespace vui::compose {
template <class T> struct StableValue {
    T value;
    friend auto operator==(const StableValue &, const StableValue &) -> bool = default;
};
template <class T> class Borrowed {
    T *value_;
  public:
    explicit Borrowed(T &value) : value_(&value) {}
    auto get() const -> T & { return *value_; }
    auto operator->() const -> T * { return value_; }
};
template <class T> auto borrow(T &value) -> Borrowed<T> { return Borrowed<T>(value); }

namespace [[clang::annotate("vui.compose.runtime.v1")]] compiler {
template <class T> inline constexpr bool input_borrow = rstd::mtp::is_ptr<T>;
template <class T> inline constexpr bool input_borrow<rstd::ref<T>> = true;
template <class T> inline constexpr bool input_borrow<rstd::mut_ref<T>> = true;
template <class T> inline constexpr bool input_borrow<rstd::ptr<T>> = true;
template <class T> inline constexpr bool input_borrow<rstd::mut_ptr<T>> = true;
template <class T> inline constexpr bool input_stable = rstd::mtp::is_arithmetic<T> || rstd::mtp::is_enum<T>;
template <> inline constexpr bool input_stable<Point> = true;
template <> inline constexpr bool input_stable<Size> = true;
template <> inline constexpr bool input_stable<Rect> = true;
template <> inline constexpr bool input_stable<Transform> = true;
template <> inline constexpr bool input_stable<Color> = true;
template <class T> inline constexpr bool input_stable<StableValue<T>> = requires(const T &a, const T &b) {
    { a == b } -> rstd::mtp::convertible_to<bool>;
};
template <class T, class A> auto retain(A &&value) -> T {
    if constexpr (rstd::mtp::init<T, A &&>) return T(rstd::forward<A>(value));
    else if constexpr (requires { { value.clone() } -> rstd::mtp::same_as<T>; }) return value.clone();
    else {
        static_assert(rstd::mtp::init<T, A &&>, "compose input needs owned storage; specialize InputTraits for this value");
    }
}
}

template <class P> struct InputTraits {
    using Stored = rstd::mtp::rm_cvf<P>;
    static constexpr bool comparable = compiler::input_stable<Stored>;
    template <class A> static auto capture(A &&value) -> Stored {
        static_assert(!compiler::input_borrow<Stored>, "compose borrowed inputs require an owning InputTraits specialization or borrow(value)");
        return compiler::retain<Stored>(rstd::forward<A>(value));
    }
    static auto replay(const Stored &value) -> const Stored & { return value; }
    static auto equal(const Stored &a, const Stored &b) -> bool {
        if constexpr (comparable) return a == b;
        else return false;
    }
};
template <class T> struct InputTraits<const T &> : InputTraits<T> {};

template <> struct InputTraits<String> {
    using Stored = String;
    static constexpr bool comparable = true;
    template <class A> static auto capture(A &&value) -> Stored { return compiler::retain<String>(rstd::forward<A>(value)); }
    static auto replay(const Stored &value) -> String { return value.clone(); }
    static auto equal(const Stored &a, const Stored &b) -> bool { return a.as_str() == b.as_str(); }
};
template <> struct InputTraits<const String &> : InputTraits<String> {
    static auto replay(const Stored &value) -> const String & { return value; }
};
template <> struct InputTraits<ref<str>> {
    using Stored = String;
    static constexpr bool comparable = true;
    static auto capture(ref<str> value) -> Stored { return String::make(value); }
    static auto replay(const Stored &value) -> ref<str> { return value.as_str(); }
    static auto equal(const Stored &a, const Stored &b) -> bool { return a.as_str() == b.as_str(); }
};
template <class T> struct InputTraits<rstd::ref<T[]>> {
    using Stored = Vec<T>;
    static constexpr bool comparable = compiler::input_stable<T>;
    static auto capture(slice<T> value) -> Stored { return Vec<T>::from(value); }
    static auto replay(const Stored &value) -> slice<T> { return value.as_slice(); }
    static auto equal(const Stored &a, const Stored &b) -> bool {
        if constexpr (comparable) return a.as_slice() == b.as_slice();
        else return false;
    }
};
}
