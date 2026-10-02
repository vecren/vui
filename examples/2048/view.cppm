export module vui.examples.game2048:view;
import vui;
import vectext;
import vui.examples;
import :game;

export namespace game2048 {
using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;

class View {
    struct CanvasLayout final : LayoutStrategy {
        auto measure(LayoutMeasureContext &context, LayoutConstraints)
            -> Result<Size, UiError> override {
            return context.measure_child(usize(), {});
        }
        auto arrange(LayoutArrangeContext &context, Size) -> Result<empty, UiError> override {
            auto size = context.desired_child(usize()).unwrap();
            return context.arrange_child(usize(), {0, 0, size.width, size.height});
        }
    };
    UiSession &session_;
    Game game_;
    UiId root_, panel_, board_, score_, message_, restart_, continue_;
    Option<UiId> tiles_[16];
    MoveResult move_;
    unsigned pending_{};
    Serial generation_{};
    bool busy_{}, continued_{}, focus_pending_{true};
    static constexpr Scalar cell = 96, gap = 12, board_size = 444;
    static constexpr Color ink{.019, .045, .095, 1};
    static constexpr Color background{.933, .953, .973, 1};

    auto ui() -> UiDocument & { return session_.document(); }
    static auto position(unsigned index, Scalar scale = 1) -> Transform {
        const auto inset = cell * (1 - scale) / 2;
        return {scale,
                0,
                0,
                scale,
                gap + (index % 4) * (cell + gap) + inset,
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
    auto label(UiId parent, ref<str> text, Scalar size, Point at, bool bold = false,
               Color tint = ink) -> UiId {
        NodeStyle style;
        style.transform.tx = at.x;
        style.transform.ty = at.y;
        style.hit_test_visible = false;
        auto id = ui().append<Text>(parent, style).unwrap();
        ui().set_text(id, text).unwrap();
        ui().set_text_format(id, {.size = size, .wrap = TextWrap::None, .bold = bold}).unwrap();
        ui().set_text_color(id, tint).unwrap();
        return id;
    }
    auto button(ref<str> title, Scalar x) -> UiId {
        auto style = examples::block(210, 48, {});
        style.transform = {1, 0, 0, 1, x, 562};
        auto id = Button::create(session_, style, {ink, {.01, .025, .05, 1}, {.035, .12, .26, 1}, 10})
                      .unwrap();
        ui().attach(panel_, id).unwrap();
        auto text = label(id, title, 17, {}, true, {1, 1, 1, 1});
        ui().set_layout(text, {.horizontal = Alignment::Center, .vertical = Alignment::Center})
            .unwrap();
        return id;
    }
    void clear_tiles() {
        for (auto &tile : tiles_) {
            if (tile.is_some())
                ui().destroy(*tile).unwrap();
            tile = None();
        }
    }
    void create_tiles() {
        for (unsigned i = 0; i < 16; ++i) {
            auto value = game_.board().cells[i];
            if (!value)
                continue;
            auto style = examples::block(cell, cell, color(value), 10);
            style.transform = position(i);
            style.hit_test_visible = false;
            auto tile = ui().append<Stack>(board_, style).unwrap();
            tiles_[i] = Some(tile);
            auto number = rstd::format("{}", value);
            Scalar size = value < 128 ? 40 : value < 1024 ? 34 : value < 16384 ? 28 : 19;
            auto text = label(tile, number.as_str(), size, {}, true,
                              value == 32 || value == 64 || value > 2048 ? Color{1, 1, 1, 1} : ink);
            ui().set_layout(text, {.horizontal = Alignment::Center, .vertical = Alignment::Center})
                .unwrap();
        }
    }
    void update_status() {
        auto score = rstd::format("{}", game_.board().score);
        ui().set_text(score_, score.as_str()).unwrap();
        bool won = game_.reached_2048() && !continued_;
        ui().set_visible(continue_, won).unwrap();
        ui().set_text(message_, !game_.can_move() ? "没有可移动的方块了，再来一局吧。"_str
                                : won             ? "合成了 2048！还想挑战更大的数字吗？"_str
                                                  : "合并相同数字，向 2048 前进。"_str)
            .unwrap();
    }
    void watch(AnimationId animation, bool sliding) {
        ++pending_;
        auto generation = generation_;
        ui().on_animation_end(animation,
                              [this, generation, sliding](auto &, const AnimationEvent &event) {
                                  if (generation != generation_ ||
                                      event.reason != AnimationEndReason::Finished)
                                      return;
                                  if (--pending_ != 0)
                                      return;
                                  if (sliding)
                                      settle();
                                  else {
                                      busy_ = false;
                                      update_status();
                                  }
                              })
            .unwrap();
    }
    void settle() {
        clear_tiles();
        create_tiles();
        for (unsigned i = 0; i < 16; ++i) {
            if (!move_.merged[i] && i != move_.spawned.cell)
                continue;
            auto tile = *tiles_[i];
            ui().set_transform(tile, position(i, move_.merged[i] ? 1.12 : .3)).unwrap();
            watch(ui().animate_transform(tile, position(i),
                                         {120'000'000, AnimationEasing::EaseInOut,
                                          AnimationEndPolicy::CommitFinal})
                      .unwrap(),
                  false);
        }
    }
    void move(Direction direction) {
        if (busy_ || !game_.can_move() || (game_.reached_2048() && !continued_))
            return;
        move_ = game_.move(direction);
        if (!move_.changed)
            return;
        busy_ = true;
        for (unsigned i = 0; i < move_.count; ++i) {
            auto movement = move_.movements[i];
            if (movement.from == movement.to)
                continue;
            watch(ui().animate_transform(*tiles_[movement.from], position(movement.to),
                                         {130'000'000, AnimationEasing::EaseInOut,
                                          AnimationEndPolicy::CommitFinal})
                      .unwrap(),
                  true);
        }
    }
    void restart() {
        ++generation_;
        pending_ = 0;
        busy_ = continued_ = false;
        clear_tiles();
        game_.restart();
        create_tiles();
        update_status();
    }
    void key(EventContext &context, const RoutedEvent &event) {
        Direction direction;
        switch (event.input.key) {
        case Key::Left:
            direction = Direction::Left;
            break;
        case Key::Right:
            direction = Direction::Right;
            break;
        case Key::Up:
            direction = Direction::Up;
            break;
        case Key::Down:
            direction = Direction::Down;
            break;
        default:
            return;
        }
        context.prevent_default();
        context.stop_propagation();
        if (!event.input.repeat)
            context
                .defer([this, direction](auto &) -> Result<empty, UiError> {
                    move(direction);
                    return Ok(empty{});
                })
                .unwrap();
    }

  public:
    View(UiSession &session, Game game) : session_(session), game_(game) {
        auto service = Box<vectext::SystemTextService>::make();
        ui().set_text_service(Box<TextService>::from_raw(
                                  rstd::move(service).into_raw().template cast<TextService>()))
            .unwrap();
        auto root_style = examples::block(520, 720, background);
        root_style.focusable = true;
        root_ = ui().create<CustomLayout>(root_style).unwrap();
        auto layout = Box<CanvasLayout>::make();
        ui().set_layout_strategy(root_,
                                 Box<LayoutStrategy>::from_raw(
                                     rstd::move(layout).into_raw().template cast<LayoutStrategy>()))
            .unwrap();
        panel_ = ui().append<Stack>(root_, examples::block(board_size, 650, {0, 0, 0, 0})).unwrap();
        label(panel_, "2048"_str, 54, {0, 0}, true);
        label(panel_, "得分"_str, 14, {330, 8});
        score_ = label(panel_, "0"_str, 28, {330, 28}, true);
        auto style = examples::block(board_size, board_size, {.729, .788, .851, 1}, 16);
        style.transform.ty = 86;
        board_ = ui().append<Stack>(panel_, style).unwrap();
        for (unsigned i = 0; i < 16; ++i) {
            auto slot = examples::block(cell, cell, {.82, .87, .92, 1}, 10);
            slot.transform = position(i);
            slot.hit_test_visible = false;
            ui().append<Rectangle>(board_, slot).unwrap();
        }
        message_ = label(panel_, ""_str, 16, {0, 538});
        restart_ = button("重新开始"_str, 0);
        continue_ = button("继续挑战"_str, 234);
        label(panel_, "方向键移动 · Tab 切换按钮 · Esc 退出"_str, 14, {0, 628});
        create_tiles();
        update_status();
        ui().mount(root_).unwrap();
        const RoutePhase phases[]{RoutePhase::Target, RoutePhase::Capture};
        for (auto phase : phases)
            session_.input()
                .on(
                    root_, EventType::KeyDown,
                    [this](auto &context, const auto &event) { key(context, event); }, phase)
                .unwrap();
        session_.input()
            .on(board_, EventType::PointerDown,
                [this](auto &, const auto &) {
                    session_.input().request_focus(Some(root_)).unwrap();
                })
            .unwrap();
        session_.input()
            .on(restart_, EventType::Activate,
                [this](auto &context, const auto &) {
                    context
                        .defer([this](auto &) -> Result<empty, UiError> {
                            restart();
                            return Ok(empty{});
                        })
                        .unwrap();
                })
            .unwrap();
        session_.input()
            .on(continue_, EventType::Activate,
                [this](auto &context, const auto &) {
                    context
                        .defer([this](auto &) -> Result<empty, UiError> {
                            continued_ = true;
                            update_status();
                            focus_pending_ = true;
                            return Ok(empty{});
                        })
                        .unwrap();
                })
            .unwrap();
        resize({{520, 720}, 1, 1});
    }
    View(const View &) = delete;
    auto operator=(const View &) -> View & = delete;
    auto game() const -> const Game & { return game_; }
    auto busy() const -> bool { return busy_; }
    void after_update() {
        if (focus_pending_) {
            session_.input().request_focus(Some(root_)).unwrap();
            focus_pending_ = false;
        }
    }
    void resize(const SurfaceInfo &surface) {
        auto size = surface.logical_size;
        if (size.width <= 0 || size.height <= 0)
            return;
        ui().set_layout(root_, {.width = Some(size.width), .height = Some(size.height)}).unwrap();
        auto sx = size.width / 500, sy = size.height / 700;
        auto scale = sx < sy ? sx : sy;
        if (scale > 1.2)
            scale = 1.2;
        ui().set_transform(panel_, {scale, 0, 0, scale, (size.width - board_size * scale) / 2,
                                    (size.height - 650 * scale) / 2})
            .unwrap();
    }
};
} // namespace game2048
