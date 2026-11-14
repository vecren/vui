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
    UiSession &session_;
    Binding lifetime_{session_.document().bindings().make()};
    Option<cx::Composition> composition_;
    Game game_;
    UiId root_;
    Vec<Tile> tiles_;
    MoveResult move_;
    unsigned pending_{};
    Serial generation_{}, next_tile_{1}, tile_epoch_{};
    bool busy_{}, continued_{}, pop_pending_{};
    static constexpr Scalar cell = 96, gap = 12, board_size = 444;
    static constexpr Color ink{.019, .045, .095, 1};
    static constexpr Color background{.933, .953, .973, 1};

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
    template <class F> auto button(ref<str> title, Scalar x, F clicked, bool visible = true) -> cx::Element {
        return cx::button(label(title, 17, {}, true, {1, 1, 1, 1})
                .style(NodeStyleBuilder{}.horizontal(Alignment::Center).vertical(Alignment::Center).hit_test_visible(false).build()),
            rstd::move(clicked), ButtonStyleBuilder{}.normal(ink).pressed({.01, .025, .05, 1})
                .focused({.035, .12, .26, 1}).radius(10).build())
            .style(NodeStyleBuilder{}.width(210).height(48).transform({1, 0, 0, 1, x, 562}).visible(visible).build());
    }
    auto tile(const Tile &value) -> cx::Element {
        auto number = rstd::format("{}", value.value);
        Scalar size = value.value < 128 ? 40 : value.value < 1024 ? 34 : value.value < 16384 ? 28 : 19;
        auto tint = value.value == 32 || value.value == 64 || value.value > 2048 ? Color{1, 1, 1, 1} : ink;
        return cx::stack({label(number.as_str(), size, {}, true, tint)
            .style(NodeStyleBuilder{}.horizontal(Alignment::Center).vertical(Alignment::Center).hit_test_visible(false).build())})
            .style(NodeStyleBuilder{examples::block(cell, cell, color(value.value), 10)}
                .transform(position(value.cell)).hit_test_visible(false).build())
            .key(cx::Key{value.id}).reference(value.node);
    }
    auto content(cx::CompositionContext &scope) -> cx::ComposeResult<cx::Element> {
        Vec<cx::Element> cells;
        for (unsigned i{}; i < 16; ++i)
            cells.push(cx::native(LayoutKind::Leaf).style(NodeStyleBuilder{
                examples::block(cell, cell, {.82, .87, .92, 1}, 10)}.transform(position(i)).hit_test_visible(false).build()));
        for (const auto &value : tiles_) cells.push(tile(value));
        auto score = rstd::format("{}", game_.board().score);
        bool won = game_.reached_2048() && !continued_;
        auto message = !game_.can_move() ? "没有可移动的方块了，再来一局吧。"_str
            : won ? "合成了 2048！还想挑战更大的数字吗？"_str : "合并相同数字，向 2048 前进。"_str;
        auto effect = scope.effect(cx::Key{"tiles"_str}, cx::EffectKeys{cx::Key{tile_epoch_}},
            [this](cx::CommittedContext &) -> cx::ComposeResult<cx::EffectLease> {
                if (pop_pending_) start_pop();
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
            button("重新开始"_str, 0, [this] { restart(); }),
            button("继续挑战"_str, 234, [this] {
                continued_ = true; update_status();
                session_.input().focus_when_ready(root_, None<ScopeId>(), lifetime_.guard()).unwrap();
            }, won),
            label("方向键移动 · Tab 切换按钮 · Esc 退出"_str, 14, {0, 628}),
        }).style(examples::block(board_size, 650, {0, 0, 0, 0})));
    }
    void reset_tiles() {
        tiles_.clear();
        for (unsigned i{}; i < 16; ++i)
            if (auto value = game_.board().cells[i]) tiles_.push(Tile{next_tile_++, value, i, {}});
    }
    void update_status() { if (composition_.is_some()) composition_->invalidate(); }
    void watch(AnimationId animation, bool sliding) {
        ++pending_;
        auto generation = generation_;
        ui().on_animation_end(animation, lifetime_.guard(),
            [this, generation, sliding](auto &, const AnimationEvent &event) {
                if (generation != generation_ || event.reason != AnimationEndReason::Finished) return;
                if (--pending_) return;
                if (sliding) settle(); else { busy_ = false; update_status(); }
            }).unwrap();
    }
    void start_pop() {
        pop_pending_ = false;
        for (const auto &tile : tiles_) {
            if (!move_.merged[tile.cell] && tile.cell != move_.spawned.cell) continue;
            auto id = tile.node.id().unwrap();
            ui().set_transform(id, position(tile.cell, move_.merged[tile.cell] ? 1.12 : .3)).unwrap();
            watch(ui().animate_transform(id, position(tile.cell),
                {120'000'000, AnimationEasing::EaseInOut, AnimationEndPolicy::CommitFinal}).unwrap(), false);
        }
        if (!pending_) { busy_ = false; update_status(); }
    }
    void settle() {
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
        tiles_ = rstd::move(next); pop_pending_ = true; ++tile_epoch_; update_status();
    }
    void move(Direction direction) {
        if (busy_ || !game_.can_move() || (game_.reached_2048() && !continued_)) return;
        move_ = game_.move(direction); if (!move_.changed) return;
        busy_ = true;
        for (unsigned i{}; i < move_.count; ++i) {
            auto movement = move_.movements[i];
            if (movement.from == movement.to) continue;
            for (const auto &tile : tiles_) if (tile.cell == movement.from) {
                watch(ui().animate_transform(tile.node.id().unwrap(), position(movement.to),
                    {130'000'000, AnimationEasing::EaseInOut, AnimationEndPolicy::CommitFinal}).unwrap(), true);
                break;
            }
        }
        if (!pending_) settle();
    }
    void restart() {
        ++generation_; ++tile_epoch_; pending_ = 0; busy_ = continued_ = pop_pending_ = false;
        game_.restart(); reset_tiles(); update_status();
    }
    void key(EventContext &context, const RoutedEvent &event) {
        Direction direction;
        switch (event.input.key) {
        case Key::Left: direction = Direction::Left; break;
        case Key::Right: direction = Direction::Right; break;
        case Key::Up: direction = Direction::Up; break;
        case Key::Down: direction = Direction::Down; break;
        default: return;
        }
        context.prevent_default(); context.stop_propagation();
        if (!event.input.repeat)
            ui().defer(root_, lifetime_.guard(), [this, direction](auto &) -> Result<empty, UiError> {
                move(direction); return Ok(empty{});
            }).unwrap();
    }
  public:
    View(UiSession &session, Game game) : session_(session), game_(game) {
        auto service = Box<vectext::SystemTextService>::make();
        ui().set_text_service(Box<TextService>::from_raw(rstd::move(service).into_raw().template cast<TextService>())).unwrap();
        root_ = ui().create<CustomLayout>(NodeStyleBuilder{examples::block(520, 720, background)}.focusable(true).build()).unwrap();
        ui().install_layout(root_, ReferenceCanvas{{500, 700}, CanvasFit::Contain, {.5, .5}, true, Some(Scalar(1.2))}).unwrap();
        ui().mount(root_).unwrap(); reset_tiles();
        composition_ = Some(cx::Composition::create(session_, root_).unwrap());
        composition_->set_content([this](cx::CompositionContext &scope) { return content(scope); }).unwrap();
        const RoutePhase phases[]{RoutePhase::Target, RoutePhase::Capture};
        for (auto phase : phases)
            session_.input().on(root_, EventType::KeyDown, lifetime_.guard(),
                [this](auto &context, const auto &event) { key(context, event); }, phase).unwrap();
        session_.input().on(root_, EventType::PointerDown, lifetime_.guard(), [this](auto &, const auto &) {
            session_.input().request_focus(Some(root_)).unwrap();
        }).unwrap();
        resize({{520, 720}, 1, 1});
        session_.input().focus_when_ready(root_, None<ScopeId>(), lifetime_.guard()).unwrap();
    }
    View(const View &) = delete;
    auto operator=(const View &) -> View & = delete;
    auto game() const -> const Game & { return game_; }
    auto busy() const -> bool { return busy_; }
    auto last_result() const -> Option<cx::ComposeResult<cx::CommitReport>> { return composition_->last_result(); }
    auto check() -> Result<empty, UiError> { return examples::composition_status(*composition_); }
    void resize(const SurfaceInfo &surface) {
        auto size = surface.logical_size;
        if (size.width > 0 && size.height > 0)
            ui().set_layout(root_, LayoutStyleBuilder{}.width(size.width).height(size.height).build()).unwrap();
    }
};
} // namespace game2048
