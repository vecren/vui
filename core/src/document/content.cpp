module vui;
import :document.interface;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto UiDocument::write_image(UiId id, Node &node, Option<ImageData> data) -> bool {
    auto &image = **node.image;
    if (image.data.is_some() == data.is_some() &&
        (data.is_none() || *image.data == *data))
        return false;
    auto old_size = image.data.is_some() ? image.data->natural_size() : Size{};
    auto new_size = data.is_some() ? data->natural_size() : Size{};
    image.data = rstd::move(data);
    node.paint_dirty = true;
    changed(old_size != new_size, Some(id));
    return true;
}

auto UiDocument::place_image_content(const Node &node) -> Option<ImagePlacement> {
    const auto &image = **node.image;
    if (image.data.is_none())
        return None();
    auto padding = node.style.layout.padding;
    return Some(place_image(*image.data,
                            {padding, padding,
                             limit(node.measured.width - 2 * padding, node.measured.width),
                             limit(node.measured.height - 2 * padding, node.measured.height)},
                            image.fit)
                    .unwrap());
}

auto UiDocument::set_scene_source(UiId id, Option<ContentSource> source) -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->scene.is_none() || (source.is_some() && !source->valid()))
        return Err(UiError::InvalidValue);
    auto &scene = **node->scene;
    if (scene.source.is_some() == source.is_some() &&
        (source.is_none() || scene.source->id() == source->id()))
        return Ok(empty{});
    scene.source = rstd::move(source);
    node->paint_dirty = true;
    changed(false);
    return Ok(empty{});
}

auto UiDocument::set_scene_presentation(UiId id, ImageFit fit, ImageSampling sampling,
                            Color placeholder)
    -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->scene.is_none() || !placeholder.valid() ||
        (fit != ImageFit::Fill && fit != ImageFit::Contain && fit != ImageFit::Cover) ||
        (sampling != ImageSampling::Nearest && sampling != ImageSampling::Linear))
        return Err(UiError::InvalidValue);
    auto &scene = **node->scene;
    if (scene.fit != fit || scene.sampling != sampling || scene.placeholder != placeholder) {
        scene.fit = fit;
        scene.sampling = sampling;
        scene.placeholder = placeholder;
        node->paint_dirty = true;
        changed(false);
    }
    return Ok(empty{});
}

auto UiDocument::scene_placement(UiId id) -> Result<Option<ScenePlacement>, UiError> {
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->scene.is_none())
        return Err(UiError::InvalidValue);
    if (layout_dirty_ || !mounted(id))
        return Err(UiError::LayoutPending);
    const auto &scene = **node->scene;
    auto frame = scene.source.is_some() ? scene.source->latest() : Option<TextureFrame>{};
    if (frame.is_none())
        return Ok(Option<ScenePlacement>{});
    auto info = frame->info();
    auto padding = node->style.layout.padding;
    auto placed = place_image(
        Size{Scalar(info.width), Scalar(info.height)},
        {padding, padding, limit(node->measured.width - 2 * padding, node->measured.width),
         limit(node->measured.height - 2 * padding, node->measured.height)},
        scene.fit);
    if (placed.is_err())
        return Err(UiError::InvalidValue);
    return Ok(Some(ScenePlacement{info, placed.unwrap()}));
}

auto UiDocument::set_image(UiId id, Option<ImageData> data) -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->image.is_none())
        return Err(UiError::InvalidValue);
    if (value_occupied(*node, UiValueProperty::Image))
        return Err(UiError::PropertyOwned);
    write_image(id, *node, rstd::move(data));
    return Ok(empty{});
}

auto UiDocument::set_image_tint(UiId id, Color tint) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_)
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->image.is_none() || !tint.valid())
        return Err(UiError::InvalidValue);
    if ((*node->image)->tint != tint) {
        (*node->image)->tint = tint;
        node->paint_dirty = true;
        changed(false, Some(id));
    }
    return Ok(empty{});
}

auto UiDocument::set_image_radial_fill(UiId id, ImageRadialFill radial) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (node->image.is_none() || !radial.valid()) return Err(UiError::InvalidValue);
    if ((*node->image)->radial != radial) {
        (*node->image)->radial = radial;
        node->paint_dirty = true;
        changed(false, Some(id));
    }
    return Ok(empty{});
}

