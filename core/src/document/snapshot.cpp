module vui;
import :document.interface;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto UiDocument::make_visual(UiId id, bool include_geometry) -> Result<UiVisual, UiError> {
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
        if (!parent->modifiers.is_empty()) placement = parent->modifier_content_transform * placement;
    }
    auto clip = node.style.clip;
    Transform clip_transform;
    if (node.viewport.is_some()) {
        auto size = (*node.viewport)->metrics.viewport;
        auto padding = node.modifiers.is_empty() ? node.style.layout.padding : Scalar(0);
        clip = {true,
                {padding, padding, size.width, size.height},
                0};
        if (!node.modifiers.is_empty()) clip_transform = node.modifier_content_transform;
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
                                style.opacity, style.visible,  node.layout_clip, style.content_tint, style.paint_color,
                                node.layout_clip_transform, clip_transform};
    if (node.frozen_paint) {
        properties = **node.frozen_paint;
        properties.transform = transform * properties.transform;
        properties.opacity *= style.opacity;
        properties.visible = properties.visible && style.visible;
    }
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
    if (include_geometry) {
        auto geometry = content_geometry(id);
        if (geometry.is_err()) return Err(geometry.unwrap_err());
        visual.content_geometry = geometry.unwrap();
    }
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
    if (recording && node.paint_dirty && !node.frozen_paint) {
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
    if (routing_ || animation_dispatching_ || subcomposition_dispatching_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    if (!viewport.valid())
        return Err(UiError::InvalidValue);
    apply_phase_invalidations();
    phase_diagnostics_.clear();
    DocumentPhaseTransaction phase_transaction(phase_diagnostics_);
    phase_transaction_ = &phase_transaction;
    struct PhaseGuard {
        UiDocument &document;
        DocumentPhaseTransaction &transaction;
        ~PhaseGuard() {
            document.phase_transaction_ = nullptr;
            document.discard_phase_reads(transaction);
        }
    } phase_guard{*this, phase_transaction};
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
    if (auto error = publish_phase_reads(phase_transaction); error.is_some()) return Err(*error);
    return Ok(rstd::move(output));
}

auto UiDocument::input_snapshot(Size viewport, DiagnosticsOptions options)
    -> Result<InputSnapshot, UiError> {
    if (updating_ || !can_route())
        return Err(UiError::Routing);
    apply_phase_invalidations();
    input_stats_ = {};
    sweep_bindings();
    if (layout_dirty_ || viewport != viewport_ || input_base_.is_none()) {
        auto base = update(viewport, options, false);
        input_stats_ = stats_;
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
auto UiDocument::capture_painted_subtree(UiId id, Index node_limit) -> Result<PaintedSubtree, UiError> {
    auto node = resolve(id);
    if (!node || !mounted(id)) return Err(UiError::InvalidId);
    if (!node->arranged_slot) return Err(UiError::LayoutPending);
    if (!node_limit) return Err(UiError::Limit);
    PaintedSubtree snapshot;
    auto visit = [&](auto &self, UiId target, Option<Index> parent) -> Result<empty, UiError> {
        if (snapshot.entries_.len() >= usize(node_limit)) return Err(UiError::Limit);
        auto visual = make_visual(target, false);
        if (visual.is_err()) return Err(visual.unwrap_err());
        auto entry = visual.unwrap();
        if (entry.content.has_frame_queries()) return Err(UiError::InvalidValue);
        auto index = Index(snapshot.entries_.len().to_primitive());
        snapshot.entries_.push({entry.properties, rstd::move(entry.content), parent});
        for (auto child : entry.children) {
            auto copied = self(self, child, Some(Index(index)));
            if (copied.is_err()) return copied;
        }
        return Ok(empty{});
    };
    auto captured = visit(visit, id, None<Index>());
    if (captured.is_err()) return Err(captured.unwrap_err());
    return Ok(rstd::move(snapshot));
}

auto UiDocument::append_painted_subtree(UiId parent, PaintedSubtree snapshot) -> Result<UiId, UiError> {
    if (snapshot.entries_.is_empty()) return Err(UiError::InvalidValue);
    Vec<UiId> nodes;
    auto fail = [&](UiError error) -> Result<UiId, UiError> {
        if (!nodes.is_empty()) (void)destroy(nodes[usize()]);
        return Err(error);
    };
    for (usize i{}; i < snapshot.entries_.len(); ++i) {
        auto &entry = snapshot.entries_[i];
        NodeStyle style{.hit_test_visible = false, .enabled = false, .hit_self = false};
        auto made = create<Stack>(style);
        if (made.is_err()) return fail(made.unwrap_err());
        auto id = made.unwrap();
        auto *node = resolve(id);
        node->content = rstd::move(entry.content);
        node->frozen_paint = Some(Box<RenderProperties>::make(entry.properties));
        if (entry.parent) {
            auto attached = attach(nodes[usize(*entry.parent)], id);
            if (attached.is_err()) {
                (void)destroy(id);
                return fail(attached.unwrap_err());
            }
        }
        nodes.push(UiId(id));
    }
    auto root = nodes[usize()];
    auto attached = attach(parent, root);
    if (attached.is_err()) return fail(attached.unwrap_err());
    return Ok(root);
}
} // namespace vui
