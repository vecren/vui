export module vui:input.protocol;
import :diagnostics;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui {
enum class EventType {
    PointerMove,
    PointerDown,
    PointerUp,
    PointerCancel,
    PointerEnter,
    PointerLeave,
    CaptureGained,
    CaptureLost,
    KeyDown,
    KeyUp,
    FocusGained,
    FocusLost,
    WindowFocusLost,
    WindowFocusGained,
    Wheel,
    DeviceLost,
    TextInput,
    TextEditing,
    TextInputCancelled
};
enum class InputDelivery { EverySample, LatestWithHistory };
enum class RoutePhase { Capture, Target, Bubble };
enum class PointerButton { None, Primary, Secondary, Middle, Back, Forward };
enum class PointerKind { Mouse, Pen, Touch };
enum class Key {
    Unknown,
    Tab,
    Enter,
    Space,
    Escape,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Backspace, Delete
};
enum class ScrollUnit { Pixels, Lines };
struct Modifiers {
    bool shift{}, control{}, alt{}, meta{};
};
using DeviceId = Serial;
using SequenceId = Serial;
enum class TimeOrigin { Native, Received };
struct SampleTime {
    Serial clock{}, nanoseconds{};
    Serial precision_ns{1'000'000};
    TimeOrigin origin{TimeOrigin::Received};
};
enum class CancelReason {
    None,
    Native,
    FocusLost,
    Detached,
    Superseded,
    ReceiverLost,
    Explicit,
    DeviceLost,
    Closed,
    ScopeChanged
};
enum class NativeCaptureResult { Succeeded, Failed, Unsupported };
enum class NativeTextInputResult { Succeeded, Failed, Unsupported };
enum class InputError { InvalidId, InvalidEvent, InvalidSequence, Unavailable, Reentrant, Limit };
struct TextInputSelection {
    Index start{};
    Option<Index> end;
    auto operator==(const TextInputSelection &) const -> bool = default;
};
class TextInput {
    struct Data {
        String text;
        Option<TextInputSelection> selection;
        Data(ref<str> value, Option<TextInputSelection> range) : text(String::make(value)), selection(range) {}
    };
    Option<rstd::sync::Arc<Data>> data_;
    TextInput(ref<str> text, Option<TextInputSelection> selection)
        : data_(Some(rstd::sync::Arc<Data>::make(text, selection))) {}
  public:
    TextInput() = default;
    TextInput(const TextInput &other) : data_(other.data_.is_some() ? Some(other.data_->clone()) : None<rstd::sync::Arc<Data>>()) {}
    auto operator=(const TextInput &other) -> TextInput & {
        if (this != &other) data_ = other.data_.is_some() ? Some(other.data_->clone()) : None<rstd::sync::Arc<Data>>();
        return *this;
    }
    TextInput(TextInput &&) = default;
    auto operator=(TextInput &&) -> TextInput & = default;
    static auto create(ref<str> text, Option<TextInputSelection> selection = {}) -> Result<TextInput, InputError> {
        if (text->len() > usize(~Index{})) return Err(InputError::Limit);
        if (selection.is_some() &&
            (usize(selection->start) > text->len() || !text->is_char_boundary(usize(selection->start)) ||
             (selection->end.is_some() && (*selection->end < selection->start ||
                usize(*selection->end) > text->len() || !text->is_char_boundary(usize(*selection->end))))))
            return Err(InputError::InvalidEvent);
        return Ok(TextInput(text, selection));
    }
    auto present() const -> bool { return data_.is_some(); }
    auto text() const -> ref<str> { return data_.is_some() ? (*data_)->text.as_str() : ""_str; }
    auto selection() const -> Option<TextInputSelection> { return data_.is_some() ? (*data_)->selection : None<TextInputSelection>(); }
};
struct TextInputRequest {
    Serial session{};
    Rect area;
    auto valid() const -> bool { return session != 0 && area.valid(); }
    auto operator==(const TextInputRequest &) const -> bool = default;
};
enum class ScrollPhase { Unknown, Begin, Update, End };
struct PointerSample {
    SampleTime time;
    Point position;
    Option<Scalar> pressure;
    Option<Point> tilt;
    Option<Scalar> twist;
};
struct InputWork {
    Serial routes{}, observations{}, claims{}, handlers_examined{}, behaviors_examined{};
};
struct InputEvent {
    EventType type{EventType::PointerMove};
    Index pointer{};
    Point position{};
    PointerButton button{};
    PointerKind pointer_kind{};
    Key key{};
    Modifiers modifiers{};
    bool repeat{};
    Point scroll_delta;
    ScrollUnit scroll_unit{ScrollUnit::Pixels};
    Option<InputStamp> stamp;
    DeviceId device{};
    SequenceId sequence{};
    SampleTime sample_time;
    // Borrowed for the current input batch and callbacks.
    slice<PointerSample> history;
    Option<Scalar> pressure;
    ScrollPhase scroll_phase{ScrollPhase::Unknown};
    Option<bool> momentum;
    Serial coordinate_generation{};
    CancelReason cancel_reason{CancelReason::None};
    Option<Serial> age_at_receipt_ns;
    Serial history_lost{};
    Option<Point> tilt;
    Option<Scalar> twist;
    TextInput text;
    Serial text_session{};
};
struct RoutedEvent {
    InputEvent input;
    UiId target, current;
    RoutePhase phase{};
    Point local;
    Serial ui_revision{};
};
struct InputPacket {
    const InputEvent &input;
    UiId target;
};
} // namespace vui