auto UiDocument::set_image_fit(UiId id, ImageFit fit) -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->image.is_none() ||
        (fit != ImageFit::Fill && fit != ImageFit::Contain && fit != ImageFit::Cover))
        return Err(UiError::InvalidValue);
    if ((*node->image)->fit != fit) {
        (*node->image)->fit = fit;
        node->paint_dirty = true;
        changed(false);
    }
    return Ok(empty{});
}

auto UiDocument::set_image_sampling(UiId id, ImageSampling sampling) -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->image.is_none() ||
        (sampling != ImageSampling::Nearest && sampling != ImageSampling::Linear))
        return Err(UiError::InvalidValue);
    if ((*node->image)->sampling != sampling) {
        (*node->image)->sampling = sampling;
        node->paint_dirty = true;
        changed(false);
    }
    return Ok(empty{});
}

auto UiDocument::image_placement(UiId id) -> Result<Option<ImagePlacement>, UiError> {
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->image.is_none())
        return Err(UiError::InvalidValue);
    if (layout_dirty_ || !mounted(id))
        return Err(UiError::LayoutPending);
    return Ok(place_image_content(*node));
}

auto UiDocument::paint(UiId id, Node &node) -> Option<UiError> {
    painting_ = true;
    struct Guard {
        bool &active;
        ~Guard() { active = false; }
    } guard{painting_};
    CpuTimer timer(active_timings_ ? &active_timings_->paint : nullptr);
    DisplayListRecorder recorder;
    auto style = effective_style(node);
    if (style.background.a > 0)
        recorder.rect({0, 0, node.measured.width, node.measured.height}, style.background,
                      style.radius);
    if (node.text.is_some() && (*node.text)->layout.is_some()) {
        auto &text = **node.text;
        recorder.text(TextLayout(*text.layout), text_origin(node), text.color, text.effects, TextColors(text.colors), TextDecorations(text.decorations), TextInlineImages(text.inline_images));
    }
    if (node.image.is_some() && (*node.image)->data.is_some()) {
        auto &image = **node.image;
        auto placed = *place_image_content(node);
        recorder.image(ImageData(*image.data), placed.destination, placed.source,
                       image.sampling, image.tint, ImageEdges::All, false,
                       ImageBlend::SourceOver, image.radial);
    }
    if (node.scene.is_some()) {
        const auto &scene = **node.scene;
        auto padding = node.style.layout.padding;
        Rect box{padding, padding,
                 limit(node.measured.width - 2 * padding, node.measured.width),
                 limit(node.measured.height - 2 * padding, node.measured.height)};
        if (scene.source.is_some())
            recorder.external(*scene.source, box, scene.fit, scene.sampling, scene.placeholder);
        else
            recorder.rect(box, scene.placeholder);
    }
    bool repaint{};
    if (node.painter.is_some()) {
        PaintContext context(id, node.measured, recorder, style);
        auto result = (*node.painter)->paint(context);
        if (result.is_err()) {
            paint_diagnostics_.push({id, {}, result.unwrap_err()});
            return Some(result.unwrap_err());
        }
        repaint = context.repaint_requested();
    } else if (node.kind != LayoutKind::Leaf && node.kind != LayoutKind::Text &&
               node.kind != LayoutKind::Image && node.kind != LayoutKind::Scene) {
        recorder.children();
    }
    auto recorded = rstd::move(recorder).finish();
    if (recorded.is_err()) {
        paint_diagnostics_.push({id, Some(recorded.unwrap_err()), UiError::PaintFailed});
        return Some(UiError::PaintFailed);
    }
    auto content = rstd::move(recorded).unwrap();
    if (!node.children.is_empty() && !content.has_children()) {
        paint_diagnostics_.push({id, {}, UiError::MissingChild});
        return Some(UiError::PaintFailed);
    }
    node.content = rstd::move(content);
    node.paint_dirty = repaint;
    if (repaint)
        changed(false);
    ++stats_.recordings;
    return None();
}

auto UiDocument::invalidate_paint(UiId id) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_)
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    node->paint_dirty = true;
    changed(false);
    return Ok(empty{});
}
} // namespace vui
