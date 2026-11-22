export module vui.examples.gallery:canvas_fourier;
import :canvas_recording;
import :canvas_word;
import :style;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
constexpr Index fourier_counts[]{16, 48, 96};

struct FourierOptions {
    Index circles{fourier_counts[1]};
    bool playing{true};
    Serial preset_revision{};
    friend auto operator==(FourierOptions, FourierOptions) -> bool = default;
};

namespace {
constexpr Index sample_count = 256;
constexpr Index stroke_limit = 1024;
constexpr Serial lap_frames = 384;
constexpr Scalar tau = 6.2831853071795864769;

auto mix(Point a, Point b, Scalar t) -> Point {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

auto distance(Point a, Point b) -> Scalar {
    const auto x = b.x - a.x, y = b.y - a.y;
    return __builtin_sqrt(x * x + y * y);
}

auto rotate(Point value, Point unit) -> Point {
    return {value.x * unit.x - value.y * unit.y, value.x * unit.y + value.y * unit.x};
}

struct UnitCircle { Point samples[sample_count]; };
const auto units = [] {
    UnitCircle result;
    for (Index i{}; i < sample_count; ++i) {
        const auto angle = tau * i / sample_count;
        result.samples[i] = {__builtin_cos(angle), __builtin_sin(angle)};
    }
    return result;
}();

struct Term {
    Point vector;
    Scalar radius;
    Index bin;
};

class CurveModel {
    Vec<Point> outline_;
    Point trace_[sample_count + 1]{};
    Vec<Term> terms_;
    Point origin_;
    Scalar length_{};

  public:
    auto fit(slice<Point> points, Index circles) -> bool {
        if (points.len() < usize(3)) return false;
        Vec<Scalar> lengths;
        lengths.reserve(points.len() + usize(1));
        lengths.push(Scalar(0));
        Scalar length{};
        for (usize i{}; i < points.len(); ++i) {
            length += distance(points[i], points[(i + usize(1)) % points.len()]);
            lengths.push(Scalar(length));
        }
        if (length < .10) return false;
        outline_ = Vec<Point>::from(points);
        length_ = length;
        Point samples[sample_count];
        usize segment{};
        for (Index i{}; i < sample_count; ++i) {
            const auto target = i * length / sample_count;
            while (segment + usize(1) < points.len() && lengths[segment + usize(1)] <= target)
                ++segment;
            const auto span = lengths[segment + usize(1)] - lengths[segment];
            const auto t = span > 0 ? (target - lengths[segment]) / span : 0;
            samples[i] = mix(points[segment], points[(segment + usize(1)) % points.len()], t);
        }

        terms_.clear();
        terms_.reserve(usize(sample_count - 1));
        for (Index bin{}; bin < sample_count; ++bin) {
            Point coefficient;
            for (Index i{}; i < sample_count; ++i) {
                const auto unit = units.samples[(sample_count - bin * i % sample_count) % sample_count];
                const auto rotated = rotate(samples[i], unit);
                coefficient.x += rotated.x / sample_count;
                coefficient.y += rotated.y / sample_count;
            }
            if (bin == 0) origin_ = coefficient;
            else terms_.push({coefficient, distance({}, coefficient), bin});
        }
        rstd::slice_::sort_unstable_by(terms_.as_mut_slice().as_mut_ref(), [](const Term &a, const Term &b) {
            return a.radius == b.radius ? a.bin < b.bin : a.radius > b.radius;
        });
        rebuild(circles);
        return true;
    }

    void rebuild(Index circles) {
        for (Index i{}; i < sample_count; ++i) {
            auto point = origin_;
            for (Index k{}; k < circles; ++k) {
                const auto &term = terms_[usize(k)];
                const auto vector = rotate(term.vector, units.samples[term.bin * i % sample_count]);
                point.x += vector.x;
                point.y += vector.y;
            }
            trace_[i] = point;
        }
        trace_[sample_count] = trace_[0];
    }

    auto outline() const -> slice<Point> { return outline_.as_slice(); }
    auto trace() const -> const Point (&)[sample_count + 1] { return trace_; }
    auto origin() const -> Point { return origin_; }
    auto length() const -> Scalar { return length_; }
    auto term(Index index) const -> const Term & { return terms_[usize(index)]; }
};

struct Plot {
    Size size;

    auto scale() const -> Scalar { return rstd::min(size.width * .33, size.height * .42); }
    auto project(Point point) const -> Point {
        const auto zoom = scale();
        return {size.width / 2 + point.x * zoom, size.height / 2 + point.y * zoom};
    }
    auto unproject(Point point) const -> Point {
        const auto zoom = scale();
        const auto x = rstd::cmp::max(Scalar(0), rstd::min(point.x, size.width));
        const auto y = rstd::cmp::max(Scalar(0), rstd::min(point.y, size.height));
        return {(x - size.width / 2) / zoom, (y - size.height / 2) / zoom};
    }
};

const auto paper = Color::from_srgba8(237, 242, 239).unwrap();
const auto grid = Color::from_srgba8(212, 226, 223).unwrap();
const auto guide = Color::from_srgba8(166, 190, 188).unwrap();
const auto ink = Color::from_srgba8(24, 63, 70).unwrap();
const auto circle = Color::from_srgba8(82, 127, 159, 126).unwrap();
const auto vector_ink = Color::from_srgba8(64, 93, 128, 208).unwrap();
const auto tip_ink = Color::from_srgba8(239, 120, 79).unwrap();

void line(Canvas &canvas, Point a, Point b, Scalar width, Color color) {
    const auto length = distance(a, b);
    if (length < .01) return;
    const auto c = (b.x - a.x) / length, s = (b.y - a.y) / length;
    canvas.save();
    canvas.transform({c, s, -s, c, a.x, a.y});
    canvas.rect({-width / 2, -width / 2, length + width, width}, color, width / 2);
    canvas.restore();
}

void grid_lines(Canvas &canvas, Plot plot) {
    constexpr Scalar spacing = 24;
    const auto size = plot.size;
    const auto x = size.width / 2, y = size.height / 2;
    for (auto pos = x - spacing * __builtin_floor(x / spacing); pos < size.width; pos += spacing)
        canvas.rect({pos - .35, 0, .70, size.height}, grid);
    for (auto pos = y - spacing * __builtin_floor(y / spacing); pos < size.height; pos += spacing)
        canvas.rect({0, pos - .35, size.width, .70}, grid);
    canvas.rect({x - .5, 0, 1, size.height}, guide);
    canvas.rect({0, y - .5, size.width, 1}, guide);
}

struct Contact { DeviceId device; Index pointer; };
} // namespace

struct FourierPainter {
    struct State {
        FourierOptions options;
        cx::Borrowed<CanvasRecording> recording;
        Vec<Vec<Point>> preset;
        Vec<CurveModel> contours;
        Vec<Point> stroke;
        Option<Contact> contact;
        UiSession *session{};
        UiId node;
        Option<MotionId> driver;
        Plot plot;
        Serial step{};

