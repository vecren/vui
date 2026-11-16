export module vui.examples.game2048:view;
import vui.compose;
import vui.controls;
import vui.examples;
import :game;

export namespace game2048 {
using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace cx = vui::compose;

class View {
    struct Tile {
        Serial id;
        uint32_t value;
        unsigned cell;
        cx::NodeRef node;
    };
    struct Model {
        Vec<Tile> tiles;
        uint64_t score{};
        Option<AnimationGate> ready;
        bool won{}, over{}, busy{};
    };
    UiSession &session_;
    Binding lifetime_{session_.document().bindings().make()};
    state::Graph graph_;
    state::Cell<rstd::rc::Rc<Model>> model_{graph_.cell(rstd::rc::make_rc<Model>(),
        [](const auto &a, const auto &b) { return a.get() == b.get(); }).unwrap()};
    Option<cx::Composition> composition_;
    Game game_;
    UiId root_;
    Vec<Tile> tiles_;
    MoveResult move_;
    Option<rstd::rc::Rc<AnimationScope>> animations_;
    AnimationRun movement_;
    Option<AnimationSequenceError> animation_error_;
    Option<AnimationGate> ready_;
    cx::NodeRef input_;
    Serial next_tile_{1};
    bool busy_{}, continued_{};
    static constexpr Scalar cell = 96, gap = 12, board_size = 444;
    static constexpr Color ink{.019, .045, .095, 1};
    static constexpr Color background{.933, .953, .973, 1};

