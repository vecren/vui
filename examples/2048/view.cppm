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

enum class InputHint { Keyboard, Swipe };

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
        bool won{}, over{}, busy{}, result_visible{};
    };
    UiSession &session_;
    Binding lifetime_{session_.document().bindings().make()};
    state::Graph graph_;
    state::Cell<rstd::rc::Rc<Model>> model_{graph_.cell(rstd::rc::make_rc<Model>(),
        [](const auto &a, const auto &b) { return a.get() == b.get(); }).unwrap()};
    state::Cell<Insets> dialog_insets_{graph_.cell(Insets{}).unwrap()};
    Option<cx::Composition> composition_, dialogs_;
    Game game_;
    InputHint input_hint_;
    UiId background_, root_;
    Vec<Tile> tiles_;
    MoveResult move_;
    Option<rstd::rc::Rc<AnimationScope>> animations_;
    AnimationRun movement_;
    Option<AnimationSequenceError> animation_error_;
    Option<AnimationGate> ready_;
    cx::NodeRef input_;
    Serial next_tile_{1};
    bool busy_{}, continued_{}, result_dismissed_{};
    static constexpr Scalar cell = 96, gap = 12, board_size = 444;
    static constexpr Color ink{.019, .045, .095, 1};
    static constexpr Color background{.933, .953, .973, 1};

    struct MoveAction {
        cx::Latest<bool> enabled;
        cx::Latest<cx::Action<void(Direction)>> on_move;
        BindingGuard lifetime;
        auto active() const -> bool {
            auto current = enabled.get();
            return lifetime.alive() && current.is_ok() && current.unwrap().value();
        }
        void enqueue(EventContext &context, UiId target, Direction direction) const {
            context.document().defer(target, lifetime,
                [action = *this, direction](UiDocument &) -> Result<empty, UiError> {
                    if (action.active()) {
                        auto callback = action.on_move.get();
                        if (callback.is_ok()) callback.unwrap().value()(direction);
                    }
                    return Ok(empty{});
                }).unwrap();
        }
    };
    struct GameInput {
        MoveAction move;
        void handle(EventContext &context, const RoutedEvent &event) {
            if (!move.lifetime.alive()) return;
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
            if (!event.input.repeat) move.enqueue(context, event.current, direction);
        }
    };
    struct GameInputDescriptor {
        using State = GameInput;
        MoveAction move;
        auto create() const -> State { return {move}; }
        void update(State &state, cx::BehaviorContext &) const { state.move = move; }
    };
    struct BoardSwipe {
        struct Contact {
            DeviceId device;
            Index pointer;
            Point start;
        };
        MoveAction move;
        Option<Contact> contact;
        void handle(EventContext &context, const RoutedEvent &event) {
            const auto &input = event.input;
            if (input.pointer_kind != PointerKind::Touch) return;
            if (input.type == EventType::PointerDown) {
                if (contact.is_none() && move.active() && context.capture_pointer().is_ok())
                    contact = Some(Contact{input.device, input.pointer, input.position});
                return;
            }
            if (contact.is_none() || contact->device != input.device || contact->pointer != input.pointer) return;
            if (input.type == EventType::PointerCancel || input.type == EventType::CaptureLost) {
                contact = None();
                return;
            }
            if (input.type != EventType::PointerUp) return;
            const auto x = input.position.x - contact->start.x;
            const auto y = input.position.y - contact->start.y;
            contact = None();
            (void)context.release_pointer();
            if (context.default_prevented()) return;
            const auto horizontal = x < 0 ? -x : x;
            const auto vertical = y < 0 ? -y : y;
            if (rstd::cmp::max(horizontal, vertical) < 24) return;
            const auto direction = horizontal >= vertical
                ? (x < 0 ? Direction::Left : Direction::Right)
                : (y < 0 ? Direction::Up : Direction::Down);
            move.enqueue(context, event.current, direction);
        }
    };
    struct BoardSwipeDescriptor {
        using State = BoardSwipe;
        MoveAction move;
        auto create() const -> State { return {move}; }
        void update(State &state, cx::BehaviorContext &) const { state.move = move; }
    };
    struct BoardLayout {
        static constexpr Size portrait{500, 700}, landscape{840, 492};

        static auto viewport(LayoutConstraints limits) -> Size {
            return {limits.max_width.unwrap_or(rstd::cmp::max(portrait.width, limits.min_width)),
                    limits.max_height.unwrap_or(rstd::cmp::max(portrait.height, limits.min_height))};
        }
        static auto wide(Size available) -> bool { return available.width > available.height * 1.25; }
        static auto reference(Size available) -> ReferenceCanvas {
            return {wide(available) ? landscape : portrait, CanvasFit::Contain, {.5, .5}, true, Some(Scalar(1.2))};
        }
        auto measure(LayoutMeasureContext &context, LayoutConstraints limits) -> Result<Size, UiError> {
            return reference(viewport(limits)).measure(context, limits);
        }
        auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
            return reference(size).arrange(context, size);
        }
    };
    template <class Layout> struct Canvas {
        Layout layout;
        auto prepare(cx::PrepareContext &context, Option<UiId> previous, NodeStyle style) -> cx::ComposeResult<UiNodeRef> {
            auto node = previous.is_some() ? context.edit().existing(*previous)
                : context.edit().create(LayoutKind::Custom, style);
            context.style(node, style);
            if (previous.is_none()) {
                context.edit().state(node, layout);
                auto target = context.target();
                context.on_mount([target](cx::CommittedContext &committed) -> cx::ComposeResult<cx::EffectLease> {
                    auto resolved = committed.resolve(target);
                    if (resolved.is_err()) return Err(resolved.unwrap_err());
                    auto attached = committed.session().document().attach_layout<Layout>(resolved.unwrap());
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
    template <class F> static auto button(ref<str> title, Point origin, F clicked, bool visible = true,
                                          Scalar height = 48) -> cx::Element {
        return cx::button(label(title, 17, {}, true, {1, 1, 1, 1})
                .style(NodeStyleBuilder{}.horizontal(Alignment::Center).vertical(Alignment::Center).hit_test_visible(false).build()),
            rstd::move(clicked), ButtonStyleBuilder{}.normal(ink).pressed({.01, .025, .05, 1})
                .focused({.035, .12, .26, 1}).radius(10).build())
            .style(NodeStyleBuilder{}.width(210).height(height)
                .transform({1, 0, 0, 1, origin.x, origin.y}).visible(visible).build());
    }
    struct BoardDecoration {
        auto draw(ModifierDrawContext &context) -> Result<empty, UiError> {
            auto &paint = context.paint();
            auto size = paint.size();
            auto &canvas = paint.canvas();
            canvas.rect({0, 0, size.width, size.height}, {.729, .788, .851, 1}, 16);
            for (unsigned i{}; i < 16; ++i) {
                auto at = position(i);
                canvas.rect({at.tx, at.ty, cell, cell}, {.82, .87, .92, 1}, 10);
            }
            return context.draw_content();
        }
    };
    struct BoardDecorationDescriptor {
        using State = BoardDecoration;
        auto create() const -> State { return {}; }
    };
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
    static auto content(cx::Borrowed<View> owner, state::Value<rstd::rc::Rc<Model>> source, bool wide)
        -> cx::ComposeResult<cx::Element> {
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
        MoveAction move{enabled.unwrap(), on_move.unwrap(), owner->lifetime_.guard()};
        auto focused = cx::effect(cx::EffectKeys{cx::Key{Serial(model.result_visible)}},
            [owner, visible = model.result_visible](cx::CommittedContext &context) -> cx::ComposeResult<cx::EffectLease> {
                if (visible) return Ok(cx::EffectLease{});
                auto target = context.resolve(owner->input_);
                if (target.is_err()) return Err(target.unwrap_err());
                auto binding = context.session().document().bindings().make();
                auto requested = context.session().input().focus_when_ready(target.unwrap(), None<ScopeId>(), binding.guard());
                if (requested.is_err()) {
                    cx::ComposeError error(cx::ComposeStage::Input); error.input = Some(requested.unwrap_err());
                    return Err(rstd::move(error));
                }
                return Ok(cx::EffectLease([binding = rstd::move(binding)]() mutable { binding.disconnect(); }));
            });
        if (focused.is_err()) return Err(focused.unwrap_err());
        Vec<cx::Element> cells;
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
        const auto hint = owner->input_hint_ == InputHint::Swipe ? "在棋盘上滑动以移动方块"_str
            : "方向键移动 · Tab 切换按钮 · Esc 退出"_str;
        constexpr Scalar panel_x = 506;
        const Point board = wide ? Point{24, 24} : Point{0, 86};
        const auto action_height = wide ? 68 : 48;
        return Ok(cx::stack({
            label("2048"_str, 54, wide ? Point{panel_x, 24} : Point{}, true),
            label("得分"_str, 14, wide ? Point{panel_x, 121} : Point{330, 8}),
            label(score.as_str(), 28, wide ? Point{panel_x, 143} : Point{330, 28}, true),
            cx::stack(rstd::move(cells)).style(NodeStyleBuilder{
                examples::block(board_size, board_size, {0, 0, 0, 0})}
                .transform({1, 0, 0, 1, board.x, board.y}).build())
                .behavior(cx::Key{1}, BoardDecorationDescriptor{}).behavior(cx::Key{2}, BoardSwipeDescriptor{move}),
            label(message, 16, wide ? Point{panel_x, 238} : Point{0, 538}),
            button("重新开始"_str, wide ? Point{panel_x, 275} : Point{0, 562},
                [owner] { owner->restart(); }, true, action_height),
            button("继续挑战"_str, wide ? Point{panel_x, 359} : Point{234, 562}, [owner] {
                owner->continue_game();
                owner->session_.input().focus_when_ready(owner->input_.id().unwrap(), None<ScopeId>(), owner->lifetime_.guard()).unwrap();
            }, model.won, action_height),
            label(hint, 14, wide ? Point{panel_x, 449} : Point{0, 628}),
        }).style(NodeStyleBuilder{examples::block(wide ? BoardLayout::landscape.width : board_size,
            wide ? BoardLayout::landscape.height : 650, {0, 0, 0, 0})}.focusable(true).build())
            .reference(owner->input_).behavior(cx::Key{1}, GameInputDescriptor{move}));
    }
    [[vui::compose]]
    static auto result_dialog(cx::Borrowed<View> owner, state::Value<rstd::rc::Rc<Model>> source,
                              state::Value<Insets> insets) -> cx::ComposeResult<cx::Element> {
        auto observed = cx::read(source);
        if (observed.is_err()) return Err(observed.unwrap_err());
        const auto &model = *observed->value();
        if (!model.result_visible) return Ok(cx::stack({}).style(NodeStyleBuilder{}.hit_self(false).build()));
        auto area = cx::read(insets);
        if (area.is_err()) return Err(area.unwrap_err());
        auto saved_focus = cx::node();
        if (saved_focus.is_err()) return Err(saved_focus.unwrap_err());
        auto result_focus = saved_focus.unwrap();
        auto restart = button("重新开始"_str, {}, [owner] { owner->restart(); })
            .style(NodeStyleBuilder{}.width(176).height(48).build());
        Vec<cx::Element> actions;
        if (model.won) {
            actions.push(rstd::move(restart));
            actions.push(button("继续挑战"_str, {}, [owner] { owner->continue_game(); })
                .style(NodeStyleBuilder{}.width(176).height(48).build()).reference(result_focus));
        } else actions.push(rstd::move(restart).reference(result_focus));
        auto message = model.over ? "没有可移动的方块了，再来一局吧。"_str
            : "合成了 2048！还想挑战更大的数字吗？"_str;
        constexpr Size panel_size{420, 224};
        auto prompt = cx::column({label(model.won ? "合成了 2048！"_str : "游戏结束"_str, 28, {}, true),
            label(message, 16), cx::row(rstd::move(actions)).gap(12)})
            .style(examples::block(panel_size.width, panel_size.height, background, 16)).padding(24).gap(14);
        auto panel = cx::element(Canvas{ReferenceCanvas{panel_size, CanvasFit::Contain, {.5, .5}, true, Some(Scalar(1))}},
            cx::elements({rstd::move(prompt)}))
            .style(NodeStyleBuilder{}.width(panel_size.width).height(panel_size.height).hit_self(false).build());
        cx::PopupOptions options;
        options.initial_focus = Some(cx::NodeRef(result_focus));
        options.content_insets = area->value();
        return Ok(cx::dialog(rstd::move(panel), [owner](PopupCloseReason) { owner->dismiss_result(); }, rstd::move(options)));
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
        next.result_visible = !busy_ && !result_dismissed_ && (next.won || next.over);
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
    void dismiss_result() { result_dismissed_ = true; update_status(); }
    void continue_game() { continued_ = true; result_dismissed_ = false; update_status(); }
    void restart() {
        movement_.cancel().unwrap(); ready_ = None(); animation_error_ = None(); busy_ = continued_ = result_dismissed_ = false;
        game_.restart(); reset_tiles(); update_status();
    }
  public:
    View(UiSession &session, Game game, InputHint hint = InputHint::Keyboard)
        : session_(session), game_(game), input_hint_(hint) {
        auto service = Box<vectext::SystemTextService>::make();
        ui().set_text_service(Box<TextService>::from_raw(rstd::move(service).into_raw().template cast<TextService>())).unwrap();
        background_ = ui().create<Stack>(NodeStyleBuilder{examples::block(520, 720, background)}.hit_self(false).build()).unwrap();
        root_ = ui().append<Stack>(background_, NodeStyleBuilder{}.width(520).height(720).build()).unwrap();
        ui().mount(background_).unwrap(); reset_tiles(); update_status();
        composition_ = Some(cx::Composition::create(session_, root_).unwrap());
        composition_->set_content(cx::with_constraints(cx::Key{1},
            [owner = cx::borrow(*this), source = model_.value()](cx::CompositionContext &, LayoutConstraints constraints)
                -> cx::ComposeResult<cx::Element> {
                const auto size = BoardLayout::viewport(constraints);
                auto board = content(owner, source, BoardLayout::wide(size));
                if (board.is_err()) return Err(board.unwrap_err());
                return Ok(cx::element(Canvas{BoardLayout{}}, cx::elements({board.unwrap()}))
                    .style(NodeStyleBuilder{}.width(size.width).height(size.height).build()));
            })).unwrap();
        auto overlay = ui().append<Stack>(background_, NodeStyleBuilder{}.hit_self(false)
            .horizontal(Alignment::Stretch).vertical(Alignment::Stretch).build()).unwrap();
        dialogs_ = Some(cx::Composition::create(session_, overlay).unwrap());
        dialogs_->set_content(result_dialog(cx::borrow(*this), model_.value(), dialog_insets_.value())).unwrap();
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
        auto body = examples::composition_status(*composition_);
        return body.is_err() ? rstd::move(body) : examples::composition_status(*dialogs_);
    }
    void resize(const SurfaceInfo &surface) {
        auto size = surface.logical_size;
        resize(size, {0, 0, size.width, size.height});
    }
    void resize(Size surface, Rect viewport) {
        if (viewport.width <= 0 || viewport.height <= 0) return;
        ui().set_layout(background_, LayoutStyleBuilder{}.width(surface.width).height(surface.height).build()).unwrap();
        ui().set_layout(root_, LayoutStyleBuilder{}.width(viewport.width).height(viewport.height).build()).unwrap();
        ui().set_transform(root_, {1, 0, 0, 1, viewport.x, viewport.y}).unwrap();
        dialog_insets_.set(examples::content_insets(surface, viewport)).unwrap();
    }
};
} // namespace game2048
