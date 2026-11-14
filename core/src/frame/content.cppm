export module vui:frame.content;
import :document.interface;
import :frame.types;
using namespace rstd::prelude;

export namespace vui {
enum class FrameContentStatus { Running, Idle };
struct FrameContentDriver {
    template <class Self, class Delegate = void> struct Api {
        using Trait = FrameContentDriver;
        auto prepare(const UiUpdate &accepted, SurfaceInfo surface)
            -> Result<FrameContentStatus, FrameError> {
            return rstd::trait_call<0>(this, accepted, surface);
        }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::prepare>;
};
class FrameContentRuntime {
    struct Entry {
        BindingGuard guard;
        Box<rstd::dyn<FrameContentDriver>> driver;
        FrameContentStatus status{FrameContentStatus::Running};
    };
    UiDocument &document_;
    Vec<Entry> entries_;
    bool busy_{}, closed_{};
  public:
    explicit FrameContentRuntime(UiDocument &document) : document_(document) {}
    template <class T> requires rstd::Impled<T, FrameContentDriver>
    auto bind(rstd::rc::Weak<T> receiver) -> Result<Binding, FrameError> {
        if (closed_ || busy_) return Err(FrameError::InvalidContent);
        if (entries_.len() >= usize(4096)) return Err(FrameError::Limit);
        auto binding = document_.bindings().make(receiver);
        struct Adapter {
            rstd::rc::Weak<T> receiver;
            auto prepare(const UiUpdate &accepted, SurfaceInfo surface)
                -> Result<FrameContentStatus, FrameError> {
                auto value = receiver.upgrade();
                if (!value) return Ok(FrameContentStatus::Idle);
                return rstd::as<FrameContentDriver>(*value->get()).prepare(accepted, surface);
            }
        };
        entries_.push({binding.guard(), Box<rstd::dyn<FrameContentDriver>>::make(Adapter{rstd::move(receiver)})});
        document_.bindings().wake();
        return Ok(rstd::move(binding));
    }
    auto request(const BindingGuard &guard) -> Result<empty, FrameError> {
        if (closed_ || busy_) return Err(FrameError::InvalidContent);
        for (auto &entry : entries_) if (entry.guard.alive() && entry.guard.same_binding(guard)) {
            entry.status = FrameContentStatus::Running;
            document_.bindings().wake();
            return Ok(empty{});
        }
        return Err(FrameError::InvalidContent);
    }
    auto prepare(auto refresh, SurfaceInfo surface) -> Result<empty, FrameError> {
        if (closed_ || busy_) return Err(FrameError::InvalidContent);
        busy_ = true;
        struct Guard { bool &value; ~Guard() {value = false;} } guard{busy_};
        entries_.retain([](const Entry &entry) {return entry.guard.alive();});
        for (auto &entry : entries_) {
            if (!entry.guard.alive()) continue;
            auto accepted = refresh();
            if (accepted.is_err()) return Err(accepted.unwrap_err());
            auto result = entry.driver->prepare(**accepted, surface);
            if (!entry.guard.alive()) continue;
            if (result.is_err()) {entry.status = FrameContentStatus::Running; return Err(result.unwrap_err());}
            entry.status = *result;
        }
        return Ok(empty{});
    }
    auto active() const -> bool {
        for (const auto &entry : entries_) if (entry.guard.alive() && entry.status == FrameContentStatus::Running) return true;
        return false;
    }
    void close() {
        closed_ = true;
        for (auto &entry : entries_) entry.guard.disconnect();
        entries_.clear();
    }
};
}
