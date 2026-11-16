module vui;
import :document.interface;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
void UiDocument::dirty_measure(UiId id) {
    auto cursor = Some(id);
    while (cursor.is_some()) {
        auto node = resolve(*cursor);
        if (!node)
            break;
        node->measure_dirty = true;
        node->arrange_dirty = true;
        node->subtree_arrange_dirty = true;
        cursor = node->parent;
    }
}

void UiDocument::dirty_arrange(UiId id) {
    auto node = resolve(id);
    node->arrange_dirty = true;
    auto cursor = Some(id);
    while (cursor.is_some()) {
        auto ancestor = resolve(*cursor);
        ancestor->subtree_arrange_dirty = true;
        cursor = ancestor->parent;
    }
}

void UiDocument::request_layout(UiId id) {
    auto &node = *resolve(id);
    if (!node.pending_layout) {
        node.pending_layout = true;
        pending_layout_.push(UiId(id));
    }
    layout_again_ = true;
    layout_requester_ = Some(id);
}

void UiDocument::apply_layout_requests() {
    auto requests = rstd::exchange(pending_layout_, Vec<UiId>{});
    for (auto id : requests) {
        auto node = resolve(id);
        if (node) {
            node->pending_layout = false;
            dirty_measure(id);
        }
    }
}

auto UiDocument::layout_failure(UiId id, LayoutIssue issue, UiError error)
    -> Option<UiError> {
    layout_diagnostics_.push(LayoutDiagnostic{id, issue});
    dirty_measure(id);
    return Some(error);
}

struct UiDocument::MeasureContext final : LayoutMeasureContext {
    UiDocument &document;
    Node &node;
    UiId id;
    Index depth;
    Vec<bool> visited;
    Option<UiError> error;
    MeasureContext(UiDocument &d, Node &n, UiId i, Index level)
        : document(d), node(n), id(i), depth(level) {
        for (usize j{}; j < node.children.len(); ++j)
            visited.push(false);
    }
    auto node_id() const -> UiId override { return id; }
    auto style() const -> LayoutStyle override {
        auto value = effective_style(node).layout;
        if (!node.modifiers.is_empty()) value.padding = 0;
        return value;
    }
    auto child_relation(usize index) const -> Result<UiRelation, UiError> override {
        if (index >= node.children.len())
            return Err(UiError::MissingChild);
        auto child = node.children[index];
        return Ok(UiRelation{id, child, document.resolve(child)->relation_generation});
    }
    auto child_count() const -> usize override { return node.children.len(); }
    auto child_id(usize index) const -> Result<UiId, UiError> override {
        if (index >= node.children.len())
            return Err(UiError::MissingChild);
        return Ok(UiId(node.children[index]));
    }
    auto measure_child(usize index, LayoutConstraints constraints)
        -> Result<Size, UiError> override {
        if (error.is_some() && *error != UiError::LayoutPending)
            return Err(*error);
        if (index >= child_count() || !constraints.valid()) {
            error = document.layout_failure(id, LayoutIssue::InvalidChild);
            return Err(*error);
        }
        auto measured = document.measure(node.children[index], constraints, depth + 1);
        if (measured.is_some()) {
            error = measured;
            return Err(*error);
        }
        visited[index] = true;
        return Ok(Size(document.resolve(node.children[index])->desired));
    }
    void request_layout() override { document.request_layout(id); }
    auto complete() -> bool {
        for (auto done : visited)
            if (!done)
                return false;
        return true;
    }
};

