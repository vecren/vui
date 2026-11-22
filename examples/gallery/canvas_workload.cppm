export module vui.examples.gallery:canvas_workload;
import :style;
import :parts;
import vui.compose;
import vui.examples;
import rstd;

namespace examples::gallery::detail {
constexpr Index canvas_loads[]{256, 1024, 4096};
constexpr Serial canvas_warmup = 8;

struct CanvasOptions {
    Index particles{canvas_loads[1]};
    bool playing{true};
    friend auto operator==(CanvasOptions, CanvasOptions) -> bool = default;
};

struct CanvasRecording {
    Serial frames{};
    CpuTiming paint;
};

namespace {
struct CanvasLayout {
    static constexpr Scalar toolbar_height = 44;
    static constexpr Scalar gap = 12;

    auto measure(LayoutMeasureContext &context, LayoutConstraints limits) -> Result<Size, UiError> {
        const auto width = limits.max_width.unwrap_or(440);
        const auto height = limits.max_height.unwrap_or(360);
        const auto controls = rstd::min(height, toolbar_height);
        auto toolbar = context.measure_child(usize(0), {Some(Scalar(width)), Some(Scalar(controls))});
        if (toolbar.is_err()) return Err(toolbar.unwrap_err());
        auto scene = context.measure_child(usize(1), {Some(Scalar(width)), Some(height - scene_top(height))});
        if (scene.is_err()) return Err(scene.unwrap_err());
        return Ok(Size{width, height});
    }

    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        auto placed = context.arrange_child(usize(0), {0, 0, size.width, rstd::min(size.height, toolbar_height)});
        if (placed.is_err()) return placed;
        const auto top = scene_top(size.height);
        return context.arrange_child(usize(1), {0, top, size.width, size.height - top});
    }

    static auto scene_top(Scalar height) -> Scalar { return rstd::min(height, toolbar_height + gap); }
};

struct Particle {
    Scalar x, y, vx, vy, width, height, angle, spin;
};

auto make_particles(Index count) -> Vec<Particle> {
    Vec<Particle> particles;
    particles.reserve(usize(count));
    Index seed = 0x47de530bu;
    auto unit = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return Scalar(seed >> 8) / 0x1p24;
    };
    for (Index i{}; i < count; ++i) {
        particles.push({2 * unit(), 2 * unit(), .12 + .29 * unit(), .14 + .34 * unit(),
                        8 + 24 * unit(), 6 + 16 * unit(), 6.28 * unit(), 3 * unit() - 1.5});
    }
    return particles;
}

auto bounce(Scalar position, Scalar span, Scalar margin) -> Scalar {
    const auto phase = position - 2 * __builtin_floor(position / 2);
    return rstd::min(phase, 2 - phase) * (span + 2 * margin) - margin;
}

auto outline(Index index, Rect bounds) -> Shape {
    switch (index % 4) {
    case 0: return Shape::circle({}, bounds.height / 2);
    case 1: return Shape::rounded_rect(bounds, {5, 2, 0, 7});
    case 2: return Shape::ellipse(bounds);
    default: return Shape::rectangle(bounds);
    }
}

struct CanvasPainter {
    struct State {
        CanvasOptions options;
        cx::Borrowed<CanvasRecording> recording;
        Vec<Particle> particles;
        ImageData sprite{examples::make_sample_image()};
        UiDocument *document{};
        UiId node;
        Option<MotionId> driver;
        Serial step{};

        auto sample(MotionContext &clock, FrameTiming) -> Result<MotionStatus, MotionError> {
            if (!options.playing) return Ok(MotionStatus::Idle);
            ++step;
            if (document->invalidate_paint(node).is_err()) return Err(MotionError::Unavailable);
            auto scheduled = clock.request_tick(clock.now_ns() + 16'666'667);
            if (scheduled.is_err()) return Err(scheduled.unwrap_err());
            return Ok(MotionStatus::Running);
        }

