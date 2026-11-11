export module vui.host:window;
import vui.render;
using namespace rstd::prelude;

export namespace vui::host {
enum class WindowBackend { Automatic, Wayland, X11 };
struct WindowEvents {
    Vec<InputEvent> input;
    SurfaceInfo surface;
    bool redraw{}, close_requested{};
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
    auto open(const char *title, Index width, Index height,
              WindowBackend backend = WindowBackend::Automatic) -> Result<empty, gpu::Error>;
    void close();
    auto backend() const -> WindowBackend;
    auto configured() const -> bool;
    auto synchronize() -> Result<empty, gpu::Error>;
    auto wait(Index milliseconds) -> Result<WindowEvents, gpu::Error>;
    auto poll() -> Result<WindowEvents, gpu::Error> { return wait(0); }
    auto frame_waker() const -> Option<FrameWaker>;
    auto wake_stats() const -> WakeStats;
    auto capture(PointerKind kind, bool enabled) -> NativeCaptureResult;
    auto set_text_input(Option<TextInputState> state) -> NativeTextInputResult;
    auto instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> override;
    auto create_surface(VkInstance instance) -> Result<gpu::NativeSurface, gpu::Error> override;
    auto pixel_extent() const -> VkExtent2D override;
};
} // namespace vui::host
