export module vui.examples.gallery:lists;
import :style;
import :parts;
import :glass;
import vui.compose;
import rstd;

namespace examples::gallery::detail {

enum class LevelMode { List, Grid };

struct Level {
    Serial id;
    bool bonus;
    friend auto operator==(const Level &, const Level &) -> bool = default;
};

struct LevelRows {
    static auto initial_levels() -> Vec<Level> {
        constexpr Index count = 2048;
        auto levels = Vec<Level>::with_capacity(usize(count));
        for (Serial id = 1; id <= count; ++id) levels.push({id, id % 8 == 1});
        return levels;
    }

    state::Cell<Vec<Level>> levels;
    state::Cell<Serial> selected;
    state::Cell<LevelMode> mode;
    cx::LazyState list, grid;

    explicit LevelRows(state::Graph &graph)
        : levels(graph.cell(initial_levels(), [](const Vec<Level> &left, const Vec<Level> &right) {
              if (left.len() != right.len()) return false;
              for (usize i{}; i < left.len(); ++i) if (left[i] != right[i]) return false;
              return true;
          }).unwrap()), selected(graph.cell(Serial(1)).unwrap()), mode(graph.cell(LevelMode::List).unwrap()) {}
};

namespace {
constexpr Index levels_per_section = 128;

struct LevelLayout {
    static constexpr Scalar rail_width = 30;
    static constexpr Scalar gap = 6;

    static auto view_width(Scalar width, Scalar rail) -> Scalar {
        return rstd::cmp::max(Scalar(0), width - rail - gap);
    }

    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError> {
        const auto width = constraints.max_width.unwrap_or(0);
        const auto height = constraints.max_height.unwrap_or(0);
        auto toolbar = context.measure_child(usize(0), {Some(Scalar(width)), Some(Scalar(height))});
        if (toolbar.is_err()) return Err(toolbar.unwrap_err());
        const auto body = rstd::cmp::max(Scalar(0), height - toolbar->height - gap);
        const auto rail = rstd::min(width, rail_width);
        auto view = context.measure_child(usize(1), {Some(view_width(width, rail)), Some(Scalar(body))});
        if (view.is_err()) return Err(view.unwrap_err());
        auto bar = context.measure_child(usize(2), {Some(Scalar(rail)), Some(Scalar(body))});
        if (bar.is_err()) return Err(bar.unwrap_err());
        return Ok(Size{width, height});
    }

    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        auto toolbar = context.desired_child(usize(0));
        if (toolbar.is_err()) return Err(toolbar.unwrap_err());
        const auto header = rstd::min(size.height, toolbar->height);
        auto result = context.arrange_child(usize(0), {0, 0, size.width, header});
        if (result.is_err()) return result;
        const auto top = rstd::min(size.height, header + gap);
        const auto rail = rstd::min(size.width, rail_width);
        result = context.arrange_child(usize(1), {0, top, view_width(size.width, rail), size.height - top});
        if (result.is_err()) return result;
        return context.arrange_child(usize(2), {size.width - rail, top, rail, size.height - top});
    }
};

struct LevelItem {};

auto level_item(cx::Borrowed<LevelRows> rows, Level level) -> cx::Element {
    return cx::component<LevelItem>(cx::Key{level.id}, [rows, level](cx::CompositionContext &context)
            -> cx::ComposeResult<cx::Element> {
        auto selected = context.read(rows->selected.value());
        if (selected.is_err()) return Err(selected.unwrap_err());
        const auto active = selected->value() == level.id;
        auto name = rstd::format("Level {:04}", level.id);
        auto title = label(name.as_str(), 15, foreground, true);
        if (level.bonus) {
            title = cx::column({rstd::move(title), label("Bonus stage"_str, 12, muted)})
                .style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).vertical(Alignment::Center)
                    .hit_test_visible(false).build()).gap(3);
        }
        auto appearance = ButtonStyleBuilder{}.normal(active ? selected_surface : white(.014f))
            .pressed(pressed_surface).focused(active ? pressed_surface : control_surface).radius(8).build();
        auto button = control_finish(cx::button(rstd::move(title), [rows, id = level.id] { rows->selected.set(id).unwrap(); }, appearance)
            .style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).min_height(44).padding(10).build()), active);
        auto semantics = Button::default_semantics();
        semantics.label = rstd::move(name);
        semantics.checked = Some(bool(active));
        return Ok(cx::with_semantics(rstd::move(button), rstd::move(semantics)));
    });
}

