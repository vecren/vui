export module vui:composition;
import :display_list;
using namespace rstd::prelude;

export namespace vui {
class Children {
    rstd::sync::Arc<Vec<RenderId>> ids_;

  public:
    explicit Children(Vec<RenderId> ids = {})
        : ids_(rstd::sync::Arc<Vec<RenderId>>::make(rstd::move(ids))) {}
    Children(const Children &other) : ids_(other.ids_.clone()) {}
    auto operator=(const Children &other) -> Children & {
        if (this != &other)
            ids_ = other.ids_.clone();
        return *this;
    }
    Children(Children &&) = default;
    auto operator=(Children &&) -> Children & = default;
    auto values() const -> slice<RenderId> { return ids_->as_slice(); }
};

struct RenderProperties {
    Transform transform;
    Rect bounds;
    Clip clip;
    Scalar opacity{1};
    bool visible{true};
    auto valid() const -> bool {
        return transform.valid() && bounds.valid() && clip.valid() && finite(opacity) &&
               opacity >= 0 && opacity <= 1;
    }
    friend auto operator==(const RenderProperties &, const RenderProperties &) -> bool = default;
};
struct RenderNode {
    RenderId id;
    RenderProperties properties;
    DisplayList content;
    Children children;
    Serial properties_version{}, content_version{}, children_version{};
};
struct TransactionHeader {
    Serial session{}, base_revision{}, revision{}, ui_revision{};
};
enum class ApplyError {
    Session,
    Revision,
    InvalidId,
    StaleId,
    Duplicate,
    Missing,
    InvalidProperties,
    MissingChildrenOp,
    MultipleParents,
    Cycle,
    Root,
    Unreachable,
    Limit,
    Allocation
};
struct Applied {
    Serial session{}, revision{}, ui_revision{};
};
struct PropertyUpdate {
    RenderId id;
    RenderProperties value;
};
struct ContentUpdate {
    RenderId id;
    DisplayList value;
};
struct ChildrenUpdate {
    RenderId id;
    Children value;
};

class TransactionBuilder;
class RenderScene;
class RenderTransaction {
    struct Data {
        TransactionHeader header;
        Vec<RenderNode> creates;
        Vec<PropertyUpdate> properties;
        Vec<ContentUpdate> contents;
        Vec<ChildrenUpdate> children;
        Vec<RenderId> deletes;
        bool root_changed{};
        Option<RenderId> root;
    };
    rstd::sync::Arc<Data> data_;
    explicit RenderTransaction(Data data) : data_(rstd::sync::Arc<Data>::make(rstd::move(data))) {}
    friend class TransactionBuilder;
    friend class RenderScene;

  public:
    RenderTransaction(RenderTransaction &&) = default;
    auto operator=(RenderTransaction &&) -> RenderTransaction & = default;
    RenderTransaction(const RenderTransaction &) = delete;
    auto operator=(const RenderTransaction &) -> RenderTransaction & = delete;
    auto header() const -> TransactionHeader { return data_->header; }
};
class TransactionBuilder {
    RenderTransaction::Data data_;

  public:
    explicit TransactionBuilder(TransactionHeader header) { data_.header = header; }
    void create(RenderId id, RenderProperties properties, DisplayList content,
                Children children = Children{}) {
        data_.creates.push(RenderNode{id, properties, rstd::move(content), rstd::move(children)});
    }
    void properties(RenderId id, RenderProperties value) { data_.properties.push({id, value}); }
    void content(RenderId id, DisplayList value) { data_.contents.push({id, rstd::move(value)}); }
    void children(RenderId id, Children value) { data_.children.push({id, rstd::move(value)}); }
    void destroy(RenderId id) { data_.deletes.push(RenderId(id)); }
    void root(Option<RenderId> value) {
        data_.root_changed = true;
        data_.root = value;
    }
    auto finish() && -> RenderTransaction { return RenderTransaction(rstd::move(data_)); }
};

class RenderScene {
    struct Slot {
        Serial generation{};
        Option<RenderNode> node;
    };
    Serial session_, revision_{}, ui_revision_{};
    Index limit_;
    Vec<Slot> slots_;
    Option<RenderId> root_;

    static auto resolve(Vec<Slot> &slots, RenderId id, Serial session) -> RenderNode * {
        auto index = usize(id.slot);
        if (!id.valid() || id.session != session || index >= slots.len())
            return nullptr;
        auto &slot = slots[index];
        if (slot.generation != id.generation || slot.node.is_none())
            return nullptr;
        return &*slot.node;
    }

