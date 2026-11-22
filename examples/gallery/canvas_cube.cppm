export module vui.examples.gallery:canvas_cube;
import :canvas_recording;
import :style;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
struct CubeOptions {
    bool playing{true};
    Serial scramble{}, reset{};
    friend auto operator==(CubeOptions, CubeOptions) -> bool = default;
};

namespace {
constexpr Index cubie_count = 26;
constexpr Index scramble_count = 12;
constexpr Index turn_frames = 15;
constexpr Index hold_frames = 36;
constexpr Index shuffle_frames = scramble_count * turn_frames;
constexpr Index cycle_frames = 2 * (hold_frames + shuffle_frames);
constexpr Scalar pi = 3.14159265358979323846;

struct Vec3 {
    Scalar values[3]{};
    constexpr auto operator[](Index axis) -> Scalar & { return values[axis]; }
    constexpr auto operator[](Index axis) const -> Scalar { return values[axis]; }
    friend constexpr auto operator==(const Vec3 &, const Vec3 &) -> bool = default;
};

constexpr auto axis_vector(Index axis, Scalar value = 1) -> Vec3 {
    Vec3 result;
    result[axis] = value;
    return result;
}

struct Rotation {
    Index axis{};
    Scalar cosine{1}, sine{};

    static auto from_angle(Index axis, Scalar angle) -> Rotation {
        return {axis, __builtin_cos(angle), __builtin_sin(angle)};
    }
    constexpr auto apply(Vec3 value) const -> Vec3 {
        const auto a = (axis + 1) % 3, b = (axis + 2) % 3;
        const auto previous = value[a];
        value[a] = cosine * previous - sine * value[b];
        value[b] = sine * previous + cosine * value[b];
        return value;
    }
};

struct Sticker {
    Vec3 normal;
    Index color{};
};

struct Cubie {
    Vec3 center;
    Sticker stickers[3];
    Index count{};
    auto color_on(Vec3 normal) const -> Option<Index> {
        for (Index i{}; i < count; ++i)
            if (stickers[i].normal == normal) return Some(Index(stickers[i].color));
        return None();
    }
};

struct Move {
    Index axis{};
    int layer{1}, direction{1};
    constexpr auto includes(const Cubie &cubie) const -> bool { return cubie.center[axis] == layer; }
    constexpr auto inverse() const -> Move { return {axis, layer, -direction}; }
};

struct Cube {
    Cubie cubies[cubie_count];

    constexpr Cube() {
        Index next{};
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z) {
                    if (x == 0 && y == 0 && z == 0) continue;
                    auto &cubie = cubies[next++];
                    cubie.center = {{Scalar(x), Scalar(y), Scalar(z)}};
                    for (Index axis{}; axis < 3; ++axis) {
                        const auto coordinate = cubie.center[axis];
                        if (coordinate == 0) continue;
                        cubie.stickers[cubie.count++] = {axis_vector(axis, coordinate), axis * 2 + (coordinate > 0)};
                    }
                }
    }

    constexpr void commit(Move move) {
        const Rotation quarter{move.axis, 0, Scalar(move.direction)};
        for (auto &cubie : cubies) {
            if (!move.includes(cubie)) continue;
            cubie.center = quarter.apply(cubie.center);
            for (Index i{}; i < cubie.count; ++i) cubie.stickers[i].normal = quarter.apply(cubie.stickers[i].normal);
        }
    }
};

struct Turn {
    Move move;
    Index frame{};
    auto rotation() const -> Rotation {
        const auto t = Scalar(frame) / turn_frames;
        const auto eased = t * t * (3 - 2 * t);
        return Rotation::from_angle(move.axis, eased * move.direction * pi / 2);
    }
};

struct Sequence {
    Move moves[scramble_count];

    constexpr explicit Sequence(Serial seed = 0) {
        Index random = 0x698b135du + static_cast<Index>(seed) * 2;
        Index axis{};
        for (auto &move : moves) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            axis = (axis + 1 + (random & 1)) % 3;
            move = {axis, (random & 2) ? 1 : -1, (random & 4) ? 1 : -1};
        }
    }

    auto turn(Index tick) const -> Option<Turn> {
        const auto undo = tick >= hold_frames + shuffle_frames;
        const auto offset = tick % (hold_frames + shuffle_frames);
        if (offset < hold_frames) return None();
        const auto slot = (offset - hold_frames) / turn_frames;
        const auto move = undo ? moves[scramble_count - 1 - slot].inverse() : moves[slot];
        return Some(Turn{move, (offset - hold_frames) % turn_frames});
    }
};

struct Orbit {
    Scalar yaw{.66}, pitch{.50};
};

struct Camera {
    Rotation yaw, pitch;
    Point center;
    Scalar scale;

    Camera(Orbit orbit, Size stage)
        : yaw(Rotation::from_angle(1, -orbit.yaw)), pitch(Rotation::from_angle(0, orbit.pitch)),
          center{stage.width * .50, stage.height * .48}, scale(rstd::min(stage.width, stage.height) * .186) {}

