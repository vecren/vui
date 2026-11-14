module vui.host;
import :threaded_frames;
using namespace rstd::prelude;

namespace vui::host {
void ThreadedFrames::collect() {
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

ThreadedFrames::~ThreadedFrames() {
    close();
    join();
}

void ThreadedFrames::join() {
    if (worker_.is_some()) {
        (void)rstd::move(*worker_).join();
        worker_ = None();
        collect();
        leases_.clear();
    }
}

auto ThreadedFrames::request() -> Result<Serial, FrameRequestError> {
    collect();
    if (closing_ || status_.closing || status_.closed) return Err(FrameRequestError::Closing);
    if (status_.fault.is_some()) return Err(FrameRequestError::Faulted);
    if (events_.len() >= usize(48)) return Err(FrameRequestError::Busy);
    if (next_request_ == ~Serial{}) return Err(FrameRequestError::Limit);
    if (pending_.is_some()) events_.push({*pending_, FrameEventType::Skipped});
    pending_ = Some(++next_request_);
    return Ok(next_request_);
}

void ThreadedFrames::set_surface(VkExtent2D extent, Serial generation) {
    auto lock = channel_->mutex.lock().unwrap();
    auto &control = channel_->control;
    if (control.extent.width == extent.width && control.extent.height == extent.height && control.generation == generation) return;
    control.extent = extent; control.generation = generation; ++control.revision;
    channel_->changed.notify_one();
}

void ThreadedFrames::set_suspended(bool value) {
    auto lock = channel_->mutex.lock().unwrap();
    if (channel_->control.suspended == value) return;
    channel_->control.suspended = value; ++channel_->control.revision;
    channel_->changed.notify_one();
}

void ThreadedFrames::set_frame_interval(Serial value) {
    auto lock = channel_->mutex.lock().unwrap();
    channel_->control.interval = value; ++channel_->control.revision;
    channel_->changed.notify_one();
}

auto ThreadedFrames::set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error> {
    if (!windowed_ || closing_ || unsigned(mode) > unsigned(gpu::PresentMode::FifoRelaxed))
        return Err(gpu::Error{gpu::Stage::State});
    auto lock = channel_->mutex.lock().unwrap();
    channel_->control.mode = mode; ++channel_->control.revision;
    channel_->changed.notify_one();
    return Ok(empty{});
}

auto ThreadedFrames::request_capture() -> Result<Serial, FrameRequestError> {
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

auto ThreadedFrames::pump_pending() -> Result<empty, gpu::Error> {
    collect();
    if (status_.fault.is_some() && !closing_) return Err(*status_.fault);
    if (closing_ || status_.closing || status_.closed || pending_.is_none() || events_.len() >= usize(48)) return Ok(empty{});
    {
        auto lock = channel_->mutex.lock().unwrap();
        auto &control = channel_->control;
        if (!channel_->ready || channel_->packet.is_some() ||
            (windowed_ && (control.suspended || !control.extent.width || !control.extent.height))) return Ok(empty{});
    }
    auto frame = session_.plan_ui_frame();
    if (frame.is_err()) {
        if (frame.unwrap_err() == FrameError::Busy) return Ok(empty{});
        events_.push({*pending_, FrameEventType::Failed, {}, 0, {}, {}, Some(frame.unwrap_err())});
        pending_ = None();
        return Ok(empty{});
    }
    auto value = frame->plan();
    if (!value.concurrent_rendering()) {
        events_.push({*pending_, FrameEventType::Failed, Some(value.identity()), 0, {}, {}, Some(FrameError::ThreadUnsafe)});
        pending_ = None();
        return Ok(empty{});
    }
    auto id = *pending_;
    leases_.push({id, FramePlan(value), Some(UiFrame(*frame))});
    auto packet = Some(FramePacket{rstd::move(value), session_.last_successful_input_timings(), false, Some(rstd::move(*frame))});
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

void ThreadedFrames::close() {
    if (closing_) return;
    closing_ = true;
    if (pending_.is_some()) { events_.push({*pending_, FrameEventType::Cancelled}); pending_ = None(); }
    auto lock = channel_->mutex.lock().unwrap();
    channel_->control.close = true;
    channel_->changed.notify_one();
}

auto ThreadedFrames::status() -> FrameLoopStatus {
    collect();
    auto result = status_.clone();
    { auto lock = channel_->mutex.lock().unwrap();
      if (channel_->packet.is_some()) result.pending = Some(Serial(channel_->request)); }
    if (pending_.is_some()) result.pending = pending_;
    result.closing |= closing_;
    return result;
}

auto ThreadedFrames::wait_milliseconds(bool animating, FrameTiming timing, Option<Serial> deadline) -> Index {
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
} // namespace vui::host
