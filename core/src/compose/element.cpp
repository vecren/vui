module vui.compose;
import :element;
import :behavior;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui::compose {
namespace {
auto forwards_modifiers(ElementKind kind) -> bool {
    return kind == ElementKind::Component || kind == ElementKind::Fragment ||
           kind == ElementKind::Provider || kind == ElementKind::Movable;
}

struct SemanticsModifier {
    using State = empty;
    SemanticsProperties properties;

    auto create() const -> State { return {}; }
    auto update(State &, BehaviorContext &context) const -> ComposeResult<empty> {
        auto described = context.session().semantics().set(context.target(), {
            properties.role, properties.label.clone(), properties.value.clone(),
            properties.actions, properties.merge_descendants, properties.checked,
        });
        if (described.is_err()) return Err(ComposeError(ComposeStage::Effect, Some(described.unwrap_err())));
        return Ok(empty{});
    }
    auto attach(State &, BehaviorContext &context, rstd::rc::Weak<State>) const -> EffectLease {
        return EffectLease([&session = context.session(), id = context.target()] { session.semantics().clear(id); });
    }
};
}
ElementData::ElementData(const ElementData &other)
    : kind(other.kind), layout(other.layout), style(other.style),
      key(other.key.is_some() ? Some(Key(*other.key)) : None<Key>()),
      identity(other.identity.is_some() ? Some(CallIdentity(*other.identity)) : None<CallIdentity>()),
      text(other.text.is_some() ? Some(UiTextValue(*other.text)) : None<UiTextValue>()),
      component_type(other.component_type), view(other.view), movable(other.movable), parameters(other.parameters), adapter(other.adapter),
      reference(other.reference.is_some() ? Some(NodeRef(*other.reference)) : None<NodeRef>()), invalid(other.invalid), source(other.source) {
    if (other.image.is_some()) {
        const auto &source = *other.image;
        image = Some(UiImageValue{source.data.is_some() ? Some(ImageData(*source.data)) : None<ImageData>(),
                                 source.fit, source.sampling, source.tint, source.radial});
    }
    for (const auto &modifier : other.modifiers) modifiers.push(ElementModifier(modifier));
    for (const auto &behavior : other.behaviors) behaviors.push(BehaviorDeclaration(behavior));
    for (const auto &local : other.locals) locals.push(LocalBinding(local));
    for (const auto &child : other.children) children.push(Element(child));
}

auto Element::style(NodeStyle value) && -> Element {
    auto next = rstd::move(*this).owned();
    if (forwards_modifiers(next.data_->kind)) {
        auto modifier = ElementModifier(ModifierKind::Style); modifier.style = value; next.data_.get()->modifiers.push(rstd::move(modifier));
    } else next.data_.get()->style = value;
    return next;
}

auto Element::gap(Scalar value) && -> Element {
    auto next = rstd::move(*this).owned();
    if (forwards_modifiers(next.data_->kind)) {
        auto modifier = ElementModifier(ModifierKind::Gap); modifier.scalar = value; next.data_.get()->modifiers.push(rstd::move(modifier));
    } else next.data_.get()->style.layout.gap = value;
    return next;
}

auto Element::padding(Scalar value) && -> Element {
    auto next = rstd::move(*this).owned();
    if (forwards_modifiers(next.data_->kind)) {
        auto modifier = ElementModifier(ModifierKind::Padding); modifier.scalar = value; next.data_.get()->modifiers.push(rstd::move(modifier));
    } else next.data_.get()->style.layout.padding = value;
    return next;
}

auto Element::format(TextFormat value) && -> Element {
    auto next = rstd::move(*this).owned();
    if (forwards_modifiers(next.data_->kind)) {
        auto modifier = ElementModifier(ModifierKind::Format); modifier.text = Some(UiTextValue{});
        modifier.text->set_format(value); next.data_.get()->modifiers.push(rstd::move(modifier));
    } else if (next.data_->text.is_some()) next.data_.get()->text->set_format(value); else next.data_.get()->invalid = true;
    return next;
}

auto Element::color(Color value) && -> Element {
    auto next = rstd::move(*this).owned();
    if (forwards_modifiers(next.data_->kind)) {
        auto modifier = ElementModifier(ModifierKind::Color); modifier.color = value; next.data_.get()->modifiers.push(rstd::move(modifier));
    } else if (next.data_->text.is_some()) next.data_.get()->text->color = value; else next.data_.get()->invalid = true;
    return next;
}

auto Element::reference(NodeRef value) && -> Element {
    auto next = rstd::move(*this).owned();
    if (forwards_modifiers(next.data_->kind)) {
        auto modifier = ElementModifier(ModifierKind::Reference); modifier.reference = Some(rstd::move(value)); next.data_.get()->modifiers.push(rstd::move(modifier));
    } else next.data_.get()->reference = Some(rstd::move(value));
    return next;
}

auto Element::behavior(BehaviorDeclaration value) && -> Element {
    auto next = rstd::move(*this).owned();
    if (forwards_modifiers(next.data_->kind)) {
        auto modifier = ElementModifier(ModifierKind::Behavior); modifier.behavior = Some(rstd::move(value));
        next.data_.get()->modifiers.push(rstd::move(modifier));
    } else next.data_.get()->behaviors.push(rstd::move(value));
    return next;
}

auto with_semantics(Element element, SemanticsProperties properties) -> Element {
    return rstd::move(element).behavior(Key{"vui.semantics"_str}, SemanticsModifier{rstd::move(properties)});
}

auto native(LayoutKind kind, Vec<Element> children, rstd::source_location source) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Native;
    data.get()->source = source; data.get()->layout = kind; data.get()->children = rstd::move(children); return Element(rstd::move(data));
}

