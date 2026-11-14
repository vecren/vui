export module vui.host:runner;
import :frames;
using namespace rstd::prelude;

export namespace vui::host {
struct HostEvents {
    Vec<InputEvent> input;
    SurfaceInfo surface;
    VkExtent2D extent{};
    bool redraw{}, close_requested{};
    Serial dropped_input{};
    Option<gpu::Error> error;
    Option<bool> minimized, suspended;
};
struct HostDriver {
    template <class Self, class Delegate = void> struct Api {
        using Trait = HostDriver;
        auto wait(Index timeout) -> HostEvents { return rstd::trait_call<0>(this, timeout); }
        auto timing() -> FrameTiming { return rstd::trait_call<1>(this); }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::wait, &T::timing>;
};
struct RunHooks {
    template <class Self, class Delegate = void> struct Api {
        using Trait = RunHooks;
        auto before_advance(const HostEvents &events) -> Result<empty, UiError> {
            return rstd::trait_call<0>(this, events);
        }
        auto after_advance(const AdvanceResult &result) -> Result<empty, UiError> {
            return rstd::trait_call<1>(this, result);
        }
        auto on_frame_event(const FrameEvent &event) -> Result<empty, UiError> {
            return rstd::trait_call<2>(this, event);
        }
        auto on_close_requested() -> bool { return rstd::trait_call<3>(this); }
    };
    template <class T>
    using Funcs = rstd::TraitFuncs<&T::before_advance, &T::after_advance, &T::on_frame_event,
                                   &T::on_close_requested>;
};
template <class B, class A, class F, class C> struct CallbackHooks {
    B before;
    A after;
    F frame;
    C close;
    auto before_advance(const HostEvents &events) -> Result<empty, UiError> {
        return before(events);
    }
    auto after_advance(const AdvanceResult &result) -> Result<empty, UiError> {
        return after(result);
    }
    auto on_frame_event(const FrameEvent &event) -> Result<empty, UiError> { return frame(event); }
    auto on_close_requested() -> bool { return close(); }
};
template <class B, class A, class F, class C> auto callback_hooks(B b, A a, F f, C c) {
    return CallbackHooks<B, A, F, C>{rstd::move(b), rstd::move(a), rstd::move(f), rstd::move(c)};
}
enum class RunStage { State, Session, Frame, Callback, Shutdown, Host };
struct RunError {
    RunStage stage;
    Option<SessionError> session;
    Option<gpu::Error> gpu;
    Option<UiError> callback;
    Option<FrameError> frame;
};
struct RunStats {
    Serial gpu_completed{}, dropped_input{};
};
template <class Frames> class SessionRunner {
    UiSession &session_;
    Frames &frames_;
    bool request_close_{}, request_redraw_{}, running_{}, finished_{};
    RunStats stats_;
    auto fail(RunError error) -> Result<RunStats, RunError> {
        return Err(rstd::move(error));
    }

  public:
    SessionRunner(UiSession &session, Frames &frames) : session_(session), frames_(frames) {}
    SessionRunner(const SessionRunner &) = delete;
    void request_close() { request_close_ = true; }
    void request_redraw() { request_redraw_ = true; }
    void set_frame_interval(Serial nanoseconds) { frames_.set_frame_interval(nanoseconds); }
    auto set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error> {
        auto previous = frames_.requested_present_mode();
        auto configured = frames_.set_present_mode(mode);
        if (configured.is_ok() && previous != mode) request_redraw();
        return configured;
    }
    auto stats() const -> RunStats { return stats_; }
    template <class Driver, class Hooks>
        requires rstd::Impled<Driver, HostDriver> && rstd::Impled<Hooks, RunHooks>
    auto run(Driver &driver, Hooks &hooks) -> Result<RunStats, RunError> {
        if (running_ || finished_)
            return Err(RunError{RunStage::State});
        running_ = true;
        struct Guard {
            bool &running, &finished;
            ~Guard() {
                running = false;
                finished = true;
            }
        } guard{running_, finished_};
        auto execute = [&]() -> Result<RunStats, RunError> {
            bool dirty{true}, closing{}, animating{};
            decltype(auto) platform = rstd::as<HostDriver>(driver);
            decltype(auto) callbacks = rstd::as<RunHooks>(hooks);
            for (;;) {
                auto timing = platform.timing();
                auto timeout = dirty && !closing
                                   ? 0
                                   : frames_.wait_milliseconds(animating, timing, session_.next_tick());
                if ((request_close_ || request_redraw_) && !closing)
                    timeout = 0;
                auto events = platform.wait(timeout);
                if (events.error.is_some())
                    return fail({RunStage::Host, {}, rstd::move(events.error)});
                dirty = dirty || rstd::exchange(request_redraw_, false);
                stats_.dropped_input +=
                    rstd::min(events.dropped_input, ~Serial{} - stats_.dropped_input);
                if ((events.close_requested || request_close_) && !closing) {
                    request_close_ = false;
                    if (callbacks.on_close_requested()) {
                        closing = true;
                        auto closed = session_.close();
                        if (closed.is_err())
                            return fail({RunStage::Session, Some(closed.unwrap_err())});
                        frames_.close();
                    }
                }
                if (!closing) {
                    auto before = callbacks.before_advance(events);
                    if (before.is_err())
                        return fail({RunStage::Callback, {}, {}, Some(before.unwrap_err())});
                    auto advanced =
                        session_.advance(events.input.as_slice(), platform.timing(), events.surface);
                    if (advanced.is_err())
                        return fail({RunStage::Session, Some(advanced.unwrap_err())});
                    auto result = advanced.unwrap();
                    animating = result.animations_active;
                    auto after = callbacks.after_advance(result);
                    if (after.is_err())
                        return fail({RunStage::Callback, {}, {}, Some(after.unwrap_err())});
                    for (const auto &mutation : session_.poll_mutations())
                        if (mutation.error.is_some())
                            return fail({RunStage::Callback, {}, {}, mutation.error});
                    frames_.set_suspended(events.suspended.unwrap_or(false));
                    frames_.set_surface(events.extent, events.surface.generation);
                    dirty = dirty || events.redraw || result.frame_needed;
                    if (dirty) {
                        auto requested = frames_.request();
                        if (requested.is_ok())
                            dirty = false;
                        else if (requested.unwrap_err() != FrameRequestError::Busy)
                            return fail({RunStage::Frame});
                    }
                }
                auto pumped = frames_.pump_with_clock([&] { return platform.timing(); });
                if (pumped.is_err())
                    return fail({RunStage::Frame, {}, Some(pumped.unwrap_err())});
                auto stopped = frames_.status().closed;
                for (const auto &event : frames_.poll_events()) {
                    if (event.type == FrameEventType::Failed)
                        return fail({RunStage::Frame, {}, event.error.clone(), {}, event.plan_error});
                    if (event.present.is_some() && *event.present != gpu::PresentStatus::Queued)
                        dirty = true;
                    if (event.type == FrameEventType::Skipped && event.plan_error == Some(FrameError::InvalidSurface)) dirty = true;
                    if (event.type == FrameEventType::GpuComplete) {
                        ++stats_.gpu_completed;
                        auto view = event.completed_view();
                        if (view && !closing) {
                            auto accepted = session_.complete_frame(rstd::move(*view));
                            if (accepted.is_err()) return fail({RunStage::Frame, {}, {}, {}, Some(accepted.unwrap_err())});
                        }
                    }
                    auto observed = callbacks.on_frame_event(event);
                    if (observed.is_err())
                        return fail({RunStage::Callback, {}, {}, Some(observed.unwrap_err())});
                }
                if (stopped) break;
            }
            return Ok(stats_);
        };
        auto result = execute();
        if (result.is_err()) {
            (void)session_.close();
            frames_.close();
            while (!frames_.status().closed) {
                (void)frames_.pump_with_clock([&] { return rstd::as<HostDriver>(driver).timing(); });
                (void)frames_.poll_events();
                (void)rstd::as<HostDriver>(driver).wait(1);
            }
        }
        return result;
    }
};
class Runner {
    FrameLoop frames_;
    SessionRunner<FrameLoop> runner_;
  public:
    Runner(UiSession &session, gpu::Renderer &renderer, bool windowed = true)
        : frames_(session, renderer, windowed), runner_(session, frames_) {}
    void request_close() { runner_.request_close(); }
    void request_redraw() { runner_.request_redraw(); }
    void set_frame_interval(Serial value) { runner_.set_frame_interval(value); }
    auto set_present_mode(gpu::PresentMode mode) -> Result<empty, gpu::Error> { return runner_.set_present_mode(mode); }
    auto stats() const -> RunStats { return runner_.stats(); }
    template <class Driver, class Hooks>
    auto run(Driver &driver, Hooks &hooks) -> Result<RunStats, RunError> { return runner_.run(driver, hooks); }
};
} // namespace vui::host
