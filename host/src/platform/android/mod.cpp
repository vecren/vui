module;
#include <android/input.h>
#include <android/native_window_jni.h>
#include <jni.h>

module vui.host;
import :platform.android;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

namespace {
class Environment {
    JavaVM *vm_{};
    bool attached_{};

  public:
    JNIEnv *env{};
    explicit Environment(JavaVM *vm) : vm_(vm) {
        if (vm_->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
            attached_ = vm_->AttachCurrentThread(&env, nullptr) == JNI_OK;
    }
    ~Environment() {
        if (attached_)
            vm_->DetachCurrentThread();
    }
};
struct ViewState {
    JavaVM *vm{};
    jobject view{};
    jmethodID schedule{}, update_text{}, revision{}, read{}, write{}, finish{}, system_bar_icons{};
    mutable rstd::sync::Mutex<empty> mutex;
    mutable bool alive{true};
    ViewState(JNIEnv *env, jobject value) {
        env->GetJavaVM(&vm);
        view = env->NewGlobalRef(value);
        auto type = env->GetObjectClass(value);
        schedule = env->GetMethodID(type, "schedulePump", "()V");
        update_text = env->GetMethodID(type, "updateText", "(JJLjava/lang/String;II)V");
        revision = env->GetMethodID(type, "updateTextRevision", "(J)V");
        read = env->GetMethodID(type, "readClipboard", "()Ljava/lang/String;");
        write = env->GetMethodID(type, "writeClipboard", "(Ljava/lang/String;)V");
        finish = env->GetMethodID(type, "finishHost", "()V");
        system_bar_icons = env->GetMethodID(type, "setSystemBarIcons", "(ZZ)V");
        env->DeleteLocalRef(type);
    }
    ~ViewState() {
        Environment current(vm);
        if (current.env && view)
            current.env->DeleteGlobalRef(view);
    }
    auto wake() const -> FrameWakeResult {
        auto lock = mutex.lock().unwrap();
        if (!alive)
            return FrameWakeResult::Closed;
        thread_local Environment current(vm);
        if (!current.env)
            return FrameWakeResult::Failed;
        current.env->CallVoidMethod(view, schedule);
        if (!current.env->ExceptionCheck())
            return FrameWakeResult::Queued;
        current.env->ExceptionClear();
        return FrameWakeResult::Failed;
    }
};
struct NativeWindow {
    ANativeWindow *window;
    explicit NativeWindow(ANativeWindow *value) : window(value) {}
    ~NativeWindow() {
        if (window)
            ANativeWindow_release(window);
    }
};
struct NativeLease {
    rstd::sync::Arc<NativeWindow> window;
    explicit NativeLease(rstd::sync::Arc<NativeWindow> value) : window(rstd::move(value)) {}
};
auto modifiers(int value) -> Modifiers {
    return {bool(value & AMETA_SHIFT_ON), bool(value & AMETA_CTRL_ON), bool(value & AMETA_ALT_ON),
            bool(value & AMETA_META_ON)};
}
struct DecodedText {
    String text;
    Vec<Index> offsets;
    auto offset(int units) const -> Option<Index> {
        if (units < 0 || usize(units) >= offsets.len() || offsets[usize(units)] == ~Index{})
            return None();
        return Some(Index(offsets[usize(units)]));
    }
};
auto decode(JNIEnv *env, jstring value) -> Option<DecodedText> {
    if (!value)
        return None();
    auto length = env->GetStringLength(value);
    auto units = env->GetStringChars(value, nullptr);
    if (!units)
        return None();
    String result;
    Vec<Index> offsets;
    offsets.resize(usize(length) + usize(1), ~Index{});
    offsets[usize()] = 0;
    bool valid{true};
    for (int i{}; i < length; ++i) {
        rstd::uint32_t cp = units[i];
        if (cp >= 0xd800 && cp <= 0xdbff) {
            if (++i == length || units[i] < 0xdc00 || units[i] > 0xdfff) {
                valid = false;
                break;
            }
            cp = 0x10000 + ((cp - 0xd800) << 10) + (units[i] - 0xdc00);
        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            valid = false;
            break;
        }
        result.push(rstd::Char::from_u32_unchecked(rstd::u32(cp)));
        if (result.len() > usize(~Index{})) {
            valid = false;
            break;
        }
        offsets[usize(i + 1)] = Index(result.len().to_primitive());
    }
    env->ReleaseStringChars(value, units);
    return valid ? Some(DecodedText{rstd::move(result), rstd::move(offsets)}) : None<DecodedText>();
}
auto encode(JNIEnv *env, ref<str> value) -> jstring {
    Vec<jchar> units;
    auto chars = value->chars();
    for (auto cp = chars.next(); cp.is_some(); cp = chars.next()) {
        auto scalar = cp->to_primitive();
        if (scalar < 0x10000)
            units.push(jchar(scalar));
        else {
            scalar -= 0x10000;
            units.push(jchar(0xd800 + (scalar >> 10)));
            units.push(jchar(0xdc00 + (scalar & 1023)));
        }
    }
    return env->NewString(units.as_slice().as_raw_ptr(), jsize(units.len().to_primitive()));
}
auto utf16_offset(ref<str> value, Index offset) -> int {
    int count{};
    auto chars = value->get(usize(), usize(offset)).unwrap()->chars();
    for (auto cp = chars.next(); cp.is_some(); cp = chars.next())
        count += cp->to_primitive() >= 0x10000 ? 2 : 1;
    return count;
}
} // namespace

struct AndroidHost::Impl {
    rstd::sync::Arc<ViewState> view;
    Option<rstd::sync::Arc<NativeWindow>> window;
    AndroidMetrics metrics;
    InputQueue queue;
    InputTraceSource trace;
    Vec<Vec<PointerSample>> history, delivered;
    struct Pointer {
        int device, id;
        SequenceId sequence;
        Serial generation;
        Point position;
        unsigned buttons{};
    };
    Vec<Pointer> pointers;
    Vec<jobject> redraws;
    Option<TextInputState> text;
    Serial dropped{}, generation{1}, surface_generation{}, sequence{new_session()}, clock{new_session()};
    Index width{1}, height{1};
    bool active{}, view_focused{}, window_focused{}, focused{}, displaying{}, redraw{true}, tracing{};
    explicit Impl(rstd::sync::Arc<ViewState> value) : view(rstd::move(value)) {}
    void push(InputEvent event) {
        event.coordinate_generation = generation;
        auto lost = queue.push(rstd::move(event), usize(), [&](InputEvent &queued) {
            if (tracing)
                queued.stamp = trace.stamp();
        });
        dropped += rstd::cmp::min(lost, ~Serial{} - dropped);
    }
    auto time(Serial value) const -> SampleTime { return {clock, value, 1'000'000, TimeOrigin::Native}; }
    void synchronize_focus() {
        bool effective = active && view_focused && window_focused && window.is_some();
        if (effective == focused)
            return;
        focused = effective;
        InputEvent event;
        event.type = focused ? EventType::WindowFocusGained : EventType::WindowFocusLost;
        push(rstd::move(event));
        if (!focused)
            pointers.clear();
    }
    void update() {
        auto scale = metrics.surface.scale;
        metrics.surface.logical_size = {Scalar(width) / scale, Scalar(height) / scale};
        metrics.surface.pixel_size = Some(PixelSize{width, height});
        metrics.surface.generation = generation;
        redraw = true;
    }
};

AndroidHost::AndroidHost(JNIEnv *env, jobject view)
    : data_(Box<Impl>::make(rstd::sync::Arc<ViewState>::make(env, view))) {
    data_->update();
    InputEvent lost;
    lost.type = EventType::WindowFocusLost;
    data_->push(rstd::move(lost));
}
AndroidHost::~AndroidHost() {
    complete_redraws();
    auto lock = data_->view->mutex.lock().unwrap();
    data_->view->alive = false;
}
auto AndroidHost::valid() const -> bool {
    return data_->view->view && data_->view->schedule && data_->view->update_text && data_->view->revision &&
           data_->view->system_bar_icons;
}
auto AndroidHost::attach(JNIEnv *env, jobject surface) -> bool {
    if (data_->window.is_some() || !surface)
        return false;
    auto window = ANativeWindow_fromSurface(env, surface);
    if (!window)
        return false;
    data_->window = Some(rstd::sync::Arc<NativeWindow>::make(window));
    data_->width = Index(rstd::cmp::max(1, ANativeWindow_getWidth(window)));
    data_->height = Index(rstd::cmp::max(1, ANativeWindow_getHeight(window)));
    ++data_->generation;
    data_->surface_generation = data_->generation;
    data_->displaying = false;
    data_->update();
    data_->synchronize_focus();
    return true;
}
void AndroidHost::detach() {
    data_->surface_generation = 0;
    data_->displaying = false;
    data_->window = None();
    ++data_->generation;
    data_->update();
    data_->synchronize_focus();
    complete_redraws();
}
void AndroidHost::resize(Index width, Index height) {
    if (!width || !height || (data_->width == width && data_->height == height))
        return;
    data_->width = width;
    data_->height = height;
    ++data_->generation;
    data_->update();
}
void AndroidHost::metrics(Scalar density, Scalar font_scale, Scalar refresh, AndroidInsets safe, AndroidInsets ime) {
    if (!finite(density) || density <= 0 || !finite(font_scale) || font_scale <= 0 || !finite(refresh) || refresh <= 0)
        return;
    if (data_->metrics.surface.scale != density)
        ++data_->generation;
    data_->metrics.surface.scale = density;
    data_->metrics.font_scale = font_scale;
    data_->metrics.refresh_hz = refresh;
    data_->metrics.safe_area = {safe.left / density, safe.top / density, safe.right / density, safe.bottom / density};
    data_->metrics.keyboard = {ime.left / density, ime.top / density, ime.right / density, ime.bottom / density};
    data_->update();
}
auto AndroidHost::metrics() const -> AndroidMetrics { return data_->metrics; }
void AndroidHost::active(bool value) {
    data_->active = value;
    data_->synchronize_focus();
    data_->redraw = true;
    if (!value)
        complete_redraws();
}
void AndroidHost::focus(bool value) {
    data_->view_focused = value;
    data_->synchronize_focus();
}
void AndroidHost::window_focus(bool value) {
    data_->window_focused = value;
    data_->synchronize_focus();
}
void AndroidHost::redraw() { data_->redraw = true; }
void AndroidHost::redraw_async(JNIEnv *env, jobject runnable) {
    data_->redraws.push(env->NewGlobalRef(runnable));
    redraw();
    if (data_->displaying || suspended())
        complete_redraws();
}
void AndroidHost::presented(Serial generation) {
    if (!attached() || generation < data_->surface_generation)
        return;
    data_->displaying = true;
    complete_redraws();
}
void AndroidHost::complete_redraws() {
    Environment current(data_->view->vm);
    if (!current.env)
        return;
    auto callbacks = rstd::exchange(data_->redraws, Vec<jobject>{});
    for (auto runnable : callbacks) {
        auto type = current.env->GetObjectClass(runnable);
        auto run = current.env->GetMethodID(type, "run", "()V");
        if (run)
            current.env->CallVoidMethod(runnable, run);
        current.env->DeleteLocalRef(type);
        current.env->DeleteGlobalRef(runnable);
    }
}
void AndroidHost::finish() {
    Environment current(data_->view->vm);
    if (current.env)
        current.env->CallVoidMethod(data_->view->view, data_->view->finish);
}
void AndroidHost::set_system_bar_icons(AndroidSystemBarIcons status, AndroidSystemBarIcons navigation) {
    Environment current(data_->view->vm);
    if (current.env)
        current.env->CallVoidMethod(data_->view->view, data_->view->system_bar_icons,
            jboolean(status == AndroidSystemBarIcons::Dark), jboolean(navigation == AndroidSystemBarIcons::Dark));
}
auto AndroidHost::attached() const -> bool { return data_->window.is_some(); }
auto AndroidHost::suspended() const -> bool { return !attached() || !data_->active; }
auto AndroidHost::events() -> HostEvents {
    data_->delivered = rstd::exchange(data_->history, Vec<Vec<PointerSample>>{});
    return {data_->queue.take(),
            data_->metrics.surface,
            pixel_extent(),
            rstd::exchange(data_->redraw, false),
            false,
            rstd::exchange(data_->dropped, Serial{}),
            {},
            {},
            Some(!attached() || !data_->active)};
}
auto AndroidHost::waker() const -> FrameWaker {
    return FrameWaker::make([view = data_->view.clone()] { return view->wake(); });
}
void AndroidHost::tracing(bool enabled) { data_->tracing = enabled; }

void AndroidHost::pointer(int action, int pointer, int tool, int device, int buttons, int meta, Serial time, Scalar x,
                          Scalar y, Scalar pressure, slice<Serial> times, slice<float> points) {
    if (!data_->focused)
        return;
    InputEvent event;
    switch (action) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
    case AMOTION_EVENT_ACTION_BUTTON_PRESS:
        event.type = EventType::PointerDown;
        break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
    case AMOTION_EVENT_ACTION_BUTTON_RELEASE:
        event.type = EventType::PointerUp;
        break;
    case AMOTION_EVENT_ACTION_CANCEL:
        event.type = EventType::PointerCancel;
        event.cancel_reason = CancelReason::Native;
        break;
    case AMOTION_EVENT_ACTION_HOVER_ENTER:
        event.type = EventType::PointerEnter;
        break;
    case AMOTION_EVENT_ACTION_HOVER_EXIT:
        event.type = EventType::PointerLeave;
        break;
    default:
        event.type = EventType::PointerMove;
        break;
    }
    auto kind =
        tool == AMOTION_EVENT_TOOL_TYPE_MOUSE
            ? PointerKind::Mouse
            : (tool == AMOTION_EVENT_TOOL_TYPE_STYLUS || tool == AMOTION_EVENT_TOOL_TYPE_ERASER ? PointerKind::Pen
                                                                                                : PointerKind::Touch);
    auto scale = data_->metrics.surface.scale;
    event.button = kind != PointerKind::Mouse                 ? PointerButton::Primary
                   : buttons & AMOTION_EVENT_BUTTON_SECONDARY ? PointerButton::Secondary
                   : buttons & AMOTION_EVENT_BUTTON_TERTIARY  ? PointerButton::Middle
                   : buttons & AMOTION_EVENT_BUTTON_BACK      ? PointerButton::Back
                   : buttons & AMOTION_EVENT_BUTTON_FORWARD   ? PointerButton::Forward
                                                              : PointerButton::Primary;
    unsigned mask = 1u << (unsigned(event.button) - 1);
    Impl::Pointer *tracked{};
    for (auto &entry : data_->pointers)
        if (entry.device == device && entry.id == pointer)
            tracked = &entry;
    if (event.type == EventType::PointerDown) {
        if (tracked && (tracked->buttons & mask))
            return;
        if (!tracked) {
            data_->pointers.push({device, pointer, new_session(), data_->generation, {x / scale, y / scale}});
            tracked = &data_->pointers[data_->pointers.len() - usize(1)];
        }
        tracked->buttons |= mask;
    } else if (event.type == EventType::PointerUp) {
        if (!tracked || !(tracked->buttons & mask))
            return;
        tracked->buttons &= ~mask;
    }
    event.device = Serial(rstd::uint32_t(device)) + 1;
    event.pointer = Index(pointer);
    event.sequence = tracked ? tracked->sequence : 0;
    event.pointer_kind = kind;
    event.position = {x / scale, y / scale};
    event.modifiers = modifiers(meta);
    event.sample_time = data_->time(time);
    event.pressure = Some(rstd::cmp::max(Scalar(0), rstd::cmp::min(Scalar(1), pressure)));
    if (event.type != EventType::PointerDown && event.type != EventType::PointerUp)
        event.button = PointerButton::None;
    if (tracked) {
        if ((action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP) &&
            kind != PointerKind::Mouse && tracked->generation == data_->generation &&
            tracked->position != event.position) {
            // Android can report the last segment of a drag only at lift-off.
            auto move = event;
            move.type = EventType::PointerMove;
            move.button = PointerButton::None;
            data_->push(rstd::move(move));
        }
        tracked->position = event.position;
        tracked->generation = data_->generation;
    }
    if (times.len() <= usize(2048) && points.len() == times.len() * usize(3)) {
        Vec<PointerSample> history;
        for (usize i{}; i < times.len(); ++i) {
            history.push(
                {data_->time(times[i]),
                 {points[i * usize(3)] / scale, points[i * usize(3) + usize(1)] / scale},
                 Some(rstd::cmp::max(Scalar(0), rstd::cmp::min(Scalar(1), Scalar(points[i * usize(3) + usize(2)]))))});
        }
        if (!history.is_empty()) {
            data_->history.push(rstd::move(history));
            event.history = data_->history.last().unwrap()->as_slice();
        }
    }
    if ((event.type == EventType::PointerUp && tracked && !tracked->buttons) || event.type == EventType::PointerCancel)
        data_->pointers.retain(
            [&](const Impl::Pointer &entry) { return entry.device != device || entry.id != pointer; });
    data_->push(rstd::move(event));
}
void AndroidHost::wheel(int device, Point position, Point delta, Serial time, int meta) {
    if (!data_->focused)
        return;
    InputEvent event;
    event.type = EventType::Wheel;
    event.device = Serial(rstd::uint32_t(device)) + 1;
    auto scale = data_->metrics.surface.scale;
    event.position = {position.x / scale, position.y / scale};
    event.scroll_delta = {-delta.x, -delta.y};
    event.scroll_unit = ScrollUnit::Lines;
    event.modifiers = modifiers(meta);
    event.sample_time = data_->time(time);
    data_->push(rstd::move(event));
}
auto AndroidHost::key(int code, bool down, bool repeat, int meta, int device, Serial time) -> bool {
    if (!data_->focused)
        return false;
    Key key{Key::Unknown};
    if (code >= AKEYCODE_A && code <= AKEYCODE_Z)
        key = Key(unsigned(Key::A) + unsigned(code - AKEYCODE_A));
    else
        switch (code) {
        case AKEYCODE_TAB:
            key = Key::Tab;
            break;
        case AKEYCODE_ENTER:
            key = Key::Enter;
            break;
        case AKEYCODE_SPACE:
            key = Key::Space;
            break;
        case AKEYCODE_ESCAPE:
            key = Key::Escape;
            break;
        case AKEYCODE_DPAD_LEFT:
            key = Key::Left;
            break;
        case AKEYCODE_DPAD_RIGHT:
            key = Key::Right;
            break;
        case AKEYCODE_DPAD_UP:
            key = Key::Up;
            break;
        case AKEYCODE_DPAD_DOWN:
            key = Key::Down;
            break;
        case AKEYCODE_MOVE_HOME:
            key = Key::Home;
            break;
        case AKEYCODE_MOVE_END:
            key = Key::End;
            break;
        case AKEYCODE_PAGE_UP:
            key = Key::PageUp;
            break;
        case AKEYCODE_PAGE_DOWN:
            key = Key::PageDown;
            break;
        case AKEYCODE_DEL:
            key = Key::Backspace;
            break;
        case AKEYCODE_FORWARD_DEL:
            key = Key::Delete;
            break;
        default:
            return false;
        }
    InputEvent event;
    event.type = down ? EventType::KeyDown : EventType::KeyUp;
    event.key = key;
    event.repeat = repeat;
    event.modifiers = modifiers(meta);
    event.device = Serial(rstd::uint32_t(device)) + 1;
    event.sample_time = data_->time(time);
    data_->push(rstd::move(event));
    return true;
}