auto text(ref<str> value, rstd::source_location source) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Native;
    data.get()->source = source; data.get()->layout = LayoutKind::Text; data.get()->text = Some(UiTextValue(value)); return Element(rstd::move(data));
}

auto image(ImageData value, rstd::source_location source) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Native;
    data.get()->source = source; data.get()->layout = LayoutKind::Image; data.get()->image = Some(UiImageValue{Some(rstd::move(value))}); return Element(rstd::move(data));
}

auto provide(Vec<LocalBinding> values, Element content, rstd::source_location source) -> Element {
    auto data = rstd::rc::make_rc<ElementData>();
    data.get()->kind = ElementKind::Provider; data.get()->source = source;
    data.get()->locals = rstd::move(values); data.get()->children.push(rstd::move(content));
    return Element(rstd::move(data));
}

auto apply_modifiers(Element value, const Vec<ElementModifier> &modifiers) -> ComposeResult<Element> {
    if (modifiers.is_empty()) return Ok(rstd::move(value));
    if (forwards_modifiers(value.data().kind)) {
        auto data = rstd::rc::make_rc<ElementData>(value.data());
        for (const auto &modifier : modifiers) data.get()->modifiers.push(ElementModifier(modifier));
        return Ok(Element(rstd::move(data)));
    }
    for (const auto &modifier : modifiers) {
        switch (modifier.kind) {
        case ModifierKind::Style: value = rstd::move(value).style(modifier.style); break;
        case ModifierKind::Gap: value = rstd::move(value).gap(modifier.scalar); break;
        case ModifierKind::Padding: value = rstd::move(value).padding(modifier.scalar); break;
        case ModifierKind::Format: value = rstd::move(value).format(modifier.text->format()); break;
        case ModifierKind::Color: value = rstd::move(value).color(modifier.color); break;
        case ModifierKind::Reference:
            if (value.data().reference.is_some() && value.data().reference->link().get() != modifier.reference->link().get()) {
                auto error = ComposeError(ComposeStage::Type);
                error.operation = String::make("component reference conflicts with its root reference"_str); return Err(rstd::move(error));
            }
            value = rstd::move(value).reference(*modifier.reference); break;
        case ModifierKind::Behavior: value = rstd::move(value).behavior(BehaviorDeclaration(*modifier.behavior)); break;
        }
        if (value.data().invalid) {
            auto error = ComposeError(ComposeStage::Type);
            error.operation = String::make("component text modifier requires a text root"_str); return Err(rstd::move(error));
        }
    }
    return Ok(rstd::move(value));
}
} // namespace vui::compose
