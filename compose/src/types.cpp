module vui.compose;
import :types;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui::compose {
auto CallIdentity::same_group(const CallIdentity &other) const -> bool {
    if (site != other.site || groups.len() != other.groups.len()) return false;
    for (usize index{}; index < groups.len(); ++index) if (groups[index] != other.groups[index]) return false;
    return true;
}

auto CallIdentity::memory_key(Option<Key> explicit_key) const -> Key {
    auto name = rstd::format("{}:{}:{}", groups.len(), site, explicit_key.is_some() ? Serial{} : occurrence);
    for (const auto &group : groups) {
        auto value = group.encoded(); name.push_str(rstd::format("/{}", value.as_str()).as_str());
    }
    if (explicit_key.is_some()) {
        auto value = explicit_key->encoded(); name.push_str(rstd::format("/{}", value.as_str()).as_str());
    }
    return Key{name.as_str()};
}

auto ComposeError::operator=(ComposeError other) -> ComposeError & {
    stage = other.stage; ui = other.ui; state = other.state; input = other.input; node = other.node;
    path = rstd::move(other.path); operation = rstd::move(other.operation);
    revision = other.revision; attempt = other.attempt; committed = other.committed; source = other.source; return *this;
}

void ComposeSignal::invalidate() {
    if (closed) return;
    pending = true;
    if (lifetime.alive()) document->bindings().wake();
}

void ComposeSignal::report(ComposeError value, Option<Serial> committed) {
    value.attempt = attempt;
    if (value.operation.is_empty()) value.operation = String::make(compose_operation_name(value.stage));
    if (!value.source.line()) value.source = source;
    value.committed = committed.is_some() || published;
    if (committed.is_some()) value.revision = *committed;
    else if (published) value.revision = committed_revision;
    else if (lifetime.alive()) value.revision = document->revision();
    if (diagnostics.entries.len() < usize(64)) diagnostics.entries.push(ComposeError(value));
    else if (diagnostics.omitted != ~Serial{}) ++diagnostics.omitted;
    error = Some(rstd::move(value));
}

auto CommittedContext::resolve(const NodeRef &node) -> ComposeResult<UiId> {
    auto id = node.id();
    if (id.is_err()) return id;
    if (!session_.document().valid(id.unwrap())) return Err(ComposeError(ComposeStage::Closed));
    return id;
}

auto compose_operation_name(ComposeStage stage) -> ref<str> {
    switch (stage) {
    case ComposeStage::Closed: return "closed"_str;
    case ComposeStage::Key: return "key"_str;
    case ComposeStage::Type: return "type"_str;
    case ComposeStage::Evaluation: return "evaluate"_str;
    case ComposeStage::State: return "state"_str;
    case ComposeStage::Prepare: return "prepare"_str;
    case ComposeStage::Publish: return "publish"_str;
    case ComposeStage::Input: return "input"_str;
    case ComposeStage::Action: return "action"_str;
    case ComposeStage::Effect: return "effect"_str;
    case ComposeStage::Limit: return "limit"_str;
    case ComposeStage::External: return "external"_str;
    }
    return "compose"_str;
}
} // namespace vui::compose
