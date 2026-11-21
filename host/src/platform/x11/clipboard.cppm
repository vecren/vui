module vui.host:platform.x11.clipboard;
import :platform.x11.ffi.xlib;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

class X11Clipboard {
    static constexpr Index limit = 1'048'576;
    static constexpr Serial timeout_ns = 5'000'000'000;
    struct Request {
        Serial id{}, deadline{};
        ffi::Window window{};
        ffi::Atom target{};
        bool incremental{}, overflow{};
        Vec<u8> bytes;
    };
    const ffi::XlibDispatch *xlib_{};
    ffi::Display *display_{};
    ffi::Window root_{};
    ffi::Atom clipboard_{}, utf8_{}, property_{}, incr_{};
    Vec<Box<Request>> requests_;
    void *sink_{};
    void (*emit_)(void *, InputEvent){};

    static auto now() -> Serial {
        auto value = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
        return value > ~Serial{} ? ~Serial{} : Serial(value);
    }
    void finish(usize index, Option<ClipboardError> error = None());
    void convert(Request &request) {
        xlib_->XConvertSelection(display_, clipboard_, request.target, property_, request.window, ffi::CurrentTime);
        xlib_->XFlush(display_);
    }
    void property(usize index, bool initial);
  public:
    X11Clipboard(void *sink, void (*emit)(void *, InputEvent)) : sink_(sink), emit_(emit) {}
    X11Clipboard(const X11Clipboard &) = delete;
    auto operator=(const X11Clipboard &) -> X11Clipboard & = delete;
    void open(const ffi::XlibDispatch &api, ffi::Display *display, ffi::Window root);
    auto request(Serial id) -> Result<empty, ClipboardError>;
    auto event(const ffi::XEvent &event) -> bool;
    void expire();
    auto wait(Index milliseconds) const -> Index;
    auto pending() const -> usize { return requests_.len(); }
    void close() {
        if (display_) for (const auto &request : requests_) xlib_->XDestroyWindow(display_, request->window);
        requests_.clear(); display_ = nullptr; root_ = 0; xlib_ = nullptr;
    }
};
}