struct UiDocument::ArrangeContext final : LayoutArrangeContext {
    UiDocument &document;
    Node &node;
    UiId id;
    Index depth;
    Scalar padding;
    Vec<bool> visited;
    Option<UiError> error;
    ArrangeContext(UiDocument &d, Node &n, UiId i, Index level, Scalar p)
        : document(d), node(n), id(i), depth(level), padding(p) {
        for (usize j{}; j < node.children.len(); ++j)
            visited.push(false);
    }
    auto node_id() const -> UiId override { return id; }
    auto style() const -> LayoutStyle override {
        auto value = effective_style(node).layout;
        if (!node.modifiers.is_empty()) value.padding = 0;
        return value;
    }
    auto child_relation(usize index) const -> Result<UiRelation, UiError> override {
        if (index >= node.children.len())
            return Err(UiError::MissingChild);
        auto child = node.children[index];
        return Ok(UiRelation{id, child, document.resolve(child)->relation_generation});
    }
    auto child_count() const -> usize override { return node.children.len(); }
    auto child_id(usize index) const -> Result<UiId, UiError> override {
        if (index >= node.children.len())
            return Err(UiError::MissingChild);
        return Ok(UiId(node.children[index]));
    }
    auto desired_child(usize index) -> Result<Size, UiError> override {
        if (error.is_some())
            return Err(*error);
        if (index >= child_count()) {
            error = document.layout_failure(id, LayoutIssue::InvalidChild);
            return Err(*error);
        }
        return Ok(Size(document.resolve(node.children[index])->desired));
    }
    auto child_text(usize index) -> Result<TextPlacement, UiError> override {
        if (index >= child_count()) return Err(UiError::MissingChild);
        const auto &child = *document.resolve(node.children[index]);
        if (child.text.is_none()) return Err(UiError::InvalidValue);
        if (!child.modifiers.is_empty() && !visited[index]) return Err(UiError::LayoutPending);
        const auto &text = **child.text;
        if (text.dirty || text.layout.is_none()) return Err(UiError::LayoutPending);
        return Ok(TextPlacement{*text.layout, text_origin(child),
            child.modifiers.is_empty() ? Transform{} : child.modifier_content_transform});
    }
    auto set_text_input_area(Option<Rect> area) -> Result<empty, UiError> override {
        if (area.is_some() && !area->valid()) return Err(UiError::InvalidValue);
        if (area.is_some()) { area->x += padding; area->y += padding; }
        if (area.is_some() && !node.modifiers.is_empty()) {
            auto mapping = node.modifier_content_transform;
            Point corners[]{mapping.map({area->x, area->y}), mapping.map({area->x + area->width, area->y}),
                mapping.map({area->x, area->y + area->height}), mapping.map({area->x + area->width, area->y + area->height})};
            auto left = corners[0].x, right = left, top = corners[0].y, bottom = top;
            for (auto point : corners) {
                left = rstd::cmp::min(left, point.x); right = maximum(right, point.x);
                top = rstd::cmp::min(top, point.y); bottom = maximum(bottom, point.y);
            }
            area = Some(Rect{left, top, right - left, bottom - top});
        }
        node.layout_has_text_input_area = true; node.layout_text_input_area = area;
        return Ok(empty{});
    }
    void request_paint() override { node.paint_dirty = true; }
    auto arrange_child(usize index, Rect slot) -> Result<empty, UiError> override {
        if (error.is_some())
            return Err(*error);
        if (index >= child_count() || visited[index] || !slot.valid()) {
            error = document.layout_failure(id, LayoutIssue::InvalidChild);
            return Err(*error);
        }
        slot.x += padding;
        slot.y += padding;
        error = document.arrange(node.children[index], slot, depth + 1);
        if (error.is_some())
            return Err(*error);
        visited[index] = true;
        return Ok(empty{});
    }
    auto place_child(usize index, LayoutPlacement placement)
        -> Result<empty, UiError> override {
        if (error.is_some())
            return Err(*error);
        if (index >= child_count() || visited[index] || !placement.valid()) {
            error = document.layout_failure(id, LayoutIssue::InvalidChild);
            return Err(*error);
        }
        placement.slot.x += padding;
        placement.slot.y += padding;
        auto child = node.children[index];
        error = document.measure(child,
                                 {Some(placement.local_size.width),
                                  Some(placement.local_size.height), placement.local_size.width,
                                  placement.local_size.height},
                                 depth + 1);
        if (error.is_none())
            error = document.arrange(child, placement.slot, depth + 1, Some(placement));
        if (error.is_some())
            return Err(*error);
        visited[index] = true;
        return Ok(empty{});
    }
    auto set_clip(Clip clip) -> Result<empty, UiError> override {
        if (!clip.valid()) {
            error = document.layout_failure(id, LayoutIssue::InvalidChild);
            return Err(*error);
        }
        clip.rect.x += padding;
        clip.rect.y += padding;
        node.layout_clip = clip;
        node.layout_clip_transform = node.modifiers.is_empty() ? Transform{} : node.modifier_content_transform;
        return Ok(empty{});
    }
    void request_layout() override { document.request_layout(id); }
    auto complete() -> bool {
        for (auto done : visited)
            if (!done)
                return false;
        return true;
    }
};

