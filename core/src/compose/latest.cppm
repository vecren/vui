export module vui.compose:latest;
import :lifecycle;
import :inputs;
using namespace rstd::prelude;

export namespace vui::compose {
template <class T> struct LatestVersion {
    T value;
    explicit LatestVersion(T stored) : value(rstd::move(stored)) {}
};
template <class T> struct LatestCell {
    Serial identity{new_session()};
    Option<rstd::rc::Rc<LatestVersion<T>>> current;
};
template <class T> class LatestSnapshot {
    using Stored = typename InputTraits<T>::Stored;
    rstd::rc::Rc<LatestVersion<Stored>> version_;
  public:
    explicit LatestSnapshot(rstd::rc::Rc<LatestVersion<Stored>> value) : version_(rstd::move(value)) {}
    auto value() const -> decltype(auto) { return InputTraits<T>::replay(version_->value); }
};
template <class T> class Latest {
    using Stored = typename InputTraits<T>::Stored;
    Remembered<rstd::rc::Rc<LatestCell<Stored>>> cell_;
    Serial identity_;
  public:
    Latest(Remembered<rstd::rc::Rc<LatestCell<Stored>>> cell, Serial identity)
        : cell_(rstd::move(cell)), identity_(identity) {}
    auto get() const -> ComposeResult<LatestSnapshot<T>> {
        auto saved = cell_.get();
        if (saved.is_err()) return Err(saved.unwrap_err());
        const auto &cell = *saved.unwrap();
        if (cell->current.is_none()) return Err(ComposeError(ComposeStage::Closed));
        return Ok(LatestSnapshot<T>((*cell->current).clone()));
    }
    auto same_instance(const Latest &other) const -> bool { return identity_ == other.identity_; }
};
namespace compiler {
template <class T>
auto latest_value(CompositionContext &scope, Key key, T value) -> ComposeResult<Latest<T>> {
    using Stored = typename InputTraits<T>::Stored;
    using Cell = LatestCell<Stored>;
    auto saved = scope.template remember<rstd::rc::Rc<Cell>>(rstd::move(key), []() -> ComposeResult<rstd::rc::Rc<Cell>> {
        return Ok(rstd::rc::make_rc<Cell>());
    });
    if (saved.is_err()) return Err(saved.unwrap_err());
    auto memory = saved.unwrap();
    auto stored = memory.get();
    if (stored.is_err()) return Err(stored.unwrap_err());
    auto cell = stored.unwrap()->clone();
    auto version = rstd::rc::make_rc<LatestVersion<Stored>>(InputTraits<T>::capture(rstd::move(value)));
    scope.on_commit([cell, version = rstd::move(version)](CommittedContext &) mutable -> ComposeResult<empty> {
        cell.get()->current = Some(rstd::move(version));
        return Ok(empty{});
    });
    return Ok(Latest<T>(rstd::move(memory), cell->identity));
}
}
template <class T>
[[clang::annotate("vui.compose.hook.v2")]]
auto latest([[clang::annotate("vui.compose.deferred.v2")]] T value, CallSite site = {}) -> ComposeResult<Latest<T>> {
    auto opened = compiler::context(site);
    if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(compiler::latest_value(*scope, scope->slot(site.id), rstd::move(value)), site);
}
template <class T>
[[clang::annotate("vui.compose.hook.v2")]]
auto latest(Key key, [[clang::annotate("vui.compose.deferred.v2")]] T value, CallSite site = {}) -> ComposeResult<Latest<T>> {
    auto opened = compiler::context(site);
    if (opened.is_err()) return Err(opened.unwrap_err());
    auto *scope = opened.unwrap();
    return compiler::located(compiler::latest_value(*scope, scope->slot(rstd::move(key)), rstd::move(value)), site);
}
template <class T> struct InputTraits<Latest<T>> {
    using Stored = Latest<T>;
    static constexpr bool comparable = true;
    static auto capture(Latest<T> value) -> Stored { return value; }
    static auto replay(const Stored &value) -> const Stored & { return value; }
    static auto equal(const Stored &a, const Stored &b) -> bool { return a.same_instance(b); }
};
} // namespace vui::compose
