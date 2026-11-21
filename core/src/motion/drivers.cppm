export module vui:motion.drivers;
import :document.interface;
using namespace rstd::prelude;

export namespace vui {
enum class MotionError { InvalidValue, Unavailable, Reentrant, Limit };
enum class MotionStatus { Running, Idle, Finished };
enum class MotionEndReason { Finished, Cancelled };
struct MotionId {
    Serial owner{}, value{};
    friend auto operator==(const MotionId &, const MotionId &) -> bool = default;
};
struct MotionEvent {
    MotionId id;
    MotionEndReason reason;
    Serial time_ns{};
};
class MotionContext {
    UiDocument &document_;
    Serial now_;
    Option<Serial> next_;
    BindingGuard driver_;

  public:
    MotionContext(UiDocument &document, Serial now, BindingGuard driver)
        : document_(document), now_(now), driver_(driver) {}
    auto now_ns() const -> Serial { return now_; }
    auto write(PresentationLease &lease, const PresentationBatch &batch)
        -> Result<empty, MotionError> {
        if (!driver_.alive())
            return Err(MotionError::Unavailable);
        auto result = document_.write_presentation(lease, batch, Some(BindingGuard(driver_)));
        return result.is_ok()
                   ? Result<empty, MotionError>(Ok(empty{}))
                   : Result<empty, MotionError>(Err(result.unwrap_err() == UiError::InvalidValue
                                                        ? MotionError::InvalidValue
                                                        : MotionError::Unavailable));
    }
    auto request_tick(Serial deadline) -> Result<empty, MotionError> {
        if (deadline < now_)
            return Err(MotionError::InvalidValue);
        if (next_.is_none() || deadline < *next_)
            next_ = Some(deadline);
        return Ok(empty{});
    }
    auto next_tick() const -> Option<Serial> { return next_; }
};
struct MotionDriver {
    template <class Self, class Delegate = void> struct Api {
        using Trait = MotionDriver;
        auto sample(MotionContext &context, FrameTiming timing)
            -> Result<MotionStatus, MotionError> {
            return rstd::trait_call<0>(this, context, timing);
        }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::sample>;
};
struct MotionRegistration {
    MotionId id;
    Binding binding;
};
class MotionRuntime {
    struct Entry {
        MotionId id;
        BindingGuard guard;
        Box<rstd::dyn<MotionDriver>> driver;
        MotionStatus status{MotionStatus::Running};
        Option<Serial> sampled, deadline;
    };
    UiDocument &document_;
    Serial owner_{new_session()}, next_{1};
    Vec<Entry> entries_;
    Vec<MotionEvent> events_;
    bool busy_{}, closed_{};
    static auto eligible(const Entry &entry, Serial time) -> bool {
        return entry.guard.alive() && entry.sampled != Some(time) &&
               (entry.deadline.is_none() || time >= *entry.deadline);
    }