auto UiDocument::measure_content(UiId id, LayoutConstraints inner, Index depth) -> Result<Size, UiError> {
    auto &node = *resolve(id);
    if (auto pending = require_subcomposition(id, inner); pending.is_some()) return Err(*pending);
    const auto style = effective_style(node).layout;
    auto fail = [&](LayoutIssue issue, UiError error = UiError::LayoutFailed) -> Result<Size, UiError> {
        layout_failure(id, issue, error);
        return Err(error);
    };
    Size desired;
    if (node.text.is_some()) {
        if (text_service_.is_none())
            return Err(UiError::TextServiceMissing);
        auto &text = **node.text;
        auto wrap_width =
            text.auto_size.is_none() && text.wrap == TextWrap::None && text.alignment == TextAlignment::Natural
                ? None<Scalar>()
                : inner.max_width;
        auto fit_height=text.auto_size.is_some()?inner.max_height:None<Scalar>();
        if (text.dirty || text.layout.is_none() || text.width != wrap_width || text.height!=fit_height) {
            ++stats_.text_layouts;
            TextRequest request{text.value.as_str(), text.family.as_str(), text.language.as_str(),
                text.size, wrap_width, text.direction, text.wrap, text.bold, text.italic, text.alignment, text.line_spacing};
            request.character_spacing = text.character_spacing;
            request.spacing = text.spacing;
            request.max_lines=text.max_lines;request.overflow=text.overflow;
            request.height=fit_height;request.auto_size=text.auto_size;
            request.source=text.source;
            request.line_breaks=text.line_breaks;request.whitespace=text.whitespace;
            if (text.boundaries.present()) request.boundaries = &text.boundaries;
            else if (text.layout.is_some() && text.layout->text() == text.value.as_str())
                request.boundaries = &text.layout->boundaries();
            request.inline_objects = text.inline_images.objects();
            request.font_spans = text.font_spans;
            auto result = (*text_service_)->layout(request);
            if (result.is_err()) {
                text.error = Some(result.unwrap_err());
                return Err(UiError::TextLayoutFailed);
            }
            auto size=result->font_size();
            if (result->inline_objects() != text.inline_images.objects() || result->font_spans() != text.font_spans || result->line_breaks()!=text.line_breaks ||
                (text.auto_size.is_some() && (size.is_none() || *size<text.auto_size->minimum || *size>text.auto_size->maximum))) {
                text.error = Some(TextError::Unsupported);
                return Err(UiError::TextLayoutFailed);
            }
            auto resolved_spacing = text.spacing.resolve(size.unwrap_or(text.size), text.character_spacing);
            if (result->character_spacing() != text.character_spacing || result->spacing() != text.spacing ||
                resolved_spacing.is_err() || result->resolved_character_spacing() != resolved_spacing.unwrap()) {
                text.error = Some(TextError::Unsupported);
                return Err(UiError::TextLayoutFailed);
            }
            text.layout = Some(rstd::move(result).unwrap());
            text.width = wrap_width;
            text.height = fit_height;
            text.error = None();
            text.dirty = false;
            node.paint_dirty = true;
        }
        auto bounds = text.layout->logical_bounds();
        desired = {bounds.width, bounds.height};
    }
    if (node.image.is_some() && (*node.image)->data.is_some())
        desired = (*node.image)->data->natural_size();

    if (node.layout_strategy.is_some() || node.kind == LayoutKind::Custom ||
        (node.kind == LayoutKind::Item && !node.children.is_empty())) {
        if (node.layout_strategy.is_none())
            return fail(LayoutIssue::Strategy);
        MeasureContext context(*this, node, id, depth);
        auto result = (*node.layout_strategy)->measure(context, inner);
        if (context.error.is_some())
            return Err(*context.error);
        if (result.is_err())
            return fail(LayoutIssue::Strategy, result.unwrap_err());
        if (!context.complete())
            return fail(LayoutIssue::MissingChild);
        desired = result.unwrap();
    } else {
        bool pending{};
        LayoutConstraints child_limits{inner.max_width, inner.max_height};
        for (auto child : node.children) {
            auto error = measure(child, child_limits, depth + 1);
            if (error.is_some()) {
                if (*error == UiError::LayoutPending) { pending = true; continue; }
                return Err(*error);
            }
            auto size = resolve(child)->desired;
            if (node.kind == LayoutKind::Row) {
                desired.width += size.width;
                desired.height = maximum(desired.height, size.height);
            } else if (node.kind == LayoutKind::Column) {
                desired.height += size.height;
                desired.width = maximum(desired.width, size.width);
            } else {
                desired.width = maximum(desired.width, size.width);
                desired.height = maximum(desired.height, size.height);
            }
        }
        if (pending) return Err(UiError::LayoutPending);
        if (node.children.len() > usize(1)) {
            auto gap = style.gap *
                       static_cast<Scalar>((node.children.len() - usize(1)).to_primitive());
            if (node.kind == LayoutKind::Row)
                desired.width += gap;
            if (node.kind == LayoutKind::Column)
                desired.height += gap;
        }
    }
    if (!desired.valid())
        return fail(LayoutIssue::Strategy, UiError::InvalidValue);
    if (!node.modifiers.is_empty()) {
        desired.width = bounded(desired.width, inner.min_width, inner.max_width);
        desired.height = bounded(desired.height, inner.min_height, inner.max_height);
    }
    node.modifier_content_desired = desired;
    return Ok(desired);
}

