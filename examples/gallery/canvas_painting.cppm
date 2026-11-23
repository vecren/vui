export module vui.examples.gallery:canvas_painting;
import :canvas_geometry;
import :style;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
constexpr Index painting_brush_count = 3;

struct CanvasPaintingOptions {
    bool dashed{};
    Index brush{};
    Serial undo{};
    Serial clear{};
    friend auto operator==(CanvasPaintingOptions, CanvasPaintingOptions) -> bool = default;
};

namespace {
constexpr usize stroke_limit(48), point_limit(256);
constexpr Scalar brush_widths[painting_brush_count]{2.5, 5, 9};
const auto board = Color::from_srgba8(23, 27, 38).unwrap();
const auto ink_color = Color::from_srgba8(236, 239, 250).unwrap();
const auto turquoise = Color::from_srgba8(117, 228, 214).unwrap();
const auto periwinkle = Color::from_srgba8(160, 164, 249).unwrap();
const auto gold = Color::from_srgba8(246, 204, 131).unwrap();

auto board_shape(Size size) -> Shape {
    return Shape::rounded_rect({0, 0, size.width, size.height}, panel_radius);
}

auto along(Point first, Point last, Scalar fraction) -> Point {
    return {first.x + (last.x - first.x) * fraction, first.y + (last.y - first.y) * fraction};
}

void dashed_segment(Canvas &canvas, Point start, Point end, Scalar width, Scalar &phase) {
    const auto length = point_distance(start, end);
    if (length <= 0) return;
    const auto dash_length = width * 3.2;
    const auto dash_period = width * 5.6;
    for (auto cursor = -phase; cursor < length; cursor += dash_period) {
        const auto from = rstd::cmp::max(Scalar(0), cursor);
        const auto to = rstd::min(length, cursor + dash_length);
        if (to > from) stroke_segment(canvas, along(start, end, from / length), along(start, end, to / length), width, ink_color);
    }
    phase = __builtin_fmod(phase + length, dash_period);
}

auto polar(Point center, Scalar radius, Scalar angle) -> Point {
    return {center.x + radius * __builtin_cos(angle), center.y + radius * __builtin_sin(angle)};
}

void draw_rosette(Canvas &canvas, Size size) {
    constexpr Index petals = 12;
    const Point center{size.width / 2, size.height / 2};
    const auto radius = rstd::min(size.width, size.height) * .425;
    constexpr Scalar rings[]{.25, .48, .73, .96, 1.07};
    for (const auto ring : rings)
        canvas.border(Shape::circle(center, radius * ring), .75, periwinkle.modulate(white(.19f)));
    for (Index i{}; i < petals; ++i) {
        const auto angle = Scalar(i) * canvas_turn / petals;
        const auto start = polar(center, radius * .29, angle);
        const auto end = polar(center, radius * 1.07, angle);
        stroke_segment(canvas, start, end, .65, periwinkle.modulate(white(.12f)));
        canvas.fill(Shape::circle(end, 1.25), turquoise.modulate(white(.65f)));
    }

    const auto width = rstd::min(Scalar(1.7), rstd::cmp::max(Scalar(.8), radius * .010));
    auto hypotrochoid = [=](Scalar t, Scalar outer, Scalar inner, Index a, Index b, Scalar rotation) -> Point {
        const auto main = polar(center, radius * outer, a * t + rotation);
        return polar(main, radius * inner, -Scalar(b) * t + rotation);
    };
    stroke_curve(canvas, 480, width, turquoise.modulate(white(.84f)), [&](Scalar t) {
        return hypotrochoid(canvas_turn * t, .61, .35, 5, 7, 0);
    });
    stroke_curve(canvas, 420, width * .77, periwinkle.modulate(white(.68f)), [&](Scalar t) {
        return hypotrochoid(canvas_turn * t, .40, .32, 7, 5, canvas_turn / (2 * petals));
    });
    stroke_curve(canvas, 240, width * .82, gold.modulate(white(.85f)), [&](Scalar t) {
        const auto angle = canvas_turn * t;
        return polar(center, radius * .275 * __builtin_cos(6 * angle), angle);
    });
}

struct PaintingArtwork {
    using State = PaintingArtwork;

    auto create() const -> State { return {}; }

    auto draw(ModifierDrawContext &modifier) -> Result<empty, UiError> {
        auto &paint = modifier.paint();
        auto &canvas = paint.canvas();
        const auto size = paint.size();
        if (size.width <= 0 || size.height <= 0) return Ok(empty{});
        const auto stage = board_shape(size);
        canvas.fill(stage, board);
        canvas.save();
        canvas.clip(stage);
        for (Scalar x = 24; x < size.width; x += 24) canvas.rect({x, 0, 1, size.height}, white(.016f));
        for (Scalar y = 24; y < size.height; y += 24) canvas.rect({0, y, size.width, 1}, white(.016f));
        draw_rosette(canvas, size);
        canvas.restore();
        return Ok(empty{});
    }
};

struct Stroke {
    Scalar width;
    Vec<Point> points;
};

struct PaintingPainter {
    struct State {
        CanvasPaintingOptions options;
        Vec<Stroke> strokes;
        Option<PointerKey> pen;
        UiSession *session{};

