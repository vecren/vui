module vui;
import :document.interface;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto UiDocument::validate_style(Node &node, const NodeStylePatch &patch) -> Result<NodeStyle, UiError> {
    if (!patch.valid()) return Err(UiError::InvalidValue);
    auto value = patch.apply(node.style);
    if (!value.valid() || (node.viewport.is_some() && value.clip.enabled)) return Err(UiError::InvalidValue);
    const auto &old = node.style;
    if ((value.layout.width != old.layout.width && property_occupied(node, PresentationProperty::Width)) ||
        (value.layout.height != old.layout.height && property_occupied(node, PresentationProperty::Height)) ||
        (value.transform != old.transform && property_occupied(node, PresentationProperty::Transform)) ||
        (value.opacity != old.opacity && property_occupied(node, PresentationProperty::Opacity)) ||
        ((value.background != old.background || value.radius != old.radius) && property_occupied(node, PresentationProperty::Background)) ||
        (value.clip != old.clip && property_occupied(node, PresentationProperty::Clip)) ||
        (value.content_tint != old.content_tint && property_occupied(node, PresentationProperty::ContentTint)) ||
        (value.paint_color != old.paint_color && property_occupied(node, PresentationProperty::PaintColor)) ||
        (value.visible != old.visible && (property_occupied(node, PresentationProperty::Visible) || value_occupied(node, UiValueProperty::Visible))))
        return Err(UiError::PropertyOwned);
    return Ok(rstd::move(value));
}

void UiDocument::style_changed(UiId id, Node &node, const NodeStyle &base) {
    if (base == node.style) return;
    auto before = effective_style(node, base);
    auto after = effective_style(node);
    if (before == after) { changed(false); return; }
    if (before.background != after.background || before.radius != after.radius || before.layout.padding != after.layout.padding)
        node.paint_dirty = true;
    if (before.layout != after.layout) {
        auto dimensions = before.layout;
        dimensions.horizontal = after.layout.horizontal; dimensions.vertical = after.layout.vertical;
        if (dimensions == after.layout && before.layout.horizontal != Alignment::Stretch && after.layout.horizontal != Alignment::Stretch &&
            before.layout.vertical != Alignment::Stretch && after.layout.vertical != Alignment::Stretch) {
            dirty_arrange(id); layout_dirty_ = true; changed(false, Some(id));
        } else changed(true, Some(id));
    } else changed(false, Some(id));
}

auto UiDocument::base_style(UiId id) -> Result<NodeStyle, UiError> {
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    return Ok(NodeStyle(node->style));
}

auto UiDocument::resolved_style(UiId id) -> Result<NodeStyle, UiError> {
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    return Ok(effective_style(*node));
}

auto UiDocument::patch_style(UiId id, const NodeStylePatch &patch) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node = resolve(id); if (!node) return Err(UiError::InvalidId);
    auto checked = validate_style(*node, patch); if (checked.is_err()) return Err(checked.unwrap_err());
    auto before = node->style; node->style = checked.unwrap(); style_changed(id, *node, before);
    return Ok(empty{});
}

auto UiDocument::set_layout(UiId id, LayoutStyle value) -> Result<empty, UiError> {
    return patch_style(id, NodeStylePatch::layout(rstd::move(value)));
}

auto UiDocument::set_background(UiId id, Color value, Scalar radius) -> Result<empty, UiError> {
    NodeStyle style; style.background = value; style.radius = radius;
    return patch_style(id, NodeStylePatch::select(style, {NodeStyleField::Background, NodeStyleField::Radius}));
}

auto UiDocument::set_transform(UiId id, Transform value) -> Result<empty, UiError> {
    NodeStyle style; style.transform = value;
    return patch_style(id, NodeStylePatch::select(style, {NodeStyleField::Transform}));
}

auto UiDocument::set_opacity(UiId id, Scalar value) -> Result<empty, UiError> {
    NodeStyle style; style.opacity = value;
    return patch_style(id, NodeStylePatch::select(style, {NodeStyleField::Opacity}));
}

auto UiDocument::set_content_tint(UiId id, Color value) -> Result<empty, UiError> {
    NodeStyle style; style.content_tint = value;
    return patch_style(id, NodeStylePatch::select(style, {NodeStyleField::ContentTint}));
}

