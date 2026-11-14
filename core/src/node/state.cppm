export module vui:node.state;
import :types;
using namespace rstd::prelude;

export namespace vui {
struct NodeStateKey {
    rstd::any::TypeId role{rstd::any::TypeId::of<void>()};
    Serial instance{};
    template <class T> static auto of() -> NodeStateKey { return {rstd::any::TypeId::of<T>()}; }
    friend auto operator==(NodeStateKey, NodeStateKey) -> bool = default;
};
} // namespace vui
