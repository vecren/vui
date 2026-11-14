export module vui.examples.interaction.view;
import vui.examples;
import vui.compose;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace interaction_example {
using namespace vui;
namespace cx = vui::compose;
class Panel {
    enum class Mode { Idle, Direct, Spring };
    UiSession &session_;
    BindingGuard session_lifetime_{session_.document().lifetime()};
    UiId root_, shade_, curtain_, content_root_, handle_;
    cx::NodeRef shade_ref_, curtain_ref_, content_ref_, handle_ref_;
    Size size_{640, 520};
    auto content(cx::CompositionContext &, rstd::rc::Weak<Panel>) -> cx::ComposeResult<cx::Element>;
    void stop();
    Scalar pending_delta_{};
    Scalar height_{340};
    ScalarMotion state_;
    Spring spring_;
    Serial started_{};
    Mode mode_{Mode::Idle};
    Option<PointerKey> pointer_;
    Scalar anchor_y_{}, anchor_value_{};
    VelocityTracker velocity_;
    PanRecognizer pan_{{6, PanAxis::Vertical, 0}};
    PresentationLease lease_;
    Binding behavior_;
    ParticipantRegistration participant_;
    MotionRegistration motion_;

    auto ui() -> UiDocument & { return session_.document(); }
    void acquire() {
        PropertyKey keys[] = {{shade_, PresentationProperty::Transform},
                              {curtain_, PresentationProperty::Opacity}};
        lease_ = ui().take_over(slice<PropertyKey>::from_raw_parts(keys, usize(2))).unwrap();
    }
    auto presentation(Scalar value) const -> PresentationBatch {
        PresentationBatch batch;
        batch.transform(shade_, {1, 0, 0, 1, 0, (value - 1) * height_ + 40});
        batch.opacity(curtain_, rstd::min(Scalar(.65), rstd::cmp::max(Scalar(0), value * .65)));
        return batch;
    }
    void grab(EventContext &context, const RoutedEvent &event) {
        acquire();
        mode_ = Mode::Direct;
        pointer_ = Some(PointerKey{event.input.device, event.input.pointer});
        anchor_y_ = event.input.position.y;
        if (pending_delta_) {
            state_.value += pending_delta_ / height_;
            pending_delta_ = 0;
            auto batch = presentation(state_.value);
            ui().write_presentation(lease_, batch).unwrap();
        }
        anchor_value_ = state_.value;
        context.focus();
    }
    void settle(Scalar target, Scalar velocity) {
        if (!lease_.active())
            acquire();
        spring_ = {{state_.value, velocity}, target, 220, 27, 1};
        started_ = ui().animation_sample_time().is_some() ? *ui().animation_sample_time() : 0;
        mode_ = Mode::Spring;
        pointer_ = None();
        session_.motion().request(motion_.id).unwrap();
    }
    static auto label(ref<str> value, Point origin, Scalar size, Color color) -> cx::Element {
        return cx::text(value).format(TextFormatBuilder{}.size(size).build()).color(color)
            .style(NodeStyleBuilder{}.transform({1, 0, 0, 1, origin.x, origin.y}).hit_test_visible(false).build());
    }

  public:
    explicit Panel(UiSession &session) : session_(session) {
        auto service = Box<vectext::SystemTextService>::make();
        ui().set_text_service(Box<TextService>::from_raw(rstd::move(service).into_raw().template cast<TextService>())).unwrap();
        root_ = ui().create<Stack>(NodeStyleBuilder{examples::block(640, 520, {.91f, .94f, .97f, 1})}.focusable(true).build()).unwrap();
    }
    static auto make(UiSession &session) -> rstd::rc::Rc<Panel>;
    void close() {
        if (!session_lifetime_.alive()) return;
        if (auto composition = ui().state<cx::Composition>(root_)) composition->close();
    }
    auto last_result() -> Option<cx::ComposeResult<cx::CommitReport>> {
        if (!session_lifetime_.alive()) return Some(cx::ComposeResult<cx::CommitReport>(Err(cx::ComposeError(cx::ComposeStage::Closed))));
        auto composition = ui().state<cx::Composition>(root_);
        return composition ? composition->last_result() : None<cx::ComposeResult<cx::CommitReport>>();
    }
    auto check() -> Result<empty, UiError> {
        if (!session_lifetime_.alive()) return Err(UiError::Cancelled);
        auto composition = ui().state<cx::Composition>(root_);
        return composition ? examples::composition_status(*composition) : Result<empty, UiError>(Ok(empty{}));
    }
    auto root() const -> UiId { return root_; }
    auto content_root() const -> UiId { return content_root_; }
    void handoff(PointerKey pointer, Scalar delta) {
        pending_delta_ = delta;
        session_.input()
            .transfer_input(participant_.id, slice<PointerKey>::from_raw_parts(&pointer, usize(1)))
            .unwrap();
    }
    auto value() const -> ScalarMotion { return state_; }
    auto dragging() const -> bool { return mode_ == Mode::Direct; }
    void resize(const SurfaceInfo &surface) {
        if (size_ == surface.logical_size) return;
        size_ = surface.logical_size;
        height_ = rstd::min(Scalar(340), rstd::cmp::max(Scalar(180), size_.height - 40));
        ui().set_layout(root_, LayoutStyleBuilder{}.width(size_.width).height(size_.height).build()).unwrap();
        if (auto composition = ui().state<cx::Composition>(root_)) composition->invalidate();
    }
    auto observe(InputContext &context, const InputPacket &packet) -> InputDecision {
        const auto &input = packet.input;
        PointerKey key{input.device, input.pointer};
        if (input.type == EventType::PointerDown) {
            if (pointer_.is_some() && *pointer_ != key)
                return InputDecision::reject();
            if (state_.value < .01 && input.position.y > 55)
                return InputDecision::reject();
            velocity_.add(input);
            if (mode_ == Mode::Spring)
                return InputDecision::accept();
        } else
            velocity_.add(input);
        return pan_.observe(context, packet);
    }
    void cancel(InputContext &context, CancelReason reason) { pan_.cancel(context, reason); }
    void handle(EventContext &context, const RoutedEvent &event) {
        if (event.input.type == EventType::CaptureGained) {
            grab(context, event);
            return;
        }
        if (event.input.type == EventType::Wheel) {
            if (mode_ == Mode::Direct)
                return;
            auto amount = context.scroll_remaining();
            if (!amount.y)
                return;
            acquire();
            mode_ = Mode::Idle;
            auto step = event.input.scroll_unit == ScrollUnit::Lines ? .08 : 1 / height_;
            auto previous = state_.value;
            state_.value =
                rstd::min(Scalar(1), rstd::cmp::max(Scalar(0), state_.value + amount.y * step));
            state_.velocity = 0;
            auto batch = presentation(state_.value);
            ui().write_presentation(lease_, batch).unwrap();
            context.consume_scroll({0, (state_.value - previous) / step}).unwrap();
            context.prevent_default();
            return;
        }
        if (event.input.type == EventType::KeyDown &&
            (event.input.key == Key::Space || event.input.key == Key::Enter)) {
            settle(state_.value < .5 ? 1 : 0, state_.velocity);
            context.prevent_default();
            return;
        }
        PointerKey key{event.input.device, event.input.pointer};
        if (pointer_ != Some(PointerKey(key)))
            return;
        if (event.input.type == EventType::PointerCancel &&
            (event.input.cancel_reason == CancelReason::Closed ||
             event.input.cancel_reason == CancelReason::Detached ||
             event.input.cancel_reason == CancelReason::ReceiverLost)) {
            mode_ = Mode::Idle;
            pointer_ = None();
            lease_.revoke();
            state_.velocity = 0;
            return;
        }
        if (event.input.type == EventType::PointerMove) {
            state_.value = rstd::min(
                Scalar(1.08),
                rstd::cmp::max(Scalar(-.05),
                               anchor_value_ + (event.input.position.y - anchor_y_) / height_));
            auto velocity = velocity_.velocity(key);
            state_.velocity = velocity.is_some() ? velocity->y / height_ : 0;
            auto batch = presentation(state_.value);
            ui().write_presentation(lease_, batch).unwrap();
            context.prevent_default();
        } else if (event.input.type == EventType::PointerUp) {
            for (const auto &contact : session_.input().contacts())
                if (contact.active && contact.key != key) {
                    PointerKey next = contact.key;
                    pointer_ = None();
                    session_.input()
                        .transfer_input(participant_.id,
                                        slice<PointerKey>::from_raw_parts(&next, usize(1)))
                        .unwrap();
                    return;
                }
            auto velocity = velocity_.velocity(key);
            auto speed = velocity.is_some() ? velocity->y / height_ : 0;
            settle(speed > .4 ? 1 : speed < -.4 ? 0 : state_.value >= .5 ? 1 : 0, speed);
        } else if (event.input.type == EventType::PointerCancel ||
                   event.input.type == EventType::CaptureLost) {
            if (mode_ == Mode::Direct)
                settle(state_.value >= .5 ? 1 : 0, 0);
        }
    }
    auto sample(MotionContext &context, FrameTiming) -> Result<MotionStatus, MotionError> {
        if (mode_ != Mode::Spring)
            return Ok(MotionStatus::Idle);
        if (!lease_.active()) {
            mode_ = Mode::Idle;
            return Ok(MotionStatus::Idle);
        }
        state_ = spring_.sample(context.now_ns() >= started_ ? context.now_ns() - started_ : 0);
        auto done = spring_.settled(state_, .0005, .0005);
        if (done)
            state_ = {spring_.target, 0};
        auto batch = presentation(state_.value);
        auto result = context.write(lease_, batch);
        if (result.is_err())
            return Err(result.unwrap_err());
        if (done) {
            mode_ = Mode::Idle;
            ui().release_presentation(lease_).unwrap();
            return Ok(MotionStatus::Idle);
        }
        context.request_tick(context.now_ns() + 8'333'333).unwrap();
        return Ok(MotionStatus::Running);
    }
};
struct Content {
    UiSession &session;
    rstd::rc::Weak<Panel> parent;
    UiId root;
    Binding behavior;
    ParticipantRegistration participant;
    PanRecognizer pan{{6, PanAxis::Vertical, 10}};
    Option<PointerKey> pointer;
    Scalar last_y{};
    Content(UiSession &owner, rstd::rc::Weak<Panel> value, UiId node)
        : session(owner), parent(rstd::move(value)), root(node) {}
    auto observe(InputContext &context, const InputPacket &packet) -> InputDecision {
        return pan.observe(context, packet);
    }
    void cancel(InputContext &context, CancelReason reason) { pan.cancel(context, reason); }
    void handle(EventContext &context, const RoutedEvent &event) {
        auto &document = context.document();
        auto metrics = document.viewport_metrics(root).unwrap();
        if (event.input.type == EventType::Wheel) {
            auto remaining = context.scroll_remaining();
            auto scale = event.input.scroll_unit == ScrollUnit::Lines ? Scalar(32) : Scalar(1);
            document.set_viewport_offset(root, {0, metrics.offset.y + remaining.y * scale})
                .unwrap();
            auto next = document.viewport_metrics(root).unwrap().offset.y;
            context.consume_scroll({0, (next - metrics.offset.y) / scale}).unwrap();
            return;
        }
        PointerKey key{event.input.device, event.input.pointer};
        if (event.input.type == EventType::CaptureGained) {
            pointer = Some(PointerKey(key));
            last_y = event.input.position.y;
            return;
        }
        if (pointer != Some(PointerKey(key)))
            return;
        if (event.input.type == EventType::PointerMove) {
            DeltaOffer offer({0, last_y - event.input.position.y}, ScrollUnit::Pixels,
                             event.input.sequence, DeltaSource::Pointer);
            last_y = event.input.position.y;
            document.set_viewport_offset(root, {0, metrics.offset.y + offer.remaining.y}).unwrap();
            offer.consume({0, document.viewport_metrics(root).unwrap().offset.y - metrics.offset.y})
                .unwrap();
            if (offer.remaining.y) {
                auto owner = parent.upgrade();
                if (owner.is_some()) {
                    owner->get()->handoff(key, -offer.remaining.y);
                }
            }
        } else if (event.input.type == EventType::PointerUp ||
                   event.input.type == EventType::PointerCancel ||
                   event.input.type == EventType::CaptureLost)
            pointer = None();
    }
};

void Panel::stop() {
    if (ui().valid(root_)) session_.input().cancel_subtree(root_);
    behavior_.disconnect(); participant_.binding.disconnect(); motion_.binding.disconnect();
    pointer_ = None(); mode_ = Mode::Idle;
    if (lease_.active() && ui().valid(shade_)) ui().release_presentation(lease_);
}

auto Panel::content(cx::CompositionContext &scope, rstd::rc::Weak<Panel> weak) -> cx::ComposeResult<cx::Element> {
    auto remembered = scope.remember<rstd::rc::Rc<Panel>>(cx::Key{"panel"_str}, [weak]() -> cx::ComposeResult<rstd::rc::Rc<Panel>> {
        auto value = weak.upgrade();
        if (value.is_none()) return Err(cx::ComposeError(cx::ComposeStage::Closed));
        return Ok(rstd::move(*value));
    });
    if (remembered.is_err()) return Err(remembered.unwrap_err());
    auto child = scope.remember<rstd::rc::Rc<Content>>(cx::Key{"content"_str}, [this, weak]() -> cx::ComposeResult<rstd::rc::Rc<Content>> {
        return Ok(rstd::rc::make_rc<Content>(session_, weak, UiId{}));
    });
    if (child.is_err()) return Err(child.unwrap_err());
    auto content = child.unwrap().get().unwrap()->clone();
    auto receiver = content.downgrade();
    auto installed = scope.effect(cx::Key{"input"_str}, {}, [weak, receiver](cx::CommittedContext &context) -> cx::ComposeResult<cx::EffectLease> {
        auto panel = weak.upgrade(); auto content = receiver.upgrade();
        if (panel.is_none() || content.is_none()) return Err(cx::ComposeError(cx::ComposeStage::Closed));
        auto target = panel->get(); auto body = content->get(); auto &session = context.session();
        target->shade_ = target->shade_ref_.id().unwrap(); target->curtain_ = target->curtain_ref_.id().unwrap();
        target->content_root_ = target->content_ref_.id().unwrap(); target->handle_ = target->handle_ref_.id().unwrap();
        body->root = target->content_root_;
        cx::EffectLease cleanup([weak, receiver] {
            auto panel = weak.upgrade(); auto content = receiver.upgrade();
            if (panel.is_some()) panel->get()->stop();
            if (content.is_some()) { content->get()->behavior.disconnect(); content->get()->participant.binding.disconnect(); }
        });
        auto behavior = session.input().bind_behavior(target->root_, weak);
        if (behavior.is_err()) { cx::ComposeError error(cx::ComposeStage::Input); error.input = Some(behavior.unwrap_err()); return Err(rstd::move(error)); }
        target->behavior_ = behavior.unwrap();
        auto participant = session.input().arena().bind(target->root_, weak);
        if (participant.is_err()) { cx::ComposeError error(cx::ComposeStage::Input); error.input = Some(participant.unwrap_err()); return Err(rstd::move(error)); }
        target->participant_ = participant.unwrap();
        auto motion = session.motion().bind(weak);
        if (motion.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect));
        target->motion_ = motion.unwrap();
        auto child_behavior = session.input().bind_behavior(body->root, receiver);
        if (child_behavior.is_err()) { cx::ComposeError error(cx::ComposeStage::Input); error.input = Some(child_behavior.unwrap_err()); return Err(rstd::move(error)); }
        body->behavior = child_behavior.unwrap();
        auto child_participant = session.input().arena().bind(body->root, receiver);
        if (child_participant.is_err()) { cx::ComposeError error(cx::ComposeStage::Input); error.input = Some(child_participant.unwrap_err()); return Err(rstd::move(error)); }
        body->participant = child_participant.unwrap();
        return Ok(rstd::move(cleanup));
    });
    if (installed.is_err()) return Err(installed.unwrap_err());
    auto size_key = rstd::format("{}x{}", size_.width, size_.height);
    auto remapped = scope.effect(cx::Key{"geometry"_str}, cx::EffectKeys{cx::Key{size_key.as_str()}},
        [weak](cx::CommittedContext &) -> cx::ComposeResult<cx::EffectLease> {
            auto panel = weak.upgrade(); if (panel.is_none()) return Err(cx::ComposeError(cx::ComposeStage::Closed));
            auto target = panel->get(); auto batch = target->presentation(target->state_.value);
            if (target->lease_.active()) target->ui().write_presentation(target->lease_, batch).unwrap();
            else target->ui().set_transform(target->shade_, batch.writes()[usize()].value.as_Matrix().value).unwrap();
            return Ok(cx::EffectLease{});
        });
    if (remapped.is_err()) return Err(remapped.unwrap_err());
    Vec<cx::Element> rows;
    for (unsigned i{}; i < 5; ++i)
        rows.push(cx::native(LayoutKind::Leaf).style(NodeStyleBuilder{examples::block(580, 64, {.16f + .03f * i, .34f, .46f, 1})}.radius(12).build()));
    auto viewport = cx::native(LayoutKind::Viewport, cx::elements({cx::column(rstd::move(rows)).gap(10)}))
        .style(NodeStyleBuilder{examples::block(rstd::cmp::max(Scalar(0), size_.width - 60), height_ - 172, {0, 0, 0, 0})}
            .transform({1, 0, 0, 1, 30, 116}).build()).reference(content_ref_);
    auto handle = cx::native(LayoutKind::Leaf).style(NodeStyleBuilder{examples::block(64, 5, {.65f, .76f, .84f, 1})}
        .transform({1, 0, 0, 1, (size_.width - 64) / 2, height_ - 22}).radius(2.5).build()).reference(handle_ref_);
    auto shade = cx::stack({label("用户定义的面板"_str, {30, 35}, 24, {1, 1, 1, 1}),
        label("滚动到边缘，再拖动面板。"_str, {30, 83}, 17, {.73f, .84f, .91f, 1}), viewport, handle})
        .style(NodeStyleBuilder{examples::block(size_.width, height_, {.08f, .15f, .23f, 1})}
            .transform({1, 0, 0, 1, 0, 40 - 340}).radius(16).build()).reference(shade_ref_);
    return Ok(cx::fragment({
        label("跟手交互 / Drag"_str, {32, 100}, 25, {.08f, .15f, .23f, 1}),
        label("拖动把手，松手后继续运动。"_str, {32, 155}, 17, {.25f, .34f, .43f, 1}),
        label("运动中再次抓取；空格展开或收起。"_str, {32, 190}, 17, {.25f, .34f, .43f, 1}),
        cx::native(LayoutKind::Leaf).style(NodeStyleBuilder{examples::block(size_.width, size_.height, {0, 0, 0, 1})}
            .opacity(0).hit_test_visible(false).build()).reference(curtain_ref_), shade,
    }));
}

auto Panel::make(UiSession &session) -> rstd::rc::Rc<Panel> {
    auto value = rstd::rc::make_rc<Panel>(session);
    auto weak = value.downgrade();
    auto composition = cx::Composition::create(session, value->root_).unwrap();
    composition.set_content([weak](cx::CompositionContext &scope) -> cx::ComposeResult<cx::Element> {
        auto value = weak.upgrade(); if (value.is_none()) return Err(cx::ComposeError(cx::ComposeStage::Closed));
        return value->get()->content(scope, weak);
    }).unwrap();
    session.document().attach_state(value->root_, rstd::move(composition)).unwrap();
    return value;
}

} // namespace interaction_example