auto UiDocument::set_paint_color(UiId id, Color value) -> Result<empty, UiError> {
    NodeStyle style; style.paint_color = value;
    return patch_style(id, NodeStylePatch::select(style, {NodeStyleField::PaintColor}));
}

auto UiDocument::set_clip(UiId id, Clip value) -> Result<empty, UiError> {
    NodeStyle style; style.clip = value;
    return patch_style(id, NodeStylePatch::select(style, {NodeStyleField::Clip}));
}

auto UiDocument::set_visible(UiId id, bool value) -> Result<empty, UiError> {
    NodeStyle style; style.visible = value;
    return patch_style(id, NodeStylePatch::select(style, {NodeStyleField::Visible}));
}

auto UiDocument::set_hit_self(UiId id, bool value) -> Result<empty, UiError> {
    NodeStyle style; style.hit_self = value;
    return patch_style(id, NodeStylePatch::select(style, {NodeStyleField::HitSelf}));
}

auto UiDocument::set_text_input_area(UiId id, Option<Rect> area) -> Result<empty, UiError> {
    NodeStyle style; style.text_input_area = area;
    return patch_style(id, NodeStylePatch::select(style, {NodeStyleField::TextInputArea}));
}

auto UiDocument::set_interaction(UiId id, bool hit_test_visible, bool enabled, bool focusable) -> Result<empty, UiError> {
    NodeStyle style; style.hit_test_visible = hit_test_visible; style.enabled = enabled; style.focusable = focusable;
    return patch_style(id, NodeStylePatch::select(style, {NodeStyleField::HitTestVisible, NodeStyleField::Enabled, NodeStyleField::Focusable}));
}

void UiDocument::revoke_property(Node &node, PresentationProperty property) {
    auto owner = node.property_owner[property_index(property)].upgrade();
    if (owner.is_some())
        owner->get()->active = false;
    node.property_owner[property_index(property)] = {};
}

auto UiDocument::read_property(UiId id, Node &node, PresentationProperty property, bool base)
    -> PropertyWrite {
    auto style = base ? node.style : effective_style(node);
    PropertyWrite result{{id, property}};
    switch (property) {
    case PresentationProperty::Transform:
        result.value = AnimationValue::Matrix(style.transform);
        break;
    case PresentationProperty::Opacity:
        result.value = AnimationValue::Number(style.opacity);
        break;
    case PresentationProperty::Width:
        result.value = AnimationValue::Number(
            style.layout.width.is_some() ? *style.layout.width : node.measured.width);
        result.automatic_size = style.layout.width.is_none();
        break;
    case PresentationProperty::Height:
        result.value = AnimationValue::Number(
            style.layout.height.is_some() ? *style.layout.height : node.measured.height);
        result.automatic_size = style.layout.height.is_none();
        break;
    case PresentationProperty::Background:
        result.value = AnimationValue::Tint(style.background);
        break;
    case PresentationProperty::ContentTint:
        result.value = AnimationValue::Tint(style.content_tint);
        break;
    case PresentationProperty::PaintColor:
        result.value = AnimationValue::Tint(style.paint_color);
        break;
    case PresentationProperty::CharacterSpacing:
        result.value = AnimationValue::Number(node.text.is_some() ? (*node.text)->character_spacing : Scalar(0));
        break;
    case PresentationProperty::TextColor:
        result.value = AnimationValue::Tint(node.text.is_some() ? (*node.text)->color : Color{});
        break;
    case PresentationProperty::Visible:
        result.value = AnimationValue::Number(style.visible ? 1 : 0);
        break;
    case PresentationProperty::Count:
        break;
    case PresentationProperty::Clip:
        result.clip = style.clip;
        break;
    case PresentationProperty::ViewportOffset:
        result.point = node.viewport.is_some() ? (*node.viewport)->requested : Point{};
        break;
    }
    return result;
}

auto UiDocument::valid_property(const PropertyWrite &write, const Node &node) const -> bool {
    auto property = write.key.property;
    if (property_index(property) >= static_cast<unsigned>(PresentationProperty::Count))
        return false;
    if (write.automatic_size)
        return property == PresentationProperty::Width ||
               property == PresentationProperty::Height;
    if (property == PresentationProperty::ContentTint || property == PresentationProperty::PaintColor)
        return write.value.is_Tint() && write.value.as_Tint().value.valid();
    if (property == PresentationProperty::CharacterSpacing)
        return node.text.is_some() && write.value.is_Number() && finite(write.value.as_Number().value);
    if (property == PresentationProperty::TextColor)
        return node.text.is_some() && write.value.is_Tint() && write.value.as_Tint().value.valid();
    if (property == PresentationProperty::Visible)
        return write.value.is_Number() && (write.value.as_Number().value == 0 || write.value.as_Number().value == 1);
    if (property == PresentationProperty::Clip)
        return write.clip.valid() && node.viewport.is_none();
    if (property == PresentationProperty::ViewportOffset)
        return node.viewport.is_some() && finite(write.point.x) && finite(write.point.y);
    return write.value.valid(static_cast<AnimationProperty>(property_index(property)));
}

