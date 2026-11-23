export module vui.examples.gallery:canvas_animation;
import :canvas_geometry;
import :style;
import :parts;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
struct CanvasAnimationOptions {
    bool playing{true};
    Index target{};
    Scalar damping{12};
    friend auto operator==(CanvasAnimationOptions, CanvasAnimationOptions) -> bool = default;
};

template <class OnChange>
auto damping_slider(Scalar initial, OnChange changed) -> cx::Element {
    return native_part([initial, changed](UiSession &session, UiId parent) -> Result<empty, UiError> {
        auto &ui = session.document();
        auto slider = Slider::create(session, fill().build(),
            {.minimum = 2, .maximum = 32, .step = .25, .keyboard_step = 1}, "Damping"_str);
        if (slider.is_err()) return Err(slider.unwrap_err().ui.unwrap_or(UiError::InvalidValue));
        auto control = slider.unwrap();
        auto attached = ui.attach(parent, control);
        if (attached.is_err()) return attached;
        auto initial_value = Slider::set_value(ui, control, initial);
        if (initial_value.is_err()) return initial_value;
        auto painted = ui.install_paint(control, SliderTrackPaint{session});
        if (painted.is_err()) return painted;
        auto binding = Slider::on_changed(ui, control, [&ui, control, changed](Scalar value) {
            changed(value);
            ui.invalidate_paint(control).unwrap();
        });
        if (binding.is_err()) return Err(binding.unwrap_err().ui.unwrap_or(UiError::InvalidValue));
        auto invalidate = [](EventContext &context, const RoutedEvent &event) {
            (void)context.document().invalidate_paint(event.current);
        };
        if (session.input().on(control, EventType::FocusGained, invalidate).is_err() ||
            session.input().on(control, EventType::FocusLost, invalidate).is_err())
            return Err(UiError::Routing);
        return ui.attach_state(control, binding.unwrap(), NodeStateKey::of<Binding>());
    });
}

