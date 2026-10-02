module;
#include <rstd/enum.hpp>
export module vui:endpoint;
import :composition;
import :diagnostics;
using namespace rstd::prelude;

export namespace vui {
enum class EnqueueResult { Accepted, Busy, Closed, Empty, Allocation };
enum class TransactionKind { Update, Snapshot };
class ApplyEvent {
    RSTD_ENUM(ApplyEvent, (Applied, (vui::Applied value;)),
              (Rejected, (TransactionHeader header; ApplyError error;)),
              (Cancelled, (TransactionHeader header;)))
};

struct ApplyTimings {
    TransactionHeader transaction;
    TransactionKind kind;
    CpuTiming apply;
    Option<ApplyError> error;
};
class RenderEndpoint {
    RenderScene scene_;
    Option<RenderTransaction> pending_;
    TransactionKind kind_{};
    Vec<ApplyEvent> events_;
    bool closed_{};
    Option<ApplyTimings> last_timings_;

  public:
    explicit RenderEndpoint(Serial session, Index node_limit = 1'000'000)
        : scene_(session, node_limit) {}
    auto scene() const -> const RenderScene & { return scene_; }
    auto try_enqueue(Option<RenderTransaction> &transaction,
                     TransactionKind kind = TransactionKind::Update) -> EnqueueResult {
        if (closed_)
            return EnqueueResult::Closed;
        if (pending_.is_some() || !events_.is_empty())
            return EnqueueResult::Busy;
        if (transaction.is_none())
            return EnqueueResult::Empty;
        if (events_.try_reserve_exact(usize(1)).is_err())
            return EnqueueResult::Allocation;
        pending_ = transaction.take();
        kind_ = kind;
        return EnqueueResult::Accepted;
    }
    auto last_apply_timings() const -> Option<ApplyTimings> { return last_timings_; }
    auto apply_pending(DiagnosticsOptions options = {}) -> bool {
        if (pending_.is_none())
            return false;
        auto transaction = pending_.take().unwrap();
        ApplyTimings sample{transaction.header(), kind_, {}, {}};
        CpuTimer timer(options.timings ? &sample.apply : nullptr);
        auto result = kind_ == TransactionKind::Snapshot ? scene_.reset(transaction)
                                                         : scene_.apply(transaction);
        timer.pause();
        if (result.is_err())
            sample.error = Some(result.unwrap_err());
        last_timings_ = options.timings ? Some(sample) : None<ApplyTimings>();
        if (result.is_ok())
            events_.push(ApplyEvent::Applied(result.unwrap()));
        else
            events_.push(ApplyEvent::Rejected(transaction.header(), result.unwrap_err()));
        return true;
    }
    auto poll_events() -> Vec<ApplyEvent> { return rstd::exchange(events_, Vec<ApplyEvent>{}); }
    void close() {
        if (closed_)
            return;
        closed_ = true;
        if (pending_.is_some()) {
            events_.push(ApplyEvent::Cancelled(pending_->header()));
            pending_ = None();
        }
    }
};
} // namespace vui