void UiDocument::apply_property(Node &node, const PropertyWrite &write) {
    auto before = effective_style(node);
    auto offset = node.viewport.is_some() ? (*node.viewport)->metrics.offset : Point{};
    switch (write.key.property) {
    case PresentationProperty::Transform:
        node.style.transform = write.value.as_Matrix().value;
        break;
    case PresentationProperty::Opacity:
        node.style.opacity = write.value.as_Number().value;
        break;
    case PresentationProperty::Width:
        node.style.layout.width =
            write.automatic_size ? None<Scalar>() : Some(Scalar(write.value.as_Number().value));
        break;
    case PresentationProperty::Height:
        node.style.layout.height =
            write.automatic_size ? None<Scalar>() : Some(Scalar(write.value.as_Number().value));
        break;
    case PresentationProperty::Background:
        node.style.background = write.value.as_Tint().value;
        break;
    case PresentationProperty::ContentTint:
        node.style.content_tint = write.value.as_Tint().value;
        break;
    case PresentationProperty::PaintColor:
        node.style.paint_color = write.value.as_Tint().value;
        break;
    case PresentationProperty::CharacterSpacing:
        write_character_spacing(write.key.target, node, write.value.as_Number().value);
        return;
    case PresentationProperty::TextColor:
        write_text_color(write.key.target, node, write.value.as_Tint().value);
        return;
    case PresentationProperty::Visible:
        write_visible(write.key.target, node, write.value.as_Number().value == 1);
        return;
    case PresentationProperty::Count:
        break;
    case PresentationProperty::Clip:
        node.style.clip = write.clip;
        break;
    case PresentationProperty::ViewportOffset: {
        auto &viewport = **node.viewport;
        viewport.requested = write.point;
        viewport.metrics.offset = viewport_offset(viewport,write.point);
        break;
    }
    }
    auto after = effective_style(node);
    auto next_offset = node.viewport.is_some() ? (*node.viewport)->metrics.offset : Point{};
    if (before.background != after.background)
        node.paint_dirty = true;
    if (before.transform != after.transform || before.opacity != after.opacity ||
        before.background != after.background || before.clip != after.clip ||
        before.layout != after.layout || before.content_tint != after.content_tint || before.paint_color != after.paint_color || offset != next_offset)
        changed(before.layout != after.layout, Some(UiId(write.key.target)));
}

