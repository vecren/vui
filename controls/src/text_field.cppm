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
    auto valid() const -> bool {
        return format.valid() && format.wrap == TextWrap::None && text.valid() && placeholder.valid() &&
            selection.valid() && caret.valid() && composition.valid() && effects.valid() && finite(caret_width) && caret_width>0 &&
            finite(caret_blink_rate) && caret_blink_rate>=0 && (caret_blink_rate==0 ||
                (.5e9/caret_blink_rate>=1 && .5e9/caret_blink_rate<=Scalar(~Serial{} / 2)));
    }
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
    TextEditState displayed_;
    Option<TextPlacement> placement_;
    Point origin_;
    Rect caret_, selection_, underline_;
    Scalar offset_{};
    bool focused_{}, native_commit_{}, reset_requested_{};
    bool caret_visible_{true};
    UiSession *session_{};
    UiId id_;
    Option<Serial> blink_started_;
    rstd::rc::Rc<Blink> blink_;
    MotionRegistration motion_;
    Option<PointerKey> pointer_;
    Option<TextFieldError> error_;
    Signal<void(EventContext &, const TextEditState &)> submitted_, cancelled_;
    Option<StateBinding<TextEditState>> binding_;

    auto caret_visible() const -> bool {
        return focused_ && caret_visible_ &&
            (!displayed_.composing || displayed_.preedit_cursor.is_none() || displayed_.preedit_cursor->visible) &&
            (caret_with_selection_ || displayed_.composing || displayed_.selection.anchor==displayed_.selection.cursor);
    }
    void reset_blink() {
        if (!caret_visible_ && focused_ && session_) session_->document().invalidate_paint(id_).unwrap();
        caret_visible_=true;blink_started_=session_ ? session_->document().animation_sample_time() : None<Serial>();
        if (motion_.binding.alive()) session_->motion().request(motion_.id).unwrap();
    }
    void finish_focus(bool blur) {
        if (!blur || session_->input().focused()!=Some(id_)) return;
        auto result=session_->input().request_focus(None<UiId>());
        if (result.is_err()) error_=Some(TextFieldError{.input=Some(result.unwrap_err())});
    }
    auto sample_blink(UiId id,MotionContext &context) -> Result<MotionStatus,MotionError> {
        if (!focused_ || session_->input().focused()!=Some(id) || caret_blink_rate_==0 ||
            (!caret_with_selection_ && !displayed_.composing && displayed_.selection.anchor!=displayed_.selection.cursor)) return Ok(MotionStatus::Idle);
        auto now=context.now_ns();if (blink_started_.is_none() || now<*blink_started_) blink_started_=Some(Serial(now));
        auto half=Serial(.5e9/caret_blink_rate_);auto elapsed=now-*blink_started_;auto visible=(elapsed/half)%2==0;
        if (visible!=caret_visible_) {
            caret_visible_=visible;if (session_->document().invalidate_paint(id).is_err()) return Err(MotionError::Unavailable);
        }
        auto wait=half-elapsed%half;
        if (wait>~Serial{}-now) return Ok(MotionStatus::Idle);
        context.request_tick(now+wait).unwrap();return Ok(MotionStatus::Running);
    }

    void consume(EventContext &context) { context.prevent_default(); context.stop_propagation(); }
    auto accept(Result<bool, TextEditError> result) -> bool {
        if (result.is_err()) { error_ = Some(TextFieldError{.edit = Some(result.unwrap_err())}); return false; }
        error_ = None(); return true;
    }
    void reset_native(EventContext &context, bool composing) {
        if (!composing) return;
        auto result = context.restart_text_input();
        if (result.is_err()) error_ = Some(TextFieldError{.input = Some(result.unwrap_err())});
        else reset_requested_ = true;
    }
    auto hit(Point point) const -> Option<Index> {
        if (placement_.is_none()) return None();
        auto found = placement_->layout.hit_grapheme({point.x - origin_.x, point.y - origin_.y});
        if (found.is_err()) return None();
        auto committed = displayed_.committed_index(found->byte_index);
        return committed.is_ok() ? Some(committed.unwrap()) : None<Index>();
    }
    auto range_rect(TextRange range) const -> Rect {
        if (range.start == range.end || placement_.is_none()) return {};
        const auto &layout = placement_->layout;
        auto start = layout.boundaries().snap(range.start, TextBoundaryBias::Before).unwrap();
        auto end = layout.boundaries().snap(range.end, TextBoundaryBias::After).unwrap();
        auto left = layout.caret_grapheme(start).unwrap().bounds, right = layout.caret_grapheme(end).unwrap().bounds;
        return {origin_.x + rstd::cmp::min(left.x, right.x), origin_.y + left.y,
            __builtin_fabs(right.x - left.x), left.height};
    }

  public:
    TextFieldState(TextEdit edit, TextFieldStyle appearance, UiId label, UiId placeholder,TextFieldOptions options={})
        : edit_(rstd::move(edit)), selection_color_(appearance.selection), caret_color_(appearance.caret),
          composition_color_(appearance.composition),caret_width_(appearance.caret_width),caret_blink_rate_(appearance.caret_blink_rate),
          caret_with_selection_(appearance.caret_with_selection),alignment_(appearance.format.alignment),options_(options),label_(label), placeholder_(placeholder), displayed_(edit_.state().unwrap()) {}
    auto model() -> TextEdit & { return edit_; }
    auto last_error() const -> Option<TextFieldError> { return error_; }
    auto metrics() const -> TextFieldMetrics { return {caret_, offset_, placement_.is_some() ? placement_->layout.id() : 0, focused_,caret_visible()}; }
    template <class F> auto on_submitted(BindingRegistry &registry, F callback) -> Result<Binding, SignalError> {
        return submitted_.connect(registry, rstd::move(callback));
    }
    template <class F> auto on_cancelled(BindingRegistry &registry, F callback) -> Result<Binding, SignalError> {
        return cancelled_.connect(registry, rstd::move(callback));
    }
    auto bind(UiSession &session, UiId id) -> Result<empty, TextFieldError> {
        session_=&session;id_=id;
        if (caret_blink_rate_>0) {
            blink_=rstd::rc::make_rc<Blink>(session,id);auto bound=session.motion().bind(blink_.downgrade());
            if (bound.is_err()) return Err(TextFieldError{.ui=Some(UiError::Limit)});motion_=rstd::move(bound).unwrap();
        }
        auto router = &session.input();
        auto result = session.state_bindings().watch(edit_.value(), [id, router](StateUpdateContext &context, const TextEditState &value) -> Result<empty, UiError> {
            auto &ui = context.document(); auto field = ui.state<TextFieldState>(id);
            if (!field) return Ok(empty{});
            if (field->displayed_.composing && !value.composing && !field->native_commit_ && !field->reset_requested_ && router->text_input_client() == Some(id)) {
                auto reset = router->restart_text_input(id);
                if (reset.is_err()) field->error_ = Some(TextFieldError{.input = Some(reset.unwrap_err())});
            }
            auto surrounding = router->set_text_input_surrounding(id,
                {TextBoundaries(value.text), value.selection.anchor, value.selection.cursor},
                field->native_commit_ ? TextChangeCause::InputMethod : TextChangeCause::Other);
            if (surrounding.is_err()) field->error_ = Some(TextFieldError{.input = Some(surrounding.unwrap_err())});
            field->native_commit_ = field->reset_requested_ = false;
            field->displayed_ = value;field->reset_blink();
            auto written = ui.set_text(field->label_, value.display);
            if (written.is_err()) return written;
            auto visible = ui.set_visible(field->placeholder_, value.display.text()->is_empty());
            if (visible.is_err()) return visible;
            return ui.invalidate_layout(id);
        });
        if (result.is_err()) return Err(TextFieldError{.ui = result.unwrap_err().ui, .state = result.unwrap_err().state});
        binding_ = Some(rstd::move(result).unwrap()); return Ok(empty{});
    }
    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError> {
        Size desired;
        for (usize i{}; i < context.child_count(); ++i) {
            auto result = context.measure_child(i,{alignment_==TextAlignment::Natural ? None<Scalar>() : constraints.max_width,None<Scalar>()});
            if (result.is_err()) return Err(result.unwrap_err());
            desired.width = rstd::cmp::max(desired.width, result->width);
            desired.height = rstd::cmp::max(desired.height, result->height);
        }
        return Ok(desired);
    }
    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        auto text = context.child_text(usize());
        if (text.is_err()) return Err(text.unwrap_err());
        placement_ = Some(rstd::move(text).unwrap());
        auto caret = placement_->layout.caret_grapheme(displayed_.caret);
        if (caret.is_err()) return Err(UiError::TextLayoutFailed);
        auto desired = context.desired_child(usize()).unwrap();
        auto x = caret->bounds.x + placement_->origin.x;
        auto available = rstd::cmp::max(Scalar(0), size.width - caret_width_);
        if (x < offset_) offset_ = x;
        if (x > offset_ + available) offset_ = x - available;
        auto logical=placement_->layout.logical_bounds();auto lower=rstd::cmp::min(Scalar(0),logical.x+placement_->origin.x);
        auto upper=rstd::cmp::max(lower,logical.x+logical.width+placement_->origin.x-available);
        offset_=rstd::cmp::max(lower,rstd::cmp::min(offset_,upper));
        auto y = rstd::cmp::max(Scalar(0), (size.height - desired.height) / 2);
        auto padding = context.style().padding;
        origin_ = {padding - offset_ + placement_->origin.x, padding + y + placement_->origin.y};
        caret_ = {origin_.x + caret->bounds.x, origin_.y + caret->bounds.y, caret_width_, caret->bounds.height};
        selection_ = displayed_.composing ? Rect{} : range_rect(displayed_.selection.range());
        underline_ = displayed_.composing ? range_rect(displayed_.composition) : Rect{};
        if (displayed_.composing && displayed_.preedit_selection.is_some() && displayed_.preedit_selection->end.is_some())
            selection_ = range_rect({displayed_.composition.start + displayed_.preedit_selection->start,
                displayed_.composition.start + *displayed_.preedit_selection->end});
        for (usize i{}; i < context.child_count(); ++i) {
            auto child = context.desired_child(i).unwrap();
            auto placed = context.arrange_child(i, {i == usize() ? -offset_ : Scalar(0), y,
                rstd::cmp::max(size.width, child.width), rstd::cmp::max(desired.height, child.height)});
            if (placed.is_err()) return placed;
        }
        auto clipped = context.set_clip({true, {0, 0, size.width, size.height}});
        if (clipped.is_err()) return clipped;
        auto area = caret_;
        area.x = rstd::cmp::max(padding, rstd::cmp::min(area.x, padding + available));
        area.y = rstd::cmp::max(padding, rstd::cmp::min(area.y, padding + size.height));
        area.height = rstd::cmp::min(area.height, size.height);
        area.x -= padding; area.y -= padding;
        auto input = context.set_text_input_area(Some(area));
        if (input.is_err()) return input;
        context.request_paint(); return Ok(empty{});
    }
    auto paint(PaintContext &context) -> Result<empty, UiError> {
        if (focused_ && selection_.width > 0) context.rect(selection_, selection_color_);
        context.children();
        if (focused_ && underline_.width > 0)
            context.rect({underline_.x, underline_.y + underline_.height - 1, underline_.width, 1}, composition_color_);
        if (caret_visible()) context.rect(caret_, caret_color_);
        return Ok(empty{});
    }
    void handle(EventContext &context, const RoutedEvent &event) {
        const auto &input = event.input;
        if (input.type == EventType::FocusGained || input.type == EventType::FocusLost) {
            focused_ = input.type == EventType::FocusGained;
            if (focused_) {
                original_=Some(edit_.state().unwrap().text);
                if (options_.select_all_on_focus) accept(edit_.select_all());
            } else { accept(edit_.cancel_composition()); pointer_ = None();original_=None(); }
            reset_blink();
            context.document().invalidate_paint(event.current).unwrap(); return;
        }
        if (input.type == EventType::TextInputCancelled) { accept(edit_.cancel_composition()); return; }
        if (input.type == EventType::PointerCancel || input.type == EventType::CaptureLost) { pointer_ = None(); return; }
        auto snapshot = edit_.state();
        if (snapshot.is_err()) { error_ = Some(TextFieldError{.edit = Some(snapshot.unwrap_err())}); return; }
        const auto &value = snapshot.unwrap();
        if (input.type == EventType::TextInput) { native_commit_ = accept(edit_.insert(input.text.text())); consume(context); return; }
        if (input.type == EventType::TextEditing) { accept(edit_.preedit(input.text)); consume(context); return; }
        if (input.type == EventType::TextTransaction) {
            auto result = edit_.apply_input_transaction(*input.transaction.get());
            if (result.is_err()) error_ = Some(TextFieldError{.edit = Some(result.unwrap_err())});
            else { native_commit_ |= result.unwrap(); error_ = None(); }
            consume(context); return;
        }
        PointerKey pointer{input.device, input.pointer};
        if (input.type == EventType::PointerDown && (input.pointer_kind == PointerKind::Touch || input.button == PointerButton::Primary)) {
            if (pointer_.is_some() || context.capture_pointer().is_err()) return;
            auto select_all=options_.select_all_on_focus && !focused_;
            pointer_ = Some(pointer); context.focus();
            if (auto at = hit(event.local); !select_all && at.is_some() && accept(edit_.select({input.modifiers.shift ? value.selection.anchor : *at, *at})))
                reset_native(context, value.composing);
            reset_blink();
            consume(context); return;
        }
        if (input.type == EventType::PointerMove && pointer_ == Some(pointer)) {
            if (auto at = hit(event.local); at.is_some()) accept(edit_.select({value.selection.anchor, *at}));
            consume(context); return;
        }
        if (input.type == EventType::PointerUp && pointer_ == Some(pointer) && (input.pointer_kind == PointerKind::Touch || input.button == PointerButton::Primary)) {
            pointer_ = None(); context.release_pointer(); consume(context); return;
        }
        if (input.type != EventType::KeyDown || event.target != event.current) return;
        bool command = (input.modifiers.control || input.modifiers.meta) && !input.modifiers.alt;
        if (value.composing && !command && (input.key == Key::Left || input.key == Key::Right || input.key == Key::Home ||
            input.key == Key::End || input.key == Key::Backspace || input.key == Key::Delete || input.key == Key::Enter)) {
            consume(context); return;
        }
        bool changed{};
        if (command && input.key == Key::C) {
            if (!value.selected_text()->is_empty()) {
                auto result = context.write_clipboard(value.selected_text());
                error_ = result.is_err() ? Some(TextFieldError{.clipboard = Some(result.unwrap_err())}) : None<TextFieldError>();
            }
        } else if (command && input.key == Key::X) {
            if (!value.selected_text()->is_empty()) {
                auto result = context.write_clipboard(value.selected_text());
                if (result.is_err()) error_ = Some(TextFieldError{.clipboard = Some(result.unwrap_err())});
                else changed = accept(edit_.insert(""_str));
            }
        } else if (command && input.key == Key::V) {
            auto result = context.read_clipboard();
            if (result.is_err()) error_ = Some(TextFieldError{.clipboard = Some(result.unwrap_err())});
            else changed = accept(edit_.insert(result->as_str()));
        } else if (command && input.key == Key::A) changed = accept(edit_.select_all());
        else if (command && input.key == Key::Z) changed = accept(input.modifiers.shift ? edit_.redo() : edit_.undo());
        else if (command && input.key == Key::Y) changed = accept(edit_.redo());
        else if (input.key == Key::Left || input.key == Key::Right) changed = accept(edit_.move(input.key == Key::Left ?
            (command ? TextEditMove::PreviousWord : TextEditMove::Previous) : (command ? TextEditMove::NextWord : TextEditMove::Next), input.modifiers.shift));
        else if (input.key == Key::Home || input.key == Key::End) changed = accept(edit_.move(input.key == Key::Home ? TextEditMove::Start : TextEditMove::End, input.modifiers.shift));
        else if (input.key == Key::Backspace) changed = accept(edit_.erase_backwards());
        else if (input.key == Key::Delete) changed = accept(edit_.erase_forwards());
        else if (input.key == Key::Enter) {
            if (!value.composing && !input.repeat) {submitted_.emit(context,value).unwrap();finish_focus(options_.blur_on_submit);}
        }
        else if (input.key == Key::Escape) {
            if (value.composing) changed = accept(edit_.cancel_composition());
            else if (!input.repeat) {
                if (options_.restore_text_on_escape && original_.is_some()) changed=accept(edit_.set_text(original_->text()));
                cancelled_.emit(context, edit_.state().unwrap()).unwrap();
                finish_focus(options_.blur_on_cancel);
            }
        } else if (command || (input.key != Key::Unknown && input.key != Key::Space && (input.key < Key::A || input.key > Key::Z))) return;
        if (changed) {reset_native(context,value.composing);reset_blink();}
        consume(context);
    }
};
class TextField {
  public:
    static auto create(UiSession &session, NodeStyle style, ref<str> text = ""_str,
        TextFieldStyle appearance = {}, TextEditOptions options = {}, ref<str> placeholder = ""_str,TextFieldOptions behavior={}) -> Result<UiId, TextFieldError> {
        if (!appearance.valid() || !options.single_line) return Err(TextFieldError{.ui = Some(UiError::InvalidValue)});
        auto model = TextEdit::create(text, options);
        if (model.is_err()) return Err(TextFieldError{.edit = Some(model.unwrap_err())});
        auto &ui = session.document(); style.focusable = true; style.hit_self = true;
        auto created = ui.create<CustomLayout>(style);
        if (created.is_err()) return Err(TextFieldError{.ui = Some(created.unwrap_err())});
        auto id = created.unwrap();
        auto fail = [&](TextFieldError error) -> Result<UiId, TextFieldError> { ui.destroy(id); return Err(error); };
        NodeStyle child; child.hit_test_visible = false;
        auto label = ui.append<Text>(id, child), hint = ui.append<Text>(id, child);
        if (label.is_err() || hint.is_err()) return fail({.ui = Some(label.is_err() ? label.unwrap_err() : hint.unwrap_err())});
        const UiId text_nodes[]{label.unwrap(), hint.unwrap()};
        for (auto node : text_nodes) {
            auto result = ui.set_text_format(node, appearance.format);
            if (result.is_err()) return fail({.ui = Some(result.unwrap_err())});
            result=ui.set_text_effects(node,appearance.effects);
            if (result.is_err()) return fail({.ui=Some(result.unwrap_err())});
        }
        ui.set_text_color(label.unwrap(), appearance.text).unwrap(); ui.set_text_color(hint.unwrap(), appearance.placeholder).unwrap();
        ui.set_text(hint.unwrap(), placeholder).unwrap();
        auto state = ui.attach_state(id, TextFieldState(rstd::move(model).unwrap(), appearance, label.unwrap(), hint.unwrap(),behavior));
        if (state.is_err()) return fail({.ui = Some(state.unwrap_err())});
        auto layout = ui.attach_layout<TextFieldState>(id);
        if (layout.is_err()) return fail({.ui = Some(layout.unwrap_err())});
        auto paint = ui.attach_paint<TextFieldState>(id);
        if (paint.is_err()) return fail({.ui = Some(paint.unwrap_err())});
        auto attached_behavior = session.input().attach_behavior<TextFieldState>(id);
        if (attached_behavior.is_err()) return fail({.input = Some(attached_behavior.unwrap_err())});
        auto bound = ui.state<TextFieldState>(id)->bind(session, id);
        if (bound.is_err()) return fail(bound.unwrap_err());
        return Ok(id);
    }
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