        void reset() {
            contours.clear();
            for (const auto &outline : preset) {
                CurveModel curve;
                if (curve.fit(outline.as_slice(), options.circles)) contours.push(rstd::move(curve));
            }
            step = 0;
        }

        void fit_stroke() {
            CurveModel curve;
            if (!curve.fit(stroke.as_slice(), options.circles)) return;
            contours.clear();
            contours.push(rstd::move(curve));
            step = 0;
        }

        auto sample(MotionContext &clock, FrameTiming) -> Result<MotionStatus, MotionError> {
            if (!options.playing || contact.is_some()) return Ok(MotionStatus::Idle);
            step = (step + 1) % lap_frames;
            if (session->document().invalidate_paint(node).is_err()) return Err(MotionError::Unavailable);
            auto scheduled = clock.request_tick(clock.now_ns() + 16'666'667);
            if (scheduled.is_err()) return Err(scheduled.unwrap_err());
            return Ok(MotionStatus::Running);
        }

        void append(Point local) {
            if (plot.scale() <= 0 || !finite(local.x) || !finite(local.y)) return;
            const auto normalized = plot.unproject(local);
            if (!stroke.is_empty() && distance(stroke[stroke.len() - usize(1)], normalized) * plot.scale() < 1.5)
                return;
            if (stroke.len() == usize(stroke_limit)) stroke[stroke.len() - usize(1)] = normalized;
            else stroke.push(Point(normalized));
        }

        void handle(EventContext &context, const RoutedEvent &event) {
            if (event.current != event.target) return;
            const auto &input = event.input;
            switch (input.type) {
            case EventType::PointerDown:
                if (contact || (input.pointer_kind != PointerKind::Touch && input.button != PointerButton::Primary) ||
                    plot.scale() <= 0 || context.capture_pointer().is_err()) return;
                stroke.clear();
                contact = Some(Contact{input.device, input.pointer});
                append(event.local);
                context.prevent_default();
                break;
            case EventType::PointerMove:
            case EventType::PointerUp:
            case EventType::CaptureLost:
            case EventType::PointerCancel:
                if (!contact || contact->device != input.device || contact->pointer != input.pointer) return;
                if (input.type == EventType::PointerUp && input.pointer_kind != PointerKind::Touch &&
                    input.button != PointerButton::Primary) return;
                if (input.type == EventType::PointerMove) append(event.local);
                else {
                    contact = None();
                    if (input.type == EventType::PointerUp) {
                        append(event.local);
                        fit_stroke();
                        (void)context.release_pointer();
                    }
                    if (options.playing && driver) (void)session->motion().request(*driver);
                }
                break;
            default:
                return;
            }
            (void)context.document().invalidate_paint(node);
        }

