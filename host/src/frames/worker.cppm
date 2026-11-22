module vui.host:frames.worker;
import :frames;
import rstd;
using namespace rstd::prelude;

namespace vui::host {
// The UI owns the session and retains every plan until the renderer releases it.
class ThreadedFrames {
    struct Control {
        VkExtent2D extent{};
        Serial generation{}, interval{}, revision{};
        gpu::SurfaceHost *replacement{};
        Serial surface_request{}, surface_generation{};
        gpu::PresentMode mode{gpu::PresentMode::Fifo};
        bool suspended{}, close{};
    };
    struct CaptureRequest {
        Serial id;
        Option<CaptureSelection> selection;
    };
    struct Channel {
        rstd::sync::Mutex<empty> mutex;
        rstd::sync::Condvar changed;
        Control control;
        Option<FramePacket> packet;
        Serial request{};
        Option<Serial> working;
        Vec<FrameEvent> events;
        Vec<Serial> released;
        FrameLoopStatus status;
        RenderSnapshot snapshot;
        bool ready{true}, stopped{};
        Option<CaptureRequest> capture;
        Serial surface_completed{};
        Option<gpu::Error> surface_error;
        Option<FrameWaker> waker;
        explicit Channel(Option<FrameWaker> wake) : waker(rstd::move(wake)) {}
        void wake_ui() const { if (waker.is_some()) (void)waker->request(); }
    };
    UiSession &session_;
    rstd::sync::Arc<Channel> channel_;
    Option<rstd::thread::JoinHandle<void>> worker_;
    Option<Serial> pending_;
    Serial next_request_{}, next_capture_{};
    struct Lease { Serial request; FramePlan plan; Option<UiFrame> view; };
    Vec<Lease> leases_;
    Vec<FrameEvent> events_;
    FrameLoopStatus status_;
    RenderSnapshot snapshot_;
    bool closing_{}, windowed_;
    gpu::DrawOptions draw_;
    Serial capture_pending_{};
    auto change_surface(gpu::SurfaceHost *, Serial generation) -> Result<empty, gpu::Error>;
    auto enqueue_capture(Option<CaptureSelection>) -> Result<Serial, FrameRequestError>;

    static auto capture_failed(Serial id, Option<gpu::Error> fault = {}) -> FrameEvent {
        FrameEvent event;
        event.type = FrameEventType::CaptureReady;
        event.capture = id;
        event.error = fault.is_some() ? rstd::move(fault) : Some(gpu::Error{gpu::Stage::State});
        return event;
    }

