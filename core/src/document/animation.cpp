module vui;
import :document.interface;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto UiDocument::animation_index(AnimationId id) const -> Option<usize> {
    for (usize i{}; i < animations_.len(); ++i)
        if (animations_[i].state.id == id)
            return Some(i);
    return None();
}

auto UiDocument::animation_reservations() const -> usize {
    return animations_.len() + animation_completions_.len() + animation_events_.len() +
           dispatching_animation_count_;
}

void UiDocument::apply_overlay(UiId id, Node &node, AnimationProperty property,
                   const AnimationValue *value) {
    auto before = effective_style(node);
    switch (property) {
    case AnimationProperty::Transform:
        node.animated.transform =
            value ? Some(Transform(value->as_Matrix().value)) : None<Transform>();
        break;
    case AnimationProperty::Opacity:
        node.animated.opacity = value ? Some(Scalar(value->as_Number().value)) : None<Scalar>();
        break;
    case AnimationProperty::Width:
        node.animated.width = value ? Some(Scalar(value->as_Number().value)) : None<Scalar>();
        break;
    case AnimationProperty::Height:
        node.animated.height = value ? Some(Scalar(value->as_Number().value)) : None<Scalar>();
        break;
    case AnimationProperty::Background:
        node.animated.background = value ? Some(Color(value->as_Tint().value)) : None<Color>();
        break;
    }
    auto after = effective_style(node);
    if (before.background != after.background)
        node.paint_dirty = true;
    if (before.transform != after.transform || before.opacity != after.opacity ||
        before.background != after.background || before.layout != after.layout)
        changed(before.layout != after.layout, Some(id));
}

void UiDocument::end_animation(usize index, AnimationEndReason reason, bool clear_overlay) {
    auto track = animations_.remove(index);
    if (auto node = resolve(track.state.target)) {
        if (reason == AnimationEndReason::Finished &&
            track.state.options.end == AnimationEndPolicy::CommitFinal) {
            const auto &value = track.state.target_value;
            switch (track.state.property) {
            case AnimationProperty::Transform:
                node->style.transform = value.as_Matrix().value;
                break;
            case AnimationProperty::Opacity:
                node->style.opacity = value.as_Number().value;
                break;
            case AnimationProperty::Width:
                node->style.layout.width = Some(Scalar(value.as_Number().value));
                break;
            case AnimationProperty::Height:
                node->style.layout.height = Some(Scalar(value.as_Number().value));
                break;
            case AnimationProperty::Background:
                node->style.background = value.as_Tint().value;
                break;
            }
        }
        if (clear_overlay)
            apply_overlay(track.state.target, *node, track.state.property, nullptr);
    }
    animation_completions_.push(AnimationCompletion{
        {track.state.id, track.state.target, track.state.property, reason,
         animation_time_.is_some() ? *animation_time_ : 0, track.state.mount_generation},
        rstd::move(track.callback)});
}

void UiDocument::detach_animations(UiId id) {
    for (auto i = animations_.len(); i > usize(); --i)
        if (animations_[i - usize(1)].state.target == id)
            end_animation(i - usize(1), AnimationEndReason::TargetDetached);
}

auto UiDocument::start_animation(UiId id, AnimationProperty property, AnimationValue to,
                     AnimationOptions options) -> Result<AnimationId, AnimationError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(AnimationError::Reentrant);
    if (animations_closed_)
        return Err(AnimationError::Closed);
    auto node = resolve(id);
    if (!node || !mounted(id))
        return Err(AnimationError::InvalidTarget);
    if (!options.valid() || !to.valid(property))
        return Err(AnimationError::InvalidValue);
    if (animation_reservations() >= usize(4096) || next_animation_ == ~Serial{})
        return Err(AnimationError::Limit);
    revoke_property(*node, static_cast<PresentationProperty>(static_cast<unsigned>(property)));
    auto style = effective_style(*node);
    auto from = AnimationValue::Number(0);
    switch (property) {
    case AnimationProperty::Transform:
        from = AnimationValue::Matrix(style.transform);
        break;
    case AnimationProperty::Opacity:
        from = AnimationValue::Number(style.opacity);
        break;
    case AnimationProperty::Background:
        from = AnimationValue::Tint(style.background);
        break;
    case AnimationProperty::Width:
        if (style.layout.width.is_none() && layout_dirty_)
            return Err(AnimationError::LayoutPending);
        from = AnimationValue::Number(style.layout.width.is_some() ? *style.layout.width
                                                                   : node->measured.width);
        break;
    case AnimationProperty::Height:
        if (style.layout.height.is_none() && layout_dirty_)
            return Err(AnimationError::LayoutPending);
        from = AnimationValue::Number(style.layout.height.is_some() ? *style.layout.height
                                                                    : node->measured.height);
        break;
    }
    for (usize i{}; i < animations_.len(); ++i)
        if (animations_[i].state.target == id && animations_[i].state.property == property) {
            end_animation(i, AnimationEndReason::Replaced, false);
            break;
        }
    AnimationId handle{session_, next_animation_++};
    animations_.push(
        RunningAnimation{{handle, id, property, AnimationValue(from), rstd::move(to), options,
                          animation_time_, node->mount_generation},
                         AnimationValue(from),
                         {}});
    apply_overlay(id, *node, property, &from);
    return Ok(handle);
}