  public:
    explicit MotionRuntime(UiDocument &document) : document_(document) {}
    template <class T>
        requires rstd::Impled<T, MotionDriver>
    auto bind(rstd::rc::Weak<T> receiver) -> Result<MotionRegistration, MotionError> {
        if (closed_)
            return Err(MotionError::Unavailable);
        if (busy_)
            return Err(MotionError::Reentrant);
        if (entries_.len() + events_.len() >= usize(4096) || next_ == ~Serial{})
            return Err(MotionError::Limit);
        auto binding = document_.bindings().make(receiver);
        struct Adapter {
            rstd::rc::Weak<T> receiver;
            BindingGuard guard;
            auto sample(MotionContext &context, FrameTiming timing)
                -> Result<MotionStatus, MotionError> {
                auto value = receiver.upgrade();
                if (!guard.alive() || value.is_none())
                    return Err(MotionError::Unavailable);
                return rstd::as<MotionDriver>(*value->get()).sample(context, timing);
            }
        };
        MotionId id{owner_, next_++};
        entries_.push(
            {id, binding.guard(),
             Box<rstd::dyn<MotionDriver>>::make(Adapter{rstd::move(receiver), binding.guard()})});
        document_.bindings().wake();
        return Ok(MotionRegistration{id, rstd::move(binding)});
    }
    auto request(MotionId id) -> Result<empty, MotionError> {
        if (busy_)
            return Err(MotionError::Reentrant);
        for (auto &entry : entries_)
            if (entry.id == id && entry.guard.alive()) {
                entry.sampled = None();
                entry.deadline = None();
                entry.status = MotionStatus::Running;
                document_.bindings().wake();
                return Ok(empty{});
            }
        return Err(MotionError::Unavailable);
    }
    auto cancel(MotionId id) -> Result<empty, MotionError> {
        for (auto &entry : entries_)
            if (entry.id == id) {
                entry.guard.disconnect();
                return Ok(empty{});
            }
        return Err(MotionError::Unavailable);
    }
    auto sample(FrameTiming timing) -> Result<empty, MotionError> {
        if (busy_ || !document_.can_route())
            return Err(MotionError::Reentrant);
        if (closed_)
            return Ok(empty{});
        auto scope = document_.acquire_route();
        if (scope.is_err())
            return Err(MotionError::Reentrant);
        busy_ = true;
        struct Guard {
            bool &value;
            ~Guard() { value = false; }
        } guard{busy_};
        auto time = timing.sample_time_ns();
        Option<MotionError> error;
        for (auto &entry : entries_) {
            if (!entry.guard.alive()) {
                events_.push({entry.id, MotionEndReason::Cancelled, time});
                continue;
            }
            if (!eligible(entry, time))
                continue;
            MotionContext context(document_, time, entry.guard);
            auto result = entry.driver->sample(context, timing);
            if (!entry.guard.alive()) {
                events_.push({entry.id, MotionEndReason::Cancelled, time});
                continue;
            }
            if (result.is_err()) {
                auto failure = result.unwrap_err();
                if (failure == MotionError::Unavailable) {
                    events_.push({entry.id, MotionEndReason::Cancelled, time});
                    entry.guard.disconnect();
                    continue;
                }
                error = Some(failure);
                break;
            }
            entry.sampled = Some(time);
            entry.status = result.unwrap();
            entry.deadline = context.next_tick();
            if (entry.status == MotionStatus::Finished) {
                events_.push({entry.id, MotionEndReason::Finished, time});
                entry.guard.disconnect();
            }
        }
        entries_.retain([](const Entry &entry) { return entry.guard.alive(); });
        if (error.is_some())
            return Err(*error);
        return Ok(empty{});
    }
    auto pending(Serial time) const -> bool {
        for (const auto &entry : entries_)
            if (eligible(entry, time))
                return true;
        return false;
    }
    auto active() const -> bool {
        for (const auto &entry : entries_)
            if (entry.guard.alive() && entry.status == MotionStatus::Running)
                return true;
        return false;
    }
    auto next_tick() const -> Option<Serial> {
        Option<Serial> next;
        for (const auto &entry : entries_)
            if (entry.guard.alive() && entry.status == MotionStatus::Running &&
                entry.deadline.is_some() && (next.is_none() || *entry.deadline < *next))
                next = entry.deadline;
        return next;
    }
    auto poll_events() -> Vec<MotionEvent> { return rstd::exchange(events_, Vec<MotionEvent>{}); }
    auto close() -> Result<empty, MotionError> {
        if (busy_)
            return Err(MotionError::Reentrant);
        closed_ = true;
        for (auto &entry : entries_) {
            entry.guard.disconnect();
            events_.push({entry.id, MotionEndReason::Cancelled,
                          entry.sampled.is_some() ? *entry.sampled : 0});
        }
        entries_.clear();
        return Ok(empty{});
    }
};
} // namespace vui
