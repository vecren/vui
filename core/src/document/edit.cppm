export module vui:document.edit;
import :node.types;
import :text;
import :image;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui {
struct UiNodeRef {
    Serial batch{};
    usize index{};
    friend auto operator==(UiNodeRef, UiNodeRef) -> bool = default;
};
enum class UiEditOperation { Handle, Style, Text, Image, State, Children, Relation, Capacity };
struct UiEditError {
    UiError code;
    Option<UiNodeRef> node;
    UiEditOperation operation;
};
inline auto ui_edit_operation_name(UiEditOperation operation) -> ref<str> {
    switch (operation) {
    case UiEditOperation::Handle: return "handle"_str;
    case UiEditOperation::Style: return "style"_str;
    case UiEditOperation::Text: return "text"_str;
    case UiEditOperation::Image: return "image"_str;
    case UiEditOperation::State: return "state"_str;
    case UiEditOperation::Children: return "children"_str;
    case UiEditOperation::Relation: return "relation"_str;
    case UiEditOperation::Capacity: return "capacity"_str;
    }
    return "edit"_str;
}
class UiTextValue {
    String text_, family_{String::make("sans"_str)}, language_{String::make("und"_str)};
    TextFormat format_;

  public:
    Color color{0, 0, 0, 1};
    explicit UiTextValue(ref<str> text = ""_str) : text_(String::make(text)) {}
    UiTextValue(const UiTextValue &other)
        : text_(String::make(other.text())), family_(String::make(other.family_.as_str())),
          language_(String::make(other.language_.as_str())), format_(other.format_), color(other.color) {}
    UiTextValue(UiTextValue &&) = default;
    auto operator=(UiTextValue other) -> UiTextValue & {
        text_ = rstd::move(other.text_); family_ = rstd::move(other.family_);
        language_ = rstd::move(other.language_); format_ = other.format_; color = other.color;
        return *this;
    }
    auto text() const -> ref<str> { return text_.as_str(); }
    void set_format(TextFormat value) {
        Option<String> family, language;
        if (family_.as_str() != value.family) family = Some(String::make(value.family));
        if (language_.as_str() != value.language) language = Some(String::make(value.language));
        if (family.is_some()) family_ = family.unwrap();
        if (language.is_some()) language_ = language.unwrap();
        format_ = rstd::move(value);
    }
    auto format() const -> TextFormat {
        auto value = format_; value.family = family_.as_str(); value.language = language_.as_str();
        return value;
    }
};
struct UiImageValue {
    Option<ImageData> data;
    ImageFit fit{ImageFit::Contain};
    ImageSampling sampling{ImageSampling::Linear};
    Color tint{1, 1, 1, 1};
    ImageRadialFill radial;
};
struct UiNodeState {
    virtual ~UiNodeState() = default;
    virtual auto type() const -> rstd::any::TypeId = 0;
};
template <class T> struct UiStoredState final : UiNodeState {
    T value;
    explicit UiStoredState(T source) : value(rstd::move(source)) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<T>(); }
};
struct UiStatePatch {
    virtual ~UiStatePatch() = default;
    virtual auto type() const -> rstd::any::TypeId = 0;
    virtual void publish(UiNodeState &) = 0;
};
class UiDocument;
class UiEditBatch {
    struct Entry {
        Option<UiId> existing;
        LayoutKind kind{LayoutKind::Item};
        Option<NodeStyle> style;
        Option<UiTextValue> text;
        Option<UiImageValue> image;
        Option<Vec<UiNodeRef>> children;
        Option<Box<UiNodeState>> state;
        Option<Box<UiStatePatch>> patch;
        Option<NodeStyle> before_style;
        Option<UiTextValue> before_text;
        Option<UiImageValue> before_image;
    };
    Vec<Entry> entries_;
    Vec<UiId> removed_;
    Serial owner_{new_session()};
    Option<UiError> error_;
    auto entry(UiNodeRef ref) -> Entry * {
        if (ref.batch != owner_ || ref.index >= entries_.len()) { error_ = Some(UiError::InvalidId); return nullptr; }
        return &entries_[ref.index];
    }
    friend class UiDocument;

  public:
    auto existing(UiId id) -> UiNodeRef {
        for (usize i{}; i < entries_.len(); ++i)
            if (entries_[i].existing == Some(id)) return {owner_, i};
        UiNodeRef result{owner_, entries_.len()}; entries_.push({Some(id)}); return result;
    }
    auto create(LayoutKind kind, NodeStyle style = {}) -> UiNodeRef {
        UiNodeRef result{owner_, entries_.len()}; entries_.push({{}, kind, Some(style)}); return result;
    }
    void style(UiNodeRef id, NodeStyle value) { if (auto target = entry(id)) target->style = Some(value); }
    void style(UiNodeRef id, NodeStyle value, NodeStyle before) {
        if (auto target = entry(id)) target->before_style = Some(before); style(id, value);
    }
    void text(UiNodeRef id, UiTextValue value, const UiTextValue &before) {
        if (auto target = entry(id)) target->before_text = Some(UiTextValue(before)); text(id, rstd::move(value));
    }
    void image(UiNodeRef id, UiImageValue value, const UiImageValue &before) {
        auto target = entry(id); if (!target) return;
        target->before_image = Some(UiImageValue{
            before.data.is_some() ? Some(ImageData(*before.data)) : None<ImageData>(),
            before.fit, before.sampling, before.tint, before.radial});
        image(id, rstd::move(value));
    }
    void text(UiNodeRef id, UiTextValue value) { if (auto target = entry(id)) target->text = Some(rstd::move(value)); }
    void image(UiNodeRef id, UiImageValue value) { if (auto target = entry(id)) target->image = Some(rstd::move(value)); }
    void children(UiNodeRef id, Vec<UiNodeRef> values) {
        if (auto target = entry(id)) target->children = Some(rstd::move(values));
    }
    template <class T> void state(UiNodeRef id, T value) {
        auto target = entry(id); if (!target) return;
        auto stored = Box<UiStoredState<T>>::make(rstd::move(value));
        target->state = Some(Box<UiNodeState>::from_raw(
            rstd::move(stored).into_raw().template cast<UiNodeState>()));
    }
    template <class T> auto prepared_state(UiNodeRef id) -> T * {
        auto target = entry(id); if (!target) return nullptr;
        auto &value = target->state;
        if (value.is_none() || (**value).type() != rstd::any::TypeId::of<T>()) return nullptr;
        return &static_cast<UiStoredState<T> &>(**value).value;
    }
    void patch(UiNodeRef id, Box<UiStatePatch> value) {
        if (auto target = entry(id)) target->patch = Some(rstd::move(value));
    }
    void remove(UiId id) { removed_.push(UiId(id)); }
};
struct UiEditRetirement {
    virtual ~UiEditRetirement() = default;
};
struct UiCommitReport {
    Serial revision{};
    Vec<UiId> retired;
    usize created{}, updated{};
    Option<Box<UiEditRetirement>> retirement;
};
} // namespace vui
