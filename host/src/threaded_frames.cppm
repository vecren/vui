export module vui.host:threaded_frames;
import :frames;
import rstd;
using namespace rstd::prelude;

export namespace vui::host {
struct RenderSnapshot {
    unsigned validation_errors{};
    Option<gpu::DrawStats> draws;
    Option<gpu::FrameTimings> timings;
    gpu::TextCacheStats text;
    gpu::ImageCacheStats images;
};
// The UI owns the session and retains every plan until the renderer releases it.
class ThreadedFrames {
    struct Control {
        VkExtent2D extent{};
        Serial generation{}, interval{}, revision{};
        gpu::PresentMode mode{gpu::PresentMode::Fifo};
        bool suspended{}, close{};
    };
    struct Channel {
        rstd::sync::Mutex<empty> mutex;
        rstd::sync::Condvar changed;
        Control control;
        Option<FramePacket> packet;
        Serial request{};
        Vec<FrameEvent> events;
        Vec<Serial> released;
        FrameLoopStatus status;
        RenderSnapshot snapshot;
        bool ready{true}, stopped{};
        Serial capture{};
        Option<FrameWaker> waker;
        explicit Channel(Option<FrameWaker> wake) : waker(rstd::move(wake)) {}
        void wake_ui() const { if (waker.is_some()) (void)waker->request(); }
    };
    UiSession &session_;
    rstd::sync::Arc<Channel> channel_;
    Option<rstd::thread::JoinHandle<void>> worker_;
    Option<Serial> pending_;
    Serial next_request_{}, next_capture_{};
    struct Lease { Serial request; FramePlan plan; };
    Vec<Lease> leases_;
    Vec<FrameEvent> events_;
    FrameLoopStatus status_;
    RenderSnapshot snapshot_;
    bool closing_{}, windowed_;
    Serial capture_pending_{};