auto UiDocument::measure(UiId id, LayoutConstraints available, Index depth) -> Option<UiError> {
    if (depth >= depth_limit_ || ++layout_work_ > 1'000'000)
        return layout_failure(id, LayoutIssue::Limit, UiError::Limit);
    auto &node = *resolve(id);
    if (!node.measure_dirty && node.measure_constraints == Some(available)) {
        ++stats_.measure_cache_hits;
        return None();
    }
    node.measure_dirty = true;
    node.arrange_dirty = true;
    auto observation = observe_phase(id, UiPhase::Measure);
    ++stats_.measurements;
    const auto style = effective_style(node).layout;
    auto max_width = intersection(style.max_width, available.max_width);
    auto max_height = intersection(style.max_height, available.max_height);
    auto min_width = maximum(style.min_width, available.min_width);
    auto min_height = maximum(style.min_height, available.min_height);
    node.constraint_conflict =
        (available.max_width.is_some() &&
         (min_width > *available.max_width ||
          (style.width.is_some() && *style.width > *available.max_width))) ||
        (available.max_height.is_some() &&
         (min_height > *available.max_height ||
          (style.height.is_some() && *style.height > *available.max_height))) ||
        (style.max_width.is_some() && *style.max_width < available.min_width) ||
        (style.max_height.is_some() && *style.max_height < available.min_height);
    // Parent constraints take precedence over a child's preferred range.
    if (max_width.is_some())
        max_width = Some(maximum(*max_width, available.min_width));
    if (max_height.is_some())
        max_height = Some(maximum(*max_height, available.min_height));
    min_width = constrain(min_width, max_width);
    min_height = constrain(min_height, max_height);
    auto width =
        style.width.is_some() ? Some(bounded(*style.width, min_width, max_width)) : max_width;
    auto height = style.height.is_some() ? Some(bounded(*style.height, min_height, max_height))
                                         : max_height;
    LayoutConstraints inner{inset(width, style.padding), inset(height, style.padding),
        maximum(Scalar(0), (style.width.is_some() ? *width : min_width) - 2 * style.padding),
        maximum(Scalar(0), (style.height.is_some() ? *height : min_height) - 2 * style.padding)};
    if (node.viewport.is_some()) {
        if ((*node.viewport)->axes.horizontal)
            inner.max_width = None();
        if ((*node.viewport)->axes.vertical)
            inner.max_height = None();
    }
    auto content = measure_modifier(id, usize(), inner, depth);
    if (content.is_err()) {
        if (content.unwrap_err() != UiError::LayoutPending && !node.modifiers.is_empty() &&
            (layout_diagnostics_.is_empty() || layout_diagnostics_[layout_diagnostics_.len() - usize(1)].node != id))
            return layout_failure(id, LayoutIssue::Strategy, content.unwrap_err());
        return Some(content.unwrap_err());
    }
    auto desired = content.unwrap();
    desired.width += 2 * style.padding;
    desired.height += 2 * style.padding;
    if (!desired.valid())
        return layout_failure(id, LayoutIssue::Strategy, UiError::InvalidValue);
    if (auto error = observation.finish(); error.is_some()) return error;
    node.desired = {width.is_some() && (style.width.is_some() || node.viewport.is_some())
                        ? *width
                        : bounded(desired.width, min_width, max_width),
                    height.is_some() && (style.height.is_some() || node.viewport.is_some())
                        ? *height
                        : bounded(desired.height, min_height, max_height)};
    node.measure_constraints = Some(available);
    node.measure_dirty = false;
    return None();
}

