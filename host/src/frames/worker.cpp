module vui.host;
import :frames.worker;
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
    detail::finish_pending_request(pending_, events_, FrameEventType::Skipped);
    pending_ = Some(++next_request_);
    return Ok(next_request_);
}

auto ThreadedFrames::change_surface(gpu::SurfaceHost *host, Serial generation) -> Result<empty, gpu::Error> {
    if (worker_.is_none() || closing_ || (host && !generation))
        return Err(gpu::Error{gpu::Stage::State});
    collect();
    detail::finish_pending_request(pending_, events_, FrameEventType::Cancelled);
    auto lock = channel_->mutex.lock().unwrap();
    if (channel_->stopped || channel_->control.surface_request == ~Serial{})
        return Err(gpu::Error{gpu::Stage::State});
    if (channel_->packet.is_some()) {
        channel_->events.push({channel_->request, FrameEventType::Cancelled});
        channel_->released.push(Serial(channel_->request));
        channel_->packet = None();
    }
    auto request = ++channel_->control.surface_request;
    channel_->control.replacement = host;
    channel_->control.surface_generation = generation;
    channel_->control.suspended = true;
    channel_->control.extent = {};
    ++channel_->control.revision;
    channel_->ready = false;
    channel_->changed.notify_all();
    while (channel_->surface_completed < request && !channel_->stopped)
        channel_->changed.wait(lock);
    if (channel_->surface_completed != request)
        return Err(gpu::Error{gpu::Stage::State});
    if (channel_->surface_error.is_some())
        return Err(*channel_->surface_error);
    return Ok(empty{});
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

auto ThreadedFrames::enqueue_capture(Option<CaptureSelection> selection) -> Result<Serial, FrameRequestError> {
    collect();
    if (closing_ || status_.closing || status_.closed) return Err(FrameRequestError::Closing);
    if (capture_pending_) return Err(FrameRequestError::Busy);
    if (next_capture_ == ~Serial{}) return Err(FrameRequestError::Limit);
    auto lock = channel_->mutex.lock().unwrap();
    if (channel_->stopped || channel_->status.closing) return Err(FrameRequestError::Closing);
    if (channel_->status.fault.is_some()) return Err(FrameRequestError::Faulted);
    channel_->capture = Some(CaptureRequest{++next_capture_, selection});
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
    auto planned = detail::plan_frame_packet(session_);
    if (planned.is_err()) {
        auto error = planned.unwrap_err();
        if (error == FrameError::Busy) return Ok(empty{});
        events_.push({*pending_, FrameEventType::Failed, {}, 0, {}, {}, Some(error)});
        pending_ = None();
        return Ok(empty{});
    }
    auto packet = rstd::move(planned).unwrap();
    if (!packet.plan.concurrent_rendering()) {
        events_.push({*pending_, FrameEventType::Failed, Some(packet.plan.identity()), 0, {}, {}, Some(FrameError::ThreadUnsafe)});
        pending_ = None();
        return Ok(empty{});
    }
    auto id = *pending_;
    leases_.push({id, FramePlan(packet.plan), Some(UiFrame(*packet.view))});
    packet.frozen = true;
    {
        auto lock = channel_->mutex.lock().unwrap();
        channel_->packet = Some(rstd::move(packet));
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
    detail::finish_pending_request(pending_, events_, FrameEventType::Cancelled);
    auto lock = channel_->mutex.lock().unwrap();
    if (worker_.is_none() && channel_->capture.is_some()) {
        events_.push(capture_failed(channel_->capture->id));
        channel_->capture = None();
        capture_pending_ = 0;
    }
    channel_->control.close = true;
    channel_->changed.notify_one();
}

auto ThreadedFrames::status() -> FrameLoopStatus {
    collect();
    auto result = status_.clone();
    { auto lock = channel_->mutex.lock().unwrap();
      if (channel_->packet.is_some()) result.pending = Some(Serial(channel_->request));
      else if (channel_->working.is_some() && result.active.is_none()) result.pending = channel_->working; }
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
    auto target = deadline;
    if (animating && timing.expected_present_ns.is_some() && *timing.expected_present_ns > timing.now_ns &&
        (target.is_none() || *timing.expected_present_ns < *target))
        target = timing.expected_present_ns;
    if (target.is_some()) return Index(rstd::min(Serial(1000), (*target > timing.now_ns ? *target - timing.now_ns + 999'999 : 0) / 1'000'000));
    return animating ? 16 : 1000;
}
} // namespace vui::host
