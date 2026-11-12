export module vui.host:window;
import vui.render;
import :display;
using namespace rstd::prelude;

export namespace vui::host {
enum class WindowBackend { Automatic, Wayland, X11 };
enum class WindowPositionResult { Requested, Unsupported, Failed };
enum class WindowMode { Windowed, Fullscreen };
enum class WindowModeError { Unavailable, Unsupported, InvalidValue, Failed };
enum class WindowActionError { Unavailable, Unsupported, Failed };
enum class FrameSignalError { Unavailable, Unsupported, Busy, Failed };
struct WindowEvents {
    Vec<InputEvent> input;
    SurfaceInfo surface;
    bool redraw{}, close_requested{};
    Serial dropped_input{};
    Option<bool> minimized, suspended;
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
    auto timing() const -> FrameTiming;
    auto frame_waker() const -> Option<FrameWaker>;
    auto wake_stats() const -> WakeStats;
    // Wayland signals after the next surface commit; false is pending, true is ready.
    auto request_frame_signal() -> Result<Serial,FrameSignalError>;
    auto frame_signal(Serial token) const -> Option<bool>;
    auto cancel_frame_signal(Serial token) -> bool;
    // Stamps are taken on native queue insertion; toggling does not change queued events.
    void set_input_tracing(bool enabled);
    auto capture(PointerKind kind, bool enabled) -> NativeCaptureResult;
    auto set_text_input(Option<TextInputState> state) -> NativeTextInputResult;
    auto request_clipboard(Serial id) -> Result<empty, ClipboardError>;
    auto request_clipboard_write(Serial id, Serial activation, ref<str> text) -> Result<empty, ClipboardError>;
    auto write_clipboard(ref<str> text) -> Result<empty, ClipboardError>;
    auto resize(Index width, Index height) -> Result<empty, gpu::Error>;
    auto logical_size() const -> Option<Size>;
    auto set_minimum_size(Index width, Index height) -> Result<empty, gpu::Error>;
    auto minimum_size() const -> Option<Size>;
    auto position() const -> Option<Point>;
    auto set_position(Point position) -> WindowPositionResult;
    auto mode() const -> Option<WindowMode>;
    auto set_mode(WindowMode mode) -> Result<empty, WindowModeError>;
    auto minimized() const -> Option<bool>;
    auto suspended() const -> Option<bool>;
    auto minimize() -> Result<empty, WindowActionError>;
    auto restore() -> Result<empty, WindowActionError>;
    auto displays() const -> DisplaySnapshot;
    auto instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> override;
    auto create_surface(VkInstance instance) -> Result<gpu::NativeSurface, gpu::Error> override;
    auto pixel_extent() const -> VkExtent2D override;
};
} // namespace vui::host
