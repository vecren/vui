export module vui.testing;
export import vui;

export namespace vui::testing {
using namespace rstd::prelude;

enum class TestStage { Advance, Clock, Limit, NotFound, Ambiguous, Action, Pointer };

struct TestError {
    TestStage stage;
    Option<SessionError> session;
    Option<InputError> input;
};

class DifferentialTester;

class UiTest {
    friend class DifferentialTester;
    UiSession &session_;
    SurfaceInfo surface_;
    Serial now_;
    Serial sample_clock_{new_session()};

  public:
    static constexpr Serial frame_interval_ns = 16'666'667;

    explicit UiTest(UiSession &session, SurfaceInfo surface, Serial now = 0)
        : session_(session), surface_(surface), now_(now) {}
    UiTest(const UiTest &) = delete;
    auto operator=(const UiTest &) -> UiTest & = delete;

    auto now_ns() const -> Serial { return now_; }
    void set_surface(SurfaceInfo surface) { surface_ = surface; }

    auto pump(slice<InputEvent> events = {}, Serial elapsed_ns = 0) -> Result<AdvanceResult, TestError> {
        if (elapsed_ns > ~Serial{} - now_)
            return Err(TestError{TestStage::Clock});
        now_ += elapsed_ns;
        auto advanced = session_.advance(events, {now_, {}, Some(now_)}, surface_);
        if (advanced.is_err())
            return Err(TestError{TestStage::Advance, Some(advanced.unwrap_err())});
        return Ok(advanced.unwrap());
    }

    auto pump_until_idle(Index max_frames = 120, Serial step_ns = frame_interval_ns)
        -> Result<AdvanceResult, TestError> {
        if (!step_ns)
            return Err(TestError{TestStage::Clock});
        for (Index frame{}; frame < max_frames; ++frame) {
            auto result = pump({}, frame ? step_ns : Serial{});
            if (result.is_err()) return Err(result.unwrap_err());
            if (!result->animations_active && !result->state_pending)
                return Ok(result.unwrap());
        }
        return Err(TestError{TestStage::Limit});
    }

    auto snapshot() -> SemanticsSnapshot { return session_.semantics().snapshot(); }

    auto find(ref<str> label, Option<SemanticsRole> role = {}) -> Result<SemanticsId, TestError> {
        auto tree = snapshot();
        Option<SemanticsId> found;
        for (const auto &node : tree.nodes()) {
            if (node.properties.label.as_str() != label || (role && node.properties.role != *role))
                continue;
            if (found) return Err(TestError{TestStage::Ambiguous});
            found = Some(SemanticsId(node.id));
        }
        if (!found) return Err(TestError{TestStage::NotFound});
        return Ok(*found);
    }

    auto perform(SemanticsId target, SemanticsAction action, Option<SemanticsPosition> position = {})
        -> Result<AdvanceResult, TestError> {
        auto handled = session_.semantics().perform(target, action, position);
        if (handled.is_err())
            return Err(TestError{TestStage::Action, {}, Some(handled.unwrap_err())});
        if (!handled.unwrap())
            return Err(TestError{TestStage::Action});
        return pump();
    }

    auto pointer(EventType type, Point position, Serial elapsed_ns = 0, Index index = 1,
                 DeviceId device = 1, PointerKind kind = PointerKind::Touch)
        -> Result<AdvanceResult, TestError> {
        if (type != EventType::PointerDown && type != EventType::PointerMove &&
            type != EventType::PointerUp && type != EventType::PointerCancel)
            return Err(TestError{TestStage::Pointer});
        if (elapsed_ns > ~Serial{} - now_)
            return Err(TestError{TestStage::Clock});
        InputEvent event;
        event.type = type;
        event.position = position;
        event.device = device;
        event.pointer = index;
        event.pointer_kind = kind;
        event.coordinate_generation = surface_.generation;
        if (type == EventType::PointerDown || type == EventType::PointerUp)
            event.button = PointerButton::Primary;
        event.sample_time = {sample_clock_, now_ + elapsed_ns, 1, TimeOrigin::Received};
        return pump(slice<InputEvent>::from_raw_parts(&event, usize(1)), elapsed_ns);
    }

