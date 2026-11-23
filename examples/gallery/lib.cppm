export module vui.examples.gallery;
import :style;
import :parts;
import :glass;
import :controls;
import :lists;
import :motion;
import :canvas_recording;
import :canvas_workload;
import vui.compose;
import vui.examples;
import rstd;

using namespace vui;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace cx = vui::compose;
using examples::block;

namespace {
using namespace examples::gallery::detail;

enum class Page { Controls, Lists, Motion, Canvas };
constexpr unsigned tab_count = unsigned(Page::Canvas) + 1;

auto page_title(Page page) -> ref<str> {
    switch (page) {
    case Page::Controls: return "Controls"_str;
    case Page::Lists: return "Lists"_str;
    case Page::Motion: return "Motion"_str;
    case Page::Canvas: return "Canvas"_str;
    }
    return ""_str;
}

struct GalleryLayout {
    static constexpr Scalar sidebar = 182;
    static constexpr Scalar gap = 12;

    static auto wide(Scalar width) -> bool { return width >= 620; }
    static auto inset(Scalar width) -> Scalar { return wide(width) ? 24 : rstd::min(Scalar(14), width / 8); }
    static auto content_width(Scalar width) -> Scalar {
        return width - 2 * inset(width) - (wide(width) ? sidebar : 0);
    }

    auto measure(LayoutMeasureContext &context, LayoutConstraints limits) -> Result<Size, UiError> {
        const auto width = limits.max_width.unwrap_or(620);
        const auto height = limits.max_height.unwrap_or(480);
        const auto navigation_width = wide(width) ? sidebar : width;
        auto navigation = context.measure_child(usize(0), {Some(Scalar(navigation_width)), Some(Scalar(height))});
        if (navigation.is_err()) return Err(navigation.unwrap_err());
        const auto top = (wide(width) ? 0 : navigation->height) + inset(width);
        const auto remaining = rstd::cmp::max(Scalar(0), height - top - inset(width));
        const auto content = content_width(width);
        auto title = context.measure_child(usize(1), {Some(Scalar(content)), Some(Scalar(remaining))});
        if (title.is_err()) return Err(title.unwrap_err());
        auto pages = context.measure_child(usize(2),
            {Some(Scalar(content)), Some(rstd::cmp::max(Scalar(0), remaining - title->height - gap))});
        if (pages.is_err()) return Err(pages.unwrap_err());
        return Ok(Size{width, height});
    }

    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        auto navigation = context.desired_child(usize(0));
        if (navigation.is_err()) return Err(navigation.unwrap_err());
        const auto desktop = wide(size.width);
        const auto head = desktop ? 0 : rstd::min(size.height, navigation->height);
        const auto margin = inset(size.width);
        auto placed = context.arrange_child(usize(0),
            {0, 0, desktop ? sidebar : size.width, desktop ? size.height : head});
        if (placed.is_err()) return placed;

        const auto x = margin + (desktop ? sidebar : 0);
        const auto top = rstd::min(size.height, head + margin);
        const auto bottom = rstd::cmp::max(top, size.height - margin);
        const auto width = content_width(size.width);
        auto title = context.desired_child(usize(1));
        if (title.is_err()) return Err(title.unwrap_err());
        const auto height = rstd::min(bottom - top, title->height);
        placed = context.arrange_child(usize(1), {x, top, width, height});
        if (placed.is_err()) return placed;
        const auto pages_top = rstd::min(bottom, top + height + gap);
        return context.arrange_child(usize(2), {x, pages_top, width, bottom - pages_top});
    }
};

struct Tabs {
    UiSession &session;
    state::Cell<Page> selected;
};

struct TabButtons { cx::NodeRef nodes[tab_count]; };

struct TabKeys {
    using State = TabKeys;
    cx::Borrowed<Tabs> tabs;
    TabButtons buttons;
    Page page;
    bool vertical;

    auto create() const -> State { return *this; }
    void update(State &state, cx::BehaviorContext &) const { state = *this; }

