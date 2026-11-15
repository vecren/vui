export module vui.compose:types;
export import vui;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui::compose {
class Key {
    String name_;
    Serial number_{};
    bool named_{};
  public:
    explicit Key(Serial number) : number_(number) {}
    explicit Key(ref<str> name) : name_(String::make(name)), named_(true) {}
    Key(const Key &other) : name_(String::make(other.name_.as_str())), number_(other.number_), named_(other.named_) {}
    Key(Key &&) = default;
    auto operator=(Key other) -> Key & {
        name_ = rstd::move(other.name_); number_ = other.number_; named_ = other.named_; return *this;
    }
    friend auto operator==(const Key &a, const Key &b) -> bool {
        return a.named_ == b.named_ && (a.named_ ? a.name_.as_str() == b.name_.as_str() : a.number_ == b.number_);
    }
    auto label() const -> String {
        return named_ ? String::make(name_.as_str()) : rstd::format("{}", number_);
    }
    auto encoded() const -> String {
        return named_ ? rstd::format("s{}:{}", name_.len(), name_.as_str()) : rstd::format("n{}", number_);
    }
};
struct CallSite {
    Serial id{};
    rstd::source_location source;
};
struct CallIdentity {
    Serial site{}, occurrence{};
    Vec<Key> groups;
    explicit CallIdentity(Serial value = 0, Serial index = 0) : site(value), occurrence(index) {}
    CallIdentity(const CallIdentity &other) : site(other.site), occurrence(other.occurrence) {
        for (const auto &key : other.groups) groups.push(Key(key));
    }
    CallIdentity(CallIdentity &&) = default;
    auto operator=(CallIdentity other) -> CallIdentity & {
        site = other.site; occurrence = other.occurrence; groups = rstd::move(other.groups); return *this;
    }
    auto same_group(const CallIdentity &other) const -> bool;
    auto memory_key(Option<Key> explicit_key = {}) const -> Key;
};
enum class ComposeStage { Closed, Key, Type, Evaluation, State, Prepare, Publish, Input, Action, Effect, Limit, External };
auto compose_operation_name(ComposeStage stage) -> ref<str>;
struct [[clang::annotate("vui.compose.error.v1")]] ComposeError {
    ComposeStage stage{ComposeStage::Evaluation};
    Option<UiError> ui;
    Option<state::StateError> state;
    Option<InputError> input;
    Option<UiId> node;
    String path, operation;
    Serial revision{}, attempt{};
    bool committed{};
    rstd::source_location source;
    explicit ComposeError(ComposeStage value = ComposeStage::Evaluation, Option<UiError> error = {}) : stage(value), ui(error) {}
    ComposeError(const ComposeError &other)
        : stage(other.stage), ui(other.ui), state(other.state), input(other.input), node(other.node),
          path(String::make(other.path.as_str())), operation(String::make(other.operation.as_str())),
          revision(other.revision), attempt(other.attempt), committed(other.committed), source(other.source) {}
    ComposeError(ComposeError &&) = default;
    auto operator=(ComposeError other) -> ComposeError &;
};
template <class T> using ComposeResult = Result<T, ComposeError>;
inline auto state_error(state::StateError error) -> ComposeError {
    ComposeError result(ComposeStage::State); result.state = Some(error); return result;
}
struct CommitReport {
    Serial revision{}, attempt{}, evaluations{};
    usize created{}, updated{}, destroyed{};
    Serial comparisons{}, skipped{};
};
struct ComposeDiagnostics {
    Vec<ComposeError> entries;
    Serial omitted{};
};
class EffectKeys {
    Vec<Key> values_;
  public:
    EffectKeys() = default;
    EffectKeys(initializer_list<Key> values) { for (const auto &value : values) values_.push(Key(value)); }
    EffectKeys(const EffectKeys &other) { for (const auto &value : other.values_) values_.push(Key(value)); }
    EffectKeys(EffectKeys &&) = default;
    auto operator=(EffectKeys other) -> EffectKeys & { values_ = rstd::move(other.values_); return *this; }
    friend auto operator==(const EffectKeys &a, const EffectKeys &b) -> bool {
        if (a.values_.len() != b.values_.len()) return false;
        for (usize i{}; i < a.values_.len(); ++i) if (a.values_[i] != b.values_[i]) return false;
        return true;
    }
};
class EffectLease {
    Option<Box<rstd::dyn<rstd::FnMut<void()>>>> cleanup_;
  public:
    EffectLease() = default;
    template <class F> explicit EffectLease(F cleanup)
        : cleanup_(Some(Box<rstd::dyn<rstd::FnMut<void()>>>::make(rstd::move(cleanup)))) {}
    EffectLease(const EffectLease &) = delete;
    EffectLease(EffectLease &&other)
        : cleanup_(rstd::exchange(other.cleanup_, None<Box<rstd::dyn<rstd::FnMut<void()>>>>())) {}
    auto operator=(EffectLease &&other) -> EffectLease & {
        if (this != &other) { close(); cleanup_ = rstd::exchange(other.cleanup_, None<Box<rstd::dyn<rstd::FnMut<void()>>>>()); } return *this;
    }
    ~EffectLease() { close(); }
    void close() {
        auto callback = rstd::exchange(cleanup_, None<Box<rstd::dyn<rstd::FnMut<void()>>>>());
        if (callback.is_some()) (**callback)();
    }
};
struct NodeLink {
    UiId id;
    bool active{}, alive{true};
};
class NodeRef {
    rstd::rc::Rc<NodeLink> link_;
  public:
    NodeRef() : link_(rstd::rc::make_rc<NodeLink>()) {}
    explicit NodeRef(rstd::rc::Rc<NodeLink> link) : link_(rstd::move(link)) {}
    auto id() const -> ComposeResult<UiId> {
        if (!link_->alive || !link_->active) return Err(ComposeError(ComposeStage::Closed));
        return Ok(link_->id);
    }
    auto link() const -> rstd::rc::Rc<NodeLink> { return link_.clone(); }
};
struct ComposeSignal {
    UiDocument *document{};
    BindingGuard lifetime;
    bool pending{true}, closed{};
    Option<ComposeError> error;
    Serial attempt{};
    rstd::source_location source;
    Serial committed_revision{};
    bool published{};
    ComposeDiagnostics diagnostics;
    void invalidate();
    void report(ComposeError value, Option<Serial> committed = {});
};
class CommittedContext {
    UiSession &session_;
  public:
    explicit CommittedContext(UiSession &session) : session_(session) {}
    CommittedContext(const CommittedContext &) = delete;
    auto session() -> UiSession & { return session_; }
    auto resolve(const NodeRef &node) -> ComposeResult<UiId>;
};
class CompositionContext;
class PrepareContext;
class Element;
} // namespace vui::compose