auto UiDocument::arrange_content(UiId id, Size inner, Index depth, Scalar padding) -> Option<UiError> {
    auto &node = *resolve(id);
    const auto style = effective_style(node).layout;
    if (node.layout_strategy.is_some()) {
        ArrangeContext context(*this, node, id, depth, padding);
        auto result = (*node.layout_strategy)->arrange(context, inner);
        if (context.error.is_some())
            return context.error;
        if (result.is_err())
            return layout_failure(id, LayoutIssue::Strategy, result.unwrap_err());
        if (!context.complete())
            return layout_failure(id, LayoutIssue::MissingChild);
    } else {
        Scalar offset{};
        for (auto child : node.children) {
            auto &item = *resolve(child);
            Rect child_slot{padding, padding, inner.width, inner.height};
            if (node.kind == LayoutKind::Row) {
                child_slot.x += offset;
                child_slot.width = item.desired.width;
                offset += item.desired.width + style.gap;
            }
            if (node.kind == LayoutKind::Column) {
                child_slot.y += offset;
                child_slot.height = item.desired.height;
                offset += item.desired.height + style.gap;
            }
            if (node.viewport.is_some()) {
                if ((*node.viewport)->axes.horizontal)
                    child_slot.width = maximum(child_slot.width, item.desired.width);
                if ((*node.viewport)->axes.vertical)
                    child_slot.height = maximum(child_slot.height, item.desired.height);
            }
            auto error = arrange(child, child_slot, depth + 1);
            if (error.is_some())
                return error;
        }
    }
    if (node.viewport.is_some()) {
        auto &view = **node.viewport;
        view.metrics.viewport = inner;
        view.metrics.extent =
            node.children.is_empty() ? Size{} : resolve(node.children[usize()])->measured;
        view.metrics.maximum = {
            view.axes.horizontal ? maximum(view.metrics.extent.width - inner.width, 0) : 0,
            view.axes.vertical ? maximum(view.metrics.extent.height - inner.height, 0) : 0};
        clamp_viewport(view);
    }
    return None();
}

