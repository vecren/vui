module;
#include "native.hpp"

module vui.host;
import :platform.macos.window;

namespace vui::host::detail {
using namespace rstd::prelude;

namespace {
auto now_ns() -> Serial {
    auto value = rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive();
    return value > ~Serial{} ? ~Serial{} : Serial(value);
}
auto key(unsigned code) -> Key {
    switch (code) {
    case 0:
        return Key::A;
    case 1:
        return Key::S;
    case 2:
        return Key::D;
    case 3:
        return Key::F;
    case 4:
        return Key::H;
    case 5:
        return Key::G;
    case 6:
        return Key::Z;
    case 7:
        return Key::X;
    case 8:
        return Key::C;
    case 9:
        return Key::V;
    case 11:
        return Key::B;
    case 12:
        return Key::Q;
    case 13:
        return Key::W;
    case 14:
        return Key::E;
    case 15:
        return Key::R;
    case 16:
        return Key::Y;
    case 17:
        return Key::T;
    case 31:
        return Key::O;
    case 32:
        return Key::U;
    case 34:
        return Key::I;
    case 35:
        return Key::P;
    case 37:
        return Key::L;
    case 38:
        return Key::J;
    case 40:
        return Key::K;
    case 45:
        return Key::N;
    case 46:
        return Key::M;
    case 36:
    case 76:
        return Key::Enter;
    case 48:
        return Key::Tab;
    case 49:
        return Key::Space;
    case 51:
        return Key::Backspace;
    case 53:
        return Key::Escape;
    case 115:
        return Key::Home;
    case 116:
        return Key::PageUp;
    case 117:
        return Key::Delete;
    case 119:
        return Key::End;
    case 121:
        return Key::PageDown;
    case 123:
        return Key::Left;
    case 124:
        return Key::Right;
    case 125:
        return Key::Down;
    case 126:
        return Key::Up;
    default:
        return Key::Unknown;
    }
}
auto button(int value) -> PointerButton {
    switch (value) {
    case 0:
        return PointerButton::Primary;
    case 1:
        return PointerButton::Secondary;
    case 2:
        return PointerButton::Middle;
    case 3:
        return PointerButton::Back;
    case 4:
        return PointerButton::Forward;
    default:
        return PointerButton::None;
    }
}
auto text(ffi::MacosBytes value) -> Result<TextInput, InputError> {
    if (value.size > ~Index{} || (!value.data && value.size))
        return Err(InputError::InvalidEvent);
    auto decoded = rstd::str_::from_utf8(slice<u8>::from_raw_parts(
        reinterpret_cast<const rstd::byte *>(value.data ? value.data : ""), usize(value.size)));
    if (decoded.is_err())
        return Err(InputError::InvalidEvent);
    return TextInput::create(decoded.unwrap());
}
auto bytes(ref<str> text) -> ffi::MacosBytes {
    return {reinterpret_cast<const char *>(text->as_bytes().as_raw_ptr()), text->len().to_primitive()};
}
} // namespace

struct MacosWindow::Impl {
    struct Cache {
        Size logical, minimum;
        Point position;
        Index pixel_width{}, pixel_height{};
        Scalar scale{1};
        Serial revision{}, dropped{};
        bool ready{}, active{}, redraw{}, close_requested{}, minimized{}, suspended{}, fullscreen{}, tracing{};
    };
    ffi::MacosWindow *native{};
    mutable rstd::sync::Mutex<Cache> cache;
    InputQueue input;
    InputTraceSource trace;
    Serial native_clock{new_session()}, received_clock{new_session()}, display_revision{};
    Vec<DisplayInfo> previous_displays;
    Option<TextInputState> text_state;

