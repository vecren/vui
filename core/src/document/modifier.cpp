module vui;
import :document.interface;
using namespace rstd::prelude;

namespace vui {
struct UiDocument::ModifierMeasure final : ModifierMeasureContext {
    UiDocument &document;
    UiId id;
    usize index;
    Index depth;
    bool called{};
    Option<UiError> error;
    ModifierMeasure(UiDocument &owner, UiId target, usize position, Index level)
        : document(owner), id(target), index(position), depth(level) {}
    auto node_id() const -> UiId override { return id; }
    auto role() const -> NodeStateKey override { return document.resolve(id)->modifiers[index].role; }
    auto measure_content(LayoutConstraints constraints) -> Result<Size, UiError> override {
        if (called || !constraints.valid()) { error = Some(UiError::InvalidValue); return Err(*error); }
        called = true;
        auto result = document.measure_modifier(id, index + usize(1), constraints, depth);
        if (result.is_err()) { error = Some(result.unwrap_err()); return Err(*error); }
        document.resolve(id)->modifiers[index].content_desired = result.unwrap();
        return result;
    }
};

auto UiDocument::measure_modifier(UiId id, usize index, LayoutConstraints constraints, Index depth)
    -> Result<Size, UiError> {
    auto &node = *resolve(id);
    if (index == node.modifiers.len()) return measure_content(id, constraints, depth);
    auto &entry = node.modifiers[index];
    if (!entry.binding.alive()) return Err(UiError::Cancelled);
    ModifierMeasure context(*this, id, index, depth);
    auto result = entry.receiver->layout() ? entry.receiver->measure(context, constraints)
                                           : context.measure_content(constraints);
    if (context.error.is_some()) return Err(*context.error);
    if (result.is_err()) return result;
    if (!context.called || !result->valid()) return Err(UiError::InvalidValue);
    auto size = result.unwrap();
    if (size.width < constraints.min_width || size.height < constraints.min_height ||
        (constraints.max_width.is_some() && size.width > *constraints.max_width) ||
        (constraints.max_height.is_some() && size.height > *constraints.max_height))
        return Err(UiError::InvalidValue);
    entry.desired = size;
    return result;
}

struct UiDocument::ModifierArrange final : ModifierArrangeContext {
    UiDocument &document;
    UiId id;
    usize index;
    Index depth;
    Transform local_to_node;
    bool called{};
    Option<UiError> error;
    ModifierArrange(UiDocument &owner, UiId target, usize position, Index level, Transform mapping)
        : document(owner), id(target), index(position), depth(level), local_to_node(mapping) {}
    auto node_id() const -> UiId override { return id; }
    auto role() const -> NodeStateKey override { return document.resolve(id)->modifiers[index].role; }
    auto desired_content() const -> Size override { return document.resolve(id)->modifiers[index].content_desired; }
    auto place_content(LayoutPlacement placement) -> Result<empty, UiError> override {
        if (called || !placement.valid() || placement.local_to_slot.inverse().is_none()) {
            error = Some(UiError::InvalidValue); return Err(*error);
        }
        called = true;
        auto &entry = document.resolve(id)->modifiers[index];
        entry.content_placement = placement;
        Transform offset{1, 0, 0, 1, placement.slot.x, placement.slot.y};
        auto mapping = local_to_node * offset * placement.local_to_slot;
        error = document.arrange_modifier(id, index + usize(1), placement.local_size, mapping, depth);
        if (error.is_some()) return Err(*error);
        return Ok(empty{});
    }
};

auto UiDocument::arrange_modifier(UiId id, usize index, Size size, Transform mapping, Index depth)
    -> Option<UiError> {
    auto &node = *resolve(id);
    if (!size.valid() || !mapping.valid() || mapping.inverse().is_none()) return Some(UiError::InvalidValue);
    if (index == node.modifiers.len()) {
        if (node.modifier_content_size != size || node.modifier_content_transform != mapping)
            node.paint_dirty = true;
        node.modifier_content_size = size;
        node.modifier_content_transform = mapping;
        return arrange_content(id, size, depth, 0);
    }
    auto &entry = node.modifiers[index];
    if (!entry.binding.alive()) return Some(UiError::Cancelled);
    if (entry.measured != size || entry.local_to_node != mapping) node.paint_dirty = true;
    entry.measured = size;
    entry.local_to_node = mapping;
    ModifierArrange context(*this, id, index, depth, mapping);
    auto result = entry.receiver->layout() ? entry.receiver->arrange(context, size)
        : context.place_content(LayoutPlacement{{0, 0, size.width, size.height}, size, {}});
    if (context.error.is_some()) return context.error;
    if (result.is_err()) return Some(result.unwrap_err());
    return context.called ? None<UiError>() : Some(UiError::MissingChild);
}

struct UiDocument::ModifierDraw final : ModifierDrawContext {
    UiDocument &document;
    UiId id;
    Node &node;
    usize index;
    DisplayListRecorder &recorder;
    Transform mapping;
    bool &repaint;
    PaintContext context;
    bool called{};
    Option<UiError> error;
    ModifierDraw(UiDocument &owner, UiId target, Node &value, usize position,
                 DisplayListRecorder &list, Transform coordinates, bool &dirty)
        : document(owner), id(target), node(value), index(position), recorder(list),
          mapping(coordinates), repaint(dirty),
          context(target, value.modifiers[position].measured, list,
              [&] { auto style = effective_style(value); style.layout.padding = 0; return style; }(), coordinates.inverse()) {}
    auto role() const -> NodeStateKey override { return node.modifiers[index].role; }
    auto paint() -> PaintContext & override { return context; }
    auto draw_content() -> Result<empty, UiError> override {
        if (called) { error = Some(UiError::InvalidValue); return Err(*error); }
        called = true;
        auto next = index + usize(1);
        auto coordinates = next == node.modifiers.len() ? node.modifier_content_transform
                                                       : node.modifiers[next].local_to_node;
        auto inverse = mapping.inverse();
        if (inverse.is_none()) { error = Some(UiError::InvalidValue); return Err(*error); }
        recorder.save();
        recorder.transform(*inverse * coordinates);
        error = document.paint_modifier(id, node, next, recorder, coordinates, repaint);
        recorder.restore();
        if (error.is_some()) return Err(*error);
        return Ok(empty{});
    }
};

auto UiDocument::paint_modifier(UiId id, Node &node, usize index, DisplayListRecorder &recorder,
                               Transform mapping, bool &repaint) -> Option<UiError> {
    if (index == node.modifiers.len()) return paint_content(id, node, recorder, repaint);
    auto &entry = node.modifiers[index];
    if (!entry.binding.alive()) return Some(UiError::Cancelled);
    ModifierDraw context(*this, id, node, index, recorder, mapping, repaint);
    auto result = entry.receiver->drawing() ? entry.receiver->draw(context) : context.draw_content();
    repaint |= context.context.repaint_requested();
    if (context.error.is_some()) return context.error;
    return result.is_err() ? Some(result.unwrap_err()) : None<UiError>();
}

auto UiDocument::register_modifier(UiId id, NodeStateKey role, BindingGuard binding,
                                  Box<ModifierReceiver> receiver) -> Result<empty, UiError> {
    if (!can_route()) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (!binding.alive()) return Err(UiError::Cancelled);
    usize active{};
    for (const auto &entry : node->modifiers) if (entry.binding.alive()) {
        ++active;
        if (entry.role == role) return Err(UiError::InvalidValue);
    }
    if (active >= usize(256)) return Err(UiError::Limit);
    for (usize i{}; i < node->modifiers.len();) {
        auto &entry = node->modifiers[i];
        if (!entry.binding.alive()) node->modifiers.remove(i);
        else ++i;
    }
    node->modifiers.push(DocumentModifier{role, rstd::move(binding), rstd::move(receiver)});
    node->paint_dirty = true;
    changed(true, Some(id));
    track_binding(id);
    return Ok(empty{});
}

auto UiDocument::set_modifier_order(UiId id, Vec<NodeStateKey> order) -> Result<empty, UiError> {
    if (!can_route()) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    for (usize i{}; i < order.len(); ++i) {
        for (usize j{}; j < i; ++j) if (order[i] == order[j]) return Err(UiError::InvalidValue);
        bool found{};
        for (const auto &entry : node->modifiers)
            found |= entry.role == order[i] && entry.binding.alive();
        if (!found) return Err(UiError::InvalidValue);
    }
    usize active{};
    for (const auto &entry : node->modifiers) if (entry.binding.alive()) ++active;
    if (active != order.len()) return Err(UiError::InvalidValue);
    bool reordered = node->modifiers.len() != order.len();
    if (!reordered) for (usize i{}; i < order.len(); ++i) reordered |= node->modifiers[i].role != order[i];
    if (!reordered) return Ok(empty{});
    Vec<DocumentModifier> entries;
    for (auto role : order) for (usize i{}; i < node->modifiers.len(); ++i)
        if (node->modifiers[i].role == role) {
            auto entry = node->modifiers.remove(i);
            entries.push(rstd::move(entry));
            break;
        }
    node->modifiers = rstd::move(entries);
    node->paint_dirty = true;
    changed(true, Some(id));
    return Ok(empty{});
}

auto UiDocument::invalidate_modifier(UiId id, NodeStateKey role) -> Result<empty, UiError> {
    if (!can_route()) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    for (const auto &entry : node->modifiers) if (entry.role == role && entry.binding.alive()) {
        node->paint_dirty = true;
        changed(entry.receiver->layout(), Some(id));
        return Ok(empty{});
    }
    return Err(UiError::InvalidValue);
}

auto UiDocument::modifier_geometry(UiId id, NodeStateKey role) -> Result<ModifierGeometry, UiError> {
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (layout_dirty_ || !mounted(id)) return Err(UiError::LayoutPending);
    for (const auto &entry : node->modifiers) if (entry.role == role && entry.binding.alive())
        return Ok(ModifierGeometry{entry.measured, entry.local_to_node});
    return Err(UiError::InvalidValue);
}

auto UiDocument::content_geometry(UiId id) -> Result<ModifierGeometry, UiError> {
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (layout_dirty_ || !mounted(id)) return Err(UiError::LayoutPending);
    return Ok(node->modifiers.is_empty() ? ModifierGeometry{node->measured, {}}
                                       : ModifierGeometry{node->modifier_content_size, node->modifier_content_transform});
}
}
