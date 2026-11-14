module vui.host:platform.x11_clipboard;
import :ffi.x11;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;
namespace x11 = ffi::x11;

class X11Clipboard {
    static constexpr Index limit = 1'048'576;
    static constexpr Serial timeout_ns = 5'000'000'000;
    struct Request {
        Serial id{}, deadline{};
        x11::Window window{};
        x11::Atom target{};
        bool incremental{}, overflow{};
        Vec<u8> bytes;
    };
    x11::Display *display_{};
    x11::Window root_{};
    x11::Atom clipboard_{}, utf8_{}, property_{}, incr_{};
    Vec<Box<Request>> requests_;
    void *sink_{};
    void (*emit_)(void *, InputEvent){};

    static auto now() -> Serial {
        auto value = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
        return value > ~Serial{} ? ~Serial{} : Serial(value);
    }
    void finish(usize index, Option<ClipboardError> error = None());
    void convert(Request &request) {
        x11::XConvertSelection(display_, clipboard_, request.target, property_, request.window, x11::current_time);
        x11::XFlush(display_);
    }
    void property(usize index, bool initial);
  public:
    X11Clipboard(void *sink, void (*emit)(void *, InputEvent)) : sink_(sink), emit_(emit) {}
    X11Clipboard(const X11Clipboard &) = delete;
    auto operator=(const X11Clipboard &) -> X11Clipboard & = delete;
    void open(x11::Display *display, x11::Window root);
    auto request(Serial id) -> Result<empty, ClipboardError>;
    auto event(const x11::XEvent &event) -> bool;
    void expire();
    auto wait(Index milliseconds) const -> Index;
    auto pending() const -> usize { return requests_.len(); }
    void close() {
        if (display_) for (const auto &request : requests_) x11::XDestroyWindow(display_, request->window);
        requests_.clear(); display_ = nullptr; root_ = 0;
    }
};
}
