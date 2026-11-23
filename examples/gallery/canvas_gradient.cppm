export module vui.examples.gallery:canvas_gradient;
import :style;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
struct CanvasGradientOptions {
    bool perceptual{};
    Index turns{};
    friend auto operator==(CanvasGradientOptions, CanvasGradientOptions) -> bool = default;
};

namespace {
struct Channels {
    Scalar r, g, b;
};

auto oklab(Channels rgb) -> Channels {
    const auto l = __builtin_cbrt(.4122214708 * rgb.r + .5363325363 * rgb.g + .0514459929 * rgb.b);
    const auto m = __builtin_cbrt(.2119034982 * rgb.r + .6806995451 * rgb.g + .1073969566 * rgb.b);
    const auto s = __builtin_cbrt(.0883024619 * rgb.r + .2817188376 * rgb.g + .6299787005 * rgb.b);
    return {.2104542553 * l + .7936177850 * m - .0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + .4505937099 * s,
            .0259040371 * l + .7827717662 * m - .8086757660 * s};
}

auto srgb(Channels lab) -> Channels {
    const auto cube = [](Scalar value) { return value * value * value; };
    const auto l = cube(lab.r + .3963377774 * lab.g + .2158037573 * lab.b);
    const auto m = cube(lab.r - .1055613458 * lab.g - .0638541728 * lab.b);
    const auto s = cube(lab.r - .0894841775 * lab.g - 1.2914855480 * lab.b);
    const auto encode = [](Scalar value) {
        value = rstd::min(Scalar(1), rstd::cmp::max(Scalar(0), value));
        return value <= .0031308 ? 12.92 * value : 1.055 * __builtin_pow(value, 1.0 / 2.4) - .055;
    };
    return {encode(4.0767416621 * l - 3.3077115913 * m + .2309699292 * s),
            encode(-1.2684380046 * l + 2.6097574011 * m - .3413193965 * s),
            encode(-.0041960863 * l - .7034186147 * m + 1.7076147010 * s)};
}

struct Stop {
    Scalar offset;
    Channels color;
};

struct Swatch {
    Scalar angle;
    Index count;
    Stop stops[4];

    auto sampling() const -> ImageSampling {
        for (Index i = 1; i < count; ++i)
            if (stops[i - 1].offset == stops[i].offset) return ImageSampling::Nearest;
        return ImageSampling::Linear;
    }
};

constexpr Swatch swatches[]{
    {.52, 3, {{0, {.99, .32, .39}}, {.52, {.98, .60, .89}}, {1, {.28, .37, .98}}}},
    {1.57, 3, {{0, {.16, .84, .79}}, {.30, {.04, .61, .79}}, {1, {.21, .28, .88}}}},
    {-.74, 2, {{0, {.99, .70, .31}}, {1, {.96, .31, .52}}}},
    {0, 3, {{.18, {.36, .74, .95}}, {.58, {.98, .89, .67}}, {.82, {.99, .44, .17}}}},
    {0, 2, {{.54, {.93, .29, .48}}, {.54, {.36, .89, .71}}}},
    {.71, 4, {{.12, {.56, .44, .96}}, {.38, {.65, .61, .96}}, {.66, {.56, .86, .88}}, {.88, {.91, .88, .67}}}},
};
constexpr Index ramp_width = 256;
constexpr Scalar eighth_turn = .7853981633974483;

auto ramp(const Swatch &swatch, bool perceptual) -> ImageData {
    const auto convert = [perceptual](Channels value) {
        if (!perceptual) return value;
        const auto linear = Color::from_srgba(value.r, value.g, value.b).unwrap();
        return oklab({linear.r, linear.g, linear.b});
    };
    Index right = 1;
    auto first = convert(swatch.stops[0].color), last = convert(swatch.stops[right].color);
    Vec<unsigned char> pixels;
    pixels.reserve(usize(ramp_width * 4));
    for (Index i{}; i < ramp_width; ++i) {
        const auto position = Scalar(i) / (ramp_width - 1);
        while (right + 1 < swatch.count && position >= swatch.stops[right].offset) {
            first = last;
            last = convert(swatch.stops[++right].color);
        }
        const auto start = swatch.stops[right - 1].offset, end = swatch.stops[right].offset;
        const auto fraction = start == end ? Scalar(position >= start)
            : rstd::min(Scalar(1), rstd::cmp::max(Scalar(0), (position - start) / (end - start)));
        const auto mix = [fraction](Scalar a, Scalar b) { return a + (b - a) * fraction; };
        Channels value{mix(first.r, last.r), mix(first.g, last.g), mix(first.b, last.b)};
        if (perceptual) value = srgb(value);
        pixels.push(static_cast<unsigned char>(value.r * 255 + .5));
        pixels.push(static_cast<unsigned char>(value.g * 255 + .5));
        pixels.push(static_cast<unsigned char>(value.b * 255 + .5));
        pixels.push(255);
    }
    return ImageData::rgba8_srgb(ramp_width, 1, pixels.as_slice()).unwrap();
}

struct GradientPainter {
    struct State {
        CanvasGradientOptions options;
        Vec<ImageData> ramps;

        void rebuild() {
            ramps.clear();
            for (const auto &swatch : swatches) ramps.push(ramp(swatch, options.perceptual));
        }

        auto draw(ModifierDrawContext &context) -> Result<empty, UiError> {
            auto &paint = context.paint();
            auto &ink = paint.canvas();
            const auto size = paint.size();
            constexpr Scalar gap = 8;
            const Index columns = size.width >= 470 ? 3 : 2;
            const auto rows = Index(6) / columns;
            const auto width = (size.width - (columns - 1) * gap) / columns;
            const auto height = (size.height - (rows - 1) * gap) / rows;
            if (width <= 0 || height <= 0) return Ok(empty{});
            for (Index i{}; i < 6; ++i) {
                const Rect cell{(i % columns) * (width + gap), (i / columns) * (height + gap), width, height};
                const auto mask = i == 5 ? Shape::ellipse(cell) : Shape::rounded_rect(cell, panel_radius);
                const auto angle = swatches[i].angle + options.turns * eighth_turn;
                const auto c = __builtin_cos(angle), s = __builtin_sin(angle);
                const auto u = __builtin_fabs(c) * width + __builtin_fabs(s) * height;
                const auto v = __builtin_fabs(s) * width + __builtin_fabs(c) * height;
                ink.save();
                ink.clip(mask);
                ink.transform({c, s, -s, c, cell.x + width / 2, cell.y + height / 2});
                ink.image(ramps[usize(i)], {-u / 2, -v / 2, u, v}, {0, 0, ramp_width, 1}, swatches[i].sampling());
                ink.restore();
                ink.border(mask, 1, white(.24f));
            }
            return Ok(empty{});
        }
    };

    CanvasGradientOptions options;
    auto create() const -> State {
        State state{options};
        state.rebuild();
        return state;
    }
    void update(State &state, cx::BehaviorContext &) const {
        const auto changed_space = state.options.perceptual != options.perceptual;
        state.options = options;
        if (changed_space) state.rebuild();
    }
};
} // namespace

auto gradient_canvas(CanvasGradientOptions options) -> cx::Element {
    return cx::native(LayoutKind::Leaf).style(fill().hit_test_visible(false).build())
        .behavior(cx::Key{"canvas-gradient"_str}, GradientPainter{options});
}
} // namespace examples::gallery::detail
