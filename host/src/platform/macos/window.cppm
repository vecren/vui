module vui.host:platform.macos.window;
import :platform.input;
import :display;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;

struct MacosSnapshot {
    Vec<InputEvent> input;
    Serial revision{}, dropped{};
    Size logical;
    Index pixel_width{}, pixel_height{};
    Scalar scale{1};
    bool redraw{}, close_requested{}, minimized{}, suspended{};
};

class MacosWindow {
    struct Impl;
    mutable Box<Impl> impl_;

  public:
    MacosWindow();
    ~MacosWindow();
    MacosWindow(const MacosWindow &) = delete;
    auto operator=(const MacosWindow &) -> MacosWindow & = delete;
    static auto on_main_thread() -> bool;
    static auto post_empty_event() -> bool;
    static void drain_retired();
    auto open(const char *title, Index width, Index height) -> bool;
    void deactivate();
    auto alive() const -> bool;
    auto pump(Index milliseconds) -> bool;
    auto synchronize() -> bool;
    auto snapshot() -> MacosSnapshot;
    auto view() const -> MacosSnapshot;
    void tracing(bool enabled);
    auto metal_layer() const -> const void *;
    auto resize(Index width, Index height) -> bool;
    auto set_minimum_size(Index width, Index height) -> bool;
    auto minimum_size() const -> Size;
    auto position() const -> Option<Point>;
    auto set_position(Point position) -> bool;
    auto fullscreen() const -> bool;
    auto set_fullscreen(bool enabled) -> bool;
    auto minimize() -> bool;
    auto restore() -> bool;
    auto displays() const -> DisplaySnapshot;
    auto set_text_input(Option<TextInputState> state) -> NativeTextInputResult;
    auto request_clipboard(Serial id) -> Result<empty, ClipboardError>;
    auto request_clipboard_write(Serial id, Serial activation, ref<str> text) -> Result<empty, ClipboardError>;
    auto write_clipboard(ref<str> text) -> Result<empty, ClipboardError>;
};
} // namespace vui::host::detail
