module vui;
import :runtime.session;
using namespace rstd::prelude;

namespace vui {
namespace {
struct SubcompositionProgress {
    struct Entry { UiId target; Serial request{}; unsigned changes{}; };
    Vec<Entry> entries;
    auto observe(const Vec<SubcompositionRequest> &requests) -> Result<bool, UiError> {
        bool progressed{};
        for (const auto &request : requests) {
            Entry *found{};
            for (auto &entry : entries) if (entry.target == request.target) { found = &entry; break; }
            if (!found) {
                if (entries.len() >= usize(4096)) return Err(UiError::Limit);
                entries.push(Entry{request.target, request.id, 1});
                progressed = true;
            } else if (found->request != request.id) {
                if (++found->changes > 16) return Err(UiError::Limit);
                found->request = request.id;
                progressed = true;
            }
        }
        return Ok(progressed);
    }
};
}

auto UiSession::begin_input_timings(slice<InputEvent> events) -> Option<InputTimings::Data> {
    if (input_sequence_ == ~Serial{} || successful_input_sequence_ == ~Serial{})
        return None();
    InputTimings::Data result;
    result.batch.input_count = events.len();
    for (usize i{}; i < events.len(); ++i) {
        if (events[i].stamp.is_none()) {
            ++result.batch.untraced;
            continue;
        }
        if (result.samples.len() >= usize(1024)) {
            ++result.batch.omitted;
            continue;
        }
        result.samples.push({i, *events[i].stamp, {}, {}, false});
        auto &sample = result.samples[result.samples.len() - usize(1)];
        sample.sample_time = events[i].sample_time;
        sample.age_at_receipt_ns = events[i].age_at_receipt_ns;
        sample.history_count = events[i].history.len().to_primitive();
        sample.history_lost = events[i].history_lost;
    }
    if (result.samples.is_empty())
        return None();
    result.batch.sequence = ++input_sequence_;
    return Some(rstd::move(result));
}

void UiSession::finish_input_timings(Option<InputTimings::Data> data, Option<SessionStage> error,
                          bool has_events) {
    active_inputs_ = nullptr;
    last_inputs_ = None();
    if (has_events)
        applied_inputs_ = None();
    if (data.is_none())
        return;
    auto value = data.unwrap();
    auto &batch = value.batch;
    batch.document = document_.session();
    batch.scene_session = render_session_;
    batch.ui_revision = document_.revision();
    batch.render_revision = endpoint_.scene().revision();
    batch.finished = rstd::time::Instant::now();
    batch.error = error;
    if (error.is_none())
        batch.successful_sequence = ++successful_input_sequence_;
    last_inputs_ = Some(InputTimings(rstd::move(value)));
    if (error.is_none())
        applied_inputs_ = Some(InputTimings(*last_inputs_));
}

auto UiSession::observe_content() -> bool {
    Vec<ContentObservation> next;
    bool changed{};
    for (const auto &visual : update_->visuals()) {
        for (const auto &op : visual.content.operations()) {
            if (!op.is_External())
                continue;
            const auto &source = op.as_External().source;
            bool duplicate{};
            for (const auto &observed : next)
                duplicate |= observed.source.id() == source.id();
            if (duplicate)
                continue;
            bool unchanged{};
            Option<ContentSubscription> subscription;
            for (auto &observed : content_) {
                if (observed.source.id() != source.id())
                    continue;
                unchanged = observed.revision == source.revision();
                subscription = rstd::move(observed.subscription);
                break;
            }
            if (source.closed())
                subscription = None();
            else if (subscription.is_none() && frame_waker_.is_some())
                subscription = Some(source.subscribe(*frame_waker_).unwrap());
            changed |= !unchanged;
            next.push(ContentObservation{source, source.revision(), rstd::move(subscription)});
        }
    }
    changed |= next.len() != content_.len();
    content_ = rstd::move(next);
    return changed;
}

auto UiSession::resume_subcomposition(UiError error) -> Result<bool, SessionError> {
    if (error != UiError::LayoutPending || document_.pending_subcompositions() == usize()) return Ok(false);
    auto dispatched = document_.dispatch_subcompositions();
    if (dispatched.is_err()) return Err(SessionError{SessionStage::Update, Some(dispatched.unwrap_err())});
    return Ok(dispatched.unwrap() != usize() || updates_.pending());
}

auto UiSession::settle_input(Size size, Serial sample_time, UpdateStats &stats)
    -> Result<empty, SessionError> {
    SubcompositionProgress progress;
    for (unsigned iteration{}; iteration < 16;) {
        ++iteration;
        if (mutations_.len() + document_.pending_mutations() > usize(4096))
            return Err(SessionError{SessionStage::Backpressure});
        auto mutations = document_.flush_mutations();
        if (mutations.is_err())
            return Err(SessionError{SessionStage::Update, Some(mutations.unwrap_err())});
        for (auto &result : mutations.unwrap())
            mutations_.push(rstd::move(result));
        auto sampled = document_.sample_animations(sample_time);
        if (sampled.is_err()) {
            SessionError e{SessionStage::Animation};
            e.animation = Some(sampled.unwrap_err());
            return Err(rstd::move(e));
        }
        auto driven = motion_.sample({sample_time, {}});
        if (driven.is_err())
            return Err(SessionError{SessionStage::Animation});
        auto callbacks = document_.dispatch_animation_callbacks();
        if (callbacks.is_err()) {
            SessionError e{SessionStage::Animation};
            e.animation = Some(callbacks.unwrap_err());
            return Err(rstd::move(e));
        }
        CpuTimer geometry(active_timings_ ? &active_timings_->geometry : nullptr);
        auto state = state_bindings_.drain(true);
        if (state.is_err())
            return Err(SessionError{SessionStage::Update, Some(state.unwrap_err())});
        auto composed = updates_.drain(*this);
        if (composed.is_err()) return Err(SessionError{SessionStage::Update, Some(composed.unwrap_err())});
        auto next = document_.input_snapshot(size, {active_timings_ != nullptr});
        geometry.pause();
        stats.add(document_.input_stats());
        if (active_timings_)
            active_timings_->work.add(document_.input_stats());
        if (active_timings_)
            if (auto timing = document_.last_update_timings(); timing.is_some()) {
                active_timings_->layout.add(timing->layout);
                active_timings_->collect.add(timing->collect);
            }
        if (next.is_err()) {
            auto observed = next.unwrap_err() == UiError::LayoutPending
                ? progress.observe(document_.subcomposition_requests()) : Result<bool, UiError>(Ok(false));
            if (observed.is_err()) {
                document_.report_subcomposition_limit();
                return Err(SessionError{SessionStage::Limit, Some(observed.unwrap_err())});
            }
            auto resumed = resume_subcomposition(next.unwrap_err());
            if (resumed.is_err()) return Err(resumed.unwrap_err());
            if (resumed.unwrap()) { if (observed.unwrap()) --iteration; continue; }
            return Err(SessionError{SessionStage::Update, Some(next.unwrap_err())});
        }
        auto snapshot = rstd::move(next).unwrap();
        auto viewport_callbacks = document_.dispatch_viewport_callbacks(snapshot);
        if (viewport_callbacks.is_err()) return Err(SessionError{SessionStage::Update, Some(viewport_callbacks.unwrap_err())});
        auto geometry_callbacks = document_.dispatch_geometry_callbacks(snapshot);
        if (geometry_callbacks.is_err()) return Err(SessionError{SessionStage::Update, Some(geometry_callbacks.unwrap_err())});
        auto revision = snapshot.revision();
        auto synced = input_.synchronize(rstd::move(snapshot));
        if (synced.is_err())
            return Err(SessionError{SessionStage::Input, {}, Some(synced.unwrap_err())});
        if (revision == document_.revision() && document_.pending_mutations() == usize() &&
            !document_.pending_animation_callbacks() && !document_.pending_viewport_callbacks() && !document_.pending_geometry_callbacks() && !input_.pending_sync() &&
            !state_bindings_.pending() && !motion_.pending(sample_time) && !updates_.pending()) {
            auto revealed = reveal_focus(input_.snapshot().unwrap());
            if (revealed.is_err())
                return Err(revealed.unwrap_err());
            if (revealed.unwrap())
                continue;
            return Ok(empty{});
        }
    }
    if (document_.pending_subcompositions() != usize()) {
        document_.report_subcomposition_limit();
        return Err(SessionError{SessionStage::Limit, Some(UiError::Limit)});
    }
    return Err(SessionError{SessionStage::Limit});
}

auto UiSession::settle(Size size, Serial sample_time, UpdateStats &stats) -> Result<empty, SessionError> {
    SubcompositionProgress progress;
    for (unsigned iteration{}; iteration < 16;) {
        ++iteration;
        if (mutations_.len() + document_.pending_mutations() > usize(4096))
            return Err(SessionError{SessionStage::Backpressure});
        auto mutations = document_.flush_mutations();
        if (mutations.is_err())
            return Err(SessionError{SessionStage::Update, Some(mutations.unwrap_err())});
        for (auto &result : mutations.unwrap())
            mutations_.push(rstd::move(result));
        auto sampled = document_.sample_animations(sample_time);
        if (sampled.is_err()) {
            SessionError error{SessionStage::Animation};
            error.animation = Some(sampled.unwrap_err());
            return Err(rstd::move(error));
        }
        auto driven = motion_.sample({sample_time, {}});
        if (driven.is_err())
            return Err(SessionError{SessionStage::Animation});
        auto callbacks = document_.dispatch_animation_callbacks();
        if (callbacks.is_err()) {
            SessionError error{SessionStage::Animation};
            error.animation = Some(callbacks.unwrap_err());
            return Err(rstd::move(error));
        }
        auto state = state_bindings_.drain();
        if (state.is_err())
            return Err(SessionError{SessionStage::Update, Some(state.unwrap_err())});
        auto state_result = state.unwrap();
        auto composed = updates_.drain(*this);
        if (composed.is_err()) return Err(SessionError{SessionStage::Update, Some(composed.unwrap_err())});
        auto next = document_.update(size, {active_timings_ != nullptr});
        stats.add(document_.update_stats());
        if (active_timings_) {
            const auto sample = document_.last_update_timings().unwrap();
            active_timings_->update.add(sample.update);
            active_timings_->layout.add(sample.layout);
            active_timings_->paint.add(sample.paint);
            active_timings_->collect.add(sample.collect);
            active_timings_->work.add(sample.work);
        }
        if (next.is_err()) {
            auto observed = next.unwrap_err() == UiError::LayoutPending
                ? progress.observe(document_.subcomposition_requests()) : Result<bool, UiError>(Ok(false));
            if (observed.is_err()) {
                document_.report_subcomposition_limit();
                return Err(SessionError{SessionStage::Limit, Some(observed.unwrap_err())});
            }
            auto resumed = resume_subcomposition(next.unwrap_err());
            if (resumed.is_err()) return Err(resumed.unwrap_err());
            if (resumed.unwrap()) { if (observed.unwrap()) --iteration; continue; }
            return Err(SessionError{SessionStage::Update, Some(next.unwrap_err())});
        }
        update_ = Some(next.unwrap());
        auto viewport_callbacks = document_.dispatch_viewport_callbacks(*update_);
        if (viewport_callbacks.is_err()) return Err(SessionError{SessionStage::Update, Some(viewport_callbacks.unwrap_err())});
        auto geometry_callbacks = document_.dispatch_geometry_callbacks(*update_);
        if (geometry_callbacks.is_err()) return Err(SessionError{SessionStage::Update, Some(geometry_callbacks.unwrap_err())});
        auto synced = input_.synchronize(*update_);
        if (synced.is_err())
            return Err(SessionError{SessionStage::Input, {}, Some(synced.unwrap_err())});
        if (document_.revision() == update_->revision() &&
            document_.pending_mutations() == usize() &&
            !document_.pending_animation_callbacks() && !document_.pending_viewport_callbacks() && !document_.pending_geometry_callbacks() && !input_.pending_sync() &&
            (!state_result.pending || state_result.groups >= usize(16)) &&
            !motion_.pending(sample_time) && !updates_.pending()) {
            auto revealed = reveal_focus(*update_);
            if (revealed.is_err())
                return Err(revealed.unwrap_err());
            if (revealed.unwrap())
                continue;
            return Ok(empty{});
        }
    }
    if (document_.pending_subcompositions() != usize()) {
        document_.report_subcomposition_limit();
        return Err(SessionError{SessionStage::Limit, Some(UiError::Limit)});
    }
    return Err(SessionError{SessionStage::Limit});
}

UiSession::~UiSession() {
    updates_.close(*this);
    document_.close_subcompositions();
    document_.clear_phase_observations();
    document_.bindings().set_waker(None());
    state_bindings_.close();
}

auto UiSession::set_frame_waker(Option<FrameWaker> waker) -> Result<empty, SessionError> {
    if (closed_ || closing_)
        return Err(SessionError{SessionStage::Closed});
    if (advancing_ || planning_ || !document_.can_route())
        return Err(SessionError{SessionStage::Reentrant});
    if (waker.is_some() && !waker->valid())
        waker = None();
    document_.bindings().set_waker(waker.is_some() ? Some(FrameWaker(*waker))
                                                   : None<FrameWaker>());
    frame_waker_ = rstd::move(waker);
    state_bindings_.set_waker(frame_waker_.is_some() ? Some(FrameWaker(*frame_waker_))
                                                     : None<FrameWaker>());
    for (auto &observed : content_) {
        observed.subscription = None();
        if (frame_waker_.is_some() && frame_waker_->valid() && !observed.source.closed())
            observed.subscription = Some(observed.source.subscribe(*frame_waker_).unwrap());
    }
    if (frame_waker_.is_some() && content_pending())
        frame_waker_->request();
    return Ok(empty{});
}

auto UiSession::content_pending() const -> bool {
    if (closed_ || closing_)
        return false;
    for (const auto &observed : content_)
        if (observed.source.revision() != observed.revision)
            return true;
    return false;
}

auto UiSession::advance(slice<InputEvent> events, FrameTiming timing, SurfaceInfo surface,
             DiagnosticsOptions options) -> Result<AdvanceResult, SessionError> {
    if (closed_ || closing_)
        return Err(SessionError{SessionStage::Closed});
    if (advancing_ || planning_ || !document_.can_route())
        return Err(SessionError{SessionStage::Reentrant});
    AdvanceTimings sample;
    sample.requested = timing;
    active_timings_ = options.timings ? &sample : nullptr;
    CpuTimer timer(active_timings_ ? &sample.advance : nullptr);
    auto inputs = begin_input_timings(events);
    active_inputs_ = inputs.is_some() ? &*inputs : nullptr;
    input_cursor_ = usize();
    auto result = advance_impl(events, timing, surface);
    timer.pause();
    active_timings_ = nullptr;
    last_timings_ = None();
    if (options.timings) {
        sample.document = document_.session();
        sample.ui_revision = document_.revision();
        sample.render_revision = visual_.applied_revision();
    }
    if (result.is_err()) {
        auto error = result.unwrap_err();
        finish_input_timings(rstd::move(inputs), Some(error.stage), !events.is_empty());
        if (options.timings) {
            sample.error = Some(error.stage);
            sample.processed_events = error.processed_events;
            last_timings_ = Some(sample);
        }
        return Err(rstd::move(error));
    }
    finish_input_timings(rstd::move(inputs), None(), !events.is_empty());
    if (options.timings)
        last_timings_ = Some(sample);
    return result;
}

auto UiSession::advance_impl(slice<InputEvent> events, FrameTiming timing, SurfaceInfo surface)
    -> Result<AdvanceResult, SessionError> {
    usize processed{};
    auto failure = [&](SessionError error) -> Result<AdvanceResult, SessionError> {
        error.processed_events = processed;
        return Err(rstd::move(error));
    };
    if (!surface.valid())
        return failure(SessionError{SessionStage::Surface});
    if (timing.now_ns < last_time_ ||
        (timing.expected_present_ns.is_some() && *timing.expected_present_ns < timing.now_ns))
        return failure(SessionError{SessionStage::Timing});
    if (!surface_ || *surface_ != surface) input_.completed_frame_ = None();
    advancing_ = true;
    struct Guard {
        bool &flag;
        ~Guard() { flag = false; }
    } guard{advancing_};
    last_time_ = timing.now_ns;
    UpdateStats stats;
    input_.begin_input_batch();
    auto sample_time =
        timing.expected_present_ns.is_some() ? *timing.expected_present_ns : timing.now_ns;
    auto settled = events.is_empty() ? settle(surface.logical_size, sample_time, stats)
                                     : settle_input(surface.logical_size, sample_time, stats);
    if (settled.is_err())
        return failure(settled.unwrap_err());
    for (usize event_index{}; event_index < events.len();) {
        InputEvent event = events[event_index];
        auto last = event_index;
        Vec<PointerSample> history;
        while (last + usize(1) < events.len() &&
               history.len() + events[last].history.len() +
                       events[last + usize(1)].history.len() <
                   usize(512) &&
               !document_.pending_mutations().to_primitive() && !state_bindings_.pending() && !updates_.pending() &&
               input_.can_coalesce(events[last], events[last + usize(1)])) {
            for (const auto &sample : events[last].history)
                history.push(PointerSample(sample));
            history.push({events[last].sample_time, events[last].position,
                          events[last].pressure, events[last].tilt, events[last].twist});
            ++last;
        }
        if (last != event_index) {
            event = events[last];
            for (auto i = event_index; i < last; ++i)
                event.history_lost +=
                    rstd::min(events[i].history_lost, ~Serial{} - event.history_lost);
            for (const auto &sample : event.history)
                history.push(PointerSample(sample));
            event.history = history.as_slice();
        }
        auto count = last - event_index + usize(1);
        auto sample_begin = input_cursor_;
        Option<rstd::time::Instant> dispatch_started;
        if (active_inputs_) {
            while (input_cursor_ < active_inputs_->samples.len() &&
                   active_inputs_->samples[input_cursor_].index < processed + count)
                ++input_cursor_;
            if (input_cursor_ != sample_begin)
                dispatch_started = Some(rstd::time::Instant::now());
        }
        CpuTimer input_time(active_timings_ ? &active_timings_->input : nullptr);
        auto dispatched = input_.dispatch(event);
        input_time.pause();
        if (dispatch_started.is_some()) {
            auto finished = rstd::time::Instant::now();
            for (auto i = sample_begin; i < input_cursor_; ++i) {
                auto &sample = active_inputs_->samples[i];
                sample.dispatch_started = dispatch_started;
                sample.dispatch_finished = Some(rstd::time::Instant(finished));
                sample.dispatched = dispatched.is_ok();
                sample.coalesced = sample.index != last;
                if (sample.index == last)
                    sample.work = input_.work();
            }
        }
        processed += count;
        event_index = last + usize(1);
        if (active_timings_)
            active_timings_->processed_events = processed;
        if (dispatched.is_err())
            return failure(
                SessionError{SessionStage::Input, {}, Some(dispatched.unwrap_err())});
        settled = settle_input(surface.logical_size, sample_time, stats);
        if (settled.is_err())
            return failure(settled.unwrap_err());
    }
    if (!events.is_empty()) {
        settled = settle(surface.logical_size, sample_time, stats);
        if (settled.is_err())
            return failure(settled.unwrap_err());
    }
    bool changed = published_ui_ != update_->revision();
    bool surface_changed = surface_.is_none() || *surface_ != surface;
    auto published = publish_update();
    if (published.is_err()) return failure(published.unwrap_err());
    surface_ = Some(surface);
    bool content_changed = observe_content();
    return Ok(AdvanceResult{update_->revision(), visual_.applied_revision(), stats,
                            changed || surface_changed || content_changed || frame_content_.active(), processed,
                            document_.animations_active() || motion_.active() || frame_content_.active(),
                            state_bindings_.pending() || updates_.pending()});
}

auto UiSession::publish_update() -> Result<empty, SessionError> {
    bool changed = published_ui_ != update_->revision();
    if (changed) {
        auto transaction = visual_.build(*update_, {active_timings_ != nullptr});
        if (active_timings_)
            active_timings_->build.add(visual_.last_build_timings()->build);
        if (transaction.is_err())
            return Err(
                SessionError{SessionStage::Publish, {}, {}, Some(transaction.unwrap_err())});
        auto pending = Some(transaction.unwrap());
        auto enqueued = endpoint_.try_enqueue(pending);
        if (enqueued != EnqueueResult::Accepted) {
            visual_.acknowledge(ApplyEvent::Cancelled(pending->header())).unwrap();
            return Err(SessionError{SessionStage::Backpressure});
        }
        endpoint_.apply_pending({active_timings_ != nullptr});
        if (active_timings_)
            active_timings_->apply.add(endpoint_.last_apply_timings()->apply);
        auto applied = endpoint_.poll_events();
        for (const auto &event : applied) {
            if (active_inputs_ && event.is_Applied())
                active_inputs_->batch.applied = Some(rstd::time::Instant::now());
            auto acknowledged = visual_.acknowledge(event);
            if (acknowledged.is_err())
                return Err(SessionError{
                    SessionStage::Publish, {}, {}, Some(acknowledged.unwrap_err())});
            if (event.is_Rejected())
                return Err(SessionError{SessionStage::Apply,
                                            {},
                                            {},
                                            {},
                                            Some(ApplyError(event.as_Rejected().error))});
            if (event.is_Cancelled())
                return Err(SessionError{SessionStage::Closed});
        }
        published_ui_ = update_->revision();
    }
    return Ok(empty{});
}

auto UiSession::plan_frame() -> Result<FramePlan, FrameError> {
    if (closed_ || closing_ || surface_.is_none())
        return Err(FrameError::InvalidSurface);
    if (advancing_ || planning_ || !document_.can_route()) return Err(FrameError::InvalidContent);
    if (frame_id_ == ~Serial{})
        return Err(FrameError::Limit);
    planning_ = true;
    struct Guard {bool &value; ~Guard() {value = false;}} guard{planning_};
    auto time = update_->animation_sample_time();
    if (!time) return Err(FrameError::InvalidContent);
    auto refresh = [&]() -> Result<const UiUpdate *, FrameError> {
        if (document_.revision() != update_->revision() || document_.pending_mutations() != usize()) {
            UpdateStats stats;
            auto settled = settle(surface_->logical_size, *time, stats);
            if (settled.is_err()) return Err(FrameError::InvalidContent);
        }
        return Ok(&*update_);
    };
    auto initial = refresh();
    if (initial.is_err()) return Err(initial.unwrap_err());
    auto layout = update_->layout_revision();
    auto prepared = frame_content_.prepare(refresh, *surface_);
    if (prepared.is_err()) return Err(prepared.unwrap_err());
    auto refreshed = refresh();
    if (refreshed.is_err()) return Err(refreshed.unwrap_err());
    if (layout != update_->layout_revision()) return Err(FrameError::InvalidContent);
    auto published = publish_update();
    if (published.is_err()) return Err(FrameError::InvalidContent);
    observe_content();
    FramePlanner planner;
    return planner.build(endpoint_.scene(), *surface_, ++frame_id_);
}

auto UiSession::plan_ui_frame() -> Result<UiFrame, FrameError> {
    auto plan = plan_frame();
    if (plan.is_err()) return Err(plan.unwrap_err());
    auto frame = visual_.frame(rstd::move(*plan));
    if (frame.is_err()) return Err(FrameError::InvalidContent);
    return Ok(rstd::move(*frame));
}

auto UiSession::complete_frame(UiFrame frame) -> Result<bool, FrameError> {
    if (closed_ || closing_) return Ok(false);
    if (advancing_ || planning_ || !document_.can_route()) return Err(FrameError::InvalidContent);
    if (frame.update().document() != document_.session() || frame.identity().scene_session != render_session_)
        return Err(FrameError::InvalidContent);
    if (!surface_ || frame.surface() != *surface_ || frame.identity().id <= input_.completed_frame_id_) return Ok(false);
    input_.completed_frame_id_ = frame.identity().id;
    input_.completed_frame_ = Some(rstd::move(frame));
    return Ok(true);
}

auto UiSession::close() -> Result<empty, SessionError> {
    if (closed_)
        return Ok(empty{});
    if (advancing_ || planning_ || !document_.can_route())
        return Err(SessionError{SessionStage::Reentrant});
    closing_ = true;
    updates_.close(*this);
    document_.close_subcompositions();
    state_bindings_.close();
    motion_.close();
    frame_content_.close();
    auto animations = document_.close_animations();
    if (animations.is_err()) {
        SessionError error{SessionStage::Animation};
        error.animation = Some(animations.unwrap_err());
        return Err(rstd::move(error));
    }
    auto result = input_.close();
    if (result.is_err())
        return Err(SessionError{SessionStage::Input, {}, Some(result.unwrap_err())});
    for (auto &mutation : document_.cancel_mutations().unwrap())
        mutations_.push(rstd::move(mutation));
    document_.clear_phase_observations();
    document_.bindings().close();
    endpoint_.close();
    content_.clear();
    frame_waker_ = None();
    closed_ = true;
    last_timings_ = None();
    last_inputs_ = None();
    applied_inputs_ = None();
    return Ok(empty{});
}
} // namespace vui