    void handle(EventContext &context, const RoutedEvent &event) {
        if (event.input.type != EventType::KeyDown || event.current != event.target) return;
        const auto previous = vertical ? Key::Up : Key::Left;
        const auto next = vertical ? Key::Down : Key::Right;
        auto index = unsigned(page);
        if (event.input.key == previous) index = (index + tab_count - 1) % tab_count;
        else if (event.input.key == next) index = (index + 1) % tab_count;
        else if (event.input.key == Key::Home || event.input.key == Key::PageUp) index = 0;
        else if (event.input.key == Key::End || event.input.key == Key::PageDown) index = tab_count - 1;
        else return;
        auto id = buttons.nodes[index].id();
        if (id.is_err()) return;
        if (tabs->session.input().request_focus(Some(id.unwrap())).is_err()) return;
        tabs->selected.set(Page(index)).unwrap();
        context.prevent_default();
        context.stop_propagation();
    }
};

auto tab(cx::Borrowed<Tabs> tabs, const TabButtons &buttons, Page page, bool selected, bool desktop, Scalar width)
    -> cx::Element {
    const auto normal = selected ? selected_surface : white(0);
    auto style = NodeStyleBuilder{}.height(44);
    if (desktop) style.horizontal(Alignment::Stretch);
    else style.width(width);
    auto title = label(page_title(page), 14, selected ? foreground : muted, true)
        .style(label_style().horizontal(desktop ? Alignment::Start : Alignment::Center).build());
    if (desktop)
        title = cx::stack({rstd::move(title)}).style(fill().padding(12).hit_test_visible(false).build());
    auto appearance = ButtonStyleBuilder{}.normal(normal).pressed(pressed_surface).focused(normal).radius(22).build();
    return control_finish(cx::button(rstd::move(title), [tabs, page] { tabs->selected.set(page).unwrap(); }, appearance)
        .style(style.build()).reference(buttons.nodes[unsigned(page)])
        .behavior(cx::Key{1}, TabKeys{tabs, buttons, page, desktop}), selected);
}

[[vui::compose]]
auto tab_list(cx::Borrowed<Tabs> tabs, Page current, Scalar width, bool desktop) -> cx::ComposeResult<cx::Element> {
    auto remembered = cx::remember<TabButtons>([] { return TabButtons{}; });
    if (remembered.is_err()) return Err(remembered.unwrap_err());
    auto buttons = remembered.unwrap().get();
    if (buttons.is_err()) return Err(buttons.unwrap_err());
    constexpr Scalar padding = 12;
    constexpr Scalar spacing = 6;
    const auto tab_width = rstd::cmp::max(Scalar(0), (width - 2 * padding - (tab_count - 1) * spacing) / tab_count);
    auto wordmark = label("vui gallery"_str, 20, foreground, true)
        .style(label_style().horizontal(desktop ? Alignment::Start : Alignment::Center).build());
    auto items = cx::elements({
        tab(tabs, **buttons, Page::Controls, current == Page::Controls, desktop, tab_width),
        tab(tabs, **buttons, Page::Lists, current == Page::Lists, desktop, tab_width),
        tab(tabs, **buttons, Page::Motion, current == Page::Motion, desktop, tab_width),
        tab(tabs, **buttons, Page::Canvas, current == Page::Canvas, desktop, tab_width),
    });
    auto group = desktop ? cx::column(rstd::move(items)) : cx::row(rstd::move(items));
    return Ok(cx::column({rstd::move(wordmark), rstd::move(group)
        .style(NodeStyleBuilder{}.horizontal(Alignment::Stretch).build()).gap(spacing)})
        .style(fill().padding(padding).background(navigation_surface).build()).gap(desktop ? 26 : 10));
}

auto tab_panel(Page page, Page current, cx::Element content) -> cx::Element {
    return cx::stack({rstd::move(content)}).style(fill().hit_self(false).visible(page == current).build());
}