auto level_content(cx::Borrowed<LevelRows> rows, state::ValueSnapshot<Vec<Level>> levels)
    -> cx::ComposeResult<cx::LazyContent> {
    cx::LazyContent content;
    for (usize section{}; section < levels.value().len(); section += usize(levels_per_section)) {
        const auto count = rstd::min(usize(levels_per_section), levels.value().len() - section);
        auto title = rstd::format("Levels {:04}-{:04}", levels.value()[section].id,
            levels.value()[section + count - usize(1)].id);
        auto header = cx::stack({label(title.as_str(), 13, foreground, true)})
            .style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).min_height(34)
                .padding(8).background(field_surface).radius(6).build());
        auto added = content.sticky_header(rstd::move(header), Some(cx::Key{title.as_str()}));
        if (added.is_err()) return Err(added.unwrap_err());
        added = content.items(Index(count.to_primitive()),
            [rows, levels, section](Index index) { return level_item(rows, levels.value()[section + usize(index)]); },
            [levels, section](Index index) {
                const auto &level = levels.value()[section + usize(index)];
                return cx::LazyItemInfo{.key = Some(cx::Key{level.id}),
                    .content_type = Some(cx::Key{Serial(level.bonus)})};
            }, [](Index index, cx::LazyGridSpanScope span) { return index == 0 ? span.max_line_span : Index(1); });
        if (added.is_err()) return Err(added.unwrap_err());
    }
    return Ok(rstd::move(content));
}

auto mode_button(cx::Borrowed<LevelRows> rows, LevelMode current, LevelMode target, ref<str> title) -> cx::Element {
    const auto active = current == target;
    auto properties = Button::default_semantics();
    properties.checked = Some(bool(active));
    return cx::with_semantics(control_finish(cx::button(button_label(title),
        [rows, target] { rows->mode.set(target).unwrap(); }, button_style(active))
        .style(NodeStyleBuilder{}.width(78).height(36).build()), active), rstd::move(properties));
}
} // namespace

[[vui::compose]]
auto lists(cx::Borrowed<LevelRows> rows) -> cx::ComposeResult<cx::Element> {
    auto current = cx::read(rows->mode.value());
    if (current.is_err()) return Err(current.unwrap_err());
    const auto mode = current->value();
    auto levels = cx::read(rows->levels.value());
    if (levels.is_err()) return Err(levels.unwrap_err());
    auto items = level_content(rows, levels.unwrap());
    if (items.is_err()) return Err(items.unwrap_err());
    auto view_ref = cx::node(cx::Key{Serial(mode)});
    if (view_ref.is_err()) return Err(view_ref.unwrap_err());
    auto rail_ref = cx::node();
    if (rail_ref.is_err()) return Err(rail_ref.unwrap_err());
    auto view = view_ref.unwrap();
    auto rail = rail_ref.unwrap();
    auto bound = cx::effect({cx::Key{Serial(mode)}}, [view, rail](cx::CommittedContext &committed)
            -> cx::ComposeResult<cx::EffectLease> {
        auto target = committed.resolve(view);
        if (target.is_err()) return Err(target.unwrap_err());
        auto scrollbar = committed.resolve(rail);
        if (scrollbar.is_err()) return Err(scrollbar.unwrap_err());
        auto result = Scrollbar::bind_scroll_view(committed.session().document(), *scrollbar, *target);
        if (result.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(result.unwrap_err())));
        return Ok(cx::EffectLease{});
    });
    if (bound.is_err()) return Err(bound.unwrap_err());

    auto toolbar = cx::row({mode_button(rows, mode, LevelMode::List, "List"_str),
        mode_button(rows, mode, LevelMode::Grid, "Grid"_str)}).gap(6);
    auto content = items.unwrap();
    const cx::LazyOptions options{.spacing = 4, .cross_spacing = 6, .estimated_extent = 48};
    auto viewport = mode == LevelMode::Grid
        ? cx::lazy_vertical_grid(cx::GridCells::adaptive(124), rstd::move(content), options, rows->grid)
        : cx::lazy_column(rstd::move(content), options, rows->list);
    viewport = rstd::move(viewport).style(fill().build()).reference(view);

    auto thumb = cx::native(LayoutKind::Leaf)
        .style(NodeStyleBuilder{}.background(muted).radius(5).hit_test_visible(false).build());
    auto scrollbar = control_finish(cx::scrollbar(rstd::move(thumb),
        {.axis = ScrollbarAxis::Vertical, .minimum_handle = 36, .handle_insets = {10, 0, 10, 0}})
        .style(fill().background(white(.02f)).radius(10).build()).reference(rail), false);
    auto body = layout_part(LevelLayout{}, cx::elements({rstd::move(toolbar), rstd::move(viewport), rstd::move(scrollbar)}))
        .style(fill().hit_self(false).build());
    return Ok(glass(LayoutKind::Stack, cx::elements({rstd::move(body)}), {.opacity = .74f})
        .style(fill().padding(5).radius(panel_radius).hit_self(false).build()));
}
} // namespace examples::gallery::detail