    auto press(SemanticsId target, Index index = 1, DeviceId device = 1)
        -> Result<AdvanceResult, TestError> {
        auto tree = snapshot();
        const auto *node = tree.get(target);
        if (!node) return Err(TestError{TestStage::NotFound});
        const auto bounds = node->bounds;
        return pointer(EventType::PointerDown, {bounds.x + bounds.width / 2, bounds.y + bounds.height / 2},
                       0, index, device);
    }
};

enum class DifferentialSide { Live, Replay };
enum class DifferentialStage { Setup, Operation, Clock, Layout, Semantics, HitTest, Frame, Clip, Draw };

struct DifferentialError {
    DifferentialStage stage;
    usize step;
    Option<DifferentialSide> side;
    Option<TestError> operation;
    Option<UiError> setup;
    Option<FrameError> frame;
    Option<usize> index;
    Option<Index> pass;
    Option<Point> point;
};

class DifferentialFixture {
    using Access = rstd::dyn<rstd::FnMut<UiSession &()>>;
    Box<Access> owner_;
    explicit DifferentialFixture(Box<Access> owner) : owner_(rstd::move(owner)) {}
    auto session() -> UiSession & { return (*owner_)(); }
    friend class DifferentialTester;

  public:
    // The fixture exposes session() and owns all state needed by that session.
    template <class T> static auto own(Box<T> fixture) -> DifferentialFixture {
        return DifferentialFixture(Box<Access>::make(
            [fixture = rstd::move(fixture)]() mutable -> UiSession & { return fixture->session(); }));
    }
};

class DifferentialTester {
    using Factory = rstd::dyn<rstd::FnMut<Result<DifferentialFixture, UiError>(DifferentialSide)>>;
    using ReplayOperation = rstd::dyn<rstd::Fn<Result<AdvanceResult, TestError>(UiTest &)>>;
    struct Instance {
        DifferentialFixture fixture;
        UiTest test;
        Instance(DifferentialFixture owner, SurfaceInfo surface)
            : fixture(rstd::move(owner)), test(fixture.session(), surface) {}
    };

    Box<Factory> factory_;
    SurfaceInfo initial_surface_;
    Box<Instance> live_;
    Vec<Box<ReplayOperation>> history_;
    Option<DifferentialError> failure_;

    DifferentialTester(Box<Factory> factory, SurfaceInfo surface, Box<Instance> live)
        : factory_(rstd::move(factory)), initial_surface_(surface), live_(rstd::move(live)) {}
    static auto create_impl(Box<Factory>, SurfaceInfo) -> Result<DifferentialTester, DifferentialError>;
    static auto start(Box<Factory> &, SurfaceInfo, DifferentialSide)
        -> Result<Box<Instance>, DifferentialError>;
    auto apply(Box<ReplayOperation>) -> Result<empty, DifferentialError>;

  public:
    // Construct a fresh fixture on every call. Each side may use a different implementation.
    template <class F>
    static auto create(SurfaceInfo surface, F factory) -> Result<DifferentialTester, DifferentialError> {
        return create_impl(Box<Factory>::make(rstd::move(factory)), surface);
    }

    auto snapshot() const -> SemanticsSnapshot;
    auto now_ns() const -> Serial { return live_->test.now_ns(); }
    // Payloads must use replayable values, as session-issued tokens differ across fixtures.
    auto pump(slice<InputEvent> events = {}, Serial elapsed_ns = 0) -> Result<empty, DifferentialError>;
    auto advance(Serial elapsed_ns) -> Result<empty, DifferentialError> { return pump({}, elapsed_ns); }
    auto pointer(EventType type, Point position, Serial elapsed_ns = 0, Index index = 1,
                 DeviceId device = 1, PointerKind kind = PointerKind::Touch) -> Result<empty, DifferentialError>;
    auto perform(ref<str> label, SemanticsAction action, Option<SemanticsRole> role = {},
                 Option<SemanticsPosition> position = {}) -> Result<empty, DifferentialError>;
    auto resize(SurfaceInfo surface) -> Result<empty, DifferentialError>;
    auto settle(Index max_frames = 120, Serial step_ns = UiTest::frame_interval_ns) -> Result<empty, DifferentialError>;
    auto verify(slice<Point> probes = {}) -> Result<empty, DifferentialError>;
};
} // namespace vui::testing
