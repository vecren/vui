module vui;
import :document.interface;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto UiDocument::make_visual(UiId id) -> Result<UiVisual, UiError> {
    auto &node = *resolve(id);
    Transform placement;
    placement.tx = node.position.x;
    placement.ty = node.position.y;
    if (node.parent.is_some()) {
        auto parent = resolve(*node.parent);
        if (parent->viewport.is_some()) {
            placement.tx -= (*parent->viewport)->metrics.offset.x;
            placement.ty -= (*parent->viewport)->metrics.offset.y;
        }
    }
    auto clip = node.style.clip;
    if (node.viewport.is_some()) {
        auto size = (*node.viewport)->metrics.viewport;
        clip = {true,
                {node.style.layout.padding, node.style.layout.padding, size.width, size.height},
                0};
    }
    Rect layout_bounds{0, 0, node.measured.width, node.measured.height};
    auto content_bounds = layout_bounds;
    if (auto extent = node.content.bounds(); extent.is_some()) {
        auto left = rstd::cmp::min(Scalar(0), extent->x),
             top = rstd::cmp::min(Scalar(0), extent->y);
        content_bounds = {
            left, top, rstd::cmp::max(layout_bounds.width, extent->x + extent->width) - left,
            rstd::cmp::max(layout_bounds.height, extent->y + extent->height) - top};
    }
    auto style = effective_style(node);
    auto transform = node.layout_mapping == Transform{}
                         ? placement * style.transform
                         : placement * node.layout_mapping * style.transform;
    RenderProperties properties{transform,     content_bounds, clip,
                                style.opacity, style.visible,  node.layout_clip, style.content_tint, style.paint_color};
    if (!properties.valid())
        return Err(UiError::InvalidValue);
    UiVisual visual{
        id,
        properties,
        node.content,
        {},
        node.style.hit_test_visible,
        node.style.enabled,
        node.style.focusable,
        node.parent,
        node.mount_generation,
        node.viewport.is_some() ? Some(ViewportMetrics((*node.viewport)->metrics))
                                : None<ViewportMetrics>(),
        layout_bounds,
        {node.position.x, node.position.y, node.measured.width, node.measured.height},
        node.arranged_slot,
        node.style.hit_self,
        node.layout_has_text_input_area ? node.layout_text_input_area : node.style.text_input_area};
    for (auto child : node.children)
        visual.children.push(UiId(child));
    return Ok(rstd::move(visual));
}

auto UiDocument::collect(UiId id, UiUpdate::Data &output, Index depth, bool recording)
    -> Option<UiError> {
    ++stats_.collected_nodes;
    if (depth >= depth_limit_)
        return Some(UiError::Limit);
    auto &node = *resolve(id);
    if (node.constraint_conflict)
        layout_diagnostics_.push(LayoutDiagnostic{id, LayoutIssue::ConstraintConflict});
    if (recording && node.paint_dirty) {
        auto error = paint(id, node);
        if (error.is_some())
            return error;
    }
    auto built = make_visual(id);
    if (built.is_err())
        return Some(built.unwrap_err());
    auto visual = rstd::move(built).unwrap();
    output.indices[usize(id.slot)] = static_cast<Index>(output.visuals.len().to_primitive());
    output.visuals.push(rstd::move(visual));
    for (auto child : node.children) {
        auto error = collect(child, output, depth + 1, recording);
        if (error.is_some())
            return error;
    }
    return None();
}