    auto view(Vec3 point) const -> Vec3 { return pitch.apply(yaw.apply(point)); }
    auto project(Vec3 point, Vec3 horizontal, Vec3 vertical) const -> Transform {
        const auto origin = view(point), x = view(horizontal), y = view(vertical);
        return {x[0], -x[1], y[0], -y[1], center.x + origin[0] * scale, center.y - origin[1] * scale};
    }
};

const auto stage_ink = Color::from_srgba8(26, 27, 41).unwrap();
const auto stage_edge = Color::from_srgba8(65, 68, 92).unwrap();
const auto track_ink = Color::from_srgba8(85, 92, 131, 105).unwrap();
const auto body_ink = Color::from_srgba8(40, 42, 57).unwrap();
const auto sticker_glint = Color::from_srgba8(255, 250, 235, 65).unwrap();
const Color stickers[]{
    Color::from_srgba8(243, 110, 77).unwrap(), Color::from_srgba8(238, 80, 111).unwrap(),
    Color::from_srgba8(248, 207, 100).unwrap(), Color::from_srgba8(255, 240, 212).unwrap(),
    Color::from_srgba8(115, 149, 250).unwrap(), Color::from_srgba8(86, 213, 173).unwrap(),
};

auto shade(Color color, float light) -> Color {
    return {color.r * light, color.g * light, color.b * light, color.a};
}

void draw_platform(Canvas &canvas, Size stage, Camera camera) {
    const auto width = camera.scale * 4.15, height = camera.scale * .74;
    const auto y = rstd::min(stage.height - height / 2 - 4, camera.center.y + camera.scale * 1.85);
    const auto ellipse = [&](Scalar w, Scalar h) {
        return Shape::ellipse({camera.center.x - w / 2, y - h / 2, w, h});
    };
    canvas.border(ellipse(width, height), 1, track_ink);
    for (Index i{}; i < 6; ++i) {
        const auto size = 1 - Scalar(i) * .08;
        canvas.fill(ellipse(width * .84 * size, height * .81 * size), {0, 0, 0, .105f});
    }
}

void draw_cubie(Canvas &canvas, const Cubie &cubie, const Camera &camera, Rotation layer) {
    const auto half = camera.scale * .477, inner = camera.scale * .381;
    for (Index axis{}; axis < 3; ++axis) {
        for (int side = -1; side <= 1; side += 2) {
            const auto normal = axis_vector(axis, side);
            const auto facing = camera.view(layer.apply(normal));
            if (facing[2] <= 1e-5) continue;
            auto local = cubie.center;
            local[axis] += .477 * side;
            const auto horizontal = axis_vector((axis + 1) % 3), vertical = axis_vector((axis + 2) % 3);
            const auto transform = camera.project(layer.apply(local), layer.apply(horizontal), layer.apply(vertical));
            const auto light = rstd::cmp::max(Scalar(0), -.28 * facing[0] + .84 * facing[1] + .46 * facing[2]);
            canvas.save();
            canvas.transform(transform);
            canvas.rect({-half, -half, 2 * half, 2 * half}, shade(body_ink, float(.53 + .47 * light)),
                        camera.scale * .035);
            if (auto color = cubie.color_on(normal)) {
                const auto sticker = Shape::rounded_rect({-inner, -inner, 2 * inner, 2 * inner}, camera.scale * .08);
                canvas.fill(sticker, shade(stickers[*color], float(.52 + .48 * light)));
                canvas.border(sticker, 1, sticker_glint);
            }
            canvas.restore();
        }
    }
}

void draw_cube(Canvas &canvas, const Cube &cube, const Camera &camera, Option<Turn> turn) {
    struct Depth { Index index; Scalar z; bool moving; };
    Depth order[cubie_count];
    const auto rotation = turn ? turn->rotation() : Rotation{};
    const auto moving_in_front = turn && camera.view(axis_vector(turn->move.axis, turn->move.layer))[2] >= 0;
    for (Index i{}; i < cubie_count; ++i) {
        const auto &cubie = cube.cubies[i];
        const auto moving = turn && turn->move.includes(cubie);
        const auto center = moving ? rotation.apply(cubie.center) : cubie.center;
        order[i] = {i, camera.view(center)[2], moving};
    }
    // The layer plane separates the two grids throughout the turn.
    rstd::slice_::sort_unstable_by(mut_ref<Depth[]>::from_raw_parts(order, usize(cubie_count)),
        [moving_in_front](Depth a, Depth b) {
            if (a.moving != b.moving) return a.moving != moving_in_front;
            return a.z == b.z ? a.index < b.index : a.z < b.z;
        });
    for (const auto &depth : order)
        draw_cubie(canvas, cube.cubies[depth.index], camera, depth.moving ? rotation : Rotation{});
}

struct Drag { DeviceId device; Index pointer; Point previous; };

struct CubePainter {
    struct State {
        CubeOptions options;
        cx::Borrowed<CanvasRecording> recording;
        Sequence sequence;
        Cube cube;
        Orbit orbit;
        Index tick{};
        Option<Drag> drag;
        UiSession *session{};
        UiId node;
        Option<MotionId> driver;