namespace {
const auto animation_stage = Color::from_srgba8(24, 32, 39).unwrap();
const auto animation_panel = Color::from_srgba8(17, 24, 32).unwrap();
const auto animation_cyan = Color::from_srgba8(107, 222, 226).unwrap();
const auto animation_gold = Color::from_srgba8(255, 195, 103).unwrap();
const auto animation_violet = Color::from_srgba8(185, 153, 251).unwrap();
constexpr Scalar track_masses[]{.55, 1, 1.8};
constexpr Index track_count = 3, primary_track = 1;

void draw_wave(Canvas &canvas, Rect bounds, Scalar phase) {
    const Point center{bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
    const auto amplitude = rstd::min(bounds.width, bounds.height) * .18;
    canvas.rect({bounds.x + bounds.width * .10, center.y - .5, bounds.width * .80, 1}, white(.09f));
    for (Index i{}; i < 3; ++i) {
        const auto point = [=](Scalar t) -> Point {
            const auto offset = __builtin_sin(canvas_turn * (1.12 * t - phase) + i * .93);
            return {bounds.x + bounds.width * (.10 + .80 * t), center.y + amplitude * offset};
        };
        const auto color = animation_cyan.modulate(white(.27f + .34f * i));
        stroke_curve(canvas, 32, .9 + .4 * i, color, point);
        if (i == 2)
            for (Index dot{}; dot <= 6; ++dot)
                canvas.fill(Shape::circle(point(Scalar(dot) / 6), rstd::min(Scalar(2.8), amplitude / 4)), animation_cyan);
    }
}

void draw_morph(Canvas &canvas, Rect bounds, Scalar phase) {
    const Point center{bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
    const auto radius = rstd::min(bounds.width, bounds.height) * .30;
    const auto power = 2.8 + 1.65 * __builtin_cos(2 * canvas_turn * phase);
    const auto aspect = .91 + .15 * __builtin_sin(2 * canvas_turn * phase);
    auto outline = [=](Scalar t) -> Point {
        const auto x = __builtin_cos(canvas_turn * t), y = __builtin_sin(canvas_turn * t);
        const auto extent = __builtin_pow(__builtin_pow(__builtin_fabs(x), power) +
                                          __builtin_pow(__builtin_fabs(y), power), -1 / power);
        return {center.x + x * extent * radius, center.y + y * extent * radius * aspect};
    };
    stroke_curve(canvas, 64, 2, animation_violet, outline);
    stroke_curve(canvas, 64, 1.25, animation_violet.modulate(white(.34f)), [&](Scalar t) -> Point {
        const auto point = outline(t);
        return {center.x + (point.x - center.x) * .74, center.y + (point.y - center.y) * .74};
    });
}

auto smooth(Scalar value) -> Scalar {
    const auto t = rstd::min(Scalar(1), rstd::cmp::max(Scalar(0), value));
    return t * t * (3 - 2 * t);
}

void draw_stagger(Canvas &canvas, Rect bounds, Scalar phase) {
    const Point center{bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
    const auto spacing = rstd::min(bounds.width, bounds.height) * .22;
    const auto side = spacing * .74;
    for (Index row{}; row < 3; ++row) {
        for (Index col{}; col < 3; ++col) {
            const auto x = center.x + (Scalar(col) - 1) * spacing;
            const auto y = center.y + (Scalar(row) - 1) * spacing;
            const auto t = __builtin_fmod(1 + 2 * phase - .10 * (row + col), 1);
            const auto progress = smooth(t / .28) * (1 - smooth((t - .57) / .32));
            const auto tile = side * (.32 + .68 * progress);
            canvas.border(Shape::rounded_rect({x - side / 2, y - side / 2, side, side}, 4), 1, white(.11f));
            if (progress > 0)
                canvas.rect({x - tile / 2, y - tile / 2 + side * .21 * (1 - progress), tile, tile},
                            animation_gold.modulate(white(float(progress))), 4);
        }
    }
}

void draw_looping_demos(Canvas &canvas, Size size, Scalar phase) {
    const auto gap = rstd::min(Scalar(12), rstd::min(size.width, size.height) * .075);
    const auto width = (size.width - 4 * gap) / 3;
    const auto height = size.height * .51 - gap;
    using Demo = void (*)(Canvas &, Rect, Scalar);
    constexpr Demo demos[]{draw_wave, draw_morph, draw_stagger};
    for (Index i{}; i < 3; ++i) {
        const Rect bounds{gap + i * (width + gap), gap, width, height};
        const auto card = Shape::rounded_rect(bounds, 10);
        canvas.fill(card, animation_panel);
        demos[i](canvas, bounds, phase);
        canvas.border(card, 1, white(.09f));
    }
}

struct SpringTrack {
    ScalarMotion position;
    Spring spring;
    Option<Serial> started;
    bool active{};

    void retarget(Index target, Scalar damping, Scalar mass) {
        spring = {position, Scalar(target), 170, damping, mass};
        started = None();
        active = true;
    }

    void sample(Serial time) {
        if (!active) return;
        if (!started) started = Some(time);
        position = spring.sample(time - rstd::min(time, *started));
        if (spring.settled(position, .002, .008)) {
            position = {spring.target, 0};
            active = false;
        }
    }
};

struct AnimationPainter {
    struct State {
        CanvasAnimationOptions options;
        SpringTrack tracks[track_count];
        Option<Serial> last_sample;
        Scalar loop_phase{};
        UiDocument *document{};
        UiId node;
        Option<MotionId> driver;

        explicit State(CanvasAnimationOptions value) : options(value) {
            for (auto &track : tracks) track.position.value = Scalar(value.target);
        }

        auto sample(MotionContext &runtime, FrameTiming timing) -> Result<MotionStatus, MotionError> {
            auto time = timing.sample_time_ns();
            if (options.playing && last_sample && time >= *last_sample)
                loop_phase = __builtin_fmod(loop_phase + Scalar(time - *last_sample) / 4'800'000'000, 1);
            last_sample = Some(time);
            bool moving{};
            for (auto &track : tracks) {
                track.sample(time);
                moving = moving || track.active;
            }
            if (document->invalidate_paint(node).is_err()) return Err(MotionError::Unavailable);
            if (!options.playing && !moving) return Ok(MotionStatus::Idle);
            if (auto scheduled = runtime.request_tick(runtime.now_ns() + 16'666'667); scheduled.is_err())
                return Err(scheduled.unwrap_err());
            return Ok(MotionStatus::Running);
        }

        auto draw(ModifierDrawContext &modifier) -> Result<empty, UiError> {
            auto &paint = modifier.paint();
            auto &ink = paint.canvas();
            const auto size = paint.size();
            if (size.width < 4 || size.height < 4) return Ok(empty{});
            const auto stage = Shape::rounded_rect({0, 0, size.width, size.height}, panel_radius);
            ink.fill(stage, animation_stage);
            ink.save();
            ink.clip(stage);
            draw_looping_demos(ink, size, loop_phase);

            const auto track = size.width * .64;
            const auto left = (size.width - track) / 2;
            const Color marks[]{animation_gold, animation_cyan, animation_violet};
            const auto knob = rstd::min(Scalar(8), size.height * .03);
            for (Index stop{}; stop < 3; ++stop) {
                const auto x = left + stop * track / 2;
                ink.rect({x - .5, size.height * .54, 1, size.height * .34},
                         white(stop == options.target ? .34f : .09f));
            }
            for (Index i{}; i < track_count; ++i) {
                const auto y = size.height * (.59 + i * .125);
                const auto head = left + tracks[i].position.value * track / 2;
                const auto velocity = tracks[i].position.velocity;
                const auto trail = track * rstd::min(Scalar(.12), __builtin_fabs(velocity) * .006);
                ink.rect({left, y - .5, track, 1}, white(.18f));
                for (Index stop{}; stop < 3; ++stop)
                    ink.fill(Shape::circle({left + stop * track / 2, y}, knob * .32), white(.28f));
                if (trail > 0) {
                    const auto start = velocity > 0 ? head - trail : head;
                    ink.fill(Shape::capsule({start, y - knob * .23, trail, knob * .46}), marks[i].modulate(white(.44f)));
                }
                ink.fill(Shape::circle({head, y}, knob), marks[i]);
            }
            const auto phase = rstd::min(Scalar(2), rstd::cmp::max(Scalar(0), tracks[primary_track].position.value));
            const auto section = phase < 1 ? 0 : 1;
            const auto fraction = float(phase - section);
            const auto from = marks[section], to = marks[section + 1];
            const auto mix = [fraction](float a, float b) { return a + (b - a) * fraction; };
            const Color color{mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), 1};
            const auto length = section == 0 ? .22 + .78 * phase : 1 - .58 * (phase - 1);
            ink.fill(Shape::capsule({left, size.height * .94, track * length, knob * .65}), color);
            ink.restore();
            ink.border(stage, 1, white(.14f));
            return Ok(empty{});
        }
    };

    CanvasAnimationOptions options;
    auto create() const -> State { return State(options); }

    auto update(State &state, cx::BehaviorContext &context) const -> cx::ComposeResult<empty> {
        if (options == state.options) return Ok(empty{});
        if (options.playing != state.options.playing) state.last_sample = None();
        for (Index i{}; i < track_count; ++i) {
            auto &track = state.tracks[i];
            if (options.target != state.options.target || (track.active && options.damping != state.options.damping))
                track.retarget(options.target, options.damping, track_masses[i]);
        }
        state.options = options;
        if (state.driver && context.session().motion().request(*state.driver).is_err())
            return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(UiError::InvalidValue)));
        return Ok(empty{});
    }

    auto attach(State &state, cx::BehaviorContext &context, rstd::rc::Weak<State> receiver) const
        -> cx::ComposeResult<cx::EffectLease> {
        state.document = &context.document();
        state.node = context.target();
        auto registered = context.session().motion().bind(rstd::move(receiver));
        if (registered.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(UiError::InvalidValue)));
        auto driver = registered.unwrap();
        state.driver = Some(MotionId(driver.id));
        return Ok(cx::EffectLease([binding = rstd::move(driver.binding)]() mutable { binding.disconnect(); }));
    }
};
} // namespace

auto animation_canvas(CanvasAnimationOptions options) -> cx::Element {
    return cx::native(LayoutKind::Leaf).style(fill().hit_test_visible(false).build())
        .behavior(cx::Key{"canvas-animation"_str}, AnimationPainter{options});
}
} // namespace examples::gallery::detail
