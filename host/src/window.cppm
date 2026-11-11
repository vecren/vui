export module vui.host:window;
import vui.render;
using namespace rstd::prelude;

export namespace vui::host {
enum class WindowBackend { Automatic, Wayland, X11 };

class Window final : public gpu::SurfaceHost {
    struct Impl;
    Box<Impl> impl_;

  public:
    Window();
    ~Window();
    Window(const Window &) = delete;
    auto operator=(const Window &) -> Window & = delete;
    auto open(const char *title, Index width, Index height,
              WindowBackend backend = WindowBackend::Automatic) -> Result<empty, gpu::Error>;
    void close();
    auto backend() const -> WindowBackend;
    auto synchronize() -> Result<empty, gpu::Error>;
    auto instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> override;
    auto create_surface(VkInstance instance) -> Result<gpu::NativeSurface, gpu::Error> override;
    auto pixel_extent() const -> VkExtent2D override;
};
} // namespace vui::host
