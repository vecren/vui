module vui.testing;

namespace vui::testing {
namespace {
struct RecordedEvent {
    InputEvent event;
    Vec<PointerSample> samples;
};

template <class T, class Equal>
auto first_difference(slice<T> live, slice<T> replay, Equal equal) -> Option<usize> {
    const auto count = rstd::min(live.len(), replay.len());
    for (usize i{}; i < count; ++i)
        if (!equal(live[i], replay[i])) return Some(i);
    return live.len() == replay.len() ? None<usize>() : Some(usize(count));
}

auto ordinal(const UiUpdate &view, Option<UiId> id) -> Option<usize> {
    if (!id) return None();
    const auto *node = view.get(*id);
    return node ? Some(usize(node - &view.visuals()[usize()])) : None<usize>();
}

auto same_node(const UiUpdate &live, const UiVisual &a, const UiUpdate &replay, const UiVisual &b) -> bool {
    if (a.properties != b.properties || a.layout_rect != b.layout_rect || a.layout_bounds != b.layout_bounds ||
        a.layout_slot != b.layout_slot || a.viewport != b.viewport || a.text_input_area != b.text_input_area ||
        a.content_geometry.size != b.content_geometry.size ||
        a.content_geometry.local_to_node != b.content_geometry.local_to_node ||
        a.hit_test_visible != b.hit_test_visible || a.hit_self != b.hit_self || a.enabled != b.enabled ||
        a.focusable != b.focusable || ordinal(live, a.parent) != ordinal(replay, b.parent))
        return false;
    return !first_difference(a.children.as_slice(), b.children.as_slice(), [&](UiId x, UiId y) {
        return ordinal(live, Some(x)) == ordinal(replay, Some(y));
    });
}

auto semantic_node(Option<SemanticsId> value) -> Option<UiId> {
    return value ? Some(UiId(value->node)) : None<UiId>();
}

auto same_semantics(const UiUpdate &live, const SemanticsNode &a, const UiUpdate &replay, const SemanticsNode &b) -> bool {
    const auto &x = a.properties;
    const auto &y = b.properties;
    return ordinal(live, Some(UiId(a.id.node))) == ordinal(replay, Some(UiId(b.id.node))) &&
           ordinal(live, semantic_node(a.parent)) == ordinal(replay, semantic_node(b.parent)) &&
           a.bounds == b.bounds && a.enabled == b.enabled && a.focused == b.focused &&
           x.role == y.role && x.label.as_str() == y.label.as_str() && x.value.as_str() == y.value.as_str() &&
           x.actions == y.actions && x.checked == y.checked && x.collection == y.collection &&
           x.merge_descendants == y.merge_descendants && x.exclude_subtree == y.exclude_subtree;
}

auto same_hit(const UiUpdate &live, const Option<HitResult> &a,
              const UiUpdate &replay, const Option<HitResult> &b) -> bool {
    if (!a || !b) return a.is_some() == b.is_some();
    if (a->local != b->local || ordinal(live, Some(UiId(a->target))) != ordinal(replay, Some(UiId(b->target))))
        return false;
    return !first_difference(a->path.as_slice(), b->path.as_slice(), [&](UiId x, UiId y) {
        return ordinal(live, Some(x)) == ordinal(replay, Some(y));
    });
}

auto same_text(const TextLayout &a, const TextLayout &b) -> bool {
    if (a.text() != b.text() || a.logical_bounds() != b.logical_bounds() || a.ink_bounds() != b.ink_bounds() ||
        a.baseline() != b.baseline() || a.hidden_range() != b.hidden_range())
        return false;
    return !first_difference(a.fonts(), b.fonts(), [](const FontInstance &x, const FontInstance &y) {
        return x.name() == y.name();
    }) && !first_difference(a.glyphs(), b.glyphs(), [](const TextGlyph &x, const TextGlyph &y) {
        return x.font == y.font && x.glyph == y.glyph && x.byte_start == y.byte_start && x.byte_end == y.byte_end &&
               x.line == y.line && x.position == y.position && x.advance == y.advance &&
               x.drawable == y.drawable && x.missing == y.missing && x.synthetic == y.synthetic;
    }) && !first_difference(a.lines(), b.lines(), [](const TextLine &x, const TextLine &y) {
        return x.byte_start == y.byte_start && x.byte_end == y.byte_end &&
               x.glyph_start == y.glyph_start && x.glyph_end == y.glyph_end && x.logical == y.logical &&
               x.ink == y.ink && x.baseline == y.baseline && x.ascent == y.ascent && x.descent == y.descent;
    });
}

auto same_image(const ImageData &a, const ImageData &b) -> bool {
    if (a.id() == b.id()) return true;
    if (a.width() != b.width() || a.height() != b.height() || a.density() != b.density() ||
        a.texture_options() != b.texture_options() || a.mip_count() != b.mip_count())
        return false;
    for (Index level{}; level < a.mip_count(); ++level)
        if (a.mip_level(level).unwrap().pixels != b.mip_level(level).unwrap().pixels) return false;
    return true;
}

auto same_external(TextureFrameInfo a, TextureFrameInfo b) -> bool {
    return a.width == b.width && a.height == b.height && a.format == b.format &&
           a.color_space == b.color_space && a.alpha == b.alpha && a.origin == b.origin;
}

auto same_draw(const FrameDraw &a, const FrameDraw &b) -> bool {
    if (a.is_Shape() && b.is_Shape()) {
        const auto &x = a.as_Shape(), &y = b.as_Shape();
        return x.shape == y.shape && x.color == y.color && x.border_width == y.border_width &&
               x.transform == y.transform && x.clip == y.clip;
    }
    if (a.is_BackdropBlur() && b.is_BackdropBlur()) {
        const auto &x = a.as_BackdropBlur(), &y = b.as_BackdropBlur();
        return x.mask == y.mask && x.sigma == y.sigma && x.transform == y.transform && x.clip == y.clip;
    }
    if (a.is_Text() && b.is_Text()) {
        const auto &x = a.as_Text(), &y = b.as_Text();
        return x.origin == y.origin && x.color == y.color && x.transform == y.transform && x.clip == y.clip &&
               x.effects == y.effects && x.colors == y.colors && x.tint == y.tint && same_text(x.layout, y.layout);
    }
    if (a.is_Image() && b.is_Image()) {
        const auto &x = a.as_Image(), &y = b.as_Image();
        return x.destination == y.destination && x.source == y.source && x.sampling == y.sampling &&
               x.tint == y.tint && x.edges == y.edges && x.clamp_source == y.clamp_source &&
               x.transform == y.transform && x.clip == y.clip && x.blend == y.blend && x.radial == y.radial &&
               x.node_to_surface == y.node_to_surface && same_image(x.data, y.data);
    }
    if (a.is_External() && b.is_External()) {
        const auto &x = a.as_External(), &y = b.as_External();
        return x.destination == y.destination && x.source == y.source && x.sampling == y.sampling &&
               x.transform == y.transform && x.clip == y.clip && x.tint == y.tint && x.blend == y.blend &&
               same_external(x.frame.info(), y.frame.info());
    }
    if (a.is_Composite() && b.is_Composite()) {
        const auto &x = a.as_Composite(), &y = b.as_Composite();
        return x.source_pass == y.source_pass && x.opacity == y.opacity && x.clip == y.clip;
    }
    return false;
}
} // namespace

auto DifferentialTester::start(Box<Factory> &factory, SurfaceInfo surface, DifferentialSide side)
    -> Result<Box<Instance>, DifferentialError> {
    auto fixture = (*factory)(side);
    if (fixture.is_err())
        return Err(DifferentialError{.stage = DifferentialStage::Setup, .side = Some(side), .setup = Some(fixture.unwrap_err())});
    auto instance = Box<Instance>::make(fixture.unwrap(), surface);
    auto first = instance->test.pump();
    if (first.is_err())
        return Err(DifferentialError{.stage = DifferentialStage::Operation, .side = Some(side), .operation = Some(first.unwrap_err())});
    return Ok(rstd::move(instance));
}

auto DifferentialTester::create_impl(Box<Factory> factory, SurfaceInfo surface)
    -> Result<DifferentialTester, DifferentialError> {
    auto live = start(factory, surface, DifferentialSide::Live);
    if (live.is_err()) return Err(live.unwrap_err());
    auto tester = DifferentialTester(rstd::move(factory), surface, live.unwrap());
    auto verified = tester.verify();
    if (verified.is_err()) return Err(verified.unwrap_err());
    return Ok(rstd::move(tester));
}

auto DifferentialTester::snapshot() const -> SemanticsSnapshot {
    return live_->test.session_.semantics().snapshot();
}

auto DifferentialTester::apply(Box<ReplayOperation> operation) -> Result<empty, DifferentialError> {
    if (failure_) return Err(DifferentialError(*failure_));
    history_.push(rstd::move(operation));
    auto advanced = (*history_[history_.len() - usize(1)])(live_->test);
    auto checked = advanced.is_ok()
        ? verify() : Result<empty, DifferentialError>(Err(DifferentialError{
            .stage = DifferentialStage::Operation, .step = history_.len(), .side = Some(DifferentialSide::Live),
            .operation = Some(advanced.unwrap_err())}));
    if (checked.is_err()) {
        auto error = checked.unwrap_err();
        failure_ = Some(error);
        return Err(rstd::move(error));
    }
    return checked;
}

auto DifferentialTester::pump(slice<InputEvent> events, Serial elapsed_ns) -> Result<empty, DifferentialError> {
    Vec<RecordedEvent> inputs;
    inputs.reserve(events.len());
    for (const auto &event : events) {
        auto copy = event;
        copy.history = {};
        inputs.push({rstd::move(copy), Vec<PointerSample>::from(event.history)});
    }
    return apply(Box<ReplayOperation>::make([inputs = rstd::move(inputs), elapsed_ns](UiTest &test) {
        Vec<InputEvent> batch;
        batch.reserve(inputs.len());
        for (const auto &record : inputs) {
            auto event = record.event;
            event.history = record.samples.as_slice();
            batch.push(rstd::move(event));
        }
        return test.pump(batch.as_slice(), elapsed_ns);
    }));
}

auto DifferentialTester::pointer(EventType type, Point position, Serial elapsed_ns, Index index,
                                 DeviceId device, PointerKind kind) -> Result<empty, DifferentialError> {
    return apply(Box<ReplayOperation>::make([=](UiTest &test) {
        return test.pointer(type, position, elapsed_ns, index, device, kind);
    }));
}

auto DifferentialTester::perform(ref<str> label, SemanticsAction action, Option<SemanticsRole> role,
                                 Option<SemanticsPosition> position) -> Result<empty, DifferentialError> {
    return apply(Box<ReplayOperation>::make(
        [name = String::make(label), action, role, position](UiTest &test) -> Result<AdvanceResult, TestError> {
            auto id = test.find(name.as_str(), role);
            if (id.is_err()) return Err(id.unwrap_err());
            return test.perform(id.unwrap(), action, position);
        }));
}

auto DifferentialTester::resize(SurfaceInfo surface) -> Result<empty, DifferentialError> {
    return apply(Box<ReplayOperation>::make([surface](UiTest &test) {
        test.set_surface(surface);
        return test.pump();
    }));
}

auto DifferentialTester::settle(Index max_frames, Serial step_ns) -> Result<empty, DifferentialError> {
    return apply(Box<ReplayOperation>::make([max_frames, step_ns](UiTest &test) {
        return test.pump_until_idle(max_frames, step_ns);
    }));
}

auto DifferentialTester::verify(slice<Point> probes) -> Result<empty, DifferentialError> {
    if (failure_) return Err(DifferentialError(*failure_));
    auto started = start(factory_, initial_surface_, DifferentialSide::Replay);
    if (started.is_err()) return Err(started.unwrap_err());
    auto replay = started.unwrap();
    for (usize i{}; i < history_.len(); ++i) {
        auto result = (*history_[i])(replay->test);
        if (result.is_err())
            return Err(DifferentialError{.stage = DifferentialStage::Operation, .step = i + usize(1),
                .side = Some(DifferentialSide::Replay), .operation = Some(result.unwrap_err())});
    }

    const auto step = history_.len();
    if (live_->test.now_ns() != replay->test.now_ns())
        return Err(DifferentialError{.stage = DifferentialStage::Clock, .step = step});
    auto &live_session = live_->test.session_;
    auto &replay_session = replay->test.session_;
    const auto &live_view = *live_session.visual_update();
    const auto &replay_view = *replay_session.visual_update();
    if (auto index = first_difference(live_view.visuals(), replay_view.visuals(), [&](const UiVisual &a, const UiVisual &b) {
            return same_node(live_view, a, replay_view, b);
        }))
        return Err(DifferentialError{.stage = DifferentialStage::Layout, .step = step, .index = index});

    const auto live_semantics = live_session.semantics().snapshot();
    const auto replay_semantics = replay_session.semantics().snapshot();
    if (auto index = first_difference(live_semantics.nodes(), replay_semantics.nodes(), [&](const SemanticsNode &a, const SemanticsNode &b) {
            return same_semantics(live_view, a, replay_view, b);
        }))
        return Err(DifferentialError{.stage = DifferentialStage::Semantics, .step = step, .index = index});

    auto live_input = live_session.input().snapshot();
    auto replay_input = replay_session.input().snapshot();
    if (live_input.is_some() != replay_input.is_some() ||
        ordinal(live_view, live_session.input().focused()) != ordinal(replay_view, replay_session.input().focused()))
        return Err(DifferentialError{.stage = DifferentialStage::HitTest, .step = step});
    if (live_input && replay_input) {
        auto matches = [&](Point point) {
            return same_hit(live_view, HitTest::at(*live_input, point), replay_view, HitTest::at(*replay_input, point));
        };
        for (const auto &point : probes)
            if (!matches(point))
                return Err(DifferentialError{.stage = DifferentialStage::HitTest, .step = step, .point = Some(Point(point))});
        for (const auto &node : live_view.visuals()) {
            auto transform = live_view.local_to_surface(node.id);
            if (transform.is_err()) continue;
            const auto bounds = node.layout_bounds;
            auto point = transform.unwrap().map({bounds.x + bounds.width / 2, bounds.y + bounds.height / 2});
            if (!matches(point))
                return Err(DifferentialError{.stage = DifferentialStage::HitTest, .step = step, .point = Some(point)});
        }
    }

    FramePlanner planner;
    auto live_frame = planner.build(live_session.render_scene(), live_->test.surface_, 1);
    if (live_frame.is_err())
        return Err(DifferentialError{.stage = DifferentialStage::Frame, .step = step,
            .side = Some(DifferentialSide::Live), .frame = Some(live_frame.unwrap_err())});
    auto replay_frame = planner.build(replay_session.render_scene(), replay->test.surface_, 1);
    if (replay_frame.is_err())
        return Err(DifferentialError{.stage = DifferentialStage::Frame, .step = step,
            .side = Some(DifferentialSide::Replay), .frame = Some(replay_frame.unwrap_err())});
    if (auto index = first_difference(live_frame->clips(), replay_frame->clips(), [](const ClipInstance &a, const ClipInstance &b) {
            return a.shape == b.shape && a.transform == b.transform && a.parent == b.parent;
        }))
        return Err(DifferentialError{.stage = DifferentialStage::Clip, .step = step, .index = index});
    if (live_frame->pass_count() != replay_frame->pass_count())
        return Err(DifferentialError{.stage = DifferentialStage::Frame, .step = step});
    for (Index pass{}; usize(pass) < live_frame->pass_count(); ++pass)
        if (auto index = first_difference(live_frame->draws(pass), replay_frame->draws(pass), same_draw))
            return Err(DifferentialError{.stage = DifferentialStage::Draw, .step = step, .index = index, .pass = Some(pass)});
    return Ok(empty{});
}
} // namespace vui::testing
