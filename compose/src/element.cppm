export module vui.compose:element;
import :types;
using namespace rstd::prelude;

export namespace vui::compose {
struct ViewFunction {
    Box<rstd::dyn<rstd::FnMut<ComposeResult<Element>(CompositionContext &)>>> invoke;
    template <class F> explicit ViewFunction(F callback)
        : invoke(Box<rstd::dyn<rstd::FnMut<ComposeResult<Element>(CompositionContext &)>>>::make(rstd::move(callback))) {}
};
struct ElementAdapter {
    template <class Self, class Delegate = void> struct Api {
        using Trait = ElementAdapter;
        auto prepare(PrepareContext &context, Option<UiId> previous, NodeStyle style) -> ComposeResult<UiNodeRef> {
            return rstd::trait_call<0>(this, context, previous, style);
        }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::prepare>;
};
struct AdapterFunction {
    rstd::any::TypeId type;
    Box<rstd::dyn<ElementAdapter>> value;
    template <class T> explicit AdapterFunction(T config)
        : type(rstd::any::TypeId::of<T>()), value(Box<rstd::dyn<ElementAdapter>>::make(rstd::move(config))) {}
};
enum class ElementKind { Native, Fragment, Component, Custom };
struct ElementData;
class Element {
    rstd::rc::Rc<ElementData> data_;
    auto copy() const -> Element;
    auto owned() && -> Element;
  public:
    Element();
    explicit Element(rstd::rc::Rc<ElementData> data);
    Element(const Element &);
    Element(Element &&) noexcept;
    auto operator=(const Element &) -> Element &;
    auto operator=(Element &&) noexcept -> Element &;
    ~Element() noexcept;
    auto data() const -> const ElementData &;
    auto style(NodeStyle) const & -> Element;
    auto style(NodeStyle) && -> Element;
    auto gap(Scalar) const & -> Element;
    auto gap(Scalar) && -> Element;
    auto padding(Scalar) const & -> Element;
    auto padding(Scalar) && -> Element;
    auto format(TextFormat) const & -> Element;
    auto format(TextFormat) && -> Element;
    auto color(Color) const & -> Element;
    auto color(Color) && -> Element;
    auto key(Key) const & -> Element;
    auto key(Key) && -> Element;
    auto reference(NodeRef) const & -> Element;
    auto reference(NodeRef) && -> Element;
};
struct ElementData {
    ElementKind kind{ElementKind::Fragment};
    LayoutKind layout{LayoutKind::Item};
    NodeStyle style;
    Option<Key> key;
    Option<UiTextValue> text;
    Option<UiImageValue> image;
    Vec<Element> children;
    rstd::any::TypeId component_type{rstd::any::TypeId::of<void>()};
    rstd::rc::Rc<ViewFunction> view;
    rstd::rc::Rc<AdapterFunction> adapter;
    Option<NodeRef> reference;
    bool invalid{};
    rstd::source_location source;
    ElementData() = default;
    ElementData(const ElementData &other)
        : kind(other.kind), layout(other.layout), style(other.style),
          key(other.key.is_some() ? Some(Key(*other.key)) : None<Key>()),
          text(other.text.is_some() ? Some(UiTextValue(*other.text)) : None<UiTextValue>()),
          component_type(other.component_type), view(other.view), adapter(other.adapter),
          reference(other.reference.is_some() ? Some(NodeRef(*other.reference)) : None<NodeRef>()), invalid(other.invalid), source(other.source) {
        if (other.image.is_some()) {
            const auto &source = *other.image;
            image = Some(UiImageValue{source.data.is_some() ? Some(ImageData(*source.data)) : None<ImageData>(),
                                     source.fit, source.sampling, source.tint, source.radial});
        }
        for (const auto &child : other.children) children.push(Element(child));
    }
};
inline Element::Element() : data_(rstd::rc::make_rc<ElementData>()) {}
inline Element::Element(rstd::rc::Rc<ElementData> data) : data_(rstd::move(data)) {}
inline Element::Element(const Element &other) : data_(other.data_.clone()) {}
inline Element::Element(Element &&) noexcept = default;
inline auto Element::operator=(const Element &other) -> Element & { data_ = other.data_.clone(); return *this; }
inline auto Element::operator=(Element &&) noexcept -> Element & = default;
inline Element::~Element() noexcept = default;
inline auto Element::data() const -> const ElementData & { return *data_.get(); }
inline auto Element::copy() const -> Element { return Element(rstd::rc::make_rc<ElementData>(*data_.get())); }
inline auto Element::owned() && -> Element { return data_.is_unique() ? rstd::move(*this) : copy(); }
inline auto Element::style(NodeStyle value) const & -> Element { return copy().style(value); }
inline auto Element::gap(Scalar value) const & -> Element { return copy().gap(value); }
inline auto Element::padding(Scalar value) const & -> Element { return copy().padding(value); }
inline auto Element::format(TextFormat value) const & -> Element { return copy().format(value); }
inline auto Element::color(Color value) const & -> Element { return copy().color(value); }
inline auto Element::key(Key value) const & -> Element { return copy().key(rstd::move(value)); }
inline auto Element::reference(NodeRef value) const & -> Element { return copy().reference(rstd::move(value)); }
inline auto Element::style(NodeStyle value) && -> Element { auto next = rstd::move(*this).owned(); next.data_.get()->style = value; return next; }
inline auto Element::gap(Scalar value) && -> Element { auto next = rstd::move(*this).owned(); next.data_.get()->style.layout.gap = value; return next; }
inline auto Element::padding(Scalar value) && -> Element { auto next = rstd::move(*this).owned(); next.data_.get()->style.layout.padding = value; return next; }
inline auto Element::format(TextFormat value) && -> Element {
    auto next = rstd::move(*this).owned(); if (next.data_->text.is_some()) next.data_.get()->text->set_format(value); else next.data_.get()->invalid = true; return next;
}
inline auto Element::color(Color value) && -> Element {
    auto next = rstd::move(*this).owned(); if (next.data_->text.is_some()) next.data_.get()->text->color = value; else next.data_.get()->invalid = true; return next;
}
inline auto Element::key(Key value) && -> Element { auto next = rstd::move(*this).owned(); next.data_.get()->key = Some(rstd::move(value)); return next; }
inline auto Element::reference(NodeRef value) && -> Element {
    auto next = rstd::move(*this).owned(); next.data_.get()->reference = Some(rstd::move(value));
    if (next.data_->kind == ElementKind::Fragment || next.data_->kind == ElementKind::Component) next.data_.get()->invalid = true;
    return next;
}
inline auto native(LayoutKind kind, Vec<Element> children = {}, rstd::source_location source = rstd::source_location::current()) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Native;
    data.get()->source = source; data.get()->layout = kind; data.get()->children = rstd::move(children); return Element(rstd::move(data));
}
inline auto elements(initializer_list<Element> values) -> Vec<Element> {
    Vec<Element> result; for (const auto &value : values) result.push(Element(value)); return result;
}
inline auto row(Vec<Element> children, rstd::source_location source = rstd::source_location::current()) -> Element { return native(LayoutKind::Row, rstd::move(children), source); }
inline auto row(initializer_list<Element> children, rstd::source_location source = rstd::source_location::current()) -> Element { return row(elements(children), source); }
inline auto column(Vec<Element> children, rstd::source_location source = rstd::source_location::current()) -> Element { return native(LayoutKind::Column, rstd::move(children), source); }
inline auto column(initializer_list<Element> children, rstd::source_location source = rstd::source_location::current()) -> Element { return column(elements(children), source); }
inline auto stack(Vec<Element> children, rstd::source_location source = rstd::source_location::current()) -> Element { return native(LayoutKind::Stack, rstd::move(children), source); }
inline auto stack(initializer_list<Element> children, rstd::source_location source = rstd::source_location::current()) -> Element { return stack(elements(children), source); }
inline auto fragment(Vec<Element> children = {}, rstd::source_location source = rstd::source_location::current()) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->source = source; data.get()->children = rstd::move(children); return Element(rstd::move(data));
}
inline auto fragment(initializer_list<Element> children, rstd::source_location source = rstd::source_location::current()) -> Element { return fragment(elements(children), source); }
inline auto text(ref<str> value, rstd::source_location source = rstd::source_location::current()) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Native;
    data.get()->source = source; data.get()->layout = LayoutKind::Text; data.get()->text = Some(UiTextValue(value)); return Element(rstd::move(data));
}
inline auto image(ImageData value, rstd::source_location source = rstd::source_location::current()) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Native;
    data.get()->source = source; data.get()->layout = LayoutKind::Image; data.get()->image = Some(UiImageValue{Some(rstd::move(value))}); return Element(rstd::move(data));
}
template <class Tag, class F> auto component(Key key, F evaluate, rstd::source_location source = rstd::source_location::current()) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Component;
    data.get()->key = Some(rstd::move(key)); data.get()->component_type = rstd::any::TypeId::of<Tag>(); data.get()->source = source;
    data.get()->view = rstd::rc::make_rc<ViewFunction>(rstd::move(evaluate)); return Element(rstd::move(data));
}
template <class T> requires rstd::Impled<T, ElementAdapter>
auto element(T config, Vec<Element> children = {}, rstd::source_location source = rstd::source_location::current()) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Custom;
    data.get()->source = source; data.get()->adapter = rstd::rc::make_rc<AdapterFunction>(rstd::move(config));
    data.get()->children = rstd::move(children); return Element(rstd::move(data));
}
} // namespace vui::compose