    auto apply_to(const RenderTransaction &transaction, bool reset) -> Result<Applied, ApplyError> {
        const auto &data = *transaction.data_;
        const auto header = data.header;
        if (!header.session || (reset ? header.session <= session_ : header.session != session_))
            return Err(ApplyError::Session);
        auto base = reset ? Serial{} : revision_;
        if (header.base_revision != base || base == ~Serial{} || header.revision != base + 1 ||
            (!reset && header.ui_revision < ui_revision_))
            return Err(ApplyError::Revision);
        usize size = reset ? usize{} : slots_.len();
        for (const auto &node : data.creates) {
            if (!node.id.valid() || node.id.session != header.session)
                return Err(ApplyError::InvalidId);
            if (node.id.slot >= limit_)
                return Err(ApplyError::Limit);
            auto required = usize(node.id.slot) + usize(1);
            if (required > size)
                size = required;
        }
        Vec<Slot> candidate;
        Vec<unsigned char> touched, parents;
        Vec<Index> queue;
        if (candidate.try_reserve_exact(size).is_err() ||
            touched.try_reserve_exact(size).is_err() || parents.try_reserve_exact(size).is_err() ||
            queue.try_reserve_exact(size).is_err())
            return Err(ApplyError::Allocation);
        for (usize i{}; i < size; ++i) {
            Slot slot;
            if (!reset && i < slots_.len()) {
                slot.generation = slots_[i].generation;
                if (slots_[i].node.is_some())
                    slot.node = Some(RenderNode(*slots_[i].node));
            }
            candidate.push(rstd::move(slot));
            touched.push(0);
            parents.push(0);
        }
        auto mark = [&](RenderId id, unsigned mask) -> Option<ApplyError> {
            if (!id.valid() || id.session != header.session)
                return Some(ApplyError::InvalidId);
            if (usize(id.slot) >= size)
                return Some(ApplyError::Missing);
            auto &flags = touched[usize(id.slot)];
            if ((flags & mask) != 0)
                return Some(ApplyError::Duplicate);
            flags = static_cast<unsigned char>(flags | mask);
            return None();
        };
        for (auto id : data.deletes) {
            auto error = mark(id, 8);
            if (error.is_some())
                return Err(*error);
            if (!resolve(candidate, id, header.session))
                return Err(ApplyError::StaleId);
            candidate[usize(id.slot)].node = None();
        }
        for (const auto &node : data.creates) {
            auto error = mark(node.id, 7);
            if (error.is_some())
                return Err(*error);
            auto &slot = candidate[usize(node.id.slot)];
            if (slot.node.is_some())
                return Err(ApplyError::Duplicate);
            if (node.id.generation <= slot.generation)
                return Err(ApplyError::StaleId);
            auto copy = node;
            copy.properties_version = copy.content_version = copy.children_version =
                header.revision;
            slot.generation = copy.id.generation;
            slot.node = Some(rstd::move(copy));
        }
        for (const auto &patch : data.properties) {
            auto error = mark(patch.id, 1);
            if (error.is_some())
                return Err(*error);
            auto node = resolve(candidate, patch.id, header.session);
            if (!node)
                return Err(ApplyError::StaleId);
            node->properties = patch.value;
            node->properties_version = header.revision;
        }
        for (const auto &patch : data.contents) {
            auto error = mark(patch.id, 2);
            if (error.is_some())
                return Err(*error);
            auto node = resolve(candidate, patch.id, header.session);
            if (!node)
                return Err(ApplyError::StaleId);
            node->content = patch.value;
            node->content_version = header.revision;
        }
        for (const auto &patch : data.children) {
            auto error = mark(patch.id, 4);
            if (error.is_some())
                return Err(*error);
            auto node = resolve(candidate, patch.id, header.session);
            if (!node)
                return Err(ApplyError::StaleId);
            node->children = patch.value;
            node->children_version = header.revision;
        }
        auto root = reset ? Option<RenderId>{} : root_;
        if (data.root_changed)
            root = data.root;
        usize count{};
        for (const auto &slot : candidate) {
            if (slot.node.is_none())
                continue;
            ++count;
            const auto &node = *slot.node;
            if (!node.properties.valid())
                return Err(ApplyError::InvalidProperties);
            if (!node.children.values().is_empty() && !node.content.has_children())
                return Err(ApplyError::MissingChildrenOp);
            for (auto child : node.children.values()) {
                if (!resolve(candidate, child, header.session))
                    return Err(ApplyError::Missing);
                auto &incoming = parents[usize(child.slot)];
                if (incoming != 0)
                    return Err(ApplyError::MultipleParents);
                incoming = 1;
            }
        }
        if (root.is_some()) {
            if (!resolve(candidate, *root, header.session) || parents[usize(root->slot)] != 0)
                return Err(ApplyError::Root);
        } else if (count != usize())
            return Err(ApplyError::Root);
        for (usize i{}; i < size; ++i)
            if (candidate[i].node.is_some() && parents[i] == 0)
                queue.push(static_cast<Index>(i.to_primitive()));
        for (usize head{}; head < queue.len(); ++head) {
            auto index = usize(queue[head]);
            for (auto child : candidate[index].node->children.values())
                queue.push(Index(child.slot));
        }
        if (queue.len() != count)
            return Err(ApplyError::Cycle);
        for (usize i{}; i < size; ++i)
            if (candidate[i].node.is_some() && parents[i] == 0 &&
                (root.is_none() || i != usize(root->slot)))
                return Err(ApplyError::Unreachable);
        slots_ = rstd::move(candidate);
        root_ = root;
        session_ = header.session;
        revision_ = header.revision;
        ui_revision_ = header.ui_revision;
        return Ok(Applied{session_, revision_, ui_revision_});
    }

  public:
    explicit RenderScene(Serial session, Index node_limit = 1'000'000)
        : session_(session), limit_(node_limit) {}
    RenderScene(const RenderScene &) = delete;
    auto operator=(const RenderScene &) -> RenderScene & = delete;
    auto session() const -> Serial { return session_; }
    auto revision() const -> Serial { return revision_; }
    auto ui_revision() const -> Serial { return ui_revision_; }
    auto root() const -> Option<RenderId> { return root_; }
    auto get(RenderId id) const -> const RenderNode * {
        if (!id.valid() || id.session != session_ || usize(id.slot) >= slots_.len())
            return nullptr;
        const auto &slot = slots_[usize(id.slot)];
        if (slot.generation != id.generation || slot.node.is_none())
            return nullptr;
        return &*slot.node;
    }
    auto apply(const RenderTransaction &transaction) -> Result<Applied, ApplyError> {
        return apply_to(transaction, false);
    }
    auto reset(const RenderTransaction &snapshot) -> Result<Applied, ApplyError> {
        return apply_to(snapshot, true);
    }
};
} // namespace vui
