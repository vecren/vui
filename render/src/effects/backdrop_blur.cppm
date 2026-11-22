module vui.render:effects.backdrop_blur;
import :renderer;

using namespace rstd::prelude;

namespace vui::gpu::detail {
struct BackdropBlur {
    struct Kernel {
        float sigma{};
        Index radius{};
    };
    VkExtent2D capture{}, filtered{};
    Index x{}, y{};
    Kernel horizontal, vertical;
    Serial samples{};

    static auto plan(const DrawData &, Scalar sigma, Size logical, VkExtent2D target,
                     slice<GpuClip> clips, Index clip) -> Option<BackdropBlur>;
    void snapshot(DrawData &) const;
    void filter(DrawData &, bool vertical_pass) const;
    void composite(DrawData &) const;
};

inline constexpr Serial backdrop_sample_budget = 64 * 1024 * 1024;
} // namespace vui::gpu::detail
