export module vui.controls:text_field;
import vui;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace vui {
struct TextFieldError {
    Option<UiError> ui;
    Option<InputError> input;
    Option<TextEditError> edit;
    Option<ClipboardError> clipboard;
    Option<state::StateError> state;
};
struct TextFieldStyle {
    TextFormat format{.wrap = TextWrap::None};
    Color text{1, 1, 1, 1}, placeholder{.6, .6, .6, 1};
    Color selection{.075, .2, .4, .5}, caret{1, 1, 1, 1}, composition{1, 1, 1, 1};
    Scalar caret_width{1},caret_blink_rate{};
    bool caret_with_selection{true};
    TextEffects effects;
    auto valid() const -> bool;
};
struct TextFieldOptions {bool select_all_on_focus{},restore_text_on_escape{},blur_on_submit{},blur_on_cancel{};};
struct TextFieldMetrics {
    Rect caret;
    Scalar offset{};
    Serial layout{};
    bool focused{},caret_visible{};
};
class TextFieldState {
    struct Blink {
        UiSession &session;UiId id;
        auto sample(MotionContext &context,FrameTiming) -> Result<MotionStatus,MotionError> {
            auto field=session.document().state<TextFieldState>(id);
            return field ? field->sample_blink(id,context) : Result<MotionStatus,MotionError>(Ok(MotionStatus::Finished));
        }
    };
    TextEdit edit_;
    Color selection_color_, caret_color_, composition_color_;
    Scalar caret_width_,caret_blink_rate_;
    bool caret_with_selection_;
    TextAlignment alignment_;
    TextFieldOptions options_;
    Option<TextBoundaries> original_;
    UiId label_, placeholder_;
    String semantic_label_;
    TextEditState displayed_;
    Option<TextPlacement> placement_;
    Point origin_;
    Rect content_clip_, caret_, selection_, underline_;
    Scalar offset_{};
    bool focused_{}, native_commit_{}, reset_requested_{};
    bool caret_visible_{true};
    UiSession *session_{};
    UiId id_;
    Option<Serial> blink_started_;
    rstd::rc::Rc<Blink> blink_;
    MotionRegistration motion_;
    Option<PointerKey> pointer_;
    Option<Serial> paste_request_;
    Option<state::Version> paste_version_;
    Option<Serial> write_request_;
    Option<state::Version> write_version_;
    bool write_cut_{};
    Option<TextFieldError> error_;
    Signal<void(EventContext &, const TextEditState &)> submitted_, cancelled_;
    Option<StateBinding<TextEditState>> binding_;

    auto describe(ref<str> text) -> Result<empty, UiError>;
    auto caret_visible() const -> bool;
    void reset_blink();
    void finish_focus(bool blur);
    auto sample_blink(UiId id,MotionContext &context) -> Result<MotionStatus,MotionError>;

    void consume(EventContext &context) { context.prevent_default(); context.stop_propagation(); }
    auto accept(Result<bool, TextEditError> result) -> bool {
        if (result.is_err()) { error_ = Some(TextFieldError{.edit = Some(result.unwrap_err())}); return false; }
        error_ = None(); return true;
    }
    void reset_native(EventContext &context, bool composing);
    auto hit(Point point) const -> Option<Index>;
    auto range_rect(TextRange range) const -> Rect;

  public:
    TextFieldState(TextEdit edit, TextFieldStyle appearance, UiId label, UiId placeholder, TextFieldOptions options = {},
                   ref<str> semantic_label = ""_str)
        : edit_(rstd::move(edit)), selection_color_(appearance.selection), caret_color_(appearance.caret),
          composition_color_(appearance.composition),caret_width_(appearance.caret_width),caret_blink_rate_(appearance.caret_blink_rate),
          caret_with_selection_(appearance.caret_with_selection),alignment_(appearance.format.alignment),options_(options),
          label_(label), placeholder_(placeholder), semantic_label_(String::make(semantic_label)), displayed_(edit_.state().unwrap()) {}
    auto model() -> TextEdit & { return edit_; }
    auto last_error() const -> Option<TextFieldError> { return error_; }
    auto metrics() const -> TextFieldMetrics { return {caret_, offset_, placement_.is_some() ? placement_->layout.id() : 0, focused_,caret_visible()}; }
    template <class F> auto on_submitted(BindingRegistry &registry, F callback) -> Result<Binding, SignalError> {
        return submitted_.connect(registry, rstd::move(callback));
    }
    template <class F> auto on_cancelled(BindingRegistry &registry, F callback) -> Result<Binding, SignalError> {
        return cancelled_.connect(registry, rstd::move(callback));
    }
    auto bind(UiSession &session, UiId id) -> Result<empty, TextFieldError>;
    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError>;
    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError>;
    auto paint(PaintContext &context) -> Result<empty, UiError>;
    void handle(EventContext &context, const RoutedEvent &event);
};
class TextField {
  public:
    static auto create(UiSession &session, NodeStyle style, ref<str> text = ""_str,
        TextFieldStyle appearance = {}, TextEditOptions options = {}, ref<str> placeholder = ""_str,TextFieldOptions behavior={}) -> Result<UiId, TextFieldError>;
    static auto model(UiDocument &ui, UiId id) -> TextEdit * { auto field = ui.state<TextFieldState>(id); return field ? &field->model() : nullptr; }
    static auto metrics(UiDocument &ui, UiId id) -> Option<TextFieldMetrics> { auto field = ui.state<TextFieldState>(id); return field ? Some(field->metrics()) : None<TextFieldMetrics>(); }
    static auto last_error(UiDocument &ui, UiId id) -> Option<TextFieldError> { auto field = ui.state<TextFieldState>(id); return field ? field->last_error() : Some(TextFieldError{.ui = Some(UiError::InvalidId)}); }
    template <class F> static auto on_submitted(UiDocument &ui, UiId id, F callback) -> Result<Binding, TextFieldError> {
        auto field = ui.state<TextFieldState>(id);
        if (!field) return Err(TextFieldError{.ui = Some(UiError::InvalidId)});
        auto result = field->on_submitted(ui.bindings(), rstd::move(callback));
        if (result.is_err()) return Err(TextFieldError{.ui = Some(UiError::Limit)});
        return Ok(rstd::move(result).unwrap());
    }
    template <class F> static auto on_cancelled(UiDocument &ui, UiId id, F callback) -> Result<Binding, TextFieldError> {
        auto field = ui.state<TextFieldState>(id);
        if (!field) return Err(TextFieldError{.ui = Some(UiError::InvalidId)});
        auto result = field->on_cancelled(ui.bindings(), rstd::move(callback));
        if (result.is_err()) return Err(TextFieldError{.ui = Some(UiError::Limit)});
        return Ok(rstd::move(result).unwrap());
    }
};
} // namespace vui