    static auto clock() -> FrameTiming {
        return {Serial(rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive()), {}};
    }
    template <class Backend> static void execute(Backend &renderer, bool windowed, gpu::DrawOptions draw,
                        rstd::sync::Arc<Channel> channel) {
        FrameExecutor frames(renderer, windowed, draw);
        Option<FramePacket> packet;
        Serial request{}, revision{}, surface_completed{};
        Option<CaptureRequest> capture;
        bool enqueued{};
        for (;;) {
            Control control;
            bool capacity{};
            {
                auto lock = channel->mutex.lock().unwrap();
                control = channel->control;
                capacity = channel->events.len() < usize(48);
                if (capture.is_none()) capture = rstd::exchange(channel->capture, Option<CaptureRequest>{});
                if (packet.is_none() && channel->packet.is_some() && control.surface_request == surface_completed) {
                    packet = rstd::move(channel->packet);
                    channel->packet = None();
                    request = channel->request;
                    channel->working = Some(request);
                    channel->ready = false;
                    enqueued = false;
                }
            }
            if (revision != control.revision) {
                revision = control.revision;
                frames.set_surface(control.extent, control.generation);
                frames.set_suspended(control.suspended);
                frames.set_frame_interval(control.interval);
                if (windowed) (void)frames.set_present_mode(control.mode);
            }
            bool changing_surface = control.surface_request != surface_completed;
            bool changed_surface{};
            if (changing_surface) {
                auto changed = [&]() -> Result<empty, gpu::Error> {
                    if constexpr (requires {
                                      renderer.detach_surface(Serial{});
                                      renderer.attach_surface(*control.replacement);
                                  }) {
                        auto detached = frames.detach_surface(0);
                        if (detached.is_err())
                            return detached;
                        if (control.replacement)
                            return frames.attach_surface(*control.replacement, control.surface_generation);
                        return Ok(empty{});
                    } else
                        return Err(gpu::Error{gpu::Stage::State});
                }();
                bool failed = changed.is_err();
                Option<gpu::Error> cause = failed ? Some(changed.unwrap_err()) : Option<gpu::Error>{};
                bool waiting = cause.is_some() && (cause->result == VK_TIMEOUT || cause->result == VK_NOT_READY);
                if (!waiting) {
                    surface_completed = control.surface_request;
                    changed_surface = true;
                    {
                        auto lock = channel->mutex.lock().unwrap();
                        channel->surface_completed = surface_completed;
                        channel->surface_error = rstd::move(cause);
                        channel->control.extent =
                            !failed && control.replacement ? renderer.pixel_extent() : VkExtent2D{};
                        channel->control.generation = control.surface_generation;
                        channel->control.suspended = failed || !control.replacement;
                        ++channel->control.revision;
                    }
                    channel->changed.notify_all();
                }
            }
            if (control.close) frames.close();
            if (packet.is_some() && !enqueued && !control.close && !changing_surface) {
                (void)frames.request(Some(request));
                enqueued = true;
            }
            if ((capacity && !changing_surface) || control.close) {
                (void)frames.pump_with_clock(clock, [&]() -> Result<FramePacket, FrameError> {
                    if (packet.is_none()) return Err(FrameError::Busy);
                    return Ok(FramePacket{FramePlan(packet->plan), packet->inputs.clone(), packet->frozen,
                        packet->view ? Some(UiFrame(*packet->view)) : Option<UiFrame>{}});
                }, [&] {
                    Control latest;
                    { auto lock = channel->mutex.lock().unwrap(); latest = channel->control; }
                    frames.set_surface(latest.extent, latest.generation);
                    frames.set_suspended(latest.suspended);
                    if (latest.close) frames.close();
                });
            }
            auto events = frames.poll_events();
            if (packet.is_some() && changed_surface) {
                bool terminal{};
                for (const auto &event : events)
                    terminal |= event.request == request && detail::is_terminal_frame_event(event.type);
                if (!terminal)
                    events.push({request, FrameEventType::Cancelled});
            }
            if (packet.is_some() && !enqueued && control.close) {
                events.push({request, FrameEventType::Cancelled});
                enqueued = true;
            }
            const FrameEvent *completed{};
            bool terminal{};
            for (const auto &event : events) {
                if (event.type == FrameEventType::GpuComplete) completed = &event;
                if (event.type == FrameEventType::Failed) frames.close();
                terminal |= detail::is_terminal_frame_event(event.type);
            }
            auto did_complete = completed != nullptr;
            bool expired{};
            if (capture.is_some() && capture->selection.is_some()) {
                auto lock = channel->mutex.lock().unwrap();
                expired = channel->control.generation > capture->selection->surface_generation;
            }
            if (expired) {
                FrameEvent result;
                result.type = FrameEventType::CaptureCancelled;
                result.capture = capture->id;
                capture = None();
                events.push(rstd::move(result));
            } else if (capture.is_some() && completed && completed->frame.is_some() &&
                       (capture->selection.is_none() ||
                        (completed->frame->surface_generation == capture->selection->surface_generation &&
                         completed->frame->ui_revision >= capture->selection->min_ui_revision &&
                         completed->present != Some(gpu::PresentStatus::OutOfDate)))) {
                FrameEvent result;
                result.type = FrameEventType::CaptureReady;
                result.capture = capture->id;
                capture = None();
                result.request = completed->request;
                result.frame = completed->frame;
                auto pixels = renderer.readback();
                if (pixels.is_ok()) result.pixels = Some(rstd::move(pixels).unwrap());
                else result.error = Some(pixels.unwrap_err());
                events.push(rstd::move(result));
            }
            RenderSnapshot snapshot{renderer.validation_errors(), renderer.draw_stats(), renderer.frame_timings(),
                                    renderer.text_cache_stats(), renderer.image_cache_stats(), {}, renderer.pixel_extent()};
            if constexpr (requires { renderer.present_mode(); })
                snapshot.present_mode = renderer.present_mode();
            auto status = frames.status();
            Option<Serial> released;
            if (packet.is_some() && terminal && status.active.is_none() && status.pending.is_none() && status.fault.is_none() && !status.closing) {
                bool safe = true;
                if (did_complete) {
                    auto release = renderer.release_completed_frame();
                    if (release.is_err()) {
                        FrameEvent failed{request, FrameEventType::Failed};
                        failed.error = Some(release.unwrap_err());
                        events.push(rstd::move(failed));
                        frames.close();
                        safe = false;
                    }
                }
                // The UI lease remains alive after this worker reference is dropped.
                if (safe) { packet = None(); released = Some(request); }
            }
            if (status.fault.is_some()) frames.close();
            bool stopped = status.closed;
            if (stopped) {
                packet = None();
                if (request) released = Some(request);
                if (capture.is_some()) events.push(capture_failed(capture->id, status.fault.clone()));
            }
            bool notify = !events.is_empty() || released.is_some() || stopped;
            {
                auto lock = channel->mutex.lock().unwrap();
                for (auto &event : events) channel->events.push(rstd::move(event));
                if (stopped && channel->capture.is_some()) {
                    channel->events.push(capture_failed(channel->capture->id, status.fault.clone()));
                    channel->capture = None();
                }
                if (released.is_some()) {
                    channel->released.push(Serial(*released));
                    if (channel->working == released) channel->working = None();
                }
                channel->status = status.clone();
                if (snapshot.draws.is_none()) snapshot.draws = channel->snapshot.draws;
                if (snapshot.timings.is_none()) snapshot.timings = channel->snapshot.timings;
                channel->snapshot = rstd::move(snapshot);
                auto ready = packet.is_none() && !stopped && !control.close && !changing_surface &&
                             status.fault.is_none() && !frames.production_wait(clock().now_ns);
                notify |= ready && !channel->ready;
                channel->ready = ready;
                channel->stopped = stopped;
            }
            if (notify) channel->wake_ui();
            if (stopped) {
                channel->changed.notify_all();
                break;
            }
            auto lock = channel->mutex.lock().unwrap();
            if (channel->control.revision != revision || channel->control.close != control.close ||
                channel->packet.is_some() || channel->capture.is_some()) continue;
            auto remaining = frames.production_wait(clock().now_ns);
            auto wait = status.active.is_some() || status.closing || changing_surface
                            ? Index(1)
                            : (remaining ? remaining : Index(1000));
            (void)channel->changed.wait_timeout(lock, rstd::time::Duration::from_millis(rstd::u64(wait ? wait : 1)));
        }
    }
    void collect();
    auto pump_pending() -> Result<empty, gpu::Error>;

