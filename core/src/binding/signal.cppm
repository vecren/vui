export module vui:signal;
import :binding;
using namespace rstd::prelude;

export namespace vui {
enum class SignalError { Reentrant, Limit, Closed };
template <class Signature> class Signal;
template <class... Args> class Signal<void(Args...)> {
    struct Entry {
        Serial id;
        BindingGuard guard;
        Box<rstd::dyn<rstd::FnMut<void(Args...)>>> callback;
    };
    Vec<Entry> entries_;
    Serial next_{1};
    bool busy_{}, closed_{};

  public:
    Signal() = default;
    Signal(const Signal &) = delete;
    Signal(Signal &&) = default;
    ~Signal() { close(); }
    template <class F>
    auto connect(BindingRegistry &registry, F callback) -> Result<Binding, SignalError> {
        if (closed_)
            return Err(SignalError::Closed);
        if (!busy_)
            entries_.retain([](const Entry &entry) { return entry.guard.alive(); });
        if (entries_.len() >= usize(4096) || next_ == ~Serial{})
            return Err(SignalError::Limit);
        auto binding = registry.make();
        struct Callback {
            F function;
            void operator()(Args... args) { function(rstd::forward<Args>(args)...); }
        };
        entries_.push(
            {next_++, binding.guard(),
             Box<rstd::dyn<rstd::FnMut<void(Args...)>>>::make(Callback{rstd::move(callback)})});
        return Ok(rstd::move(binding));
    }
    template <class T, class F>
    auto bind(BindingRegistry &registry, rstd::rc::Weak<T> receiver, F callback)
        -> Result<Binding, SignalError> {
        if (closed_)
            return Err(SignalError::Closed);
        if (!busy_)
            entries_.retain([](const Entry &entry) { return entry.guard.alive(); });
        if (entries_.len() >= usize(4096) || next_ == ~Serial{})
            return Err(SignalError::Limit);
        auto binding = registry.make(receiver);
        auto guard = binding.guard();
        entries_.push({next_++, guard,
                       Box<rstd::dyn<rstd::FnMut<void(Args...)>>>::make(
                           [guard, receiver = rstd::move(receiver),
                            callback = rstd::move(callback)](Args... args) mutable {
                               auto value = receiver.upgrade();
                               if (guard.alive() && value.is_some())
                                   callback(*value->get(), rstd::forward<Args>(args)...);
                           })});
        return Ok(rstd::move(binding));
    }
    auto emit(Args... args) -> Result<empty, SignalError> {
        if (closed_)
            return Err(SignalError::Closed);
        if (busy_)
            return Err(SignalError::Reentrant);
        busy_ = true;
        auto cutoff = next_;
        for (usize i{}; i < entries_.len(); ++i) {
            auto &entry = entries_[i];
            if (entry.id < cutoff && entry.guard.alive())
                entry.callback->operator()(args...);
        }
        busy_ = false;
        entries_.retain([](const Entry &entry) { return entry.guard.alive(); });
        return Ok(empty{});
    }
    void close() {
        closed_ = true;
        for (auto &entry : entries_)
            entry.guard.disconnect();
        if (!busy_)
            entries_.clear();
    }
};
} // namespace vui
