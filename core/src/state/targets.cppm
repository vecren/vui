export module vui:state.targets;
import :binding.scope;
import :image.types;
using namespace rstd::prelude;

export namespace vui {
enum class UiValueProperty { Text, Visible, Image };
struct UiTargetStatus {
    bool valid{}, mounted{};
    Serial generation{};
    friend bool operator==(UiTargetStatus, UiTargetStatus) = default;
};
struct UiTargetObserver {
    BindingGuard guard;
    Box<rstd::dyn<rstd::FnMut<void(UiTargetStatus)>>> callback;
};
struct UiTargetState {
    Serial document;
    UiId node;
    UiValueProperty property;
    BindingGuard owner;
    UiTargetStatus status;
    Vec<rstd::rc::Rc<UiTargetObserver>> observers;
};
struct UiValueWriterState {
    rstd::rc::Rc<UiTargetState> target;
    bool active{true};
    explicit UiValueWriterState(rstd::rc::Rc<UiTargetState> value) : target(rstd::move(value)) {}
};
class UiDocument;
class UiValueBatch;
template <class T> class UiPropertyTarget {
    rstd::rc::Rc<UiTargetState> state_;
    friend class UiDocument;
    explicit UiPropertyTarget(rstd::rc::Rc<UiTargetState> state) : state_(rstd::move(state)) {}

  public:
    UiPropertyTarget() = default;
    auto node() const -> UiId { return state_ ? state_->node : UiId{}; }
    auto document() const -> Serial { return state_ ? state_->document : 0; }
    auto status() const -> UiTargetStatus {
        return state_ && state_->owner.alive() ? state_->status : UiTargetStatus{};
    }
};
template <class T> class UiValueWriter {
    rstd::rc::Rc<UiValueWriterState> state_;
    friend class UiDocument;
    friend class UiValueBatch;
    explicit UiValueWriter(rstd::rc::Rc<UiValueWriterState> state) : state_(rstd::move(state)) {}

  public:
    UiValueWriter() = default;
    UiValueWriter(const UiValueWriter &) = delete;
    UiValueWriter(UiValueWriter &&) = default;
    auto operator=(UiValueWriter &&other) -> UiValueWriter & {
        if (this != &other) {
            release();
            state_ = rstd::move(other.state_);
        }
        return *this;
    }
    ~UiValueWriter() { release(); }
    void release() {
        if (state_)
            state_.get()->active = false;
    }
    auto alive() const -> bool {
        return state_ && state_->active && state_->target->owner.alive() &&
               state_->target->status.valid;
    }
};
using UiTargetSubscription = Binding;
struct UiValueBatchError {
    usize index;
    UiError error;
};
class UiValueBatch {
    struct Entry {
        rstd::rc::Rc<UiValueWriterState> writer;
        Serial generation{};
        String text;
        bool visible{};
        Option<ImageData> image;
    };
    Vec<Entry> entries_;
    friend class UiDocument;
    template <class T> auto entry(const UiValueWriter<T> &writer) -> Entry {
        return {writer.state_,
                writer.state_ ? writer.state_->target->status.generation : 0,
                {},
                false,
                {}};
    }

  public:
    void set(const UiValueWriter<String> &writer, const String &value) {
        auto item = entry(writer);
        item.text = String::make(value.as_str());
        entries_.push(rstd::move(item));
    }
    void set(const UiValueWriter<bool> &writer, bool value) {
        auto item = entry(writer);
        item.visible = value;
        entries_.push(rstd::move(item));
    }
    void set(const UiValueWriter<Option<ImageData>> &writer, const Option<ImageData> &value) {
        auto item = entry(writer);
        if (value.is_some())
            item.image = Some(ImageData(*value));
        entries_.push(rstd::move(item));
    }
    auto size() const -> usize { return entries_.len(); }
};
} // namespace vui
