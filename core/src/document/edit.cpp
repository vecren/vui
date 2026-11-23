module vui;
import :document.interface;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto UiDocument::node_kind(const UiEditBatch &edit, UiNodeRef ref) -> Result<LayoutKind, UiError> {
    if (ref.batch != edit.owner_ || ref.index >= edit.entries_.len()) return Err(UiError::InvalidId);
    const auto &entry = edit.entries_[ref.index];
    if (entry.existing.is_none()) return Ok(entry.kind);
    auto node = resolve(*entry.existing);
    return node ? Result<LayoutKind, UiError>(Ok(node->kind)) : Result<LayoutKind, UiError>(Err(UiError::InvalidId));
}

auto UiDocument::prepare(UiEditBatch edit) -> Result<PreparedEdit, UiEditError> {
    Option<UiNodeRef> active_ref;
    auto operation = UiEditOperation::Handle;
    auto fail = [&](UiError error) -> Result<PreparedEdit, UiEditError> {
        return Err(UiEditError{error, active_ref, operation});
    };
    if (!can_route()) return fail(UiError::Routing);
    if (edit.error_.is_some()) return fail(*edit.error_);
    if (!edit.access_.valid(session_)) return fail(UiError::PropertyOwned);
    PreparedEdit result;
    result.batch_ = edit.owner_;
    result.document_ = this;
    result.session_ = session_; result.base_ = revision_; result.lifetime_ = lifetime();
    for (auto id : edit.removed_) {
        if (!valid(id)) return fail(UiError::InvalidId);
        if (root_ == Some(id)) return fail(UiError::Root);
        auto allowed = relation_access(id, edit.access_);
        if (allowed.is_err()) return fail(allowed.unwrap_err());
        for (auto previous : result.retired_) {
            if (is_descendant(previous, id).unwrap() || is_descendant(id, previous).unwrap())
                return fail(UiError::InvalidValue);
        }
        result.retired_.push(UiId(id));
        if (resolve(id)->parent.is_some()) edit.existing(*resolve(id)->parent);
    }
    for (usize i{}; i < edit.entries_.len(); ++i) {
        auto existing = edit.entries_[i].existing;
        if (existing.is_none()) continue;
        if (!valid(*existing)) return fail(UiError::InvalidId);
        for (auto removed : result.retired_)
            if (is_descendant(removed, *existing).unwrap()) return fail(UiError::InvalidValue);
        auto node = resolve(*existing);
        if (edit.entries_[i].children.is_some())
            for (auto child : node->children) {
                bool retired{};
                for (auto removed : result.retired_)
                    retired |= is_descendant(removed, child).unwrap();
                if (!retired) edit.existing(child);
            }
        if (node->parent.is_some()) edit.existing(*node->parent);
    }
    usize next_slot{};
    usize entry_index{};
    for (auto &entry : edit.entries_) {
        active_ref = Some(UiNodeRef{edit.owner_, entry_index++}); operation = UiEditOperation::Handle;
        typename PreparedEdit::Entry prepared;
        if (entry.existing.is_some()) {
            prepared.id = *entry.existing;
            auto node = resolve(prepared.id);
            entry.kind = node->kind;
            prepared.parent = node->parent;
            for (auto child : node->children) prepared.children.push(UiId(child));
        } else {
            while (next_slot < slots_.len() &&
                   (slots_[next_slot].node.is_some() || slots_[next_slot].generation == ~Serial{}))
                ++next_slot;
            if (next_slot >= usize(1'000'000)) return fail(UiError::Limit);
            prepared.id = {session_, Index(next_slot.to_primitive()),
                           next_slot < slots_.len() ? slots_[next_slot].generation : 1};
            ++next_slot;
            if (unsigned(entry.kind) > unsigned(LayoutKind::Item)) return fail(UiError::InvalidValue);
            prepared.fresh = Some(Node(entry.kind, NodeStyle{}));
            if (entry.kind == LayoutKind::Viewport) prepared.fresh->viewport = Some(Box<ViewportLayout>::make());
            if (entry.kind == LayoutKind::Text) prepared.fresh->text = Some(Box<TextContent>::make());
            if (entry.kind == LayoutKind::Image) prepared.fresh->image = Some(Box<ImageContent>::make());
            if (entry.kind == LayoutKind::Scene) prepared.fresh->scene = Some(Box<SceneContent>::make());
        }
        auto node = entry.existing.is_some() ? resolve(prepared.id) : &*prepared.fresh;
        if (entry.style.is_some()) {
            operation = UiEditOperation::Style;
            auto checked = validate_style(*node, *entry.style);
            if (checked.is_err()) return fail(checked.unwrap_err());
            auto style = checked.unwrap();
            auto &old = node->style;
            if (entry.existing.is_none() || old != style) prepared.style = Some(style);
        }
        for (auto &state : entry.states) {
            operation = UiEditOperation::State;
            if (node_state(node->states, state.key)) return fail(UiError::InvalidValue);
            prepared.states.push(rstd::move(state));
        }
        for (auto &patch : entry.patches) {
            operation = UiEditOperation::State;
            auto state = node_state(prepared.states, patch.key);
            if (!state) state = node_state(node->states, patch.key);
            if (!state || state->type() != patch.value->type()) return fail(UiError::InvalidValue);
            prepared.patches.push(rstd::move(patch));
        }
        for (const auto &state : node->states) prepared.state_types.push({state.key, state.value->type()});
        for (const auto &state : prepared.states) prepared.state_types.push({state.key, state.value->type()});
        if (entry.children_owner.is_some()) {
            operation = UiEditOperation::Children;
            auto permit = *entry.children_owner;
            if (!accepts_children(node->kind) || !permit.alive() || permit.document() != session_)
                return fail(UiError::PropertyOwned);
            if (node->children_owner.is_some() && node->children_owner->alive() && !permit.same_owner(*node->children_owner))
                return fail(UiError::PropertyOwned);
            if (node->children_owner.is_none() || !permit.same_owner(*node->children_owner))
                prepared.children_owner = Some(rstd::move(permit));
        }
        if (entry.text.is_some()) {
            operation = UiEditOperation::Text;
            auto &value = *entry.text; auto format = value.format();
            if (node->text.is_none() || !format.valid() || !format.line_breaks.matches(value.text()) || !value.color.valid())
                return fail(UiError::InvalidValue);
            auto &old = **node->text;
            auto content = value.text(); auto color = value.color;
            if (entry.before_text.is_some()) {
                auto &before = *entry.before_text;
                format = merge_format(text_format(old), before.format(), format);
                if (before.text() == value.text()) content = old.value.as_str();
                if (before.color == value.color) color = old.color;
            }
            auto changed_value = old.value.as_str() != content;
            if ((changed_value && value_occupied(*node, UiValueProperty::Text)) ||
                (old.character_spacing != format.character_spacing && property_occupied(*node, PresentationProperty::CharacterSpacing)) ||
                (old.color != color && property_occupied(*node, PresentationProperty::TextColor)))
                return fail(UiError::PropertyOwned);
            if (entry.existing.is_none() || changed_value || format_changed(old, format) || old.color != color) {
                auto text = Box<TextContent>::make();
                text->value = String::make(content); assign_format(*text, format); text->color = color;
                text->effects = old.effects;
                if (!changed_value) {
                    text->colors = TextColors(old.colors); text->decorations = TextDecorations(old.decorations);
                    text->inline_images = TextInlineImages(old.inline_images); text->font_spans = TextFontSpans(old.font_spans);
                    text->boundaries = TextBoundaries(old.boundaries);
                }
                prepared.text = Some(rstd::move(text));
            }
        }

        if (entry.image.is_some()) {
            operation = UiEditOperation::Image;
            auto &value = *entry.image;
            if (node->image.is_none() || !value.tint.valid() || !value.radial.valid() ||
                unsigned(value.fit) > unsigned(ImageFit::Cover) || unsigned(value.sampling) > unsigned(ImageSampling::Linear))
                return fail(UiError::InvalidValue);
            auto &old = **node->image;
            if (entry.before_image.is_some()) {
                auto &before = *entry.before_image;
                bool same_data = before.data.is_some() == value.data.is_some() &&
                    (before.data.is_none() || *before.data == *value.data);
                if (same_data) value.data = old.data.is_some() ? Some(ImageData(*old.data)) : None<ImageData>();
                if (before.fit == value.fit) value.fit = old.fit;
                if (before.sampling == value.sampling) value.sampling = old.sampling;
                if (before.tint == value.tint) value.tint = old.tint;
                if (before.radial == value.radial) value.radial = old.radial;
            }
            bool same_image = old.data.is_some() == value.data.is_some() &&
                (old.data.is_none() || *old.data == *value.data);
            if (!same_image && value_occupied(*node, UiValueProperty::Image)) return fail(UiError::PropertyOwned);
            if (entry.existing.is_none() || !same_image || old.fit != value.fit || old.sampling != value.sampling ||
                old.tint != value.tint || old.radial != value.radial) {
                auto image = Box<ImageContent>::make();
                image->data = value.data.is_some() ? Some(ImageData(*value.data)) : None<ImageData>(); image->fit = value.fit; image->sampling = value.sampling;
                image->tint = value.tint; image->radial = value.radial; prepared.image = Some(rstd::move(image));
            }
        }
        prepared.kind = entry.kind; prepared.created = entry.existing.is_none();
        result.entries_.push(rstd::move(prepared));
    }
    auto lookup = [&](UiId id) -> typename PreparedEdit::Entry * {
        for (auto &entry : result.entries_) if (entry.id == id) return &entry;
        return nullptr;
    };
    for (usize i{}; i < edit.entries_.len(); ++i) {
        active_ref = Some(UiNodeRef{edit.owner_, i}); operation = UiEditOperation::Children;
        auto &source = edit.entries_[i]; auto &entry = result.entries_[i];
        if (source.children.is_none()) continue;
        if (source.existing.is_some()) {
            auto allowed = children_access(entry.id, edit.access_);
            if (allowed.is_err()) return fail(allowed.unwrap_err());
        }
        if (source.children_owner.is_some() && !edit.access_.allows(*source.children_owner)) return fail(UiError::PropertyOwned);
        if ((source.kind == LayoutKind::Leaf || source.kind == LayoutKind::Text || source.kind == LayoutKind::Image || source.kind == LayoutKind::Scene) && !source.children->is_empty())
            return fail(UiError::MissingChild);
        if (source.kind == LayoutKind::Viewport && source.children->len() > usize(1)) return fail(UiError::InvalidValue);
        for (auto child : entry.children) if (auto previous = lookup(child)) previous->parent = None();
        entry.children.clear();
    }
    for (usize i{}; i < edit.entries_.len(); ++i) {
        active_ref = Some(UiNodeRef{edit.owner_, i}); operation = UiEditOperation::Children;
        auto &source = edit.entries_[i]; auto &entry = result.entries_[i];
        if (source.children.is_none()) continue;
        for (auto ref : *source.children) {
            if (ref.batch != edit.owner_ || ref.index >= result.entries_.len()) return fail(UiError::InvalidId);
            auto &child = result.entries_[ref.index];
            if (!child.created && child.parent != Some(entry.id)) {
                auto allowed = relation_access(child.id, edit.access_);
                if (allowed.is_err()) return fail(allowed.unwrap_err());
            }
            if (root_ == Some(child.id)) return fail(UiError::Root);
            for (auto id : entry.children) if (id == child.id) return fail(UiError::InvalidValue);
            if (child.parent.is_some() && *child.parent != entry.id) {
                auto parent = lookup(*child.parent);
                if (!parent) return fail(UiError::InvalidValue);
                auto parent_index = usize(parent - result.entries_.as_mut_ptr().as_raw_ptr());
                if (edit.entries_[parent_index].children.is_some()) return fail(UiError::InvalidValue);
            }
            child.parent = Some(entry.id); entry.children.push(UiId(child.id));
        }
    }
    for (auto removed : result.retired_) {
        auto parent = resolve(removed)->parent;
        if (parent.is_some()) lookup(*parent)->children.retain([&](UiId id) { return id != removed; });
    }
    entry_index = usize();
    for (auto &entry : result.entries_) {
        active_ref = Some(UiNodeRef{edit.owner_, entry_index++}); operation = UiEditOperation::Relation;
        entry.children.retain([&](UiId id) {
            auto child = lookup(id); return !child || child->parent == Some(entry.id);
        });
        auto cursor = entry.parent; Index depth{};
        while (cursor.is_some()) {
            if (*cursor == entry.id) return fail(UiError::Cycle);
            if (++depth >= depth_limit_) return fail(UiError::Limit);
            auto ancestor = lookup(*cursor);
            cursor = ancestor ? ancestor->parent : resolve(*cursor)->parent;
        }
        if (auto old = resolve(entry.id)) {
            entry.relation_changed = old->parent != entry.parent;
            entry.children_changed = old->children.len() != entry.children.len();
            if (!entry.children_changed)
                for (usize i{}; i < entry.children.len(); ++i)
                    entry.children_changed |= old->children[i] != entry.children[i];
        } else {
            entry.relation_changed = entry.parent.is_some();
            entry.children_changed = !entry.children.is_empty();
        }
        auto node = entry.fresh.is_some() ? &*entry.fresh : resolve(entry.id);
        node->states.reserve(entry.states.len());
    }
    for (usize i{}; i < edit.entries_.len(); ++i) {
        if (!edit.entries_[i].preserve_mount) continue;
        active_ref = Some(UiNodeRef{edit.owner_, i}); operation = UiEditOperation::Relation;
        auto &entry = result.entries_[i];
        if (entry.created || !mounted(entry.id)) return fail(UiError::InvalidValue);
        auto allowed = relation_access(entry.id, edit.access_);
        if (allowed.is_err()) return fail(allowed.unwrap_err());
        auto cursor = Some(entry.id);
        bool connected{};
        for (Index depth{}; cursor.is_some() && depth < depth_limit_; ++depth) {
            for (auto retired : result.retired_) if (*cursor == retired) return fail(UiError::InvalidValue);
            if (cursor == root_) { connected = true; break; }
            auto candidate = lookup(*cursor);
            auto live = resolve(*cursor);
            cursor = candidate ? candidate->parent : live ? live->parent : None<UiId>();
        }
        if (!connected) return fail(UiError::InvalidValue);
        result.preserved_.push(UiId(entry.id));
    }
    result.slots_ = next_slot > slots_.len() ? next_slot : slots_.len();
    slots_.reserve(result.slots_ - slots_.len());
    input_changes_.reserve(result.entries_.len() + result.retired_.len());
    animation_completions_.reserve(animations_.len());
    result.retirement_ = Some(Box<typename PreparedEdit::Retirement>::make());
    result.access_ = rstd::move(edit.access_);
    return Ok(rstd::move(result));
}