auto UiDocument::update_impl(Size viewport, bool recording) -> Result<UiUpdate::Data, UiError> {
    if (routing_ || animation_dispatching_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    if (!viewport.valid())
        return Err(UiError::InvalidValue);
    if (viewport != viewport_) {
        viewport_ = viewport;
        changed(true, root_);
    }
    paint_diagnostics_ = {};
    sweep_bindings();
    UiUpdate::Data output{session_, revision_, root_, {}, {}, {}, layout_revision_, {}, animation_time_};
    output.indices.reserve(slots_.len());
    for (usize i{}; i < slots_.len(); ++i)
        output.indices.push(~Index{});
    if (root_.is_some()) {
        if (layout_dirty_) {
            CpuTimer layout(active_timings_ ? &active_timings_->layout : nullptr);
            layout_active_ = true;
            struct LayoutScope {
                bool &active;
                ~LayoutScope() { active = false; }
            } scope{layout_active_};
            layout_diagnostics_ = {};
            layout_work_ = 0;
            for (Index pass{}; pass < 16; ++pass) {
                ++stats_.layout_passes;
                layout_again_ = false;
                layout_requester_ = None();
                auto error = measure(*root_, {Some(viewport.width), Some(viewport.height)}, 0);
                if (error.is_none())
                    error = arrange(*root_, {0, 0, viewport.width, viewport.height}, 0);
                apply_layout_requests();
                if (error.is_some())
                    return Err(*error);
                if (!layout_again_) {
                    layout_dirty_ = false;
                    break;
                }
            }
            if (layout_dirty_) {
                layout_failure(layout_requester_.is_some() ? *layout_requester_ : *root_,
                               LayoutIssue::Limit, UiError::Limit);
                return Err(UiError::Limit);
            }
            if (layout_revision_ == ~Serial{})
                rstd::panic("vui layout revision exhausted");
            ++layout_revision_;
        }
        layout_diagnostics_ = {};
        CpuTimer collect_time(active_timings_ ? &active_timings_->collect : nullptr);
        auto error = collect(*root_, output, 0, recording);
        if (error.is_some())
            return Err(*error);
    }
    if (root_.is_none() && layout_dirty_) {
        if (layout_revision_ == ~Serial{})
            rstd::panic("vui layout revision exhausted");
        ++layout_revision_;
        layout_dirty_ = false;
        layout_diagnostics_ = {};
    }
    output.layout_revision = layout_revision_;
    output.stats = stats_;
    return Ok(rstd::move(output));
}

auto UiDocument::input_snapshot(Size viewport, DiagnosticsOptions options)
    -> Result<InputSnapshot, UiError> {
    if (updating_ || !can_route())
        return Err(UiError::Routing);
    input_stats_ = {};
    sweep_bindings();
    if (layout_dirty_ || viewport != viewport_ || input_base_.is_none()) {
        auto base = update(viewport, options, false);
        if (base.is_err())
            return Err(base.unwrap_err());
        auto frozen = rstd::move(base).unwrap();
        input_stats_ = frozen.stats();
        return Ok(InputSnapshot(rstd::move(frozen)));
    }
    Vec<UiVisual> changes;
    last_timings_ = None();
    for (auto id : input_changes_) {
        auto node = resolve(id);
        if (!node || !mounted(id))
            continue;
        auto visual = make_visual(id);
        if (visual.is_err())
            return Err(visual.unwrap_err());
        changes.push(rstd::move(visual).unwrap());
        ++input_stats_.geometry_overrides;
    }
    return Ok(InputSnapshot(UiUpdate(*input_base_), rstd::move(changes), revision_));
}

auto UiDocument::update(Size viewport, DiagnosticsOptions options, bool recording)
    -> Result<UiUpdate, UiError> {
    if (updating_)
        return Err(UiError::Routing);
    updating_ = true;
    stats_ = {};
    UpdateTimings sample;
    active_timings_ = options.timings ? &sample : nullptr;
    CpuTimer timer(active_timings_ ? &sample.update : nullptr);
    auto result = update_impl(viewport, recording);
    timer.pause();
    active_timings_ = nullptr;
    updating_ = false;
    last_timings_ = None();
    if (options.timings) {
        sample.document = session_;
        sample.revision = revision_;
        sample.layout_revision = layout_revision_;
        sample.work = stats_;
        if (result.is_err())
            sample.error = Some(result.unwrap_err());
        last_timings_ = Some(sample);
    }
    if (result.is_err())
        return Err(result.unwrap_err());
    auto output = result.unwrap();
    output.timings = last_timings_;
    auto snapshot = UiUpdate(rstd::move(output));
    input_base_ = Some(UiUpdate(snapshot));
    input_changes_.clear();
    return Ok(rstd::move(snapshot));
}
} // namespace vui
