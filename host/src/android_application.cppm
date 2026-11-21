export module vui.host:android_application;
export import :platform.android;
import :frames;
import :runner;

namespace vui::host::detail {
struct AndroidBridge;
}

export namespace vui::host {
using namespace rstd::prelude;

class AndroidApplication {
    friend struct detail::AndroidBridge;
    struct Impl;
    Box<Impl> data_;

  public:
    AndroidApplication();
    virtual ~AndroidApplication();
    AndroidApplication(const AndroidApplication &) = delete;
    auto operator=(const AndroidApplication &) -> AndroidApplication & = delete;
    auto session() -> UiSession &;
    void request_redraw();
    void request_close();
    auto request_capture() -> Result<Serial, FrameRequestError>;
    auto snapshot() -> RenderSnapshot;
    auto metrics() const -> AndroidMetrics;
    void set_system_bar_icons(AndroidSystemBarIcons status, AndroidSystemBarIcons navigation);
    void set_input_tracing(bool enabled);
    virtual auto configure(UiSession &) -> Result<empty, UiError> = 0;
    virtual auto before_advance(const HostEvents &) -> Result<empty, UiError>;
    virtual auto after_advance(const AdvanceResult &) -> Result<empty, UiError>;
    virtual auto on_frame_event(const FrameEvent &) -> Result<empty, UiError>;
    virtual auto on_close_requested() -> bool { return true; }
    virtual auto on_back_requested() -> bool { return false; }
    virtual auto can_handle_back() const -> bool { return false; }
    virtual void on_metrics_changed(const AndroidMetrics &) {}
};
} // namespace vui::host
