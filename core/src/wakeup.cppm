export module vui:wakeup;
import :types;
using namespace rstd::prelude;

export namespace vui {
enum class FrameWakeResult { Queued, Coalesced, Closed, Failed };
class FrameWaker {
    struct Signal {
        virtual ~Signal() = default;
        virtual auto request() const -> FrameWakeResult = 0;
    };
    template <class F> struct Callback : Signal {
        F callback;
        explicit Callback(F value) : callback(rstd::move(value)) {}
        auto request() const -> FrameWakeResult override { return callback(); }
    };
    struct State {
        Box<Signal> signal;
        explicit State(Box<Signal> value) : signal(rstd::move(value)) {}
    };
    rstd::sync::Arc<State> state_;
    explicit FrameWaker(Box<Signal> signal)
        : state_(rstd::sync::Arc<State>::make(rstd::move(signal))) {}

  public:
    FrameWaker(const FrameWaker &other) : state_(other.state_.clone()) {}
    auto operator=(const FrameWaker &other) -> FrameWaker & {
        if (this != &other)
            state_ = other.state_.clone();
        return *this;
    }
    FrameWaker(FrameWaker &&) = default;
    auto operator=(FrameWaker &&) -> FrameWaker & = default;
    // Callbacks only enqueue work. Cross-thread use requires a thread-safe callback and captures.
    template <class F> static auto make(F callback) -> FrameWaker {
        auto value = Box<Callback<F>>::make(rstd::move(callback));
        return FrameWaker(
            Box<Signal>::from_raw(rstd::move(value).into_raw().template cast<Signal>()));
    }
    auto valid() const -> bool { return bool(state_); }
    auto request() const -> FrameWakeResult {
        return state_ ? state_->signal->request() : FrameWakeResult::Closed;
    }
};
} // namespace vui
