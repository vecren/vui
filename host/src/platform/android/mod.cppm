module;
#include <android/native_window.h>
#include <jni.h>

export module vui.host:platform.android;
import :runner;
import :platform.input;
import vui;
import rstd;

export namespace vui::host {
enum class AndroidSystemBarIcons { Light, Dark };

struct AndroidInsets {
    Scalar left{}, top{}, right{}, bottom{};
};
struct AndroidMetrics {
    SurfaceInfo surface;
    AndroidInsets safe_area, keyboard;
    Scalar font_scale{1}, refresh_hz{60};
};
} // namespace vui::host

namespace vui::host::detail {
using namespace rstd::prelude;

class AndroidHost final : public gpu::SurfaceHost {
    struct Impl;
    Box<Impl> data_;

  public:
    AndroidHost(JNIEnv *, jobject view);
    ~AndroidHost();
    AndroidHost(const AndroidHost &) = delete;
    auto operator=(const AndroidHost &) -> AndroidHost & = delete;
    auto valid() const -> bool;
    auto attach(JNIEnv *, jobject surface) -> bool;
    void detach();
    void resize(Index width, Index height);
    void metrics(Scalar density, Scalar font_scale, Scalar refresh, AndroidInsets safe, AndroidInsets ime);
    auto metrics() const -> AndroidMetrics;
    void active(bool value);
    void focus(bool value);
    void window_focus(bool value);
    void redraw();
    void redraw_async(JNIEnv *, jobject runnable);
    void presented(Serial generation);
    void complete_redraws();
    void finish();
    void set_system_bar_icons(AndroidSystemBarIcons status, AndroidSystemBarIcons navigation);
    auto attached() const -> bool;
    auto suspended() const -> bool;
    auto events() -> HostEvents;
    auto waker() const -> FrameWaker;
    void tracing(bool enabled);
    void pointer(int action, int pointer, int tool, int device, int buttons, int modifiers, Serial time, Scalar x,
                 Scalar y, Scalar pressure, slice<Serial> history_times, slice<float> history);
    void wheel(int device, Point position, Point delta, Serial time, int modifiers);
    auto key(int code, bool down, bool repeat, int modifiers, int device, Serial time) -> bool;
    void editing(JNIEnv *, Serial session, Serial revision, jstring text, int anchor, int cursor, int composing_start,
                 int composing_end);
    auto text_input(Option<TextInputState> state) -> NativeTextInputResult;
    auto read_clipboard() -> Result<String, ClipboardError>;
    auto accessibility_nodes(JNIEnv *, const SemanticsSnapshot &) const -> jobjectArray;
    auto write_clipboard(ref<str>) -> Result<empty, ClipboardError>;
    auto request_clipboard(Serial) -> Result<empty, ClipboardError>;
    auto request_clipboard_write(Serial, Serial activation, ref<str>) -> Result<empty, ClipboardError>;
    auto instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> override;
    auto create_surface(const vvk::Instance &) -> Result<gpu::NativeSurface, gpu::Error> override;
    auto pixel_extent() const -> VkExtent2D override;
};
} // namespace vui::host::detail
