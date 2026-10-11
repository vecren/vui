module;
#include <SDL_events.h>
export module vui.sdl:window;
import vui.render;
using namespace rstd::prelude;

export namespace vui::sdl {
struct Events {
    Vec<InputEvent> input;
    SurfaceInfo surface;
    bool redraw{}, close_requested{}, minimized{};
    Serial dropped_input{};
};
struct WakeStats {
    Serial queued{}, coalesced{}, failed{};
    bool pending{}, closed{};
};
class Window final : public gpu::SurfaceHost {
    struct Impl;
    Box<Impl> impl_;

  public:
    Window();
    ~Window();
    Window(const Window &) = delete;
    auto operator=(const Window &) -> Window & = delete;
    auto open(const char *title, Index width, Index height) -> Result<empty, gpu::Error>;
    auto poll() -> Events;
    auto wait(Index milliseconds) -> Events;
    auto frame_waker() const -> Option<FrameWaker>;
    auto wake_stats() const -> WakeStats;
    auto handle_event(const SDL_Event &event) -> Events;
    auto timing() const -> FrameTiming;
    void set_input_tracing(bool enabled);
    auto capture(PointerKind kind, bool enabled) -> NativeCaptureResult;
    auto resize(Index width, Index height) -> Result<empty, gpu::Error>;
    void minimize();
    void restore();
    auto id() const -> Index;
    auto error() const -> const char *;
    auto instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> override;
    auto create_surface(VkInstance) -> Result<gpu::NativeSurface, gpu::Error> override;
    auto pixel_extent() const -> VkExtent2D override;
};
} // namespace vui::sdl
