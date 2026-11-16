export module vui.compose:environment;
import :types;
import :inputs;
using namespace rstd::prelude;

export namespace vui::compose {
struct LocalValue {
    virtual ~LocalValue() = default;
    virtual auto type() const -> rstd::any::TypeId = 0;
    virtual auto equal(const LocalValue &) const -> bool = 0;
};
template <class T> struct LocalStored final : LocalValue {
    using Traits = InputTraits<T>;
    typename Traits::Stored value;
    explicit LocalStored(typename Traits::Stored source) : value(rstd::move(source)) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<T>(); }
    auto equal(const LocalValue &other) const -> bool override {
        if (other.type() != type()) return false;
        if constexpr (Traits::comparable) return Traits::equal(value, static_cast<const LocalStored &>(other).value);
        else return false;
    }
};
template <class T> class CompositionLocal;
class CompositionEnvironment;
template <class T> class LocalSnapshot {
    rstd::rc::Rc<LocalValue> value_;
    friend class CompositionEnvironment;
    friend class CompositionContext;
    explicit LocalSnapshot(rstd::rc::Rc<LocalValue> value) : value_(rstd::move(value)) {}
  public:
    auto value() const -> decltype(auto) {
        return InputTraits<T>::replay(static_cast<const LocalStored<T> &>(*value_).value);
    }
};
class LocalBinding {
    Serial identity_;
    rstd::rc::Rc<LocalValue> value_;
    template <class T> friend class CompositionLocal;
    friend class CompositionEnvironment;
    friend class CompositionContext;
    LocalBinding(Serial identity, rstd::rc::Rc<LocalValue> value)
        : identity_(identity), value_(rstd::move(value)) {}
  public:
    LocalBinding(const LocalBinding &) = default;
    LocalBinding(LocalBinding &&) = default;
    auto operator=(const LocalBinding &) -> LocalBinding & = default;
    auto operator=(LocalBinding &&) -> LocalBinding & = default;
    auto identity() const -> Serial { return identity_; }
    auto same_value(const LocalBinding &other) const -> bool;
};
template <class T> class CompositionLocal {
    Serial identity_{new_session()};
    rstd::rc::Rc<LocalValue> default_;
    friend class CompositionContext;
    friend class CompositionEnvironment;
    auto fallback() const -> LocalBinding { return LocalBinding(identity_, default_.clone()); }
    template <class A> static auto capture(A &&value) -> rstd::rc::Rc<LocalValue> {
        return rstd::rc::Rc<LocalValue>(new LocalStored<T>(InputTraits<T>::capture(rstd::forward<A>(value))));
    }
  public:
    template <class A> requires (!rstd::mtp::same_as<rstd::mtp::decay<A>, CompositionLocal>)
    explicit CompositionLocal(A &&value) : default_(capture(rstd::forward<A>(value))) {}
    CompositionLocal(const CompositionLocal &) = default;
    CompositionLocal(CompositionLocal &&) = default;
    auto operator=(const CompositionLocal &) -> CompositionLocal & = default;
    auto operator=(CompositionLocal &&) -> CompositionLocal & = default;
    template <class A> auto provides(A &&value) const -> LocalBinding {
        return LocalBinding(identity_, capture(rstd::forward<A>(value)));
    }
    auto same_instance(const CompositionLocal &other) const -> bool { return identity_ == other.identity_; }
};
struct LocalRead {
    LocalBinding fallback, value;
};
struct LocalEnvironmentData {
    Vec<LocalBinding> values;
};
class CompositionEnvironment {
    rstd::rc::Rc<LocalEnvironmentData> data_;
    friend class CompositionContext;
    auto resolve(const LocalBinding &) const -> LocalBinding;
  public:
    auto extended(slice<LocalBinding>) const -> ComposeResult<CompositionEnvironment>;
    auto changed(slice<LocalRead>) const -> bool;
};
template <class T> struct InputTraits<CompositionLocal<T>> {
    using Stored = CompositionLocal<T>;
    static constexpr bool comparable = true;
    static auto capture(CompositionLocal<T> value) -> Stored { return value; }
    static auto replay(const Stored &value) -> const Stored & { return value; }
    static auto equal(const Stored &a, const Stored &b) -> bool { return a.same_instance(b); }
};
}