auto UiDocument::arrange(UiId id, Rect slot, Index depth, Option<LayoutPlacement> mapped)
    -> Option<UiError> {
    if (depth >= depth_limit_ || ++layout_work_ > 1'000'000)
        return layout_failure(id, LayoutIssue::Limit, UiError::Limit);
    if (!slot.valid())
        return layout_failure(id, LayoutIssue::Strategy, UiError::InvalidValue);
    auto &node = *resolve(id);
    if (!node.arrange_dirty && !node.subtree_arrange_dirty &&
        node.arranged_slot == Some(slot) &&
        node.layout_mapping == (mapped.is_some() ? mapped->local_to_slot : Transform{}) &&
        node.layout_local_size ==
            (mapped.is_some() ? Some(mapped->local_size) : None<Size>())) {
        ++stats_.arrange_cache_hits;
        return None();
    }
    auto allocated = slot;
    auto observation = observe_phase(id, UiPhase::Placement);
    auto mapping = mapped.is_some() ? mapped->local_to_slot : Transform{};
    auto local_size = mapped.is_some() ? Some(mapped->local_size) : None<Size>();
    if (mapped.is_some()) {
        slot.width = mapped->local_size.width;
        slot.height = mapped->local_size.height;
    }
    node.layout_mapping = mapping;
    node.layout_local_size = local_size;
    const auto style = effective_style(node).layout;
    auto horizontal_stretch = style.horizontal == Alignment::Stretch && style.width.is_none();
    auto vertical_stretch = style.vertical == Alignment::Stretch && style.height.is_none();
    const auto previous = *node.measure_constraints;
    auto min_width = maximum(style.min_width, previous.min_width);
    auto min_height = maximum(style.min_height, previous.min_height);
    auto max_width = style.max_width.is_some()
                         ? Some(maximum(*style.max_width, previous.min_width))
                         : None<Scalar>();
    auto max_height = style.max_height.is_some()
                          ? Some(maximum(*style.max_height, previous.min_height))
                          : None<Scalar>();
    max_width = intersection(max_width, Some(slot.width));
    max_height = intersection(max_height, Some(slot.height));
    auto width =
        bounded(horizontal_stretch ? slot.width : node.desired.width, min_width, max_width);
    auto height =
        bounded(vertical_stretch ? slot.height : node.desired.height, min_height, max_height);
    if (node.desired.width > width || node.desired.height > height ||
        (horizontal_stretch && node.desired.width != width) ||
        (vertical_stretch && node.desired.height != height)) {
        auto error =
            measure(id,
                    {Some(width), max_height, constrain(previous.min_width, Some(width)),
                     constrain(previous.min_height, max_height)},
                    depth);
        if (error.is_some())
            return error;
        if (!horizontal_stretch)
            width = constrain(node.desired.width, Some(width));
        if (!vertical_stretch)
            height = bounded(node.desired.height, min_height, max_height);
    }
    ++stats_.arrangements;
    Size measured{width, height};
    bool visit_children =
        node.arrange_dirty || node.subtree_arrange_dirty || node.measured != measured;
    if (node.measured != measured)
        node.paint_dirty = true;
    node.measured = measured;
    node.position = {slot.x + aligned_offset(slot.width - width, style.horizontal),
                     slot.y + aligned_offset(slot.height - height, style.vertical)};
    Size inner{limit(width - 2 * style.padding, width),
               limit(height - 2 * style.padding, height)};
    if (visit_children) {
        node.layout_clip = {};
        node.layout_clip_transform = {};
        node.layout_has_text_input_area = false;
        node.layout_text_input_area = None();
    }
    if (visit_children) {
        auto error = node.modifiers.is_empty()
            ? arrange_content(id, inner, depth, style.padding)
            : arrange_modifier(id, usize(), inner,
                Transform{1, 0, 0, 1, style.padding, style.padding}, depth);
        if (error.is_some()) {
            if (*error != UiError::LayoutPending && !node.modifiers.is_empty()) return layout_failure(id, LayoutIssue::Strategy, *error);
            return error;
        }
        if (node.modifiers.is_empty()) {
            node.modifier_content_size = inner;
            node.modifier_content_transform = {};
        }
    }
    if (auto error = observation.finish(visit_children); error.is_some()) return error;
    node.arranged_slot = Some(allocated);
    node.arrange_dirty = false;
    node.subtree_arrange_dirty = false;
    return None();
}