auto UiDocument::take_over(slice<PropertyKey> keys, TakeoverPolicy policy)
    -> Result<PresentationLease, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_)
        return Err(UiError::Routing);
    if (keys.is_empty() || keys.len() > usize(256) || !lease_owner_.alive())
        return Err(UiError::InvalidValue);
    if (next_lease_ == ~Serial{})
        return Err(UiError::Limit);
    for (usize i{}; i < keys.len(); ++i) {
        auto node = resolve(keys[i].target);
        if (!node || !mounted(keys[i].target))
            return Err(UiError::InvalidId);
        if (property_index(keys[i].property) >= static_cast<unsigned>(PresentationProperty::Count))
            return Err(UiError::InvalidValue);
        if ((keys[i].property == PresentationProperty::TextColor || keys[i].property == PresentationProperty::CharacterSpacing) && node->text.is_none())
            return Err(UiError::InvalidValue);
        if (keys[i].property == PresentationProperty::Visible && value_occupied(*node, UiValueProperty::Visible))
            return Err(UiError::PropertyOwned);
        for (usize j{}; j < i; ++j)
            if (keys[j] == keys[i])
                return Err(UiError::InvalidValue);
        if ((keys[i].property == PresentationProperty::Width ||
             keys[i].property == PresentationProperty::Height) &&
            layout_dirty_)
            return Err(UiError::LayoutPending);
        if (keys[i].property == PresentationProperty::ViewportOffset &&
            node->viewport.is_none())
            return Err(UiError::InvalidValue);
        if (keys[i].property == PresentationProperty::Clip && node->viewport.is_some())
            return Err(UiError::InvalidValue);
        if (policy == TakeoverPolicy::RejectOccupied) {
            if (property_occupied(*node, keys[i].property))
                return Err(UiError::PropertyOwned);
            for (const auto &animation : animations_)
                if (animation.state.target == keys[i].target &&
                    property_index(keys[i].property) ==
                        static_cast<unsigned>(animation.state.property))
                    return Err(UiError::PropertyOwned);
        }
    }
    auto state = rstd::rc::make_rc<PropertyLeaseState>();
    auto stored = state.get();
    stored->document = session_;
    stored->id = next_lease_++;
    stored->owner = lease_owner_.guard();
    for (const auto &key : keys) {
        auto &node = *resolve(key.target);
        stored->keys.push(PropertyKey(key));
        stored->mounts.push(Serial(node.mount_generation));
        stored->originals.push(read_property(key.target, node, key.property, true));
        auto current = read_property(key.target, node, key.property);
        stored->current.push(PropertyWrite(current));
        revoke_property(node, key.property);
        for (auto i = animations_.len(); i > usize(); --i) {
            auto &animation = animations_[i - usize(1)];
            if (animation.state.target == key.target &&
                property_index(key.property) == static_cast<unsigned>(animation.state.property))
                end_animation(i - usize(1), AnimationEndReason::Replaced, true);
        }
        apply_property(node, current);
        node.property_owner[property_index(key.property)] = state.downgrade();
    }
    return Ok(PresentationLease(rstd::move(state)));
}

auto UiDocument::write_presentation(PresentationLease &lease, const PresentationBatch &batch,
                        Option<BindingGuard> driver) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_)
        return Err(UiError::Routing);
    if (!lease.active() || lease.state_.get()->document != session_)
        return Err(UiError::PropertyOwned);
    if (driver.is_some() && !driver->alive())
        return Err(UiError::PropertyOwned);
    auto state = lease.state_.get();
    if (driver.is_some() && state->driver.is_some() && !driver->same_binding(*state->driver))
        return Err(UiError::PropertyOwned);
    for (usize i{}; i < state->keys.len(); ++i) {
        auto node = resolve(state->keys[i].target);
        if (!node || node->mount_generation != state->mounts[i])
            return Err(UiError::InvalidId);
        auto owner = node->property_owner[property_index(state->keys[i].property)].upgrade();
        if (owner.is_none() || owner->get() != state)
            return Err(UiError::PropertyOwned);
    }
    for (usize i{}; i < batch.writes().len(); ++i) {
        const auto &write = batch.writes()[i];
        auto node = resolve(write.key.target);
        if (!node || !valid_property(write, *node))
            return Err(UiError::InvalidValue);
        bool found{};
        for (const auto &key : state->keys)
            found |= key == write.key;
        if (!found)
            return Err(UiError::PropertyOwned);
        for (usize j{}; j < i; ++j)
            if (batch.writes()[j].key == write.key)
                return Err(UiError::InvalidValue);
    }
    if (driver.is_some())
        state->driver = rstd::move(driver);
    for (const auto &write : batch.writes()) {
        auto &node = *resolve(write.key.target);
        apply_property(node, write);
        for (auto &current : state->current)
            if (current.key == write.key)
                current = write;
    }
    return Ok(empty{});
}

auto UiDocument::set_presentation(const PresentationBatch &batch) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    if (batch.writes().len()>usize(256)) return Err(UiError::Limit);
    for (usize i{};i<batch.writes().len();++i) {
        const auto &write=batch.writes()[i];auto node=resolve(write.key.target);
        if (!node) return Err(UiError::InvalidId);
        if (!valid_property(write,*node)) return Err(UiError::InvalidValue);
        if (property_occupied(*node,write.key.property)) return Err(UiError::PropertyOwned);
        if (write.key.property==PresentationProperty::Visible && value_occupied(*node,UiValueProperty::Visible)) return Err(UiError::PropertyOwned);
        for (usize j{};j<i;++j) if (batch.writes()[j].key==write.key) return Err(UiError::InvalidValue);
    }
    for (const auto &write:batch.writes()) apply_property(*resolve(write.key.target),write);
    return Ok(empty{});
}

