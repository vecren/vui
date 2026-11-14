export module vui:update.protocol;
import :document;
using namespace rstd::prelude;

export namespace vui {
class UiSession;
struct UiUpdateParticipant {
    template <class Self, class Delegate = void> struct Api {
        using Trait = UiUpdateParticipant;
        auto pending() const -> bool { return rstd::trait_call<0>(this); }
        auto update(UiSession &session) -> Result<empty, UiError> { return rstd::trait_call<1>(this, session); }
        void close(UiSession &session) { rstd::trait_call<2>(this, session); }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::pending, &T::update, &T::close>;
};
class UiUpdates {
    struct Entry {
        BindingGuard guard;
        Box<rstd::dyn<UiUpdateParticipant>> participant;
        bool closed{};
    };
    UiDocument &document_;
    Vec<Entry> entries_;
    Vec<Entry> incoming_;
    bool busy_{}, closed_{};

  public:
    explicit UiUpdates(UiDocument &document) : document_(document) {}
    template <class T> requires rstd::Impled<T, UiUpdateParticipant>
    auto bind(rstd::rc::Rc<T> receiver) -> Result<Binding, UiError> {
        if (closed_) return Err(UiError::Cancelled);
        if (document_.routing()) return Err(UiError::Routing);
        if (entries_.len() + incoming_.len() >= usize(4096)) return Err(UiError::Limit);
        struct Adapter {
            rstd::rc::Rc<T> receiver;
            auto pending() const -> bool { return rstd::as<UiUpdateParticipant>(*receiver.get()).pending(); }
            auto update(UiSession &session) -> Result<empty, UiError> {
                return rstd::as<UiUpdateParticipant>(*receiver.get()).update(session);
            }
            void close(UiSession &session) { rstd::as<UiUpdateParticipant>(*receiver.get()).close(session); }
        };
        auto binding = document_.bindings().make();
        Entry entry{binding.guard(), Box<rstd::dyn<UiUpdateParticipant>>::make(Adapter{rstd::move(receiver)})};
        if (busy_) incoming_.push(rstd::move(entry)); else entries_.push(rstd::move(entry));
        document_.bindings().wake();
        return Ok(rstd::move(binding));
    }
    auto pending() const -> bool {
        if (closed_) return false;
        if (!incoming_.is_empty()) return true;
        for (const auto &entry : entries_)
            if (!entry.guard.alive() || entry.participant->pending()) return true;
        return false;
    }
  private:
    friend class UiSession;
    auto drain(UiSession &session) -> Result<empty, UiError> {
        if (closed_) return Ok(empty{});
        if (busy_) return Err(UiError::Routing);
        busy_ = true;
        struct Guard { bool &value; ~Guard() { value = false; } } guard{busy_};
        for (auto &entry : entries_) {
            if (entry.closed) continue;
            if (!entry.guard.alive()) { entry.closed = true; entry.participant->close(session); }
            else if (entry.participant->pending()) {
                auto result = entry.participant->update(session);
                if (result.is_err()) return result;
            }
        }
        entries_.retain([](const auto &entry) { return !entry.closed; });
        for (auto &entry : incoming_) entries_.push(rstd::move(entry));
        incoming_.clear();
        return Ok(empty{});
    }
    void close(UiSession &session) {
        if (closed_) return;
        closed_ = true;
        for (auto &entry : entries_) if (!entry.closed) entry.participant->close(session);
        for (auto &entry : incoming_) if (!entry.closed) entry.participant->close(session);
        entries_.clear();
        incoming_.clear();
    }
};
} // namespace vui
