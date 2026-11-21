export module vui.examples.gallery:glass;
import :parts;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
namespace {
struct Pixel { Scalar red, green, blue, alpha{255}; };

auto unit(Scalar value) -> Scalar { return rstd::cmp::max(Scalar(0), rstd::min(Scalar(1), value)); }

auto grain(Index x, Index y) -> Scalar {
    auto bits = (x * 0x1f123bb5u) ^ (y * 0x5f356495u);
    bits = (bits ^ (bits >> 16)) * 0x45d9f3bu;
    return Scalar((bits ^ (bits >> 16)) & 255) / 255 - .5;
}

auto light(Scalar x, Scalar y, Scalar u, Scalar v) -> Scalar {
    const auto dx = (x - u) / .58;
    const auto dy = (y - v) / .68;
    return __builtin_exp(-(dx * dx + dy * dy) * 2.6);
}

auto texture(Index width, Index height, auto sample) -> ImageRegion {
    Vec<unsigned char> pixels;
    pixels.reserve(usize(width) * usize(height) * usize(4));
    auto byte = [](Scalar value) { return static_cast<unsigned char>(255 * unit(value / 255) + .5); };
    for (Index y{}; y < height; ++y) {
        for (Index x{}; x < width; ++x) {
            auto color = sample((x + .5) / width, (y + .5) / height, grain(x, y));
            pixels.push(byte(color.red));
            pixels.push(byte(color.green));
            pixels.push(byte(color.blue));
            pixels.push(byte(color.alpha));
        }
    }
    return ImageRegion::whole(ImageData::rgba8_srgb(width, height, pixels.as_slice()).unwrap());
}

auto backdrop_image() -> const ImageRegion & {
    static const auto image = texture(384, 320, [](Scalar x, Scalar y, Scalar noise) {
        const auto halo = light(x, y, .48, .32);
        const auto cool = light(x, y, -.12, .20);
        const auto gray = 10 + 15 * halo + 2 * noise;
        return Pixel{gray, gray + 11 * cool, gray + 18 * cool};
    });
    return image;
}

auto panel_light() -> const ImageRegion & {
    static const auto image = texture(160, 112, [](Scalar x, Scalar y, Scalar noise) {
        const auto fade = unit(9 * rstd::min(x, 1 - x)) * unit(9 * rstd::min(y, 1 - y));
        const auto gray = 128 * __builtin_sqrt(light(x, y, .44, .39) * fade) + 2 * noise;
        return Pixel{gray, gray, gray, 18};
    });
    return image;
}

auto frost_image() -> const ImageRegion & {
    static const auto image = texture(256, 160, [](Scalar, Scalar, Scalar noise) {
        const auto gray = 228 + 14 * noise;
        return Pixel{gray, gray, gray, 3.25};
    });
    return image;
}

auto sheen_image() -> const ImageRegion & {
    static const auto image = texture(96, 32, [](Scalar x, Scalar y, Scalar noise) {
        const auto sheen = __builtin_exp(-6 * y) * (1 - .55 * x);
        const auto gray = 126 * __builtin_sqrt(sheen) + noise;
        return Pixel{gray, gray, gray, 24};
    });
    return image;
}

constexpr Scalar rim_inset = 16;

auto rim_image() -> const ImageRegion & {
    static const auto image = texture(35, 35, [](Scalar x, Scalar y, Scalar) {
        const auto dx = rstd::cmp::max(Scalar(0), __builtin_fabs((x - .5) * 35) - (17.5 - rim_inset));
        const auto dy = rstd::cmp::max(Scalar(0), __builtin_fabs((y - .5) * 35) - (17.5 - rim_inset));
        const auto distance = __builtin_sqrt(dx * dx + dy * dy) - rim_inset;
        const auto coverage = unit(.5 - distance) - unit(-1 - distance);
        return Pixel{255, 255, 255, 255 * coverage};
    });
    return image;
}
} // namespace

constexpr auto white(float opacity) -> Color { return {opacity, opacity, opacity, opacity}; }

void glass_rim(PaintContext &context, Rect bounds, Scalar radius, Color color) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    constexpr Insets source{rim_inset, rim_inset, rim_inset, rim_inset};
    context.nine_patch(rim_image(), {source, Some(Insets{radius, radius, radius, radius}), false}, bounds, color);
}

void glass_sheen(PaintContext &context, Rect bounds, Scalar radius) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const auto &image = sheen_image();
    context.save();
    context.clip({true, bounds, radius});
    context.image(image.image, bounds, image.source);
    context.restore();
}

struct GlassPaint {
    float opacity{1};
    bool popup{};

    auto paint(PaintContext &context) -> Result<empty, UiError> {
        const auto size = context.size();
        const Rect bounds{0, 0, size.width, size.height};
        if (size.width > 0 && size.height > 0) {
            const auto radius = context.style().radius;
            if (popup) context.rect(bounds, Color::from_srgba8(12, 12, 12, 254).unwrap(), radius);
            context.save();
            context.clip({true, bounds, radius});
            const auto &glow = panel_light();
            context.image(glow.image, bounds, glow.source);
            const auto &image = frost_image();
            context.image(image.image, bounds, image.source, ImageSampling::Linear, white(opacity));
            context.restore();
            glass_rim(context, bounds, radius, white(.012f));
        }
        context.children();
        return Ok(empty{});
    }
};

struct BackdropPaint {
    auto paint(PaintContext &context) -> Result<empty, UiError> {
        const auto size = context.size();
        const auto &image = backdrop_image();
        if (size.width > 0 && size.height > 0)
            context.image(image.image, {0, 0, size.width, size.height}, image.source);
        context.children();
        return Ok(empty{});
    }
};

auto glass(LayoutKind layout, Vec<cx::Element> children, GlassPaint material = {}) -> cx::Element {
    auto mount = [material](UiSession &session, UiId id) { return session.document().install_paint(id, material); };
    return cx::element(MountedPart{layout, mount}, rstd::move(children));
}
} // namespace examples::gallery::detail