        void draw_trace(Canvas &canvas, const CurveModel &curve, Index reached) const {
            const auto &trace = curve.trace();
            for (Index i{}; i < reached; ++i)
                line(canvas, plot.project(trace[i]), plot.project(trace[i + 1]), 2, ink);
        }

        void draw_active(Canvas &canvas, const CurveModel &curve, Scalar phase) const {
            const auto reached = Index(phase * sample_count);
            draw_trace(canvas, curve, reached);
            auto center = curve.origin();
            for (Index i{}; i < options.circles; ++i) {
                const auto &term = curve.term(i);
                const auto frequency = term.bin > sample_count / 2 ? int(term.bin) - int(sample_count) : int(term.bin);
                const auto angle = tau * phase * frequency;
                const auto delta = rotate(term.vector, {__builtin_cos(angle), __builtin_sin(angle)});
                const Point next{center.x + delta.x, center.y + delta.y};
                const auto radius = term.radius * plot.scale();
                if (radius >= .65) {
                    canvas.border(Shape::circle(plot.project(center), radius + .4), .8, circle);
                    line(canvas, plot.project(center), plot.project(next), 1, vector_ink);
                }
                center = next;
            }
            line(canvas, plot.project(curve.trace()[reached]), plot.project(center), 2, ink);
            canvas.fill(Shape::circle(plot.project(center), 3.4), tip_ink);
        }

        void draw_epicycles(Canvas &canvas) const {
            Scalar length{};
            for (const auto &curve : contours) {
                length += curve.length();
                const auto outline = curve.outline();
                for (usize i{}; i < outline.len(); ++i)
                    line(canvas, plot.project(outline[i]), plot.project(outline[(i + usize(1)) % outline.len()]), 1, guide);
            }
            auto remaining = length * Scalar(step) / lap_frames;
            for (const auto &curve : contours) {
                if (remaining < curve.length()) {
                    draw_active(canvas, curve, remaining / curve.length());
                    break;
                }
                draw_trace(canvas, curve, sample_count);
                remaining -= curve.length();
            }
        }

        auto draw(ModifierDrawContext &context) -> Result<empty, UiError> {
            auto &paint = context.paint();
            plot.size = paint.size();
            if (plot.scale() <= 0) return Ok(empty{});
            auto timer = recording->measure();
            auto &canvas = paint.canvas();
            const auto stage = Shape::rounded_rect({0, 0, plot.size.width, plot.size.height}, panel_radius);
            canvas.fill(stage, paper);
            canvas.save();
            canvas.clip(stage);
            grid_lines(canvas, plot);
            if (contact) {
                for (usize i(1); i < stroke.len(); ++i)
                    line(canvas, plot.project(stroke[i - usize(1)]), plot.project(stroke[i]), 2, ink);
                if (!stroke.is_empty())
                    canvas.fill(Shape::circle(plot.project(stroke[stroke.len() - usize(1)]), 3.4), tip_ink);
            } else {
                draw_epicycles(canvas);
            }
            canvas.restore();
            return Ok(empty{});
        }
    };

    FourierOptions options;
    cx::Borrowed<CanvasRecording> recording;

    auto create() const -> State { return {options, recording}; }

    auto update(State &state, cx::BehaviorContext &context) const -> cx::ComposeResult<empty> {
        const auto reset = state.options.preset_revision != options.preset_revision;
        const auto rebuild = state.options.circles != options.circles;
        const auto resume = options.playing && !state.options.playing;
        state.options = options;
        if (reset) state.reset();
        else if (rebuild)
            for (auto &curve : state.contours) curve.rebuild(options.circles);
        if (resume && state.driver && context.session().motion().request(*state.driver).is_err())
            return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(UiError::InvalidValue)));
        return Ok(empty{});
    }

    auto attach(State &state, cx::BehaviorContext &context, rstd::rc::Weak<State> receiver) const
        -> cx::ComposeResult<cx::EffectLease> {
        state.session = &context.session();
        state.node = context.target();
        state.preset = canvas_word_outline();
        state.reset();
        auto registered = context.session().motion().bind(rstd::move(receiver));
        if (registered.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(UiError::InvalidValue)));
        auto driver = registered.unwrap();
        state.driver = Some(MotionId(driver.id));
        return Ok(cx::EffectLease([binding = rstd::move(driver.binding)]() mutable { binding.disconnect(); }));
    }
};

auto fourier_canvas(FourierOptions options, cx::Borrowed<CanvasRecording> recording) -> cx::Element {
    auto drawing = cx::native(LayoutKind::Leaf).style(fill().build())
        .behavior(cx::Key{"canvas-fourier"_str}, FourierPainter{options, recording});
    auto tip = cx::column({label("Draw a shape to replay it"_str, 12, ink)})
        .style(fill().padding(14).hit_test_visible(false).build());
    return cx::stack({rstd::move(drawing), rstd::move(tip)}).style(fill().hit_self(false).build());
}
} // namespace examples::gallery::detail
