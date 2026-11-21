module;
#include <android/log.h>
#include <jni.h>

module vui.host;
import :android_application;
import :frames.worker;

extern "C" auto vui_android_create_application() -> vui::host::AndroidApplication *;

namespace vui::host {
using namespace rstd::prelude;

struct AndroidApplication::Impl {
    UiSession session;
    gpu::Renderer renderer;
    Option<Box<detail::AndroidHost>> host;
    Option<Box<ThreadedFrames>> frames;
    Option<Box<SessionRunner<ThreadedFrames>>> runner;
    bool initialized{}, failed{}, closed{};
};
AndroidApplication::AndroidApplication() : data_(Box<Impl>::make()) {}
AndroidApplication::~AndroidApplication() {
    if (data_->frames.is_some()) {
        (*data_->frames)->close();
        (*data_->frames)->join();
    }
    (void)data_->session.close();
    (void)data_->renderer.shutdown(5'000'000'000);
}
auto AndroidApplication::session() -> UiSession & { return data_->session; }
void AndroidApplication::request_redraw() {
    if (data_->runner.is_some())
        (*data_->runner)->request_redraw();
    if (data_->host.is_some())
        (*data_->host)->waker().request();
}
void AndroidApplication::request_close() {
    if (data_->runner.is_some())
        (*data_->runner)->request_close();
    if (data_->host.is_some())
        (*data_->host)->waker().request();
}
auto AndroidApplication::request_capture() -> Result<Serial, FrameRequestError> {
    return data_->frames.is_some() ? (*data_->frames)->request_capture() : Err(FrameRequestError::Busy);
}
auto AndroidApplication::snapshot() -> RenderSnapshot {
    return data_->frames.is_some() ? (*data_->frames)->snapshot() : RenderSnapshot{};
}
auto AndroidApplication::metrics() const -> AndroidMetrics {
    return data_->host.is_some() ? (*data_->host)->metrics() : AndroidMetrics{};
}
void AndroidApplication::set_system_bar_icons(AndroidSystemBarIcons status, AndroidSystemBarIcons navigation) {
    if (data_->host.is_some())
        (*data_->host)->set_system_bar_icons(status, navigation);
}
void AndroidApplication::set_input_tracing(bool enabled) {
    if (data_->host.is_some())
        (*data_->host)->tracing(enabled);
}
auto AndroidApplication::before_advance(const HostEvents &) -> Result<empty, UiError> { return Ok(empty{}); }
auto AndroidApplication::after_advance(const AdvanceResult &) -> Result<empty, UiError> { return Ok(empty{}); }
auto AndroidApplication::on_frame_event(const FrameEvent &) -> Result<empty, UiError> { return Ok(empty{}); }
} // namespace vui::host

namespace vui::host::detail {
using namespace rstd::prelude;

struct AndroidBridge {
    static auto get(jlong handle) -> AndroidApplication & { return *reinterpret_cast<AndroidApplication *>(handle); }
    static auto host(jlong handle) -> AndroidHost & { return **get(handle).data_->host; }
    static void failure(AndroidApplication &app, const char *operation, const gpu::Error &error) {
        app.data_->failed = true;
        __android_log_print(ANDROID_LOG_ERROR, "Vui", "%s failed: stage=%u vk=%d", operation, unsigned(error.stage),
                            int(error.result));
        if (error.text.is_some())
            __android_log_print(ANDROID_LOG_ERROR, "Vui", "text error=%u", unsigned(*error.text));
        auto message = [](const char *kind, ref<str> text) {
            __android_log_print(ANDROID_LOG_ERROR, "Vui", "%s: %.*s", kind,
                                int(rstd::cmp::min(text->len(), usize(2048)).to_primitive()),
                                reinterpret_cast<const char *>(text->as_bytes().as_raw_ptr()));
        };
        auto diagnostic = [&](const vrento::ErrorDiagnostic &value) {
            message("backend", value.message.as_str());
            for (const auto &source : value.sources)
                message("caused by", source.as_str());
        };
        if (error.backend.is_some())
            diagnostic(*error.backend);
        if (error.content.is_some())
            diagnostic(error.content->diagnostic);
        if (error.graph_error.is_some())
            diagnostic(error.graph_error->diagnostic);
    }
    static auto create(JNIEnv *env, jobject view) -> jlong {
        auto app = vui_android_create_application();
        if (!app)
            return 0;
        auto &data = *app->data_;
        data.host = Some(Box<AndroidHost>::make(env, view));
        auto &host = **data.host;
        if (!host.valid()) {
            delete app;
            return 0;
        }
        auto &input = data.session.input();
        input.set_native_capture([](PointerKind kind, bool) {
            return kind == PointerKind::Mouse ? NativeCaptureResult::Unsupported : NativeCaptureResult::Succeeded;
        });
        (void)input.set_native_text_input_state(
            [&host](Option<TextInputState> state) { return host.text_input(rstd::move(state)); });
        (void)input.set_clipboard([&host] { return host.read_clipboard(); },
                                  [&host](ref<str> text) { return host.write_clipboard(text); });
        (void)input.set_clipboard_request([&host](Serial id) { return host.request_clipboard(id); });
        (void)input.set_clipboard_write_request([&host](Serial id, Serial activation, ref<str> text) {
            return host.request_clipboard_write(id, activation, text);
        });
        (void)data.session.set_frame_waker(Some(host.waker()));
        auto configured = app->configure(data.session);
        if (configured.is_err()) {
            __android_log_print(ANDROID_LOG_ERROR, "Vui", "configure failed: %u", unsigned(configured.unwrap_err()));
            delete app;
            return 0;
        }
        return reinterpret_cast<jlong>(app);
    }
    static auto surface(JNIEnv *env, jlong handle, jobject surface) -> bool {
        auto &app = get(handle);
        auto &data = *app.data_;
        auto &host = **data.host;
        if (host.attached()) {
            if (data.frames.is_some()) {
                auto detached = (*data.frames)->detach_surface();
                if (detached.is_err()) {
                    failure(app, "detach surface", detached.unwrap_err());
                    return false;
                }
            }
            host.detach();
        }
        if (!surface)
            return true;
        if (!host.attach(env, surface))
            return false;
        if (!data.initialized) {
            auto initialized = data.renderer.initialize(host, false);
            if (initialized.is_err()) {
                failure(app, "initialize renderer", initialized.unwrap_err());
                host.detach();
                return false;
            }
            data.frames = Some(Box<ThreadedFrames>::make(data.session, true, Some(host.waker())));
            auto started = (*data.frames)->start(data.renderer);
            if (started.is_err()) {
                failure(app, "start renderer", started.unwrap_err());
                return false;
            }
            data.runner = Some(Box<SessionRunner<ThreadedFrames>>::make(data.session, **data.frames));
            data.initialized = true;
        } else {
            auto attached = (*data.frames)->attach_surface(host, host.metrics().surface.generation);
            if (attached.is_err()) {
                failure(app, "attach surface", attached.unwrap_err());
                host.detach();
                return false;
            }
        }
        (*data.runner)->request_redraw();
        return true;
    }
    static auto pump(jlong handle, FrameTiming timing) -> int {
        auto &app = get(handle);
        auto &data = *app.data_;
        auto &host = **data.host;
        if (data.failed || data.closed || data.runner.is_none())
            return -1;
        auto hooks = callback_hooks(
            [&app](const HostEvents &events) { return app.before_advance(events); },
            [&app](const AdvanceResult &result) { return app.after_advance(result); },
            [&app, &host](const FrameEvent &event) {
                if (event.type == FrameEventType::PresentQueued && event.frame.is_some())
                    host.presented(event.frame->surface_generation);
                return app.on_frame_event(event);
            },
            [&app] { return app.on_close_requested(); });
        auto result = (*data.runner)->pump(host.events(), timing, hooks);
        if (result.is_err()) {
            data.failed = true;
            const auto &error = result.unwrap_err();
            __android_log_print(ANDROID_LOG_ERROR, "Vui", "session pump failed: stage=%u",
                                unsigned(error.stage));
            if (error.session.is_some())
                __android_log_print(ANDROID_LOG_ERROR, "Vui", "session stage=%u", unsigned(error.session->stage));
            if (error.callback.is_some())
                __android_log_print(ANDROID_LOG_ERROR, "Vui", "callback error=%u", unsigned(*error.callback));
            if (error.frame.is_some())
                __android_log_print(ANDROID_LOG_ERROR, "Vui", "frame error=%u", unsigned(*error.frame));
            if (error.request.is_some())
                __android_log_print(ANDROID_LOG_ERROR, "Vui", "frame request error=%u", unsigned(*error.request));
            if (error.gpu.is_some())
                failure(app, "session frame", *error.gpu);
            return -1;
        }
        if (!result.unwrap()) {
            data.closed = true;
            host.finish();
            return -1;
        }
        if (host.suspended())
            return 1000;
        // Choreographer schedules the next frame independently of this frame's prediction.
        return int((*data.runner)->next_wait({timing.now_ns}));
    }
    static void redraw_async(JNIEnv *env, jlong handle, jobject runnable) {
        host(handle).redraw_async(env, runnable);
    }
};
} // namespace vui::host::detail

using rstd::prelude::Vec;
using vui::host::detail::AndroidBridge;
extern "C" {
JNIEXPORT jlong JNICALL Java_io_vui_VuiView_nativeCreate(JNIEnv *env, jclass, jobject view) {
    return AndroidBridge::create(env, view);
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeDestroy(JNIEnv *, jclass, jlong handle) {
    delete &AndroidBridge::get(handle);
}
JNIEXPORT jboolean JNICALL Java_io_vui_VuiView_nativeSurface(JNIEnv *env, jclass, jlong handle, jobject surface) {
    return AndroidBridge::surface(env, handle, surface);
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeResize(JNIEnv *, jclass, jlong handle, jint width, jint height) {
    if (width > 0 && height > 0)
        AndroidBridge::host(handle).resize(width, height);
}
JNIEXPORT jint JNICALL Java_io_vui_VuiView_nativePump(JNIEnv *, jclass, jlong handle, jlong time, jlong sample,
                                                   jlong expected) {
    using vui::Serial;
    using namespace rstd::prelude;
    auto present = expected > time ? Some(Serial(expected)) : None<Serial>();
    return AndroidBridge::pump(handle, {Serial(time), present, Some(Serial(sample))});
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeActive(JNIEnv *, jclass, jlong handle, jboolean value) {
    AndroidBridge::host(handle).active(value);
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeFocus(JNIEnv *, jclass, jlong handle, jboolean value) {
    AndroidBridge::host(handle).focus(value);
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeWindowFocus(JNIEnv *, jclass, jlong handle, jboolean value) {
    AndroidBridge::host(handle).window_focus(value);
}
JNIEXPORT jboolean JNICALL Java_io_vui_VuiView_nativeBack(JNIEnv *, jclass, jlong handle) {
    return AndroidBridge::get(handle).on_back_requested();
}
JNIEXPORT jboolean JNICALL Java_io_vui_VuiView_nativeCanBack(JNIEnv *, jclass, jlong handle) {
    return AndroidBridge::get(handle).can_handle_back();
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeRedraw(JNIEnv *, jclass, jlong handle) {
    AndroidBridge::host(handle).redraw();
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeRedrawAsync(JNIEnv *env, jclass, jlong handle, jobject runnable) {
    AndroidBridge::redraw_async(env, handle, runnable);
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeMetrics(JNIEnv *, jclass, jlong handle, jfloat density,
                                                         jfloat font_scale, jfloat refresh, jint left, jint top,
                                                         jint right, jint bottom, jint ime_left, jint ime_top,
                                                         jint ime_right, jint ime_bottom) {
    auto &host = AndroidBridge::host(handle);
    host.metrics(density, font_scale, refresh, {double(left), double(top), double(right), double(bottom)},
                 {double(ime_left), double(ime_top), double(ime_right), double(ime_bottom)});
    AndroidBridge::get(handle).on_metrics_changed(host.metrics());
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativePointer(JNIEnv *env, jclass, jlong handle, jint action, jint pointer,
                                                         jint tool, jint device, jint buttons, jint modifiers,
                                                         jlong time, jfloat x, jfloat y, jfloat pressure,
                                                         jlongArray history_times, jfloatArray history) {
    auto count = env->GetArrayLength(history_times), points = env->GetArrayLength(history);
    if (count < 0 || count > 2048 || points != count * 3)
        return;
    Vec<jlong> times;
    times.resize(rstd::usize(count), jlong{});
    Vec<float> values;
    values.resize(rstd::usize(points), float{});
    env->GetLongArrayRegion(history_times, 0, count, times.as_mut_slice().as_raw_ptr());
    env->GetFloatArrayRegion(history, 0, points, values.as_mut_slice().as_raw_ptr());
    Vec<vui::Serial> samples;
    for (auto value : times)
        samples.push(vui::Serial(value));
    AndroidBridge::host(handle).pointer(action, pointer, tool, device, buttons, modifiers, time, x, y, pressure,
                                        samples.as_slice(), values.as_slice());
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeWheel(JNIEnv *, jclass, jlong handle, jint device, jfloat x, jfloat y,
                                                       jfloat dx, jfloat dy, jlong time, jint modifiers) {
    AndroidBridge::host(handle).wheel(device, {x, y}, {dx, dy}, time, modifiers);
}
JNIEXPORT jboolean JNICALL Java_io_vui_VuiView_nativeKey(JNIEnv *, jclass, jlong handle, jint code, jboolean down,
                                                         jboolean repeat, jint modifiers, jint device, jlong time) {
    return AndroidBridge::host(handle).key(code, down, repeat, modifiers, device, time);
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeText(JNIEnv *env, jclass, jlong handle, jlong session, jlong revision,
                                                      jstring text, jint anchor, jint cursor, jint start, jint end) {
    AndroidBridge::host(handle).editing(env, session, revision, text, anchor, cursor, start, end);
}
JNIEXPORT void JNICALL Java_io_vui_VuiView_nativeImeDismissed(JNIEnv *, jclass, jlong handle, jlong session) {
    auto &input = AndroidBridge::get(handle).session().input();
    auto request = input.text_input_request();
    if (request.is_some() && request->session == vui::Serial(session))
        (void)input.request_focus(rstd::None<vui::UiId>());
}
}
