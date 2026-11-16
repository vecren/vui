module vui;
import :document.interface;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
auto UiDocument::format_changed(const TextContent &text, TextFormat format) -> bool {
    return text.family.as_str() != format.family || text.language.as_str() != format.language ||
        text.size != format.size || text.direction != format.direction ||
        text.wrap != format.wrap || text.bold != format.bold || text.italic != format.italic ||
        text.alignment != format.alignment || text.line_spacing != format.line_spacing || text.character_spacing != format.character_spacing || text.spacing != format.spacing ||
        text.max_lines != format.max_lines || text.overflow != format.overflow || text.auto_size!=format.auto_size || text.source!=format.source || text.line_breaks!=format.line_breaks || text.whitespace!=format.whitespace;
}

void UiDocument::assign_format(TextContent &text, TextFormat format) {
        text.family = String::make(format.family);
        text.language = String::make(format.language);
        text.size = format.size;
        text.direction = format.direction;
        text.wrap = format.wrap;
        text.bold = format.bold;
        text.italic = format.italic;
        text.alignment = format.alignment;
        text.line_spacing = format.line_spacing;
        text.character_spacing = format.character_spacing;
        text.spacing = format.spacing;
        text.max_lines = format.max_lines;
        text.overflow = format.overflow;
        text.auto_size = format.auto_size;
        text.source = format.source;
        text.line_breaks=format.line_breaks;text.whitespace=format.whitespace;
}

auto UiDocument::text_format(const TextContent &text) -> TextFormat {
    TextFormat result; result.family = text.family.as_str(); result.language = text.language.as_str();
    result.size = text.size;
    result.direction = text.direction;
    result.wrap = text.wrap;
    result.bold = text.bold;
    result.italic = text.italic;
    result.alignment = text.alignment;
    result.line_spacing = text.line_spacing;
    result.character_spacing = text.character_spacing;
    result.spacing = text.spacing;
    result.max_lines = text.max_lines;
    result.overflow = text.overflow;
    result.auto_size = text.auto_size;
    result.source = text.source;
    result.line_breaks = text.line_breaks;
    result.whitespace = text.whitespace;
    return result;
}

auto UiDocument::merge_format(TextFormat current, TextFormat before, TextFormat value) -> TextFormat {
    if (before.family != value.family) current.family = value.family;
    if (before.language != value.language) current.language = value.language;
    if (before.size != value.size) current.size = value.size;
    if (before.direction != value.direction) current.direction = value.direction;
    if (before.wrap != value.wrap) current.wrap = value.wrap;
    if (before.bold != value.bold) current.bold = value.bold;
    if (before.italic != value.italic) current.italic = value.italic;
    if (before.alignment != value.alignment) current.alignment = value.alignment;
    if (before.line_spacing != value.line_spacing) current.line_spacing = value.line_spacing;
    if (before.character_spacing != value.character_spacing) current.character_spacing = value.character_spacing;
    if (before.spacing != value.spacing) current.spacing = value.spacing;
    if (before.max_lines != value.max_lines) current.max_lines = value.max_lines;
    if (before.overflow != value.overflow) current.overflow = value.overflow;
    if (before.auto_size != value.auto_size) current.auto_size = value.auto_size;
    if (before.source != value.source) current.source = value.source;
    if (before.line_breaks != value.line_breaks) current.line_breaks = value.line_breaks;
    if (before.whitespace != value.whitespace) current.whitespace = value.whitespace;
    return current;
}

auto UiDocument::write_text(UiId id, Node &node, String value, TextBoundaries boundaries) -> bool {
    auto &text = **node.text;
    if (text.value.as_str() == value.as_str() && (!boundaries.present() || boundaries == text.boundaries))
        return false;
    if (text.value.as_str() != value.as_str()) { text.colors = {}; text.decorations = {}; text.inline_images = {}; text.font_spans = {};text.line_breaks={}; }
    text.value = rstd::move(value);
    text.boundaries = rstd::move(boundaries);
    text.dirty = true;
    changed(true, Some(id));
    return true;
}

