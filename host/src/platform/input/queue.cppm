module vui.host:platform.input.queue;
import vui;

namespace vui::host::detail {
using namespace rstd::prelude;

class InputQueue {
    Vec<InputEvent> events_;
    Option<usize> reset_;
    Option<bool> focused_;
    usize capacity_;

    static auto completion(EventType type) -> bool {
        return type == EventType::ClipboardResult || type == EventType::ClipboardWriteResult;
    }

  public:
    explicit InputQueue(usize capacity = usize(4096)) : capacity_(capacity) {}
    auto can_reserve(usize pending) const -> bool {
        // Keep space for one reset and every accepted clipboard completion.
        return reset_.is_none() && capacity_ > usize(1) && events_.len() + pending < capacity_ - usize(1);
    }
    template <class F> auto push(InputEvent event, usize pending, F prepare) -> Serial {
        if (event.type == EventType::WindowFocusLost) focused_ = Some(false);
        else if (event.type == EventType::WindowFocusGained) focused_ = Some(true);
        if (completion(event.type)) {
            if (events_.len() >= capacity_) return 1;
            prepare(event); events_.push(rstd::move(event)); return 0;
        }
        if (reset_.is_some()) return 1;
        if (!can_reserve(pending)) {
            if (events_.len() < capacity_) {
                InputEvent reset; reset.type = EventType::InputReset;
                prepare(reset); reset_ = Some(events_.len()); events_.push(rstd::move(reset));
            }
            return 1;
        }
        prepare(event); events_.push(rstd::move(event)); return 0;
    }
    auto is_empty() const -> bool { return events_.is_empty(); }
    auto take() -> Vec<InputEvent> {
        if (reset_.is_some()) events_[*reset_].window_focused = focused_;
        reset_ = None();
        return rstd::exchange(events_, Vec<InputEvent>{});
    }
    void clear() { events_.clear(); reset_ = None(); focused_ = None(); }
};
} // namespace vui::host::detail
