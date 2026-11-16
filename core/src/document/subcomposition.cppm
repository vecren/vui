export module vui:document.subcomposition;
import :layout.types;
import :binding.scope;
using namespace rstd::prelude;

export namespace vui {
struct SubcompositionRequest {
    UiId target;
    Serial id{};
    LayoutConstraints constraints;
    friend auto operator==(const SubcompositionRequest &, const SubcompositionRequest &) -> bool = default;
};
}

namespace vui {
struct DocumentSubcomposition {
    UiId target;
    BindingGuard guard;
    Box<rstd::dyn<rstd::FnMut<Result<empty, UiError>(SubcompositionRequest)>>> callback;
    Option<LayoutConstraints> completed;
    Option<SubcompositionRequest> request;
    Serial mount{};
    bool dispatched{};
    DocumentSubcomposition(UiId node, BindingGuard binding,
        Box<rstd::dyn<rstd::FnMut<Result<empty, UiError>(SubcompositionRequest)>>> receiver)
        : target(node), guard(rstd::move(binding)), callback(rstd::move(receiver)) {}
};
}

export namespace vui {
class SubcompositionLease {
    rstd::rc::Rc<DocumentSubcomposition> state_;
    Binding binding_;
    friend class UiDocument;
    SubcompositionLease(rstd::rc::Rc<DocumentSubcomposition> state, Binding binding)
        : state_(rstd::move(state)), binding_(rstd::move(binding)) {}
  public:
    SubcompositionLease(const SubcompositionLease &) = delete;
    SubcompositionLease(SubcompositionLease &&) = default;
    auto operator=(SubcompositionLease &&) -> SubcompositionLease & = default;
    auto alive() const -> bool { return binding_.alive(); }
    void close() { binding_.disconnect(); }
    auto request() const -> Option<SubcompositionRequest> {
        return alive() ? state_->request : None<SubcompositionRequest>();
    }
};
}
