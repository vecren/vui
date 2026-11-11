export module vui:input.protocol;
import :diagnostics;
import :text;
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
    TextInputCancelled,
    WindowPointerEnter,
    WindowPointerLeave,
    TextTransaction
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
enum class ClipboardError { Unavailable, Failed, Unsupported, InvalidText, Limit };
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
struct TextPreeditCursor {
    Index anchor{}, cursor{};
    bool visible{true};
    bool has_range{};
    auto operator==(const TextPreeditCursor &) const -> bool = default;
};
struct TextSurroundingRevision {
    Serial text{};
    Index anchor{}, cursor{};
    auto operator==(const TextSurroundingRevision &) const -> bool = default;
};
struct TextInputTransaction {
    TextInput commit;
    TextInput preedit;
    Option<TextPreeditCursor> preedit_cursor;
    Index delete_before{}, delete_after{};
    Option<TextSurroundingRevision> expected_surrounding;
    auto valid() const -> bool {
        if ((expected_surrounding.is_some() && expected_surrounding->text == 0) ||
            (commit.present() && commit.selection().is_some()) ||
            (preedit.present() && preedit.selection().is_some()) ||
            (preedit_cursor.is_some() && !preedit.present())) return false;
        if (preedit_cursor.is_some() && preedit_cursor->visible) {
            auto text = preedit.text();
            if (usize(preedit_cursor->anchor) > text->len() ||
                usize(preedit_cursor->cursor) > text->len() ||
                !text->is_char_boundary(usize(preedit_cursor->anchor)) ||
                !text->is_char_boundary(usize(preedit_cursor->cursor))) return false;
        }
        return true;
    }
};
class TextTransaction {
    Option<rstd::sync::Arc<TextInputTransaction>> data_;
    explicit TextTransaction(TextInputTransaction transaction)
        : data_(Some(rstd::sync::Arc<TextInputTransaction>::make(rstd::move(transaction)))) {}
  public:
    TextTransaction() = default;
    TextTransaction(const TextTransaction &other)
        : data_(other.data_.is_some() ? Some(other.data_->clone()) : None<rstd::sync::Arc<TextInputTransaction>>()) {}
    auto operator=(const TextTransaction &other) -> TextTransaction & {
        if (this != &other) data_ = other.data_.is_some() ? Some(other.data_->clone()) : None<rstd::sync::Arc<TextInputTransaction>>();
        return *this;
    }
    TextTransaction(TextTransaction &&) = default;
    auto operator=(TextTransaction &&) -> TextTransaction & = default;
    static auto create(TextInputTransaction transaction) -> Result<TextTransaction, InputError> {
        if (!transaction.valid()) return Err(InputError::InvalidEvent);
        return Ok(TextTransaction(rstd::move(transaction)));
    }
    auto get() const -> const TextInputTransaction * {
        return data_.is_some() ? data_->as_ptr().as_raw_ptr() : nullptr;
    }
};
enum class TextChangeCause { InputMethod, Other };
struct TextSurroundingRange {
    TextBoundaries text;
    Index start{}, end{}, anchor{}, cursor{};
    auto view() const -> ref<str> { return text.text()->get(usize(start), usize(end)).unwrap(); }
};
struct TextSurrounding {
    TextBoundaries text;
    Index anchor{}, cursor{};
    auto present() const -> bool { return text.present(); }
    auto valid() const -> bool {
        if (!present()) return anchor == 0 && cursor == 0;
        auto content = text.text();
        return content->len() <= usize(~Index{}) && usize(anchor) <= content->len() &&
            usize(cursor) <= content->len() && content->is_char_boundary(usize(anchor)) &&
            content->is_char_boundary(usize(cursor));
    }
    auto window(Index max_bytes) const -> Option<TextSurroundingRange> {
        if (!present() || !valid()) return None();
        auto content = text.text();
        Index length = Index(content->len().to_primitive());
        auto low = rstd::cmp::min(anchor, cursor), high = rstd::cmp::max(anchor, cursor);
        if (high - low > max_bytes) return None();
        auto room = max_bytes - (high - low);
        auto left = rstd::cmp::min(low, room / 2);
        auto right = rstd::cmp::min(length - high, room - left);
        left += rstd::cmp::min(low - left, room - left - right);
        auto start = low - left, end = high + right;
        while (start < low && !content->is_char_boundary(usize(start))) ++start;
        while (end > high && !content->is_char_boundary(usize(end))) --end;
        return Some(TextSurroundingRange{TextBoundaries(text), start, end, anchor - start, cursor - start});
    }
    auto operator==(const TextSurrounding &) const -> bool = default;
};
struct TextInputRequest {
    Serial session{};
    Rect area;
    auto valid() const -> bool { return session != 0 && area.valid(); }
    auto operator==(const TextInputRequest &) const -> bool = default;
};
struct TextInputState {
    TextInputRequest request;
    TextSurrounding surrounding;
    TextChangeCause change_cause{TextChangeCause::Other};
    auto valid() const -> bool { return request.valid() && surrounding.valid() &&
        unsigned(change_cause) <= unsigned(TextChangeCause::Other); }
    auto operator==(const TextInputState &) const -> bool = default;
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
    TextTransaction transaction;
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