auto UiDocument::animate_transform(UiId id, Transform to, AnimationOptions options)
    -> Result<AnimationId, AnimationError> {
    return start_animation(id, AnimationProperty::Transform, AnimationValue::Matrix(to),
                           options);
}

auto UiDocument::animate_opacity(UiId id, Scalar to, AnimationOptions options)
    -> Result<AnimationId, AnimationError> {
    return start_animation(id, AnimationProperty::Opacity, AnimationValue::Number(to), options);
}

auto UiDocument::animate_width(UiId id, Scalar to, AnimationOptions options)
    -> Result<AnimationId, AnimationError> {
    return start_animation(id, AnimationProperty::Width, AnimationValue::Number(to), options);
}

auto UiDocument::animate_height(UiId id, Scalar to, AnimationOptions options)
    -> Result<AnimationId, AnimationError> {
    return start_animation(id, AnimationProperty::Height, AnimationValue::Number(to), options);
}

auto UiDocument::animate_background(UiId id, Color to, AnimationOptions options)
    -> Result<AnimationId, AnimationError> {
    return start_animation(id, AnimationProperty::Background, AnimationValue::Tint(to),
                           options);
}

auto UiDocument::animation_state(AnimationId id) const -> Result<AnimationState, AnimationError> {
    auto index = animation_index(id);
    if (index.is_none())
        return Err(AnimationError::InvalidHandle);
    return Ok(AnimationState(animations_[*index].state));
}

auto UiDocument::retarget_animation(AnimationId id, AnimationValue to, AnimationOptions options)
    -> Result<empty, AnimationError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(AnimationError::Reentrant);
    if (animations_closed_)
        return Err(AnimationError::Closed);
    auto index = animation_index(id);
    if (index.is_none())
        return Err(AnimationError::InvalidHandle);
    auto &track = animations_[*index];
    if (!options.valid() || !to.valid(track.state.property))
        return Err(AnimationError::InvalidValue);
    track.from = track.state.value;
    track.state.target_value = rstd::move(to);
    track.state.options = options;
    track.state.started_ns = animation_time_;
    return Ok(empty{});
}

auto UiDocument::retarget_animation(AnimationId id, AnimationValue to) -> Result<empty, AnimationError> {
    auto index = animation_index(id);
    if (index.is_none())
        return Err(AnimationError::InvalidHandle);
    return retarget_animation(id, rstd::move(to), animations_[*index].state.options);
}

auto UiDocument::cancel_animation(AnimationId id) -> Result<empty, AnimationError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(AnimationError::Reentrant);
    auto index = animation_index(id);
    if (index.is_none())
        return Err(AnimationError::InvalidHandle);
    end_animation(*index, AnimationEndReason::Cancelled);
    return Ok(empty{});
}

auto UiDocument::poll_animation_events() -> Vec<AnimationEvent> {
    return rstd::exchange(animation_events_, Vec<AnimationEvent>{});
}

auto UiDocument::sample_animations(Serial time) -> Result<empty, AnimationError> {
    if (!can_route())
        return Err(AnimationError::Reentrant);
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(AnimationError::Reentrant);
    if (animations_closed_)
        return Ok(empty{});
    if (animation_time_.is_some() && time < *animation_time_)
        time = *animation_time_;
    animation_time_ = Some(time);
    for (usize i{}; i < animations_.len();) {
        auto &track = animations_[i];
        if (track.state.started_ns.is_none())
            track.state.started_ns = Some(time);
        auto elapsed = time - *track.state.started_ns;
        auto duration = track.state.options.duration_ns;
        bool finished = elapsed >= duration;
        Scalar t = finished ? 1 : Scalar(elapsed) / Scalar(duration);
        if (track.state.options.easing == AnimationEasing::EaseInOut)
            t = t * t * (3 - 2 * t);
        track.state.value = track.from.interpolate(track.state.target_value, t).unwrap();
        if (!finished || track.state.options.end == AnimationEndPolicy::CommitFinal)
            apply_overlay(track.state.target, *resolve(track.state.target),
                          track.state.property, &track.state.value);
        if (finished)
            end_animation(i, AnimationEndReason::Finished);
        else
            ++i;
    }
    return Ok(empty{});
}

auto UiDocument::dispatch_animation_callbacks() -> Result<empty, AnimationError> {
    if (routing_ || draining_ || animation_dispatching_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(AnimationError::Reentrant);
    animation_dispatching_ = true;
    auto pending = rstd::exchange(animation_completions_, Vec<AnimationCompletion>{});
    dispatching_animation_count_ = pending.len();
    for (auto &completion : pending) {
        --dispatching_animation_count_;
        animation_events_.push(AnimationEvent(completion.event));
        if (completion.callback.is_some())
            (*completion.callback)->invoke(*this, completion.event);
    }
    animation_dispatching_ = false;
    return Ok(empty{});
}

auto UiDocument::close_animations() -> Result<empty, AnimationError> {
    if (!can_route())
        return Err(AnimationError::Reentrant);
    animations_closed_ = true;
    while (!animations_.is_empty())
        end_animation(usize(), AnimationEndReason::Cancelled);
    return dispatch_animation_callbacks();
}
} // namespace vui
