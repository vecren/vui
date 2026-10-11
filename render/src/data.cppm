module vui.render:data;
import vrento;
namespace vui::gpu::detail {
inline constexpr auto format = VK_FORMAT_R8G8B8A8_SRGB;
struct DrawData {
    float rect[4], color[4], row0[4], row1[4], params[4], surface[4], uv[4];
};
static_assert(sizeof(DrawData) == 112);
struct GpuClip {
    float rect[4], row0[4], row1[4], params[4];
};
} // namespace vui::gpu::detail
