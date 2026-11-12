export module vui.examples.interaction.view;
import vui.examples;
using namespace rstd::prelude;
using namespace rstd::literals;

export namespace interaction_example {
using namespace vui;
class Panel {
    enum class Mode { Idle, Direct, Spring };
    UiSession &session_;
    UiId root_, shade_, curtain_, content_root_, handle_;
    Option<Box<rstd::dyn<rstd::any::Any>>> content_;
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
    auto label(UiId parent, ref<str> text, Point origin, Scalar size, Color color) -> UiId {
        auto style = NodeStyleBuilder{}
                         .transform({1, 0, 0, 1, origin.x, origin.y})
                         .hit_test_visible(false)
                         .build();
        auto id = ui().append<Text>(parent, style).unwrap();
        ui().set_text(id, text).unwrap();
        ui().set_text_format(id, TextFormatBuilder{}.size(size).build()).unwrap();
        ui().set_text_color(id, color).unwrap();
        return id;
    }

  public:
    explicit Panel(UiSession &session) : session_(session) {
        auto service = Box<vectext::SystemTextService>::make();
        ui().set_text_service(Box<TextService>::from_raw(
                                  rstd::move(service).into_raw().template cast<TextService>()))
            .unwrap();
        auto style = NodeStyleBuilder{examples::block(640, 520, {.91f, .94f, .97f, 1})}
                         .focusable(true)
                         .build();
        root_ = ui().create<Stack>(style).unwrap();
        label(root_, "跟手交互 / Drag"_str, {32, 100}, 25, {.08f, .15f, .23f, 1});
        label(root_, "拖动把手，松手后继续运动。"_str, {32, 155}, 17, {.25f, .34f, .43f, 1});
        label(root_, "运动中再次抓取；空格展开或收起。"_str, {32, 190}, 17, {.25f, .34f, .43f, 1});
        auto curtain = NodeStyleBuilder{examples::block(640, 520, {0, 0, 0, 1})}
                           .opacity(0)
                           .hit_test_visible(false)
                           .build();
        curtain_ = ui().append<Rectangle>(root_, curtain).unwrap();
        auto shade = NodeStyleBuilder{examples::block(640, height_, {.08f, .15f, .23f, 1})}
                         .transform({1, 0, 0, 1, 0, 40 - height_})
                         .radius(16)
                         .build();
        shade_ = ui().append<Stack>(root_, shade).unwrap();
        label(shade_, "用户定义的面板"_str, {30, 35}, 24, {1, 1, 1, 1});
        label(shade_, "滚动到边缘，再拖动面板。"_str, {30, 83}, 17, {.73f, .84f, .91f, 1});
        auto viewport = NodeStyleBuilder{examples::block(580, 168, {0, 0, 0, 0})}
                            .transform({1, 0, 0, 1, 30, 116})
                            .build();
        content_root_ = ui().append<Viewport>(shade_, viewport).unwrap();
        auto column = NodeStyleBuilder{}.gap(10).build();
        auto body = ui().append<Column>(content_root_, column).unwrap();
        for (unsigned i{}; i < 5; ++i) {
            auto tile = NodeStyleBuilder{examples::block(580, 64, {.16f + .03f * i, .34f, .46f, 1})}
                            .radius(12)
                            .build();
            ui().append<Rectangle>(body, tile).unwrap();
        }
        auto handle = NodeStyleBuilder{examples::block(64, 5, {.65f, .76f, .84f, 1})}
                          .transform({1, 0, 0, 1, 288, height_ - 22})
                          .radius(2.5)
                          .build();
        handle_ = ui().append<Rectangle>(shade_, handle).unwrap();
    }
    static auto make(UiSession &session) -> rstd::rc::Rc<Panel>;
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
        auto style = ui().base_style(root_).unwrap().layout;
        if (style.width != Some(Scalar(surface.logical_size.width)) ||
            style.height != Some(Scalar(surface.logical_size.height))) {
            ui().set_layout(root_, LayoutStyleBuilder{style}
                                      .width(surface.logical_size.width)
                                      .height(surface.logical_size.height)
                                      .build())
                .unwrap();
        }
        auto height =
            rstd::min(Scalar(340), rstd::cmp::max(Scalar(180), surface.logical_size.height - 40));
        bool remapped = height_ != height;
        height_ = height;
        UiId responsive[] = {shade_, curtain_, content_root_};
        for (auto id : responsive) {
            auto layout = LayoutStyleBuilder{ui().base_style(id).unwrap().layout}
                              .width(id == content_root_
                                         ? rstd::cmp::max(Scalar(0), surface.logical_size.width - 60)
                                         : surface.logical_size.width)
                              .height(id == curtain_ ? surface.logical_size.height
                                      : id == shade_ ? height_
                                                     : height_ - 172)
                              .build();
            if (layout != ui().base_style(id).unwrap().layout)
                ui().set_layout(id, layout).unwrap();
        }
        ui().set_transform(handle_,
                           {1, 0, 0, 1, (surface.logical_size.width - 64) / 2, height_ - 22})
            .unwrap();
        if (remapped) {
            auto batch = presentation(state_.value);
            if (lease_.active())
                ui().write_presentation(lease_, batch).unwrap();
            else
                ui().set_transform(shade_, batch.writes()[usize()].value.as_Matrix().value)
                    .unwrap();
        }
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

auto Panel::make(UiSession &session) -> rstd::rc::Rc<Panel> {
    auto value = rstd::rc::make_rc<Panel>(session);
    auto target = value.get();
    target->behavior_ = session.input().bind_behavior(target->root_, value.downgrade()).unwrap();
    target->participant_ = session.input().arena().bind(target->root_, value.downgrade()).unwrap();
    target->motion_ = session.motion().bind(value.downgrade()).unwrap();
    auto content = rstd::rc::make_rc<Content>(session, value.downgrade(), target->content_root_);
    content.get()->behavior =
        session.input().bind_behavior(content.get()->root, content.downgrade()).unwrap();
    content.get()->participant =
        session.input().arena().bind(content.get()->root, content.downgrade()).unwrap();
    target->content_ = Some(Box<rstd::dyn<rstd::any::Any>>::make(rstd::move(content)));
    return value;
}

} // namespace interaction_example