    static auto clock() -> FrameTiming {
        return {Serial(rstd::time::Instant::now().duration_since_epoch().as_nanos().to_primitive()), {}};
    }
    template <class Backend> static void execute(Backend &renderer, bool windowed,
                        rstd::sync::Arc<Channel> channel) {
        FrameExecutor frames(renderer, windowed, {.timings = true});
        Option<FramePacket> packet;
        Serial request{}, revision{}, capture{};
        bool enqueued{};
        for (;;) {
            Control control;
            bool capacity{};
            {
                auto lock = channel->mutex.lock().unwrap();
                control = channel->control;
                capacity = channel->events.len() < usize(48);
                if (channel->capture && !capture) capture = rstd::exchange(channel->capture, Serial{});
                if (packet.is_none() && channel->packet.is_some()) {
                    packet = rstd::move(channel->packet);
                    channel->packet = None();
                    request = channel->request;
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
            if (control.close) frames.close();
            if (packet.is_some() && !enqueued && !control.close) {
                (void)frames.request(Some(request));
                enqueued = true;
            }
            if (capacity || control.close) {
                (void)frames.pump_with_clock(clock, [&]() -> Result<FramePacket, FrameError> {
                    if (packet.is_none()) return Err(FrameError::Busy);
                    return Ok(FramePacket{FramePlan(packet->plan), packet->inputs.clone(), true});
                }, [&] {
                    Control latest;
                    { auto lock = channel->mutex.lock().unwrap(); latest = channel->control; }
                    frames.set_surface(latest.extent, latest.generation);
                    frames.set_suspended(latest.suspended);
                    if (latest.close) frames.close();
                });
            }
            auto events = frames.poll_events();
            if (packet.is_some() && !enqueued && control.close) {
                events.push({request, FrameEventType::Cancelled});
                enqueued = true;
            }
            bool completed{}, terminal{};
            for (const auto &event : events) {
                completed |= event.type == FrameEventType::GpuComplete;
                if (event.type == FrameEventType::Failed) frames.close();
                terminal |= event.type == FrameEventType::GpuComplete || event.type == FrameEventType::Skipped ||
                            event.type == FrameEventType::Cancelled || event.type == FrameEventType::Failed;
            }
            if (completed && capture) {
                FrameEvent result;
                result.type = FrameEventType::CaptureReady;
                result.capture = rstd::exchange(capture, Serial{});
                result.request = request;
                result.frame = Some(packet->plan.identity());
                auto pixels = renderer.readback();
                if (pixels.is_ok()) result.pixels = Some(rstd::move(pixels).unwrap());
                else result.error = Some(pixels.unwrap_err());
                events.push(rstd::move(result));
            }
            RenderSnapshot snapshot{renderer.validation_errors(), renderer.draw_stats(), renderer.frame_timings(), renderer.text_cache_stats(), renderer.image_cache_stats()};
            auto status = frames.status();
            Option<Serial> released;
            if (packet.is_some() && terminal && status.active.is_none() && status.pending.is_none() && status.fault.is_none() && !status.closing) {
                bool safe = true;
                if (completed) {
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
                if (capture) {
                    FrameEvent result;
                    result.type = FrameEventType::CaptureReady;
                    result.capture = rstd::exchange(capture, Serial{});
                    result.error = Some(gpu::Error{gpu::Stage::State});
                    events.push(rstd::move(result));
                }
            }
            bool notify = !events.is_empty() || released.is_some() || stopped;
            {
                auto lock = channel->mutex.lock().unwrap();
                for (auto &event : events) channel->events.push(rstd::move(event));
                if (released.is_some()) channel->released.push(Serial(*released));
                channel->status = status.clone();
                if (snapshot.draws.is_none()) snapshot.draws = channel->snapshot.draws;
                if (snapshot.timings.is_none()) snapshot.timings = channel->snapshot.timings;
                channel->snapshot = rstd::move(snapshot);
                auto ready = packet.is_none() && !stopped && !control.close && status.fault.is_none() &&
                             !frames.production_wait(clock().now_ns);
                notify |= ready && !channel->ready;
                channel->ready = ready;
                channel->stopped = stopped;
            }
            if (notify) channel->wake_ui();
            if (stopped) break;
            auto lock = channel->mutex.lock().unwrap();
            if (channel->control.revision != revision || channel->control.close != control.close || channel->packet.is_some()) continue;
            auto remaining = frames.production_wait(clock().now_ns);
            auto wait = status.active.is_some() || status.closing ? Index(1) : (remaining ? remaining : Index(1000));
            (void)channel->changed.wait_timeout(lock, rstd::time::Duration::from_millis(rstd::u64(wait ? wait : 1)));
        }
    }
    void collect() {
        Vec<FrameEvent> events;
        Vec<Serial> released;
        {
            auto lock = channel_->mutex.lock().unwrap();
            events = rstd::exchange(channel_->events, Vec<FrameEvent>{});
            released = rstd::exchange(channel_->released, Vec<Serial>{});
            status_ = channel_->status.clone();
            snapshot_ = channel_->snapshot;
        }
        for (auto id : released) leases_.retain([id](const Lease &lease) { return lease.request != id; });
        for (auto &event : events) {
            if (event.capture == capture_pending_ && event.capture) capture_pending_ = 0;
            events_.push(rstd::move(event));
        }
        channel_->changed.notify_one();
    }

  public:
    // Renderer is exclusively borrowed until join; it must outlive this object.
    ThreadedFrames(UiSession &session, bool windowed, Option<FrameWaker> waker = {})
        : session_(session), channel_(rstd::sync::Arc<Channel>::make(rstd::move(waker))), windowed_(windowed) {}
    ThreadedFrames(const ThreadedFrames &) = delete;
    ~ThreadedFrames() {
        close();
        join();
    }
    template <class Backend> auto start(Backend &renderer) -> Result<empty, gpu::Error> {
        if (worker_.is_some() || closing_) return Err(gpu::Error{gpu::Stage::State});
        auto spawned = rstd::thread::spawn([&renderer, windowed = windowed_, channel = channel_.clone()]() mutable {
            execute(renderer, windowed, rstd::move(channel));
        });
        if (spawned.is_err()) return Err(gpu::Error{gpu::Stage::State});
        worker_ = Some(rstd::move(spawned).unwrap());
        return Ok(empty{});
    }
    void join() {
        if (worker_.is_some()) {
            (void)rstd::move(*worker_).join();
            worker_ = None();
            collect();
            leases_.clear();
        }
    }
    auto request() -> Result<Serial, FrameRequestError> {
        collect();
        if (closing_ || status_.closing || status_.closed) return Err(FrameRequestError::Closing);
        if (status_.fault.is_some()) return Err(FrameRequestError::Faulted);
        if (events_.len() >= usize(48)) return Err(FrameRequestError::Busy);
        if (next_request_ == ~Serial{}) return Err(FrameRequestError::Limit);
        if (pending_.is_some()) events_.push({*pending_, FrameEventType::Skipped});
        pending_ = Some(++next_request_);
        return Ok(next_request_);
    }
    void set_surface(VkExtent2D extent, Serial generation) {
        auto lock = channel_->mutex.lock().unwrap();
        auto &control = channel_->control;
        if (control.extent.width == extent.width && control.extent.height == extent.height && control.generation == generation) return;
        control.extent = extent; control.generation = generation; ++control.revision;
        channel_->changed.notify_one();
    }
    void set_suspended(bool value) {
        auto lock = channel_->mutex.lock().unwrap();
        if (channel_->control.suspended == value) return;
        channel_->control.suspended = value; ++channel_->control.revision;
        channel_->changed.notify_one();
    }
    void set_frame_interval(Serial value) {
        auto lock = channel_->mutex.lock().unwrap();
        channel_->control.interval = value; ++channel_->control.revision;
        channel_->changed.notify_one();
    }
    auto requested_present_mode() const -> gpu::PresentMode {
        auto lock = channel_->mutex.lock().unwrap();
        return channel_->control.mode;
    }
    auto set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error> {
        if (!windowed_ || closing_ || unsigned(mode) > unsigned(gpu::PresentMode::FifoRelaxed))
            return Err(gpu::Error{gpu::Stage::State});
        auto lock = channel_->mutex.lock().unwrap();
        channel_->control.mode = mode; ++channel_->control.revision;
        channel_->changed.notify_one();
        return Ok(empty{});
    }
    // Captures the next GPU completion observed after the render thread accepts this request.
    auto request_capture() -> Result<Serial, FrameRequestError> {
        collect();
        if (closing_ || status_.closing || status_.closed) return Err(FrameRequestError::Closing);
        if (capture_pending_) return Err(FrameRequestError::Busy);
        if (next_capture_ == ~Serial{}) return Err(FrameRequestError::Limit);
        auto lock = channel_->mutex.lock().unwrap();
        if (channel_->capture) return Err(FrameRequestError::Busy);
        channel_->capture = ++next_capture_;
        capture_pending_ = next_capture_;
        channel_->changed.notify_one();
        return Ok(next_capture_);
    }
    auto pump_with_clock(auto) -> Result<empty, gpu::Error> {
        collect();
        if (status_.fault.is_some() && !closing_) return Err(*status_.fault);
        if (closing_ || status_.closing || status_.closed || pending_.is_none() || events_.len() >= usize(48)) return Ok(empty{});
        {
            auto lock = channel_->mutex.lock().unwrap();
            auto &control = channel_->control;
            if (!channel_->ready || channel_->packet.is_some() ||
                (windowed_ && (control.suspended || !control.extent.width || !control.extent.height))) return Ok(empty{});
        }
        auto plan = session_.plan_frame();
        if (plan.is_err()) {
            if (plan.unwrap_err() == FrameError::Busy) return Ok(empty{});
            events_.push({*pending_, FrameEventType::Failed, {}, 0, {}, {}, Some(plan.unwrap_err())});
            pending_ = None();
            return Ok(empty{});
        }
        auto value = rstd::move(plan).unwrap();
        if (!value.concurrent_rendering()) {
            events_.push({*pending_, FrameEventType::Failed, Some(value.identity()), 0, {}, {}, Some(FrameError::ThreadUnsafe)});
            pending_ = None();
            return Ok(empty{});
        }
        auto id = *pending_;
        leases_.push({id, FramePlan(value)});
        auto packet = Some(FramePacket{rstd::move(value), session_.last_successful_input_timings()});
        {
            auto lock = channel_->mutex.lock().unwrap();
            channel_->packet = rstd::move(packet);
            channel_->request = id;
            channel_->ready = false;
        }
        pending_ = None();
        channel_->changed.notify_one();
        return Ok(empty{});
    }
    auto pump(FrameTiming timing = {}) -> Result<empty, gpu::Error> {
        return pump_with_clock([timing] { return timing; });
    }
    void close() {
        if (closing_) return;
        closing_ = true;
        if (pending_.is_some()) { events_.push({*pending_, FrameEventType::Cancelled}); pending_ = None(); }
        auto lock = channel_->mutex.lock().unwrap();
        channel_->control.close = true;
        channel_->changed.notify_one();
    }
    auto status() -> FrameLoopStatus {
        collect();
        auto result = status_.clone();
        { auto lock = channel_->mutex.lock().unwrap();
          if (channel_->packet.is_some()) result.pending = Some(Serial(channel_->request)); }
        if (pending_.is_some()) result.pending = pending_;
        result.closing |= closing_;
        return result;
    }
    auto snapshot() -> RenderSnapshot { collect(); return snapshot_; }
    auto poll_events() -> Vec<FrameEvent> {
        collect();
        return rstd::exchange(events_, Vec<FrameEvent>{});
    }
    auto wait_milliseconds(bool animating = false, FrameTiming timing = {}, Option<Serial> deadline = {}) -> Index {
        collect();
        if (!events_.is_empty()) return 0;
        if (closing_) return 1;
        if (pending_.is_some()) {
            auto lock = channel_->mutex.lock().unwrap();
            if (channel_->ready && (!windowed_ || (!channel_->control.suspended && channel_->control.extent.width && channel_->control.extent.height))) return 1;
        }
        auto target = deadline.is_some() ? deadline : timing.expected_present_ns;
        if (target.is_some()) return Index(rstd::min(Serial(1000), (*target > timing.now_ns ? *target - timing.now_ns + 999'999 : 0) / 1'000'000));
        return animating ? 16 : 1000;
    }
};
} // namespace vui::host