        auto draw(ModifierDrawContext &modifier) -> Result<empty, UiError> {
            auto &paint = modifier.paint();
            auto &canvas = paint.canvas();
            const auto size = paint.size();
            if (size.width <= 0 || size.height <= 0) return Ok(empty{});
            ++recording->frames;
            CpuTimer timer(recording->frames > canvas_warmup ? &recording->paint : nullptr);
            const auto stage = Shape::rounded_rect({0, 0, size.width, size.height}, panel_radius);
            canvas.fill(stage, {0.009f, 0.016f, 0.023f, 1});
            canvas.save();
            canvas.clip(stage);
            const auto time = Scalar(step) / 60;
            constexpr Color accents[]{
                {.036f, .27f, .23f, .42f}, {.17f, .08f, .30f, .42f},
                {.32f, .091f, .057f, .42f}, {.23f, .22f, .095f, .42f},
                {.054f, .16f, .32f, .42f},
            };
            for (Index i{}; i < options.particles; ++i) {
                const auto &particle = particles[usize(i)];
                const auto angle = particle.angle + particle.spin * time;
                const auto c = __builtin_cos(angle);
                const auto s = __builtin_sin(angle);
                const auto x = bounce(particle.x + particle.vx * time, size.width, particle.width / 2);
                const auto y = bounce(particle.y + particle.vy * time, size.height, particle.height / 2);
                const Rect body{-particle.width / 2, -particle.height / 2, particle.width, particle.height};
                canvas.save();
                canvas.transform({c, s, -s, c, x, y});
                if (i % 16 == 0) canvas.clip(Shape::ellipse(body));
                if (i % 32 == 0) {
                    canvas.image(sprite, body, {0, 0, Scalar(sprite.width()), Scalar(sprite.height())},
                                 ImageSampling::Linear, white(.72f));
                } else {
                    const auto shape = outline(i, body);
                    const auto ink = accents[i % 5];
                    canvas.fill(shape, ink.modulate(white(.57f)));
                    if (i % 8 < 4) canvas.border(shape, 1.25, ink);
                }
                canvas.restore();
            }
            canvas.restore();
            canvas.border(stage, 1, white(.14f));
            return Ok(empty{});
        }
    };

    CanvasOptions options;
    cx::Borrowed<CanvasRecording> recording;

    auto create() const -> State { return {options, recording, make_particles(options.particles)}; }

    auto update(State &state, cx::BehaviorContext &context) const -> cx::ComposeResult<empty> {
        if (state.options == options) return Ok(empty{});
        const auto resume = options.playing && !state.options.playing;
        if (state.options.particles != options.particles) state.particles = make_particles(options.particles);
        state.options = options;
        if (resume && state.driver.is_some() && context.session().motion().request(*state.driver).is_err())
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

auto workload_button(ref<str> title, Scalar width, bool active, auto action) -> cx::Element {
    return control_finish(cx::button(button_label(title), rstd::move(action), button_style(active))
        .style(NodeStyleBuilder{}.width(width).height(CanvasLayout::toolbar_height).build()), active);
}
} // namespace

[[vui::compose]]
auto canvas_workload(cx::Borrowed<state::Cell<CanvasOptions>> settings,
                     cx::Borrowed<CanvasRecording> recording, Scalar width) -> cx::ComposeResult<cx::Element> {
    auto read = cx::read(settings->value());
    if (read.is_err()) return Err(read.unwrap_err());
    const auto options = read->value();
    constexpr Scalar gap = 6;
    const auto button_width = rstd::cmp::max(Scalar(0), rstd::min(Scalar(84), (width - 3 * gap) / 4));
    auto load = [&](ref<str> label, Index count) {
        return workload_button(label, button_width, options.particles == count, [settings, count] {
            auto updated = state::read(settings->value()).unwrap().value();
            updated.particles = count;
            settings->set(updated).unwrap();
        });
    };
    auto playback = workload_button(options.playing ? "Pause"_str : "Run"_str, button_width, false, [settings] {
        auto updated = state::read(settings->value()).unwrap().value();
        updated.playing = !updated.playing;
        settings->set(updated).unwrap();
    });
    auto controls = cx::row({load("256"_str, canvas_loads[0]), load("1024"_str, canvas_loads[1]),
        load("4096"_str, canvas_loads[2]), rstd::move(playback)}).gap(gap);
    auto scene = cx::native(LayoutKind::Leaf).style(fill().hit_test_visible(false).build())
        .behavior(cx::Key{"canvas-particles"_str}, CanvasPainter{options, recording});
    return Ok(layout_part(CanvasLayout{}, cx::elements({rstd::move(controls), rstd::move(scene)}))
        .style(fill().hit_self(false).build()));
}
} // namespace examples::gallery::detail