        auto sample(MotionContext &clock, FrameTiming) -> Result<MotionStatus, MotionError> {
            if (!options.playing) return Ok(MotionStatus::Idle);
            auto active = sequence.turn(tick);
            if (active && active->frame + 1 == turn_frames) cube.commit(active->move);
            tick = (tick + 1) % cycle_frames;
            if (session->document().invalidate_paint(node).is_err()) return Err(MotionError::Unavailable);
            auto scheduled = clock.request_tick(clock.now_ns() + 16'666'667);
            if (scheduled.is_err()) return Err(scheduled.unwrap_err());
            return Ok(MotionStatus::Running);
        }

        void handle(EventContext &context, const RoutedEvent &event) {
            if (event.current != event.target) return;
            const auto &input = event.input;
            if (input.type == EventType::PointerDown) {
                if (drag || (input.pointer_kind != PointerKind::Touch && input.button != PointerButton::Primary) ||
                    context.capture_pointer().is_err()) return;
                drag = Some(Drag{input.device, input.pointer, event.local});
                context.prevent_default();
                return;
            }
            if (!drag || drag->device != input.device || drag->pointer != input.pointer) return;
            if (input.type == EventType::PointerMove) {
                const auto dx = event.local.x - drag->previous.x, dy = event.local.y - drag->previous.y;
                if (!finite(dx) || !finite(dy)) return;
                drag->previous = event.local;
                orbit.yaw = __builtin_fmod(orbit.yaw - dx * .009, 2 * pi);
                orbit.pitch = rstd::cmp::max(Scalar(-1.47), rstd::min(Scalar(1.47), orbit.pitch + dy * .009));
                (void)context.document().invalidate_paint(node);
            } else if (input.type == EventType::PointerUp &&
                       (input.pointer_kind == PointerKind::Touch || input.button == PointerButton::Primary)) {
                drag = None();
                (void)context.release_pointer();
            } else if (input.type == EventType::PointerCancel || input.type == EventType::CaptureLost) {
                drag = None();
            }
        }

        auto draw(ModifierDrawContext &context) -> Result<empty, UiError> {
            auto &paint = context.paint();
            const auto size = paint.size();
            if (size.width <= 0 || size.height <= 0) return Ok(empty{});
            auto timer = recording->measure();
            auto &canvas = paint.canvas();
            const auto stage = Shape::rounded_rect({0, 0, size.width, size.height}, panel_radius);
            canvas.fill(stage, stage_ink);
            canvas.save();
            canvas.clip(stage);
            const Camera camera(orbit, size);
            draw_platform(canvas, size, camera);
            draw_cube(canvas, cube, camera, sequence.turn(tick));
            canvas.restore();
            canvas.border(stage, 1, stage_edge);
            return Ok(empty{});
        }
    };

    CubeOptions options;
    cx::Borrowed<CanvasRecording> recording;

    auto create() const -> State {
        return {options, recording, Sequence(options.scramble), Cube{}, Orbit{},
                options.scramble && options.playing ? hold_frames : Index{}};
    }

    auto update(State &state, cx::BehaviorContext &context) const -> cx::ComposeResult<empty> {
        const auto reset = state.options.reset != options.reset;
        const auto scramble = state.options.scramble != options.scramble;
        const auto resume = options.playing && !state.options.playing;
        state.options = options;
        if (reset || scramble) {
            state.cube = Cube{};
            state.sequence = Sequence(options.scramble);
            state.tick = reset ? 0 : hold_frames;
        }
        if (reset) state.orbit = Orbit{};
        if (resume && state.driver && context.session().motion().request(*state.driver).is_err())
            return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(UiError::InvalidValue)));
        return Ok(empty{});
    }

    auto attach(State &state, cx::BehaviorContext &context, rstd::rc::Weak<State> receiver) const
        -> cx::ComposeResult<cx::EffectLease> {
        state.session = &context.session();
        state.node = context.target();
        auto registered = context.session().motion().bind(rstd::move(receiver));
        if (registered.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(UiError::InvalidValue)));
        auto driver = registered.unwrap();
        state.driver = Some(MotionId(driver.id));
        return Ok(cx::EffectLease([binding = rstd::move(driver.binding)]() mutable { binding.disconnect(); }));
    }
};
} // namespace

auto cube_canvas(CubeOptions options, cx::Borrowed<CanvasRecording> recording) -> cx::Element {
    auto drawing = cx::native(LayoutKind::Leaf).style(fill().build())
        .behavior(cx::Key{"canvas-cube"_str}, CubePainter{options, recording});
    auto tip = cx::column({label("Drag to orbit"_str, 12, Color::from_srgba8(183, 193, 218).unwrap())})
        .style(fill().padding(14).hit_test_visible(false).build());
    return cx::stack({rstd::move(drawing), rstd::move(tip)}).style(fill().hit_self(false).build());
}
} // namespace examples::gallery::detail