[[vui::compose]]
auto dialog(cx::Borrowed<state::Cell<bool>> shown, state::Value<Insets> insets) -> cx::ComposeResult<cx::Element> {
    auto open = cx::read(shown->value());
    if (open.is_err()) return Err(open.unwrap_err());
    if (!open->value()) return Ok(cx::stack({}).style(NodeStyleBuilder{}.hit_self(false).build()));
    auto area = cx::read(insets);
    if (area.is_err()) return Err(area.unwrap_err());
    auto close = cx::node();
    if (close.is_err()) return Err(close.unwrap_err());
    auto button = control_finish(cx::button(button_label("Close"_str), [shown] { shown->set(false).unwrap(); },
        button_style()).style(block(112, 44, {0, 0, 0, 0})).reference(*close));
    auto panel = cx::with_semantics(glass(LayoutKind::Column, cx::elements({label("Dialog"_str, 20, foreground, true), rstd::move(button)}),
        {.blur = 12, .tint = Color::from_srgba8(16, 16, 16, 168).unwrap()})
        .style(block(320, 146, white(0), panel_radius)).padding(20).gap(18),
        {.role = SemanticsRole::Dialog, .label = String::make("Dialog"_str), .merge_descendants = true});
    cx::PopupOptions options;
    options.initial_focus = Some(close.unwrap());
    options.backdrop = {0, 0, 0, .32f};
    options.content_insets = area->value();
    return Ok(cx::dialog(rstd::move(panel), [shown](PopupCloseReason) { shown->set(false).unwrap(); }, rstd::move(options)));
}
[[vui::compose]]
auto gallery_content(cx::Borrowed<Tabs> tabs, cx::Borrowed<LevelRows> rows, cx::NodeRef picture,
             cx::Borrowed<state::Cell<bool>> dialog_open, state::Value<Insets> insets,
             state::Value<Scalar> width, cx::Borrowed<state::Cell<CanvasOptions>> canvas_options,
             cx::Borrowed<CanvasRecording> canvas_recording) -> cx::ComposeResult<cx::Element> {
    auto selected = cx::read(tabs->selected.value());
    if (selected.is_err()) return Err(selected.unwrap_err());
    auto available = cx::read(width);
    if (available.is_err()) return Err(available.unwrap_err());
    auto current = selected->value();
    auto content_width = GalleryLayout::content_width(available->value());

    auto controls_page = controls(dialog_open, content_width);
    if (controls_page.is_err()) return Err(controls_page.unwrap_err());
    auto lists_page = lists(rows);
    if (lists_page.is_err()) return Err(lists_page.unwrap_err());
    auto motion_page = motion(cx::borrow(tabs->session), picture, content_width);
    if (motion_page.is_err()) return Err(motion_page.unwrap_err());
    auto canvas_page = cx::stack({});
    if (current == Page::Canvas) {
        auto workload = canvas_workload(canvas_options, canvas_recording, content_width);
        if (workload.is_err()) return Err(workload.unwrap_err());
        canvas_page = workload.unwrap();
    }
    auto panels = cx::stack({
        tab_panel(Page::Controls, current, controls_page.unwrap()),
        tab_panel(Page::Lists, current, lists_page.unwrap()),
        tab_panel(Page::Motion, current, motion_page.unwrap()),
        tab_panel(Page::Canvas, current, rstd::move(canvas_page)),
    }).style(fill().hit_self(false).build());
    auto navigation = GalleryLayout::wide(available->value())
        ? tab_list(tabs, current, available->value(), true)
        : tab_list(tabs, current, available->value(), false);
    if (navigation.is_err()) return Err(navigation.unwrap_err());
    auto body = layout_part(GalleryLayout{}, cx::elements({
        navigation.unwrap(), label(page_title(current), 30, foreground, true), rstd::move(panels),
    })).style(fill().hit_self(false).build());
    auto overlay = dialog(dialog_open, insets);
    if (overlay.is_err()) return Err(overlay.unwrap_err());
    return Ok(cx::popup_host(rstd::move(body), {overlay.unwrap()}).style(fill().hit_self(false).build()));
}
} // namespace

export namespace examples::gallery {
class View {
    UiSession &session_;
    UiId background_, top_bar_, root_;
    cx::NodeRef picture_;
    state::Graph graph_;
    LevelRows rows_{graph_};
    Tabs tabs_;
    state::Cell<Scalar> width_{graph_.cell(Scalar(0)).unwrap()};
    state::Cell<bool> dialog_open_{graph_.cell(false).unwrap()};
    state::Cell<Insets> dialog_insets_{graph_.cell(Insets{}).unwrap()};
    state::Cell<CanvasOptions> canvas_options_;
    CanvasRecording canvas_recording_;
    Option<cx::Composition> composition_;

