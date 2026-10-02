export module vui.examples:sample_image;
import vui;

export namespace examples {
auto make_sample_image() -> vui::ImageData {
    using namespace rstd::prelude;
    constexpr unsigned width = 64, height = 32;
    Vec<unsigned char> pixels;
    pixels.reserve(usize(width * height * 4));
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const bool light = ((x / 8) + (y / 8)) % 2 == 0;
            pixels.push(static_cast<unsigned char>(light ? 240 : 30));
            pixels.push(static_cast<unsigned char>(80 + y * 4));
            pixels.push(static_cast<unsigned char>(light ? 50 : 220));
            pixels.push(static_cast<unsigned char>(x < width / 2 ? 255 : 144));
        }
    }
    return vui::ImageData::rgba8_srgb(width, height, pixels.as_slice()).unwrap();
}
} // namespace examples