void AndroidHost::editing(JNIEnv *env, Serial session, Serial revision, jstring text, int anchor, int cursor, int start,
                          int end) {
    if (!data_->focused || data_->text.is_none() || session != data_->text->request.session ||
        !data_->text->surrounding.present() || revision != data_->text->surrounding.text.identity())
        return;
    auto decoded = decode(env, text);
    if (decoded.is_none())
        return;
    auto a = decoded->offset(anchor), c = decoded->offset(cursor);
    if (a.is_none() || c.is_none())
        return;
    TextInputEditing editing{TextInput::create(decoded->text.as_str()).unwrap(), *a, *c};
    if (start >= 0 && end >= start) {
        auto begin = decoded->offset(start), finish = decoded->offset(end);
        if (begin.is_none() || finish.is_none())
            return;
        editing.composition = Some(TextInputSelection{*begin, Some(Index(*finish))});
    }
    TextInputTransaction transaction;
    auto &surrounding = data_->text->surrounding;
    transaction.expected_surrounding = Some(TextSurroundingRevision{revision, surrounding.anchor, surrounding.cursor});
    transaction.editing = rstd::move(editing);
    auto value = TextTransaction::create(rstd::move(transaction));
    if (value.is_err())
        return;
    InputEvent event;
    event.type = EventType::TextTransaction;
    event.text_session = session;
    event.transaction = rstd::move(value).unwrap();
    data_->push(rstd::move(event));
}
auto AndroidHost::text_input(Option<TextInputState> state) -> NativeTextInputResult {
    Environment current(data_->view->vm);
    if (!current.env)
        return NativeTextInputResult::Failed;
    if (state.is_some() && !state->valid())
        return NativeTextInputResult::Failed;
    bool unchanged = state.is_some() && data_->text.is_some() &&
                     state->request.session == data_->text->request.session &&
                     state->change_cause == TextChangeCause::InputMethod;
    data_->text = rstd::move(state);
    if (unchanged) {
        current.env->CallVoidMethod(data_->view->view, data_->view->revision,
                                    jlong(data_->text->surrounding.text.identity()));
    } else {
        auto text =
            data_->text.is_some() && data_->text->surrounding.present() ? data_->text->surrounding.text.text() : ""_str;
        auto encoded = encode(current.env, text);
        auto session = data_->text.is_some() ? data_->text->request.session : 0;
        auto revision = data_->text.is_some() ? data_->text->surrounding.text.identity() : 0;
        auto anchor = data_->text.is_some() ? utf16_offset(text, data_->text->surrounding.anchor) : 0;
        auto cursor = data_->text.is_some() ? utf16_offset(text, data_->text->surrounding.cursor) : 0;
        current.env->CallVoidMethod(data_->view->view, data_->view->update_text, jlong(session), jlong(revision),
                                    encoded, anchor, cursor);
        current.env->DeleteLocalRef(encoded);
    }
    if (!current.env->ExceptionCheck())
        return NativeTextInputResult::Succeeded;
    current.env->ExceptionClear();
    return NativeTextInputResult::Failed;
}
auto AndroidHost::read_clipboard() -> Result<String, ClipboardError> {
    Environment current(data_->view->vm);
    if (!current.env)
        return Err(ClipboardError::Unavailable);
    auto value = static_cast<jstring>(current.env->CallObjectMethod(data_->view->view, data_->view->read));
    if (current.env->ExceptionCheck()) {
        current.env->ExceptionClear();
        return Err(ClipboardError::Failed);
    }
    auto result = decode(current.env, value);
    if (value)
        current.env->DeleteLocalRef(value);
    return result.is_some() ? Result<String, ClipboardError>(Ok(rstd::move(result->text)))
                            : Err(ClipboardError::InvalidText);
}
auto AndroidHost::write_clipboard(ref<str> text) -> Result<empty, ClipboardError> {
    Environment current(data_->view->vm);
    if (!current.env)
        return Err(ClipboardError::Unavailable);
    auto value = encode(current.env, text);
    current.env->CallVoidMethod(data_->view->view, data_->view->write, value);
    current.env->DeleteLocalRef(value);
    if (!current.env->ExceptionCheck())
        return Ok(empty{});
    current.env->ExceptionClear();
    return Err(ClipboardError::Failed);
}
auto AndroidHost::request_clipboard(Serial id) -> Result<empty, ClipboardError> {
    if (!data_->queue.can_reserve(usize()))
        return Err(ClipboardError::Limit);
    auto text = read_clipboard();
    InputEvent event;
    event.type = EventType::ClipboardResult;
    event.clipboard_request = id;
    if (text.is_ok())
        event.text = TextInput::create(text->as_str()).unwrap();
    else
        event.clipboard_error = Some(text.unwrap_err());
    data_->push(rstd::move(event));
    return Ok(empty{});
}
auto AndroidHost::request_clipboard_write(Serial id, Serial, ref<str> text) -> Result<empty, ClipboardError> {
    if (!data_->queue.can_reserve(usize()))
        return Err(ClipboardError::Limit);
    auto result = write_clipboard(text);
    InputEvent event;
    event.type = EventType::ClipboardWriteResult;
    event.clipboard_request = id;
    if (result.is_err())
        event.clipboard_error = Some(result.unwrap_err());
    data_->push(rstd::move(event));
    return Ok(empty{});
}
auto AndroidHost::instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, gpu::Error> {
    Vec<vrento::vulkan::Extension> result;
    result.push({true, "VK_KHR_surface"_str});
    result.push({true, "VK_KHR_android_surface"_str});
    return Ok(rstd::move(result));
}
auto AndroidHost::create_surface(const vvk::Instance &instance) -> Result<gpu::NativeSurface, gpu::Error> {
    if (data_->window.is_none())
        return Err(gpu::Error{gpu::Stage::Surface});
    VkAndroidSurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
                                       .window = (*data_->window)->window};
    VkSurfaceKHR surface{};
    auto create = instance.Dispatch().vkCreateAndroidSurfaceKHR;
    if (!create)
        return Err(gpu::Error{gpu::Stage::Surface});
    auto result = create(*instance, &info, nullptr, &surface);
    if (result != VK_SUCCESS)
        return Err(gpu::Error{gpu::Stage::Surface, result});
    return Ok(gpu::NativeSurface{surface, Box<dyn<gpu::SurfaceLifetime>>::make(NativeLease(data_->window->clone()))});
}
auto AndroidHost::pixel_extent() const -> VkExtent2D {
    return data_->window.is_some() ? VkExtent2D{data_->width, data_->height} : VkExtent2D{};
}
} // namespace vui::host::detail