  public:
    // Renderer is exclusively borrowed until join; it must outlive this object.
    ThreadedFrames(UiSession &session, bool windowed, Option<FrameWaker> waker = {},
                   gpu::DrawOptions draw = {.timings = true})
        : session_(session), channel_(rstd::sync::Arc<Channel>::make(rstd::move(waker))), windowed_(windowed),
          draw_(draw) {}
    ThreadedFrames(const ThreadedFrames &) = delete;
    ~ThreadedFrames();
    template <class Backend> auto start(Backend &renderer) -> Result<empty, gpu::Error> {
        if (worker_.is_some() || closing_) return Err(gpu::Error{gpu::Stage::State});
        auto spawned = rstd::thread::spawn([&renderer, windowed = windowed_, draw = draw_,
                                            channel = channel_.clone()]() mutable {
            execute(renderer, windowed, draw, rstd::move(channel));
        });
        if (spawned.is_err()) return Err(gpu::Error{gpu::Stage::State});
        worker_ = Some(rstd::move(spawned).unwrap());
        return Ok(empty{});
    }
    void join();
    auto request() -> Result<Serial, FrameRequestError>;
    void set_surface(VkExtent2D extent, Serial generation);
    void set_suspended(bool value);
    auto detach_surface() -> Result<empty, gpu::Error> { return change_surface(nullptr, 0); }
    auto attach_surface(gpu::SurfaceHost &host, Serial generation) -> Result<empty, gpu::Error> {
        return change_surface(&host, generation);
    }
    void set_frame_interval(Serial value);
    auto requested_present_mode() const -> gpu::PresentMode {
        auto lock = channel_->mutex.lock().unwrap();
        return channel_->control.mode;
    }
    auto set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error>;
    // Captures the next GPU completion observed after the render thread accepts this request.
    auto request_capture() -> Result<Serial, FrameRequestError> { return enqueue_capture({}); }
    // A superseded surface generation finishes the request with CaptureCancelled.
    auto request_capture(CaptureSelection selection) -> Result<Serial, FrameRequestError> {
        return enqueue_capture(Some(selection));
    }
    auto pump_with_clock(auto) -> Result<empty, gpu::Error> { return pump_pending(); }
    auto pump(FrameTiming timing = {}) -> Result<empty, gpu::Error> {
        return pump_with_clock([timing] { return timing; });
    }
    void close();
    auto status() -> FrameLoopStatus;
    auto snapshot() -> RenderSnapshot { collect(); return snapshot_; }
    auto poll_events() -> Vec<FrameEvent> {
        collect();
        return rstd::exchange(events_, Vec<FrameEvent>{});
    }
    auto wait_milliseconds(bool animating = false, FrameTiming timing = {}, Option<Serial> deadline = {}) -> Index;
};
} // namespace vui::host