  public:
    static constexpr Serial canvas_warmup_frames = detail::canvas_warmup;
    using CanvasScene = detail::CanvasScene;
    struct CanvasLaunch {
        Index particles{detail::canvas_loads[1]};
        CanvasScene scene{CanvasScene::Particles};
    };

    static auto supports_canvas_load(Index count) -> bool {
        for (auto load : detail::canvas_loads)
            if (count == load) return true;
        return false;
    }

    explicit View(UiSession &session, Option<CanvasLaunch> canvas = {})
        : session_(session), tabs_{session_, graph_.cell(canvas ? Page::Canvas : Page::Controls).unwrap()},
          canvas_options_(graph_.cell(CanvasOptions{.particles = canvas ? canvas->particles : detail::canvas_loads[1],
                                                    .scene = canvas ? canvas->scene : CanvasScene::Particles}).unwrap()) {}

    auto mount() -> Result<empty, UiError> {
        auto &ui = session_.document();
        auto service = Box<vectext::SystemTextService>::make();
        auto installed = ui.set_text_service(
            Box<TextService>::from_raw(rstd::move(service).into_raw().template cast<TextService>()));
        if (installed.is_err()) return installed;
        auto background = ui.create<Stack>(fill().background(canvas).hit_self(false).build());
        if (background.is_err()) return Err(background.unwrap_err());
        background_ = background.unwrap();
        auto painted = ui.install_paint(background_, BackdropPaint{});
        if (painted.is_err()) return painted;
        auto top = ui.append<Rectangle>(background_, NodeStyleBuilder{}.background(navigation_surface).hit_test_visible(false).build());
        if (top.is_err()) return Err(top.unwrap_err());
        top_bar_ = top.unwrap();
        auto root = ui.append<Stack>(background_, fill().hit_self(false).build());
        if (root.is_err()) return Err(root.unwrap_err());
        root_ = root.unwrap();
        auto mounted = ui.mount(background_);
        if (mounted.is_err()) return mounted;

        auto made = cx::Composition::create(session_, root_);
        if (made.is_err()) return Err(UiError::LayoutFailed);
        auto composition = made.unwrap();
        auto content = gallery_content(cx::borrow(tabs_), cx::borrow(rows_), picture_, cx::borrow(dialog_open_),
            dialog_insets_.value(), width_.value(), cx::borrow(canvas_options_), cx::borrow(canvas_recording_));
        if (content.is_err()) return Err(UiError::InvalidValue);
        auto applied = composition.set_content(content.unwrap());
        if (applied.is_err()) return Err(UiError::PaintFailed);
        composition_ = Some(rstd::move(composition));
        return Ok(empty{});
    }

    auto resize(Size surface, Rect viewport) -> Result<empty, UiError> {
        auto &ui = session_.document();
        auto resized = ui.set_layout(background_, LayoutStyleBuilder{}.width(surface.width).height(surface.height).build());
        if (resized.is_err()) return resized;
        resized = ui.set_layout(top_bar_, LayoutStyleBuilder{}.width(surface.width).height(viewport.y).build());
        if (resized.is_err()) return resized;
        const auto width = rstd::min(Scalar(1000), viewport.width);
        resized = ui.set_layout(root_, LayoutStyleBuilder{}.width(width).height(viewport.height).build());
        if (resized.is_err()) return resized;
        auto moved = ui.set_transform(root_, {1, 0, 0, 1,
            viewport.x + (viewport.width - width) / 2, viewport.y});
        if (moved.is_err()) return moved;
        if (width_.set(width).is_err()) return Err(UiError::InvalidValue);
        return dialog_insets_.set(examples::content_insets(surface, viewport)).is_ok()
            ? Result<empty, UiError>(Ok(empty{})) : Result<empty, UiError>(Err(UiError::InvalidValue));
    }

    auto dismiss_dialog() -> bool {
        auto open = state::read(dialog_open_.value());
        return open.is_ok() && open->value() && dialog_open_.set(false).is_ok();
    }

    auto status() -> Result<empty, UiError> {
        if (composition_.is_none()) return Err(UiError::InvalidValue);
        return examples::composition_status(*composition_);
    }
    auto picture() const -> cx::ComposeResult<UiId> { return picture_.id(); }
    auto canvas_paint_time() const -> CpuTiming { return canvas_recording_.paint; }
};
} // namespace examples::gallery
