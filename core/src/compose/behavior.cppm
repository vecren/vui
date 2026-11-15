export module vui.compose:behavior;
import :types;
using namespace rstd::prelude;

export namespace vui::compose {
class BehaviorContext {
    UiSession &session_;
    UiId target_;
    NodeStateKey role_;
  public:
    BehaviorContext(UiSession &session, UiId target, NodeStateKey role)
        : session_(session), target_(target), role_(role) {}
    auto session() -> UiSession & { return session_; }
    auto document() -> UiDocument & { return session_.document(); }
    auto target() const -> UiId { return target_; }
    auto role() const -> NodeStateKey { return role_; }
};
struct AttachedBehaviorState {
    virtual ~AttachedBehaviorState() = default;
};
template <class T> struct AttachedBehaviorValue final : AttachedBehaviorState {
    rstd::rc::Rc<T> receiver;
    explicit AttachedBehaviorValue(T value) : receiver(rstd::rc::make_rc<T>(rstd::move(value))) {}
};
struct AttachedBehaviorConfig {
    virtual ~AttachedBehaviorConfig() = default;
    virtual auto type() const -> rstd::any::TypeId = 0;
    virtual auto create() const -> ComposeResult<rstd::rc::Rc<AttachedBehaviorState>> = 0;
    virtual auto validate(const AttachedBehaviorState &) const -> ComposeResult<empty> = 0;
    virtual auto update(AttachedBehaviorState &, BehaviorContext &) const -> ComposeResult<empty> = 0;
    virtual auto attach(AttachedBehaviorState &, BehaviorContext &) const -> ComposeResult<EffectLease> = 0;
    virtual auto input() const -> bool = 0;
    virtual auto prepare_input(EventRouter &, EventRouter::BehaviorEdit &, const UiDocument::PreparedEdit &,
                               UiNodeRef, NodeStateKey, AttachedBehaviorState &) const -> Result<Binding, InputError> = 0;
};
template <class T> struct AttachedBehaviorModel final : AttachedBehaviorConfig {
    using State = typename T::State;
    T value;
    explicit AttachedBehaviorModel(T config) : value(rstd::move(config)) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<T>(); }
    auto create() const -> ComposeResult<rstd::rc::Rc<AttachedBehaviorState>> override {
        auto created = [&]() -> ComposeResult<State> {
            if constexpr (rstd::mtp::same_as<decltype(value.create()), State>) return Ok(value.create());
            else return value.create();
        }();
        if (created.is_err()) return Err(created.unwrap_err());
        return Ok(rstd::rc::Rc<AttachedBehaviorState>(new AttachedBehaviorValue<State>(created.unwrap())));
    }
    auto validate(const AttachedBehaviorState &state) const -> ComposeResult<empty> override {
        const auto &receiver = *static_cast<const AttachedBehaviorValue<State> &>(state).receiver;
        if constexpr (requires { value.validate(receiver); }) return value.validate(receiver);
        else return Ok(empty{});
    }
    auto update(AttachedBehaviorState &state, BehaviorContext &context) const -> ComposeResult<empty> override {
        auto &receiver = *static_cast<AttachedBehaviorValue<State> &>(state).receiver.get();
        if constexpr (requires { value.update(receiver, context); }) {
            if constexpr (rstd::mtp::same_as<decltype(value.update(receiver, context)), void>) {
                value.update(receiver, context); return Ok(empty{});
            } else return value.update(receiver, context);
        } else return Ok(empty{});
    }
    auto attach(AttachedBehaviorState &state, BehaviorContext &context) const -> ComposeResult<EffectLease> override {
        auto &receiver = static_cast<AttachedBehaviorValue<State> &>(state).receiver;
        if constexpr (requires { value.attach(*receiver.get(), context, receiver.downgrade()); }) {
            if constexpr (rstd::mtp::same_as<decltype(value.attach(*receiver.get(), context, receiver.downgrade())), EffectLease>)
                return Ok(value.attach(*receiver.get(), context, receiver.downgrade()));
            else return value.attach(*receiver.get(), context, receiver.downgrade());
        } else return Ok(EffectLease{});
    }
    auto input() const -> bool override { return rstd::Impled<State, vui::Behavior>; }
    auto prepare_input(EventRouter &input, EventRouter::BehaviorEdit &edit, const UiDocument::PreparedEdit &document,
                       UiNodeRef target, NodeStateKey role, AttachedBehaviorState &state) const -> Result<Binding, InputError> override {
        if constexpr (rstd::Impled<State, vui::Behavior>)
            return input.prepare_bind_behavior(edit, document, target,
                static_cast<AttachedBehaviorValue<State> &>(state).receiver.downgrade(), InputDelivery::EverySample, role);
        else return Err(InputError::InvalidSequence);
    }
};
struct BehaviorDeclaration {
    Key key;
    rstd::rc::Rc<AttachedBehaviorConfig> config;
    template <class T> BehaviorDeclaration(Key name, T value)
        : key(rstd::move(name)), config(rstd::rc::Rc<AttachedBehaviorConfig>(new AttachedBehaviorModel<T>(rstd::move(value)))) {}
    BehaviorDeclaration(const BehaviorDeclaration &other) : key(other.key), config(other.config) {}
    BehaviorDeclaration(BehaviorDeclaration &&) = default;
    auto operator=(BehaviorDeclaration other) -> BehaviorDeclaration & {
        key = rstd::move(other.key); config = rstd::move(other.config); return *this;
    }
};
} // namespace vui::compose
