export module vui:motion.properties;
import :animation;
import :binding;
using namespace rstd::prelude;

export namespace vui {
enum class PresentationProperty {
    Transform,
    Opacity,
    Width,
    Height,
    Background,
    Clip,
    ViewportOffset,
    ContentTint,
    PaintColor,
    TextColor,
    Visible,
    CharacterSpacing,
    Count
};
enum class TakeoverPolicy { PreserveCurrent, RejectOccupied };
enum class LeaseEndPolicy { CommitCurrent, RestoreBase };
struct PropertyKey {
    UiId target;
    PresentationProperty property;
    friend auto operator==(const PropertyKey &, const PropertyKey &) -> bool = default;
};
struct PropertyWrite {
    PropertyKey key;
    AnimationValue value{AnimationValue::Number(0)};
    vui::Clip clip;
    Point point;
    bool automatic_size{};
};
class PresentationBatch {
    Vec<PropertyWrite> writes_;

  public:
    auto writes() const -> slice<PropertyWrite> { return writes_.as_slice(); }
    void append(PropertyWrite value) { writes_.push(rstd::move(value)); }
    void transform(UiId id, Transform value) {
        writes_.push({{id, PresentationProperty::Transform}, AnimationValue::Matrix(value)});
    }
    void opacity(UiId id, Scalar value) {
        writes_.push({{id, PresentationProperty::Opacity}, AnimationValue::Number(value)});
    }
    void width(UiId id, Scalar value) {
        writes_.push({{id, PresentationProperty::Width}, AnimationValue::Number(value)});
    }
    void height(UiId id, Scalar value) {
        writes_.push({{id, PresentationProperty::Height}, AnimationValue::Number(value)});
    }
    void reset_width(UiId id) {
        writes_.push({{id, PresentationProperty::Width}, AnimationValue::Number(0), {}, {}, true});
    }
    void reset_height(UiId id) {
        writes_.push({{id, PresentationProperty::Height}, AnimationValue::Number(0), {}, {}, true});
    }
    void background(UiId id, Color value) {
        writes_.push({{id, PresentationProperty::Background}, AnimationValue::Tint(value)});
    }
    void content_tint(UiId id, Color value) {
        writes_.push({{id, PresentationProperty::ContentTint}, AnimationValue::Tint(value)});
    }
    void paint_color(UiId id, Color value) {
        writes_.push({{id, PresentationProperty::PaintColor}, AnimationValue::Tint(value)});
    }
    void text_color(UiId id, Color value) {
        writes_.push({{id, PresentationProperty::TextColor}, AnimationValue::Tint(value)});
    }
    void character_spacing(UiId id, Scalar value) {
        writes_.push({{id, PresentationProperty::CharacterSpacing}, AnimationValue::Number(value)});
    }
    void visible(UiId id, bool value) {
        writes_.push({{id, PresentationProperty::Visible}, AnimationValue::Number(value ? 1 : 0)});
    }
    void clip(UiId id, vui::Clip value) {
        writes_.push({{id, PresentationProperty::Clip}, AnimationValue::Number(0), value});
    }
    void viewport_offset(UiId id, Point value) {
        writes_.push(
            {{id, PresentationProperty::ViewportOffset}, AnimationValue::Number(0), {}, value});
    }
};
struct PropertyLeaseState {
    Serial document{}, id{};
    bool active{true};
    BindingGuard owner;
    Option<BindingGuard> driver;
    Vec<PropertyKey> keys;
    Vec<Serial> mounts;
    Vec<PropertyWrite> originals, current;
};
class PresentationLease {
    rstd::rc::Rc<PropertyLeaseState> state_;
    friend class UiDocument;
    explicit PresentationLease(rstd::rc::Rc<PropertyLeaseState> state)
        : state_(rstd::move(state)) {}

  public:
    PresentationLease() = default;
    PresentationLease(const PresentationLease &) = delete;
    auto operator=(const PresentationLease &) -> PresentationLease & = delete;
    PresentationLease(PresentationLease &&) = default;
    auto operator=(PresentationLease &&other) -> PresentationLease & {
        if (this != &other) {
            revoke();
            state_ = rstd::move(other.state_);
        }
        return *this;
    }
    ~PresentationLease() { revoke(); }
    auto active() const -> bool {
        return state_ && state_.get()->active && state_.get()->owner.alive() &&
               (state_.get()->driver.is_none() || state_.get()->driver->alive());
    }
    auto id() const -> Serial { return state_ ? state_.get()->id : 0; }
    auto current() const -> slice<PropertyWrite> {
        return state_ ? state_.get()->current.as_slice() : slice<PropertyWrite>{};
    }
    void revoke() {
        if (state_)
            state_.get()->active = false;
    }
};
} // namespace vui
