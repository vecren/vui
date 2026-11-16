export module vui:state.tracking;
import rstd;
using namespace rstd::prelude;

export namespace vui::state {
class GraphView;
struct NodeId;
enum class StateError;

class ReadObserver {
  public:
    virtual ~ReadObserver() = default;
    virtual auto observed(const GraphView &, const NodeId &) -> Option<StateError> = 0;
};

class ReadTracking {
    inline static thread_local ReadObserver *active_{};
    ReadObserver *previous_;

  public:
    explicit ReadTracking(ReadObserver &observer) : previous_(rstd::exchange(active_, &observer)) {}
    ReadTracking(const ReadTracking &) = delete;
    ReadTracking(ReadTracking &&) = delete;
    ~ReadTracking() { active_ = previous_; }
    static auto current() -> ReadObserver * { return active_; }
};
}