    auto snapshot(bool consume) -> MacosSnapshot {
        MacosSnapshot result;
        {
            auto state = cache.lock().unwrap();
            result.logical = state->logical;
            result.scale = state->scale;
            result.pixel_width = state->pixel_width;
            result.pixel_height = state->pixel_height;
            result.revision = state->revision;
            result.dropped = state->dropped;
            result.redraw = state->redraw;
            result.close_requested = state->close_requested;
            result.minimized = state->minimized;
            result.suspended = state->suspended;
            if (consume) {
                state->redraw = false;
                state->dropped = 0;
            }
        }
        if (consume)
            result.input = input.take();
        return result;
    }
    void emit(InputEvent event, double timestamp = -1) {
        auto state = cache.lock().unwrap();
        if (!state->active)
            return;
        event.sample_time = finite(timestamp) && timestamp >= 0 && timestamp * 1e9 < Scalar(~Serial{})
                                ? SampleTime{native_clock, Serial(timestamp * 1e9), 1'000'000, TimeOrigin::Native}
                                : SampleTime{received_clock, now_ns(), 1, TimeOrigin::Received};
        event.coordinate_generation = state->revision + 1;
        auto lost = input.push(rstd::move(event), usize(), [&](InputEvent &queued) {
            if (state->tracing)
                queued.stamp = trace.stamp();
        });
        state->dropped += rstd::min(lost, ~Serial{} - state->dropped);
        state->redraw = true;
    }
    static void geometry(void *context, const ffi::MacosGeometry &value) {
        auto &self = *static_cast<Impl *>(context);
        auto state = self.cache.lock().unwrap();
        Size logical{value.width, value.height};
        Point position{value.x, value.y};
        if (!state->ready || state->logical != logical || state->position != position || state->scale != value.scale ||
            state->pixel_width != value.pixel_width || state->pixel_height != value.pixel_height ||
            state->minimized != value.minimized || state->suspended != value.suspended ||
            state->fullscreen != value.fullscreen) {
            state->logical = logical;
            state->position = position;
            state->scale = value.scale;
            state->pixel_width = value.pixel_width;
            state->pixel_height = value.pixel_height;
            state->minimized = value.minimized;
            state->suspended = value.suspended;
            state->fullscreen = value.fullscreen;
            ++state->revision;
            if (state->active)
                state->redraw = true;
        }
        state->ready = true;
    }
    static void event(void *context, const ffi::MacosEvent &value) {
        auto &self = *static_cast<Impl *>(context);
        using Kind = ffi::MacosEventKind;
        if (value.kind == Kind::Close || value.kind == Kind::Redraw) {
            auto state = self.cache.lock().unwrap();
            if (state->active) {
                state->redraw = true;
                state->close_requested |= value.kind == Kind::Close;
            }
            return;
        }
        InputEvent result;
        switch (value.kind) {
        case Kind::Move:
            result.type = EventType::PointerMove;
            break;
        case Kind::Down:
            result.type = EventType::PointerDown;
            break;
        case Kind::Up:
            result.type = EventType::PointerUp;
            break;
        case Kind::Enter:
            result.type = EventType::WindowPointerEnter;
            break;
        case Kind::Leave:
            result.type = EventType::WindowPointerLeave;
            break;
        case Kind::Wheel:
            result.type = EventType::Wheel;
            break;
        case Kind::KeyDown:
            result.type = EventType::KeyDown;
            break;
        case Kind::KeyUp:
            result.type = EventType::KeyUp;
            break;
        case Kind::FocusGained:
            result.type = EventType::WindowFocusGained;
            break;
        case Kind::FocusLost:
            result.type = EventType::WindowFocusLost;
            break;
        default:
            return;
        }
        result.position = {value.x, value.y};
        if (value.kind == Kind::Down || value.kind == Kind::Up)
            result.button = button(value.button);
        if (value.kind == Kind::KeyDown || value.kind == Kind::KeyUp)
            result.key = key(value.keycode);
        result.modifiers = {bool(value.modifiers & 1), bool(value.modifiers & 2), bool(value.modifiers & 4),
                            bool(value.modifiers & 8)};
        result.repeat = value.repeat;
        result.scroll_delta = {value.delta_x, value.delta_y};
        result.scroll_unit = value.precise ? ScrollUnit::Pixels : ScrollUnit::Lines;
        switch (value.phase) {
        case ffi::MacosScrollPhase::Begin:
            result.scroll_phase = ScrollPhase::Begin;
            break;
        case ffi::MacosScrollPhase::Update:
            result.scroll_phase = ScrollPhase::Update;
            break;
        case ffi::MacosScrollPhase::End:
            result.scroll_phase = ScrollPhase::End;
            break;
        default:
            break;
        }
        if (value.momentum >= 0)
            result.momentum = Some(value.momentum != 0);
        self.emit(rstd::move(result), value.timestamp);
    }
    static void transaction(void *context, const ffi::MacosTextTransaction &value) {
        auto &self = *static_cast<Impl *>(context);
        TextInputTransaction result;
        if (value.has_commit) {
            auto commit = text(value.commit);
            if (commit.is_err())
                return;
            result.commit = commit.unwrap();
        }
        if (value.has_preedit) {
            auto preedit = text(value.preedit);
            if (preedit.is_err())
                return;
            result.preedit = preedit.unwrap();
        }
        if (value.has_cursor)
            result.preedit_cursor =
                Some(TextPreeditCursor{value.anchor, value.cursor, value.cursor_visible, value.cursor_range});
        result.delete_before = value.delete_before;
        result.delete_after = value.delete_after;
        if (value.has_expected)
            result.expected_surrounding =
                Some(TextSurroundingRevision{value.expected_text, value.expected_anchor, value.expected_cursor});
        auto transaction = TextTransaction::create(rstd::move(result));
        if (transaction.is_err())
            return;
        InputEvent event;
        event.type = EventType::TextTransaction;
        event.text_session = value.session;
        event.transaction = transaction.unwrap();
        self.emit(rstd::move(event));
    }
};

MacosWindow::MacosWindow() : impl_(Box<Impl>::make()) {}
MacosWindow::~MacosWindow() {
    auto *owner = rstd::move(impl_).into_raw().as_raw_ptr();
    ffi::macos_destroy(owner->native, owner, [](void *context) {
        (void)Box<Impl>::from_raw(mut_ptr<Impl>::from_raw_parts(static_cast<Impl *>(context)));
    });
}
auto MacosWindow::on_main_thread() -> bool { return ffi::macos_on_main_thread(); }
auto MacosWindow::post_empty_event() -> bool { return ffi::macos_post_empty_event(); }
void MacosWindow::drain_retired() { ffi::macos_drain_retired(); }
auto MacosWindow::open(const char *title, Index width, Index height) -> bool {
    if (!on_main_thread() || impl_->native || !title || !width || !height || width > 32768 || height > 32768)
        return false;
    {
        auto state = impl_->cache.lock().unwrap();
        state->active = true;
    }
    impl_.get()->native =
        ffi::macos_open(title, width, height, impl_.get(), {Impl::event, Impl::geometry, Impl::transaction});
    if (!impl_->native) {
        auto state = impl_->cache.lock().unwrap();
        state->active = false;
        state->ready = false;
        impl_.get()->input.clear();
        return false;
    }
    return alive();
}
void MacosWindow::deactivate() {
    {
        auto state = impl_->cache.lock().unwrap();
        state->active = false;
    }
    ffi::macos_deactivate(impl_->native);
    if (on_main_thread()) {
        impl_.get()->text_state = None();
        impl_.get()->input.clear();
    }
}
auto MacosWindow::alive() const -> bool {
    auto state = impl_->cache.lock().unwrap();
    return state->ready;
}
auto MacosWindow::pump(Index milliseconds) -> bool { return ffi::macos_pump(impl_->native, milliseconds); }
auto MacosWindow::synchronize() -> bool { return ffi::macos_synchronize(impl_->native); }
auto MacosWindow::snapshot() -> MacosSnapshot { return impl_.get()->snapshot(true); }
auto MacosWindow::view() const -> MacosSnapshot { return impl_.get()->snapshot(false); }
void MacosWindow::tracing(bool enabled) {
    auto state = impl_->cache.lock().unwrap();
    state->tracing = enabled;
}
auto MacosWindow::metal_layer() const -> const void * { return ffi::macos_metal_layer(impl_->native); }
auto MacosWindow::resize(Index width, Index height) -> bool { return ffi::macos_resize(impl_->native, width, height); }
auto MacosWindow::set_minimum_size(Index width, Index height) -> bool {
    if (!ffi::macos_set_minimum_size(impl_->native, width, height))
        return false;
    auto state = impl_->cache.lock().unwrap();
    state->minimum = {Scalar(width), Scalar(height)};
    return true;
}
auto MacosWindow::minimum_size() const -> Size {
    auto state = impl_->cache.lock().unwrap();
    return state->minimum;
}
auto MacosWindow::position() const -> Option<Point> {
    auto state = impl_->cache.lock().unwrap();
    return state->ready ? Some(state->position) : None<Point>();
}
auto MacosWindow::set_position(Point position) -> bool {
    return ffi::macos_set_position(impl_->native, position.x, position.y);
}
auto MacosWindow::fullscreen() const -> bool {
    auto state = impl_->cache.lock().unwrap();
    return state->fullscreen;
}
auto MacosWindow::set_fullscreen(bool enabled) -> bool { return ffi::macos_set_fullscreen(impl_->native, enabled); }
auto MacosWindow::minimize() -> bool { return ffi::macos_minimize(impl_->native); }
auto MacosWindow::restore() -> bool { return ffi::macos_restore(impl_->native); }
auto MacosWindow::displays() const -> DisplaySnapshot {
    if (!on_main_thread() || !impl_->native)
        return {};
    DisplaySnapshot result;
    if (!ffi::macos_displays(impl_->native, &result, [](void *context, const ffi::MacosDisplay &value) {
            DisplayInfo info;
            info.id = value.id;
            info.bounds = Some(Rect{value.x, value.y, value.width, value.height});
            info.usable_bounds = Some(Rect{value.usable_x, value.usable_y, value.usable_width, value.usable_height});
            info.pixel_size = Some(PixelSize{value.pixel_width, value.pixel_height});
            if (finite(value.refresh_hz) && value.refresh_hz > 0)
                info.refresh_hz = Some(Scalar(value.refresh_hz));
            static_cast<DisplaySnapshot *>(context)->outputs.push(rstd::move(info));
        }))
        return {};
    bool same = impl_->previous_displays.len() == result.outputs.len();
    if (same)
        for (usize i{}; i < result.outputs.len(); ++i)
            same &= impl_->previous_displays[i] == result.outputs[i];
    if (!same || !impl_->display_revision) {
        ++impl_.get()->display_revision;
        impl_.get()->previous_displays = Vec<DisplayInfo>::from(result.outputs.as_slice());
    }
    result.status = result.outputs.is_empty() ? DisplayStatus::Unavailable : DisplayStatus::Available;
    result.revision = impl_->display_revision;
    return result;
}
auto MacosWindow::set_text_input(Option<TextInputState> state) -> NativeTextInputResult {
    if (!on_main_thread() || !alive() || (state.is_some() && !state->valid()))
        return NativeTextInputResult::Failed;
    ffi::MacosTextState native;
    native.reset =
        state.is_none() || impl_->text_state.is_none() ||
        state->request.session != impl_->text_state->request.session ||
        (state->change_cause == TextChangeCause::Other && state->surrounding != impl_->text_state->surrounding);
    if (state.is_some()) {
        native.enabled = true;
        native.session = state->request.session;
        auto area = state->request.area;
        native.x = area.x;
        native.y = area.y;
        native.width = area.width;
        native.height = area.height;
        native.surrounding_present = state->surrounding.present();
        if (native.surrounding_present) {
            native.surrounding = bytes(state->surrounding.text.text());
            native.text = state->surrounding.text.identity();
            native.anchor = state->surrounding.anchor;
            native.cursor = state->surrounding.cursor;
        }
    }
    if (!ffi::macos_set_text_input(impl_->native, native))
        return NativeTextInputResult::Failed;
    impl_.get()->text_state = rstd::move(state);
    return NativeTextInputResult::Succeeded;
}
auto MacosWindow::request_clipboard(Serial) -> Result<empty, ClipboardError> {
    return Err(alive() ? ClipboardError::Unsupported : ClipboardError::Unavailable);
}
auto MacosWindow::request_clipboard_write(Serial, Serial, ref<str>) -> Result<empty, ClipboardError> {
    return Err(alive() ? ClipboardError::Unsupported : ClipboardError::Unavailable);
}
auto MacosWindow::write_clipboard(ref<str> text) -> Result<empty, ClipboardError> {
    if (!on_main_thread() || !alive())
        return Err(ClipboardError::Unavailable);
    return ffi::macos_write_clipboard(impl_->native, bytes(text)) ? Result<empty, ClipboardError>(Ok(empty{}))
                                                                  : Err(ClipboardError::Failed);
}
} // namespace vui::host::detail