auto UiDocument::write_character_spacing(UiId id, Node &node, Scalar value) -> bool {
    if ((*node.text)->character_spacing == value)
        return false;
    (*node.text)->character_spacing = value;
    (*node.text)->dirty = true;
    changed(true, Some(id));
    return true;
}

auto UiDocument::write_text_color(UiId id, Node &node, Color value) -> bool {
    if ((*node.text)->color == value)
        return false;
    (*node.text)->color = value;
    node.paint_dirty = true;
    changed(false, Some(id));
    return true;
}

auto UiDocument::set_text_service(Box<TextService> service) -> Result<empty, UiError> {
    if (routing_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    text_service_ = Some(rstd::move(service));
    return text_fonts_changed();
}

auto UiDocument::text_fonts_changed() -> Result<empty, UiError> {
    if (routing_ || (layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    for (usize index{}; index < slots_.len(); ++index) {
        auto &slot = slots_[index];
        if (slot.node.is_some() && slot.node->text.is_some()) {
            (*slot.node->text)->dirty = true;
            dirty_measure(
                {session_, static_cast<Index>(index.to_primitive()), slot.generation});
        }
    }
    layout_dirty_ = true;
    changed(false);
    return Ok(empty{});
}

auto UiDocument::set_text(UiId id, ref<str> value) -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->text.is_none())
        return Err(UiError::InvalidValue);
    if (value_occupied(*node, UiValueProperty::Text))
        return Err(UiError::PropertyOwned);
    if ((**node->text).value.as_str() != value)
        write_text(id, *node, String::make(value));
    return Ok(empty{});
}

auto UiDocument::set_text(UiId id, const TextBoundaries &value) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (node->text.is_none() || !value.present()) return Err(UiError::InvalidValue);
    if (value_occupied(*node, UiValueProperty::Text)) return Err(UiError::PropertyOwned);
    if ((**node->text).boundaries != value)
        write_text(id, *node, String::make(value.text()), TextBoundaries(value));
    return Ok(empty{});
}

auto UiDocument::set_text_format(UiId id, TextFormat format) -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->text.is_none() || !format.valid() || !format.line_breaks.matches((**node->text).value.as_str()))
        return Err(UiError::InvalidValue);
    if (property_occupied(*node, PresentationProperty::CharacterSpacing))
        return Err(UiError::PropertyOwned);
    auto &text = **node->text;
    if (format_changed(text, format)) {
        assign_format(text, format);
        text.dirty = true;
        changed(true, Some(id));
    }
    return Ok(empty{});
}

auto UiDocument::set_text_font_spans(UiId id, TextFontSpans spans) -> Result<empty,UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node=resolve(id); if (!node) return Err(UiError::InvalidId);
    if (node->text.is_none() || !spans.matches((*node->text)->value.as_str())) return Err(UiError::InvalidValue);
    auto &text=**node->text;
    if (text.font_spans != spans) { text.font_spans=rstd::move(spans); text.dirty=true; changed(true,Some(id)); }
    return Ok(empty{});
}

auto UiDocument::set_text_font_spans(UiId id, slice<TextFontSpan> spans) -> Result<empty,UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node=resolve(id); if (!node) return Err(UiError::InvalidId);
    if (node->text.is_none()) return Err(UiError::InvalidValue);
    auto value=TextFontSpans::create((*node->text)->value.as_str(),spans);
    if (value.is_err()) return Err(UiError::InvalidValue);
    return set_text_font_spans(id,rstd::move(value).unwrap());
}

auto UiDocument::set_text_effects(UiId id, TextEffects effects) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (node->text.is_none() || !effects.valid()) return Err(UiError::InvalidValue);
    if ((*node->text)->effects != effects) {
        (*node->text)->effects = effects;
        node->paint_dirty = true;
        changed(false);
    }
    return Ok(empty{});
}

