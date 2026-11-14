export module vui:document.lifecycle;
import :node.ownership;
import :node.types;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
struct DocumentMutation {
    Serial id;
    explicit DocumentMutation(Serial value) : id(value) {}
    virtual ~DocumentMutation() = default;
    virtual auto apply(UiDocument &) -> Result<empty, UiError> = 0;
};

template <class F> struct DocumentDeferred : DocumentMutation {
    F callback;
    DocumentDeferred(Serial id, F value) : DocumentMutation(id), callback(rstd::move(value)) {}
    auto apply(UiDocument &document) -> Result<empty, UiError> override {
        return callback(document);
    }
};
} // namespace vui