    struct GameInput {
        cx::Latest<bool> enabled;
        cx::Latest<cx::Action<void(Direction)>> on_move;
        BindingGuard lifetime;
        void handle(EventContext &context, const RoutedEvent &event) {
            if (!lifetime.alive()) return;
            if (event.input.type == EventType::PointerDown && event.target == event.current) {
                context.focus().unwrap();
                return;
            }
            if (event.input.type != EventType::KeyDown) return;
            Direction direction;
            switch (event.input.key) {
            case Key::Left: direction = Direction::Left; break;
            case Key::Right: direction = Direction::Right; break;
            case Key::Up: direction = Direction::Up; break;
            case Key::Down: direction = Direction::Down; break;
            default: return;
            }
            context.prevent_default(); context.stop_propagation();
            if (event.input.repeat) return;
            context.document().defer(event.current, lifetime,
                [enabled = enabled, callback = on_move, direction](UiDocument &) -> Result<empty, UiError> {
                    auto active = enabled.get(); auto current = callback.get();
                    if (active.is_ok() && active.unwrap().value() && current.is_ok()) current.unwrap().value()(direction);
                    return Ok(empty{});
                }).unwrap();
        }
    };
    struct GameInputDescriptor {
        using State = GameInput;
        cx::Latest<bool> enabled;
        cx::Latest<cx::Action<void(Direction)>> on_move;
        BindingGuard lifetime;
        auto create() const -> State { return {enabled, on_move, lifetime}; }
        void update(State &state, cx::BehaviorContext &) const { state.enabled = enabled; state.on_move = on_move; }
        auto attach(State &, cx::BehaviorContext &context, rstd::rc::Weak<State>) const -> cx::EffectLease {
            context.session().input().focus_when_ready(context.target(), None<ScopeId>(), context.document().lifetime()).unwrap();
            return {};
        }
    };
    struct BoardCanvas {
        auto prepare(cx::PrepareContext &context, Option<UiId> previous, NodeStyle style) -> cx::ComposeResult<UiNodeRef> {
            auto node = previous.is_some() ? context.edit().existing(*previous)
                : context.edit().create(LayoutKind::Custom, style);
            context.style(node, style);
            if (previous.is_none()) {
                context.edit().state(node, ReferenceCanvas{{500, 700}, CanvasFit::Contain,
                    {.5, .5}, true, Some(Scalar(1.2))});
                auto target = context.target();
                context.on_mount([target](cx::CommittedContext &committed) -> cx::ComposeResult<cx::EffectLease> {
                    auto resolved = committed.resolve(target);
                    if (resolved.is_err()) return Err(resolved.unwrap_err());
                    auto attached = committed.session().document().attach_layout<ReferenceCanvas>(resolved.unwrap());
                    if (attached.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(attached.unwrap_err())));
                    return Ok(cx::EffectLease{});
                });
            }
            return Ok(node);
        }
    };
    auto ui() -> UiDocument & { return session_.document(); }
    static auto position(unsigned index, Scalar scale = 1) -> Transform {
        auto inset = cell * (1 - scale) / 2;
        return {scale, 0, 0, scale, gap + (index % 4) * (cell + gap) + inset,
                gap + (index / 4) * (cell + gap) + inset};
    }
    static auto color(uint32_t value) -> Color {
        switch (value) {
        case 2:
            return {.89, .94, .98, 1};
        case 4:
            return {.75, .87, .95, 1};
        case 8:
            return {.56, .79, .89, 1};
        case 16:
            return {.40, .73, .80, 1};
        case 32:
            return {.22, .59, .70, 1};
        case 64:
            return {.15, .43, .61, 1};
        case 128:
            return {.98, .86, .55, 1};
        case 256:
            return {.96, .82, .43, 1};
        case 512:
            return {.94, .78, .37, 1};
        case 1024:
            return {.97, .68, .32, 1};
        case 2048:
            return {.98, .56, .30, 1};
        default:
            return {.39, .36, .63, 1};
        }
    }
    static auto label(ref<str> value, Scalar size, Point at = {}, bool bold = false, Color tint = ink) -> cx::Element {
        return cx::text(value).format(TextFormatBuilder{}.size(size).wrap(TextWrap::None).bold(bold).build())
            .color(tint).style(NodeStyleBuilder{}.transform({1, 0, 0, 1, at.x, at.y}).hit_test_visible(false).build());
    }
    template <class F> static auto button(ref<str> title, Scalar x, F clicked, bool visible = true) -> cx::Element {
        return cx::button(label(title, 17, {}, true, {1, 1, 1, 1})
                .style(NodeStyleBuilder{}.horizontal(Alignment::Center).vertical(Alignment::Center).hit_test_visible(false).build()),
            rstd::move(clicked), ButtonStyleBuilder{}.normal(ink).pressed({.01, .025, .05, 1})
                .focused({.035, .12, .26, 1}).radius(10).build())
            .style(NodeStyleBuilder{}.width(210).height(48).transform({1, 0, 0, 1, x, 562}).visible(visible).build());
    }
    [[vui::compose]]
    static auto tile(Tile value) -> cx::Element {
        auto number = rstd::format("{}", value.value);
        Scalar size = value.value < 128 ? 40 : value.value < 1024 ? 34 : value.value < 16384 ? 28 : 19;
        auto tint = value.value == 32 || value.value == 64 || value.value > 2048 ? Color{1, 1, 1, 1} : ink;
        return cx::stack({label(number.as_str(), size, {}, true, tint)
            .style(NodeStyleBuilder{}.horizontal(Alignment::Center).vertical(Alignment::Center).hit_test_visible(false).build())})
            .style(NodeStyleBuilder{examples::block(cell, cell, color(value.value), 10)}
                .transform(position(value.cell)).hit_test_visible(false).build())
            .reference(value.node);
    }
    [[vui::compose]]
    static auto content(cx::Borrowed<View> owner, state::Value<rstd::rc::Rc<Model>> source) -> cx::ComposeResult<cx::Element> {
        auto observed = cx::read(source);
        if (observed.is_err()) return Err(observed.unwrap_err());
        auto snapshot = observed.unwrap();
        const auto &model = *snapshot.value();
        auto animated = cx::animation_scope();
        if (animated.is_err()) return Err(animated.unwrap_err());
        auto animations = animated.unwrap();
        auto installed = cx::effect({}, [owner, animations](cx::CommittedContext &) -> cx::EffectLease {
            owner->animations_ = Some(rstd::rc::Rc<AnimationScope>(animations));
            return {};
        });
        if (installed.is_err()) return Err(installed.unwrap_err());
        auto enabled = cx::latest(!model.busy && !model.over && !model.won);
        if (enabled.is_err()) return Err(enabled.unwrap_err());
        auto on_move = cx::latest(cx::Action<void(Direction)>([owner](Direction direction) { owner->move(direction); }));
        if (on_move.is_err()) return Err(on_move.unwrap_err());
        Vec<cx::Element> cells;
        for (unsigned i{}; i < 16; ++i)
            cells.push(cx::native(LayoutKind::Leaf).style(NodeStyleBuilder{
                examples::block(cell, cell, {.82, .87, .92, 1}, 10)}.transform(position(i)).hit_test_visible(false).build()));
        for (const auto &value : model.tiles) cells.push(tile(value).key(cx::Key{value.id}));
        auto score = rstd::format("{}", model.score);
        auto message = model.over ? "没有可移动的方块了，再来一局吧。"_str
            : model.won ? "合成了 2048！还想挑战更大的数字吗？"_str : "合并相同数字，向 2048 前进。"_str;
        auto effect = cx::effect(cx::EffectKeys{cx::Key{model.ready.is_some() ? model.ready->id() : 0}},
            [ready = model.ready.is_some() ? Some(AnimationGate(*model.ready)) : None<AnimationGate>()]
            (cx::CommittedContext &) -> cx::ComposeResult<cx::EffectLease> {
                if (ready.is_some()) {
                    auto signaled = ready->signal();
                    if (signaled.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, signaled.unwrap_err().ui));
                }
                return Ok(cx::EffectLease{});
            });
        if (effect.is_err()) return Err(effect.unwrap_err());
        return Ok(cx::stack({
            label("2048"_str, 54, {0, 0}, true),
            label("得分"_str, 14, {330, 8}), label(score.as_str(), 28, {330, 28}, true),
            cx::stack(rstd::move(cells)).style(NodeStyleBuilder{
                examples::block(board_size, board_size, {.729, .788, .851, 1}, 16)}
                .transform({1, 0, 0, 1, 0, 86}).build()),
            label(message, 16, {0, 538}),
            button("重新开始"_str, 0, [owner] { owner->restart(); }),
            button("继续挑战"_str, 234, [owner] {
                owner->continued_ = true; owner->update_status();
                owner->session_.input().focus_when_ready(owner->input_.id().unwrap(), None<ScopeId>(), owner->lifetime_.guard()).unwrap();
            }, model.won),
            label("方向键移动 · Tab 切换按钮 · Esc 退出"_str, 14, {0, 628}),
        }).style(NodeStyleBuilder{examples::block(board_size, 650, {0, 0, 0, 0})}.focusable(true).build())
            .reference(owner->input_).behavior(cx::Key{1}, GameInputDescriptor{enabled.unwrap(), on_move.unwrap(), owner->lifetime_.guard()}));
    }
    void reset_tiles() {
        tiles_.clear();
        for (unsigned i{}; i < 16; ++i)
            if (auto value = game_.board().cells[i]) tiles_.push(Tile{next_tile_++, value, i, {}});
    }
    void update_status() {
        Model next;
        for (auto tile : tiles_) next.tiles.push(rstd::move(tile));
        next.score = game_.board().score;
        if (ready_.is_some()) next.ready = Some(AnimationGate(*ready_));
        next.won = game_.reached_2048() && !continued_;
        next.over = !game_.can_move();
        next.busy = busy_;
        model_.set(rstd::rc::make_rc<Model>(rstd::move(next))).unwrap();
    }
    auto pop_group() -> AnimationGroup {
        AnimationGroup group;
        for (const auto &tile : tiles_) {
            if (!move_.merged[tile.cell] && tile.cell != move_.spawned.cell) continue;
            group.push(AnimationTrack::transform(tile.node.id().unwrap(), position(tile.cell),
                {120'000'000, AnimationEasing::EaseInOut, AnimationEndPolicy::CommitFinal},
                Some(position(tile.cell, move_.merged[tile.cell] ? 1.12 : .3))));
        }
        return group;
    }
    void settle(AnimationGate ready) {
        bool occupied[16]{};
        Vec<Tile> next;
        for (unsigned i{}; i < move_.count; ++i) {
            auto movement = move_.movements[i];
            if (occupied[movement.to]) continue;
            for (const auto &old : tiles_) if (old.cell == movement.from) {
                auto value = old; value.cell = movement.to; value.value = game_.board().cells[movement.to];
                next.push(rstd::move(value)); occupied[movement.to] = true; break;
            }
        }
        next.push(Tile{next_tile_++, move_.spawned.value, move_.spawned.cell, {}});
        tiles_ = rstd::move(next); ready_ = Some(rstd::move(ready)); update_status();
    }
    void move(Direction direction) {
        if (busy_ || !game_.can_move() || (game_.reached_2048() && !continued_)) return;
        move_ = game_.move(direction); if (!move_.changed) return;
        busy_ = true;
        auto owner = cx::borrow(*this);
        AnimationGroup sliding;
        for (unsigned i{}; i < move_.count; ++i) {
            auto movement = move_.movements[i];
            if (movement.from == movement.to) continue;
            for (const auto &tile : tiles_) if (tile.cell == movement.from) {
                sliding.push(AnimationTrack::transform(tile.node.id().unwrap(), position(movement.to),
                    {130'000'000, AnimationEasing::EaseInOut, AnimationEndPolicy::CommitFinal}));
                break;
            }
        }
        AnimationGate committed;
        AnimationSequence sequence;
        sequence.parallel(rstd::move(sliding));
        sequence.then([owner, committed](UiDocument &) -> AnimationSequenceResult<empty> {
            owner->settle(committed); return Ok(empty{});
        });
        sequence.wait(committed);
        sequence.parallel([owner](UiDocument &) -> AnimationSequenceResult<AnimationGroup> {
            return Ok(owner->pop_group());
        });
        movement_ = (*animations_).get()->play(rstd::move(sequence), [owner](const AnimationRunResult &result) {
            if (!owner->lifetime_.alive()) return;
            owner->busy_ = false;
            owner->animation_error_ = result.error;
            if (result.reason != AnimationRunEndReason::Finished && result.reason != AnimationRunEndReason::ScopeClosed) {
                owner->ready_ = None();
                owner->reset_tiles();
            }
            if (result.reason != AnimationRunEndReason::ScopeClosed) owner->update_status();
        }).unwrap();
        update_status();
    }
    void restart() {
        movement_.cancel().unwrap(); ready_ = None(); animation_error_ = None(); busy_ = continued_ = false;
        game_.restart(); reset_tiles(); update_status();
    }
  public:
    View(UiSession &session, Game game) : session_(session), game_(game) {
        auto service = Box<vectext::SystemTextService>::make();
        ui().set_text_service(Box<TextService>::from_raw(rstd::move(service).into_raw().template cast<TextService>())).unwrap();
        root_ = ui().create<Stack>(examples::block(520, 720, background)).unwrap();
        ui().mount(root_).unwrap(); reset_tiles(); update_status();
        composition_ = Some(cx::Composition::create(session_, root_).unwrap());
        composition_->set_content(cx::with_constraints(cx::Key{1},
            [owner = cx::borrow(*this), source = model_.value()](cx::CompositionContext &, LayoutConstraints constraints)
                -> cx::ComposeResult<cx::Element> {
                auto width = constraints.max_width.unwrap_or(rstd::cmp::max(Scalar(500), constraints.min_width));
                auto height = constraints.max_height.unwrap_or(rstd::cmp::max(Scalar(700), constraints.min_height));
                auto board = content(owner, source);
                if (board.is_err()) return Err(board.unwrap_err());
                return Ok(cx::element(BoardCanvas{}, cx::elements({board.unwrap()}))
                    .style(NodeStyleBuilder{}.width(width).height(height).build()));
            })).unwrap();
        resize({{520, 720}, 1, 1});
    }
    ~View() {
        lifetime_.disconnect();
        if (animations_.is_some()) (*animations_).get()->close();
    }
    View(const View &) = delete;
    auto operator=(const View &) -> View & = delete;
    auto game() const -> const Game & { return game_; }
    auto busy() const -> bool { return busy_; }
    auto last_result() const -> Option<cx::ComposeResult<cx::CommitReport>> { return composition_->last_result(); }
    auto check() -> Result<empty, UiError> {
        if (animation_error_.is_some()) return Err(animation_error_->ui.unwrap_or(UiError::InvalidValue));
        return examples::composition_status(*composition_);
    }
    void resize(const SurfaceInfo &surface) {
        auto size = surface.logical_size;
        if (size.width > 0 && size.height > 0)
            ui().set_layout(root_, LayoutStyleBuilder{}.width(size.width).height(size.height).build()).unwrap();
    }
};
} // namespace game2048