auto UiDocument::release_presentation(PresentationLease &lease,
                          LeaseEndPolicy policy)
    -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_)
        return Err(UiError::Routing);
    if (!lease.active() || lease.state_.get()->document != session_)
        return Err(UiError::PropertyOwned);
    if (policy == LeaseEndPolicy::RestoreBase) {
        for (const auto &write : lease.state_.get()->originals)
            if (!resolve(write.key.target))
                return Err(UiError::InvalidId);
        for (const auto &write : lease.state_.get()->originals)
            apply_property(*resolve(write.key.target), write);
    }
    lease.revoke();
    return Ok(empty{});
}

void UiDocument::notify_targets() {
    Vec<rstd::rc::Rc<UiTargetState>> changed_targets;
    for (auto &weak : value_targets_) {
        auto target = weak.upgrade();
        if (target.is_none())
            continue;
        auto &state = *target->get();
        auto node = resolve(state.node);
        UiTargetStatus next{node != nullptr, node && mounted(state.node),
                            node ? node->mount_generation : 0};
        if (state.status != next) {
            state.status = next;
            changed_targets.push(rstd::move(*target));
        }
    }
    value_targets_.retain([](const auto &weak) { return !weak.expired(); });
    for (auto &target : changed_targets) {
        auto &state = *target.get();
        Vec<rstd::rc::Rc<UiTargetObserver>> callbacks;
        for (auto &observer : state.observers)
            if (observer->guard.alive())
                callbacks.push(observer.clone());
        for (auto &observer : callbacks)
            if (observer->guard.alive())
                observer.get()->callback->operator()(state.status);
        state.observers.retain([](const auto &observer) { return observer->guard.alive(); });
    }
}

auto UiDocument::value_occupied(Node &node, UiValueProperty property) -> bool {
    auto writer = node.value_writer[unsigned(property)].upgrade();
    return writer.is_some() && (*writer)->active;
}

auto UiDocument::write_visible(UiId id, Node &node, bool value) -> bool {
    if (node.style.visible == value)
        return false;
    node.style.visible = value;
    changed(false, Some(id));
    return true;
}

auto UiDocument::text_target(UiId id) -> Result<UiPropertyTarget<String>, UiError> {
    return value_target<String>(id, UiValueProperty::Text);
}

auto UiDocument::visible_target(UiId id) -> Result<UiPropertyTarget<bool>, UiError> {
    return value_target<bool>(id, UiValueProperty::Visible);
}

auto UiDocument::image_target(UiId id) -> Result<UiPropertyTarget<Option<ImageData>>, UiError> {
    return value_target<Option<ImageData>>(id, UiValueProperty::Image);
}

auto UiDocument::apply_values(UiValueBatch batch) -> Result<usize, UiValueBatchError> {
    if (layout_active_ || painting_ || evaluating_ || editing_)
        return Err(UiValueBatchError{usize(), UiError::Routing});
    if (batch.size() > usize(4096))
        return Err(UiValueBatchError{usize(), UiError::Limit});
    for (usize i{}; i < batch.entries_.len(); ++i) {
        const auto &entry = batch.entries_[i];
        if (!entry.writer || !entry.writer->active)
            return Err(UiValueBatchError{i, UiError::Cancelled});
        const auto &target = *entry.writer->target;
        auto node = resolve(target.node);
        if (target.document != session_ || !node)
            return Err(UiValueBatchError{i, UiError::InvalidId});
        if (!mounted(target.node) || node->mount_generation != entry.generation)
            return Err(UiValueBatchError{i, UiError::Cancelled});
        auto owner = node->value_writer[unsigned(target.property)].upgrade();
        if (owner.is_none() || owner->get() != entry.writer.get())
            return Err(UiValueBatchError{i, UiError::PropertyOwned});
    }
    usize changed_count{};
    for (auto &entry : batch.entries_) {
        const auto &target = *entry.writer->target;
        auto node = resolve(target.node);
        switch (target.property) {
        case UiValueProperty::Text:
            changed_count += usize(write_text(target.node, *node, rstd::move(entry.text)));
            break;
        case UiValueProperty::Visible:
            changed_count += usize(write_visible(target.node, *node, entry.visible));
            break;
        case UiValueProperty::Image:
            changed_count += usize(write_image(target.node, *node, rstd::move(entry.image)));
            break;
        }
    }
    return Ok(changed_count);
}
} // namespace vui