auto UiDocument::set_layout_strategy(UiId id, Box<LayoutStrategy> strategy) -> Result<empty, UiError> {
    struct Adapter {
        Box<LayoutStrategy> strategy;
        auto measure(LayoutMeasureContext &context, LayoutConstraints c)
            -> Result<Size, UiError> {
            return strategy->measure(context, c);
        }
        auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
            return strategy->arrange(context, size);
        }
    };
    return install_layout(id, Adapter{rstd::move(strategy)});
}

auto UiDocument::invalidate_layout(UiId id) -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    if (!resolve(id))
        return Err(UiError::InvalidId);
    changed(true, Some(id));
    return Ok(empty{});
}

auto UiDocument::set_viewport_axes(UiId id, ScrollAxes axes) -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->viewport.is_none())
        return Err(UiError::InvalidValue);
    auto &view = **node->viewport;
    if (view.axes != axes) {
        view.axes = axes;
        changed(true, Some(id));
    }
    return Ok(empty{});
}

auto UiDocument::set_viewport_boundary(UiId id, ViewportBoundary boundary) -> Result<empty,UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node=resolve(id); if (!node) return Err(UiError::InvalidId);
    if (node->viewport.is_none() || (boundary!=ViewportBoundary::Clamped && boundary!=ViewportBoundary::Unclamped)) return Err(UiError::InvalidValue);
    auto &view=**node->viewport;
    if (view.metrics.boundary!=boundary) {
        view.metrics.boundary=boundary;
        if (!layout_dirty_ && mounted(id)) clamp_viewport(view);
        changed(false,Some(id));
    }
    return Ok(empty{});
}

auto UiDocument::set_viewport_offset(UiId id, Point offset) -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (property_occupied(*node, PresentationProperty::ViewportOffset))
        return Err(UiError::PropertyOwned);
    if (node->viewport.is_none() || !finite(offset.x) || !finite(offset.y))
        return Err(UiError::InvalidValue);
    auto &view = **node->viewport;
    if (!layout_dirty_ && mounted(id))
        offset = viewport_offset(view,offset);
    if (view.requested != offset) {
        view.requested = offset;
        if (!layout_dirty_ && mounted(id))
            clamp_viewport(view);
        else {
            dirty_arrange(id);
            layout_dirty_ = true;
        }
        changed(false, Some(id));
    }
    return Ok(empty{});
}

auto UiDocument::viewport_metrics(UiId id) -> Result<ViewportMetrics, UiError> {
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->viewport.is_none())
        return Err(UiError::InvalidValue);
    if (layout_dirty_ || !mounted(id))
        return Err(UiError::LayoutPending);
    return Ok(ViewportMetrics((*node->viewport)->metrics));
}

auto UiDocument::viewport_content(UiId id) -> Result<Option<UiId>, UiError> {
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->viewport.is_none())
        return Err(UiError::InvalidValue);
    return Ok(node->children.is_empty() ? None<UiId>() : Some(UiId(node->children[usize()])));
}
} // namespace vui