auto UiDocument::publish(PreparedEdit edit) -> Result<UiCommitReport, UiError> {
    if (!can_route()) return Err(UiError::Routing);
    if (!edit.lifetime_.alive() || edit.session_ != session_ || edit.base_ != revision_)
        return Err(UiError::StaleBase);
    if (!edit.access_.valid(session_)) return Err(UiError::PropertyOwned);
    for (const auto &entry : edit.entries_)
        if (entry.children_owner.is_some() && !entry.children_owner->alive()) return Err(UiError::PropertyOwned);
    editing_ = true;
    struct Guard { bool &value; ~Guard() { value = false; } } guard{editing_};
    UiCommitReport report;
    while (slots_.len() < edit.slots_) slots_.push(Slot{});
    for (auto &entry : edit.entries_)
        if (entry.fresh.is_some()) {
            slots_[usize(entry.id.slot)].node = rstd::move(entry.fresh); ++report.created;
        }
    for (auto &entry : edit.entries_) {
        auto node = resolve(entry.id);
        entry.updated = entry.style.is_some() || entry.text.is_some() || entry.image.is_some() || !entry.states.is_empty() || !entry.patches.is_empty() || entry.children_owner.is_some() || entry.axes.is_some() || entry.children_changed || entry.relation_changed;
        if (entry.style.is_some()) { entry.previous_style = Some(node->style); node->style = *entry.style; }
        if (entry.text.is_some()) { auto previous = rstd::move(node->text); node->text = rstd::move(entry.text); entry.text = rstd::move(previous); node->paint_dirty = true; }
        if (entry.image.is_some()) { auto previous = rstd::move(node->image); node->image = rstd::move(entry.image); entry.image = rstd::move(previous); node->paint_dirty = true; }
        for (auto &state : entry.states) node->states.push(rstd::move(state));
        for (auto &patch : entry.patches) patch.value->publish(*node_state(node->states, patch.key));
        if (entry.children_owner.is_some()) node->children_owner = Some(UiTreePermit(*entry.children_owner));
        if (entry.axes.is_some()) (*node->viewport)->axes = *entry.axes;
        if (entry.relation_changed) { ++node->relation_generation; node->parent = entry.parent; }
        if (entry.children_changed) node->children = rstd::move(entry.children);
    }
    for (auto id : edit.retired_) {
        auto node = resolve(id); node->parent = None(); ++node->relation_generation;
    }
    for (auto &entry : edit.entries_) {
        const bool measured = entry.relation_changed && !entry.created && resolve(entry.id)->premeasured;
        if (entry.relation_changed && !entry.created) remount(entry.id, edit.preserved_.as_slice());
        if (entry.previous_style.is_some()) style_changed(entry.id, *resolve(entry.id), *entry.previous_style);
        if (entry.created || !entry.states.is_empty() || !entry.patches.is_empty() || entry.children_owner.is_some() || entry.text.is_some() || entry.image.is_some() || entry.axes.is_some() || entry.children_changed || (entry.relation_changed && !measured))
            changed(true, Some(entry.id));
        else if (measured) {
            if (auto parent = resolve(entry.id)->parent; parent) dirty_measure(*parent);
            layout_dirty_ = true;
            changed(false, Some(entry.id));
        }
        if (entry.updated && !entry.created) ++report.updated;
    }
    for (auto id : edit.retired_) changed(true, Some(id));
    report.retired = rstd::move(edit.retired_); report.revision = revision_;
    (*edit.retirement_)->entries = rstd::move(edit.entries_);
    report.retirement = Some(Box<UiEditRetirement>::from_raw(
        rstd::move(*edit.retirement_).into_raw().template cast<UiEditRetirement>()));
    return Ok(rstd::move(report));
}
} // namespace vui
