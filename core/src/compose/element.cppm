export module vui.compose:element;
import :types;
import :behavior;
import :environment;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui::compose {
struct ViewFunction {
    Box<rstd::dyn<rstd::FnMut<ComposeResult<Element>(CompositionContext &)>>> invoke;
    template <class F> explicit ViewFunction(F callback)
        : invoke(Box<rstd::dyn<rstd::FnMut<ComposeResult<Element>(CompositionContext &)>>>::make(rstd::move(callback))) {}
};
struct MovableIdentity {
    Serial identity{new_session()};
    rstd::rc::Rc<ViewFunction> view;
    rstd::rc::Weak<ComposeSignal> owner;
    rstd::source_location source;
    MovableIdentity(rstd::rc::Rc<ViewFunction> content, rstd::source_location location)
        : view(rstd::move(content)), source(location) {}
};
struct ComponentParameters {
    virtual ~ComponentParameters() = default;
    virtual auto type() const -> rstd::any::TypeId = 0;
    virtual auto equal(const ComponentParameters &) const -> bool = 0;
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
enum class ElementKind { Native, Fragment, Component, Custom, Provider, Movable };
enum class ModifierKind { Style, Gap, Padding, Format, Color, Reference, Behavior };
struct ElementModifier {
    ModifierKind kind;
    NodeStyle style;
    Scalar scalar{};
    Option<UiTextValue> text;
    Color color;
    Option<NodeRef> reference;
    Option<BehaviorDeclaration> behavior;
    explicit ElementModifier(ModifierKind value) : kind(value) {}
    ElementModifier(const ElementModifier &other)
        : kind(other.kind), style(other.style), scalar(other.scalar),
          text(other.text.is_some() ? Some(UiTextValue(*other.text)) : None<UiTextValue>()),
          color(other.color), reference(other.reference.is_some() ? Some(NodeRef(*other.reference)) : None<NodeRef>()),
          behavior(other.behavior.is_some() ? Some(BehaviorDeclaration(*other.behavior)) : None<BehaviorDeclaration>()) {}
    ElementModifier(ElementModifier &&) = default;
    auto operator=(ElementModifier other) -> ElementModifier & {
        kind = other.kind; style = other.style; scalar = other.scalar; text = rstd::move(other.text);
        color = other.color; reference = rstd::move(other.reference); behavior = rstd::move(other.behavior); return *this;
    }
};
struct ElementData;
class [[clang::annotate("vui.compose.element.v1")]] Element {
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
    auto identity(CallIdentity) const & -> Element;
    auto identity(CallIdentity) && -> Element;
    auto reference(NodeRef) const & -> Element;
    auto reference(NodeRef) && -> Element;
    auto behavior(BehaviorDeclaration) const & -> Element;
    auto behavior(BehaviorDeclaration) && -> Element;
    template <class T> auto behavior(Key key, T value) const & -> Element {
        return behavior(BehaviorDeclaration(rstd::move(key), rstd::move(value)));
    }
    template <class T> auto behavior(Key key, T value) && -> Element {
        return rstd::move(*this).behavior(BehaviorDeclaration(rstd::move(key), rstd::move(value)));
    }
};
struct ElementData {
    ElementKind kind{ElementKind::Fragment};
    LayoutKind layout{LayoutKind::Item};
    NodeStyle style;
    Option<Key> key;
    Option<CallIdentity> identity;
    Vec<ElementModifier> modifiers;
    Vec<BehaviorDeclaration> behaviors;
    Vec<LocalBinding> locals;
    Option<UiTextValue> text;
    Option<UiImageValue> image;
    Vec<Element> children;
    rstd::any::TypeId component_type{rstd::any::TypeId::of<void>()};
    rstd::rc::Rc<ViewFunction> view;
    rstd::rc::Rc<MovableIdentity> movable;
    rstd::rc::Rc<ComponentParameters> parameters;
    rstd::rc::Rc<AdapterFunction> adapter;
    Option<NodeRef> reference;
    bool invalid{};
    rstd::source_location source;
    ElementData() = default;
    ElementData(const ElementData &other);
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
inline auto Element::identity(CallIdentity value) const & -> Element { return copy().identity(rstd::move(value)); }
inline auto Element::reference(NodeRef value) const & -> Element { return copy().reference(rstd::move(value)); }
inline auto Element::behavior(BehaviorDeclaration value) const & -> Element { return copy().behavior(rstd::move(value)); }





inline auto Element::key(Key value) && -> Element { auto next = rstd::move(*this).owned(); next.data_.get()->key = Some(rstd::move(value)); return next; }
inline auto Element::identity(CallIdentity value) && -> Element {
    auto next = rstd::move(*this).owned(); next.data_.get()->identity = Some(rstd::move(value)); next.data_.get()->key = None<Key>(); return next;
}


auto native(LayoutKind kind, Vec<Element> children = {}, rstd::source_location source = rstd::source_location::current()) -> Element;
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
auto provide(Vec<LocalBinding> values, Element content, rstd::source_location source = rstd::source_location::current()) -> Element;
inline auto provide(initializer_list<LocalBinding> values, Element content,
                    rstd::source_location source = rstd::source_location::current()) -> Element {
    Vec<LocalBinding> owned;
    for (const auto &value : values) owned.push(LocalBinding(value));
    return provide(rstd::move(owned), rstd::move(content), source);
}
auto text(ref<str> value, rstd::source_location source = rstd::source_location::current()) -> Element;
auto image(ImageData value, rstd::source_location source = rstd::source_location::current()) -> Element;
template <class Tag, class F> auto component(Key key, F evaluate, rstd::source_location source = rstd::source_location::current(),
    rstd::rc::Rc<ComponentParameters> parameters = {}) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Component;
    data.get()->key = Some(rstd::move(key)); data.get()->component_type = rstd::any::TypeId::of<Tag>(); data.get()->source = source;
    data.get()->view = rstd::rc::make_rc<ViewFunction>(rstd::move(evaluate));
    data.get()->parameters = rstd::move(parameters); return Element(rstd::move(data));
}
template <class Tag, class F> auto component(CallIdentity identity, F evaluate, rstd::source_location source = rstd::source_location::current(),
    rstd::rc::Rc<ComponentParameters> parameters = {}) -> Element {
    return component<Tag>(Key{Serial{}}, rstd::move(evaluate), source, rstd::move(parameters)).identity(rstd::move(identity));
}
template <class T> requires rstd::Impled<T, ElementAdapter>
auto element(T config, Vec<Element> children = {}, rstd::source_location source = rstd::source_location::current()) -> Element {
    auto data = rstd::rc::make_rc<ElementData>(); data.get()->kind = ElementKind::Custom;
    data.get()->source = source; data.get()->adapter = rstd::rc::make_rc<AdapterFunction>(rstd::move(config));
    data.get()->children = rstd::move(children); return Element(rstd::move(data));
}
auto apply_modifiers(Element value, const Vec<ElementModifier> &modifiers) -> ComposeResult<Element>;
} // namespace vui::compose
