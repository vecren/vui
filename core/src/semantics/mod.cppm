export module vui:semantics;
import :types;
import :input.protocol;
import :input.router;
import :document.interface;
using namespace rstd::prelude;

export namespace vui {
enum class SemanticsRole { Generic, Text, Button, Slider, TextField, ScrollArea, Dialog, Switch, Image };

struct SemanticsCollection {
    Option<Index> rows, columns;
    bool horizontal{};
    friend auto operator==(const SemanticsCollection &, const SemanticsCollection &) -> bool = default;
};

struct SemanticsProperties {
    SemanticsRole role{};
    String label, value;
    unsigned actions{};
    bool merge_descendants{};
    Option<bool> checked;
    Option<SemanticsCollection> collection;
    bool exclude_subtree{};

    auto clone() const -> SemanticsProperties {
        return {role, label.clone(), value.clone(), actions, merge_descendants, checked, collection, exclude_subtree};
    }
};

struct SemanticsId {
    UiId node;
    Serial mount_generation{};
    friend auto operator==(const SemanticsId &, const SemanticsId &) -> bool = default;
};

struct SemanticsNode {
    SemanticsId id;
    Option<SemanticsId> parent;
    Rect bounds;
    SemanticsProperties properties;
    bool enabled{}, focused{};
};

class SemanticsSnapshot {
    rstd::sync::Arc<Vec<SemanticsNode>> data_;
    explicit SemanticsSnapshot(Vec<SemanticsNode> nodes)
        : data_(rstd::sync::Arc<Vec<SemanticsNode>>::make(rstd::move(nodes))) {}
    friend class Semantics;

  public:
    SemanticsSnapshot() : SemanticsSnapshot(Vec<SemanticsNode>{}) {}
    SemanticsSnapshot(const SemanticsSnapshot &other) : data_(other.data_.clone()) {}
    auto operator=(const SemanticsSnapshot &other) -> SemanticsSnapshot & {
        if (this != &other) data_ = other.data_.clone();
        return *this;
    }
    SemanticsSnapshot(SemanticsSnapshot &&) = default;
    auto operator=(SemanticsSnapshot &&) -> SemanticsSnapshot & = default;
    auto nodes() const -> slice<SemanticsNode> { return data_->as_slice(); }
    auto get(SemanticsId id) const -> const SemanticsNode *;
};

class Semantics {
    UiDocument &document_;
    EventRouter &input_;
    friend class UiSession;
    Semantics(UiDocument &document, EventRouter &input) : document_(document), input_(input) {}
    auto set_impl(UiId, SemanticsProperties, bool defaults) -> Result<empty, UiError>;

  public:
    Semantics(const Semantics &) = delete;
    auto operator=(const Semantics &) -> Semantics & = delete;
    auto set(UiId id, SemanticsProperties properties) -> Result<empty, UiError>;
    auto set_default(UiId id, SemanticsProperties properties) -> Result<empty, UiError>;
    auto clear(UiId id) -> Result<empty, UiError>;
    auto snapshot() const -> SemanticsSnapshot;
    auto perform(SemanticsId id, SemanticsAction action, Option<SemanticsPosition> position = {})
        -> Result<bool, InputError>;
};
} // namespace vui
