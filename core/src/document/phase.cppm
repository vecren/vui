export module vui:document.phase;
import :state.graph;
import :state.tracking;
import :node.types;
using namespace rstd::prelude;

export namespace vui {
enum class UiPhase { Measure, Placement, Paint };
struct UiPhaseDiagnostic {
    UiId target;
    UiPhase phase;
    state::StateError error;
};
}

namespace vui {
struct DocumentPhaseDependency {
    state::GraphView graph;
    state::NodeId value;
};
struct DocumentPhaseObservation {
    UiId target;
    UiPhase phase;
    Serial mount{};
    bool active{}, pending{};
    Vec<DocumentPhaseDependency> dependencies;
    Vec<state::Subscription> subscriptions;
    DocumentPhaseObservation(UiId node, UiPhase stage, Serial generation)
        : target(node), phase(stage), mount(generation) {}
    void close() { active = false; pending = false; subscriptions.clear(); }
};
struct DocumentPhaseCandidate {
    rstd::rc::Rc<DocumentPhaseObservation> observation;
    bool completed{};
    bool reused{};
};
class DocumentPhaseTransaction {
    struct Protection {
        state::GraphView graph;
        state::StateWriteGuard guard;
        state::Version epoch;
    };
    Vec<Protection> protections_;
    Vec<UiPhaseDiagnostic> &diagnostics_;
  public:
    Vec<DocumentPhaseCandidate> candidates;
    bool published{};
    explicit DocumentPhaseTransaction(Vec<UiPhaseDiagnostic> &diagnostics) : diagnostics_(diagnostics) {}
    auto protect(state::GraphView graph) -> Option<state::StateError>;
    void report(UiId node, UiPhase phase, state::StateError error);
    void stage(rstd::rc::Rc<DocumentPhaseObservation> observation, bool completed);
    auto ready() const -> Option<state::StateError>;
};
class DocumentPhaseReadScope final : public state::ReadObserver {
    DocumentPhaseTransaction &transaction_;
    rstd::rc::Rc<DocumentPhaseObservation> observation_;
    bool previous_{}, finished_{};
    Option<state::StateError> error_;
    state::ReadTracking tracking_{*this};
  public:
    DocumentPhaseReadScope(DocumentPhaseTransaction &transaction, UiId node, UiPhase phase,
                           Serial mount, bool previous)
        : transaction_(transaction), observation_(rstd::rc::make_rc<DocumentPhaseObservation>(node, phase, mount)),
          previous_(previous) {}
    DocumentPhaseReadScope(const DocumentPhaseReadScope &) = delete;
    DocumentPhaseReadScope(DocumentPhaseReadScope &&) = delete;
    ~DocumentPhaseReadScope();
    auto observed(const state::GraphView &graph, const state::NodeId &value)
        -> Option<state::StateError> override;
    auto finish(bool executed = true) -> Option<UiError>;
};
}
