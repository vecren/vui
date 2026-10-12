export module vui:input.protocol;
import :diagnostics;
using namespace rstd::prelude;

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
    DeviceLost
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
    PageDown
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
};
struct RoutedEvent {
    InputEvent input;
    UiId target, current;
    RoutePhase phase{};
    Point local;
    Serial ui_revision{};
};
enum class InputError { InvalidId, InvalidEvent, InvalidSequence, Unavailable, Reentrant, Limit };
struct InputPacket {
    const InputEvent &input;
    UiId target;
};
} // namespace vui
