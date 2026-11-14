export module vui:binding.scope;
import :runtime.wakeup;
using namespace rstd::prelude;

export namespace vui {
struct BindingOwner {
    bool open{true};
    Option<FrameWaker> waker;
};
struct BindingState {
    bool active{true};
    rstd::rc::Weak<BindingOwner> owner;
    Box<rstd::dyn<rstd::Fn<bool()>>> available;
    BindingState(rstd::rc::Weak<BindingOwner> source, Box<rstd::dyn<rstd::Fn<bool()>>> receiver)
        : owner(rstd::move(source)), available(rstd::move(receiver)) {}
    auto alive() const -> bool {
        auto source = owner.upgrade();
        return active && source.is_some() && source->get()->open && available->operator()();
    }
    void disconnect() {
        if (!active)
            return;
        active = false;
        auto source = owner.upgrade();
        if (source.is_some() && source->get()->open && source->get()->waker.is_some())
            source->get()->waker->request();
    }
};
class BindingGuard {
    rstd::rc::Weak<BindingState> state_;

  public:
    BindingGuard() = default;
    explicit BindingGuard(rstd::rc::Weak<BindingState> state) : state_(rstd::move(state)) {}
    auto alive() const -> bool {
        auto state = state_.upgrade();
        return state.is_some() && state->get()->alive();
    }
    void disconnect() const {
        auto state = state_.upgrade();
        if (state.is_some())
            state->get()->disconnect();
    }
    auto same_binding(const BindingGuard &other) const -> bool {
        auto state = state_.upgrade(), previous = other.state_.upgrade();
        return state.is_some() && previous.is_some() && state->get() == previous->get();
    }
};
class Binding {
    rstd::rc::Rc<BindingState> state_;

  public:
    Binding() = default;
    explicit Binding(rstd::rc::Rc<BindingState> state) : state_(rstd::move(state)) {}
    Binding(const Binding &) = delete;
    auto operator=(const Binding &) -> Binding & = delete;
    Binding(Binding &&) = default;
    auto operator=(Binding &&other) -> Binding & {
        if (this != &other) {
            disconnect();
            state_ = rstd::move(other.state_);
        }
        return *this;
    }
    ~Binding() { disconnect(); }
    auto guard() const -> BindingGuard { return BindingGuard(state_.downgrade()); }
    auto alive() const -> bool { return state_ && state_.get()->alive(); }
    void disconnect() {
        if (state_)
            state_.get()->disconnect();
    }
};
class BindingRegistry {
    rstd::rc::Rc<BindingOwner> owner_{rstd::rc::make_rc<BindingOwner>()};

  public:
    BindingRegistry() = default;
    BindingRegistry(const BindingRegistry &) = delete;
    ~BindingRegistry() { close(); }
    template <class F> auto make(F available) -> Binding {
        if (!owner_.get()->open)
            return {};
        return Binding(rstd::rc::make_rc<BindingState>(
            owner_.downgrade(), Box<rstd::dyn<rstd::Fn<bool()>>>::make(rstd::move(available))));
    }
    template <class T> auto make(rstd::rc::Weak<T> receiver) -> Binding {
        return make([receiver = rstd::move(receiver)] { return !receiver.expired(); });
    }
    auto make() -> Binding {
        return make([] { return true; });
    }
    void set_waker(Option<FrameWaker> waker) { owner_.get()->waker = rstd::move(waker); }
    auto wake() -> FrameWakeResult {
        return owner_.get()->open && owner_.get()->waker.is_some() ? owner_.get()->waker->request()
                                                                   : FrameWakeResult::Closed;
    }
    void close() {
        owner_.get()->open = false;
        owner_.get()->waker = None();
    }
};
} // namespace vui