auto UiDocument::set_text_character_spacing(UiId id, Scalar value) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_)
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->text.is_none() || !finite(value))
        return Err(UiError::InvalidValue);
    if (property_occupied(*node, PresentationProperty::CharacterSpacing))
        return Err(UiError::PropertyOwned);
    write_character_spacing(id, *node, value);
    return Ok(empty{});
}

auto UiDocument::set_text_color(UiId id, Color color) -> Result<empty, UiError> {
    if ((layout_active_ || painting_ || evaluating_ || editing_))
        return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->text.is_none() || !color.valid())
        return Err(UiError::InvalidValue);
    if (property_occupied(*node, PresentationProperty::TextColor))
        return Err(UiError::PropertyOwned);
    write_text_color(id, *node, color);
    return Ok(empty{});
}

auto UiDocument::set_text_colors(UiId id, TextColors colors) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (node->text.is_none() || !colors.matches((*node->text)->value.as_str())) return Err(UiError::InvalidValue);
    if ((*node->text)->colors != colors) {
        (*node->text)->colors = rstd::move(colors);
        node->paint_dirty = true;
        changed(false);
    }
    return Ok(empty{});
}

auto UiDocument::set_text_colors(UiId id, slice<TextColorSpan> spans) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (node->text.is_none()) return Err(UiError::InvalidValue);
    auto colors = TextColors::create((*node->text)->value.as_str(), spans);
    if (colors.is_err()) return Err(UiError::InvalidValue);
    return set_text_colors(id, rstd::move(colors).unwrap());
}

auto UiDocument::set_text_decorations(UiId id, TextDecorations decorations) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (node->text.is_none() || !decorations.matches((*node->text)->value.as_str())) return Err(UiError::InvalidValue);
    if ((*node->text)->decorations != decorations) {
        (*node->text)->decorations = rstd::move(decorations);
        node->paint_dirty = true;
        changed(false);
    }
    return Ok(empty{});
}

auto UiDocument::set_text_decorations(UiId id, slice<TextDecorationSpan> spans) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (node->text.is_none()) return Err(UiError::InvalidValue);
    auto decorations = TextDecorations::create((*node->text)->value.as_str(), spans);
    if (decorations.is_err()) return Err(UiError::InvalidValue);
    return set_text_decorations(id, rstd::move(decorations).unwrap());
}

auto UiDocument::set_text_inline_images(UiId id, TextInlineImages images) -> Result<empty, UiError> {
    if (layout_active_ || painting_ || evaluating_ || editing_) return Err(UiError::Routing);
    auto node = resolve(id);
    if (!node) return Err(UiError::InvalidId);
    if (node->text.is_none() || !images.matches((*node->text)->value.as_str())) return Err(UiError::InvalidValue);
    auto &text = **node->text;
    if (text.inline_images != images) {
        bool layout = text.inline_images.objects() != images.objects();
        text.inline_images = rstd::move(images);
        node->paint_dirty = true;
        if (layout) { text.dirty = true; changed(true, Some(id)); } else changed(false);
    }
    return Ok(empty{});
}

auto UiDocument::text_layout(UiId id) -> Result<TextLayout, UiError> {
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->text.is_none())
        return Err(UiError::InvalidValue);
    auto &text = **node->text;
    if (layout_dirty_ || text.dirty || text.layout.is_none() || !mounted(id))
        return Err(UiError::LayoutPending);
    return Ok(TextLayout(*text.layout));
}

auto UiDocument::text_placement(UiId id) -> Result<TextPlacement, UiError> {
    auto result = text_layout(id);
    if (result.is_err())
        return Err(result.unwrap_err());
    auto layout = rstd::move(result).unwrap();
    auto &node = *resolve(id);
    return Ok(TextPlacement{rstd::move(layout), text_origin(node),
        node.modifiers.is_empty() ? Transform{} : node.modifier_content_transform});
}

auto UiDocument::text_error(UiId id) -> Result<Option<TextError>, UiError> {
    auto node = resolve(id);
    if (!node)
        return Err(UiError::InvalidId);
    if (node->text.is_none())
        return Err(UiError::InvalidValue);
    return Ok(Option<TextError>((*node->text)->error));
}
} // namespace vui