        void append(Point point, bool straighten, Size bounds) {
            if (!finite(point.x) || !finite(point.y)) return;
            auto &line = strokes[strokes.len() - usize(1)].points;
            const auto start = line[usize()];
            point.x = rstd::min(bounds.width, rstd::cmp::max(Scalar(0), point.x));
            point.y = rstd::min(bounds.height, rstd::cmp::max(Scalar(0), point.y));
            if (straighten) {
                if (__builtin_fabs(point.x - start.x) >= __builtin_fabs(point.y - start.y)) point.y = start.y;
                else point.x = start.x;
                line.truncate(usize(1));
            }
            const auto last = line.len() - usize(1);
            if (point_distance(line[last], point) < .8) return;
            if (line.len() < point_limit) line.push(Point(point));
            else line[last] = point;
        }

        void handle(EventContext &context, const RoutedEvent &event) {
            if (event.current != event.target) return;
            const auto &input = event.input;
            PointerKey key{input.device, input.pointer};
            const bool primary = input.pointer_kind == PointerKind::Touch || input.button == PointerButton::Primary;
            if (input.type == EventType::PointerDown) {
                if (pen || !primary || context.capture_pointer().is_err()) return;
                if (strokes.len() >= stroke_limit) strokes.remove(usize());
                Stroke stroke{brush_widths[options.brush], {}};
                stroke.points.push(Point(event.local));
                strokes.push(rstd::move(stroke));
                pen = Some(key);
                context.prevent_default();
            } else {
                if (pen != Some(key)) return;
                if (input.type == EventType::PointerCancel || input.type == EventType::CaptureLost) {
                    pen = None();
                    return;
                }
                const bool released = input.type == EventType::PointerUp && primary;
                if (input.type != EventType::PointerMove && !released) return;
                const auto *node = context.visual(event.current);
                if (node) {
                    const Size bounds{node->layout_bounds.width, node->layout_bounds.height};
                    if (!input.history.is_empty() && !input.modifiers.shift) {
                        auto snapshot = session->input().snapshot();
                        if (snapshot) {
                            auto mapping = snapshot->local_to_surface(event.current);
                            if (mapping) {
                                auto inverse = mapping->inverse();
                                if (inverse) for (const auto &sample : input.history) append(inverse->map(sample.position), false, bounds);
                            }
                        }
                    }
                    append(event.local, input.modifiers.shift, bounds);
                }
                if (released) {
                    pen = None();
                    (void)context.release_pointer();
                }
            }
            (void)context.document().invalidate_paint(event.current);
        }

        auto draw(ModifierDrawContext &modifier) -> Result<empty, UiError> {
            auto &paint = modifier.paint();
            auto &canvas = paint.canvas();
            const auto size = paint.size();
            if (size.width <= 0 || size.height <= 0) return Ok(empty{});
            const auto stage = board_shape(size);
            canvas.save();
            canvas.clip(stage);
            for (const auto &stroke : strokes) {
                const auto &line = stroke.points;
                if (line.len() == usize(1)) canvas.fill(Shape::circle(line[usize()], stroke.width / 2), ink_color);
                Scalar phase{};
                for (usize i(1); i < line.len(); ++i) {
                    const auto a = line[i - usize(1)], b = line[i];
                    if (options.dashed) dashed_segment(canvas, a, b, stroke.width, phase);
                    else stroke_segment(canvas, a, b, stroke.width, ink_color);
                }
            }
            canvas.restore();
            canvas.border(stage, 1, white(.14f));
            return Ok(empty{});
        }
    };

    CanvasPaintingOptions options;
    auto create() const -> State { return {options}; }

    void update(State &state, cx::BehaviorContext &) const {
        if (state.options.clear != options.clear) {
            state.pen = None();
            state.strokes.clear();
        } else if (state.options.undo != options.undo) {
            const auto undone = rstd::min(state.strokes.len(), usize(options.undo - state.options.undo));
            state.strokes.truncate(state.strokes.len() - undone);
            state.pen = None();
        }
        state.options = options;
    }

    auto attach(State &state, cx::BehaviorContext &context, rstd::rc::Weak<State>) const -> cx::EffectLease {
        state.session = &context.session();
        return {};
    }
};
} // namespace

auto painting_canvas(CanvasPaintingOptions options) -> cx::Element {
    auto artwork = cx::native(LayoutKind::Leaf).style(fill().hit_test_visible(false).build())
        .behavior(cx::Key{"canvas-rosette"_str}, PaintingArtwork{});
    auto strokes = cx::native(LayoutKind::Leaf).style(fill().build())
        .behavior(cx::Key{"canvas-painting"_str}, PaintingPainter{options});
    return cx::stack({rstd::move(artwork), rstd::move(strokes)}).style(fill().hit_self(false).build());
}
} // namespace examples::gallery::detail
