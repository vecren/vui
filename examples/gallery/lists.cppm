export module vui.examples.gallery:lists;
import :style;
import :parts;
import :glass;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
namespace {
constexpr Index level_count = 2048;

struct LevelLayout {
    static constexpr Scalar rail_width = 30;
    static constexpr Scalar gap = 6;

    static auto list_width(Scalar width) -> Scalar {
        return rstd::cmp::max(Scalar(0), width - rail_width - gap);
    }

    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError> {
        auto width = constraints.max_width.unwrap_or(0);
        auto rail = rstd::min(width, rail_width);
        auto list = context.measure_child(usize(0), {Some(list_width(width)), constraints.max_height});
        if (list.is_err()) return Err(list.unwrap_err());
        auto bar = context.measure_child(usize(1), {Some(rail), constraints.max_height});
        if (bar.is_err()) return Err(bar.unwrap_err());
        return Ok(Size{width, constraints.max_height.unwrap_or(rstd::cmp::max(list->height, bar->height))});
    }

    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> {
        auto list = context.arrange_child(usize(0), {0, 0, list_width(size.width), size.height});
        if (list.is_err()) return list;
        auto rail = rstd::min(size.width, rail_width);
        return context.arrange_child(usize(1), {size.width - rail, 0, rail, size.height});
    }
};
} // namespace

auto row_style(bool active) -> ButtonStyle {
    return ButtonStyleBuilder{}.normal(active ? selected_surface : white(.014f))
        .pressed(pressed_surface).focused(active ? pressed_surface : control_surface).radius(8).build();
}

struct LevelSelection {
    UiId list;
    Serial selected{1};

    void select(UiDocument &ui, UiId row, Serial key) {
        if (key == selected) return;
        auto previous = VirtualList::item(ui, list, selected).unwrap();
        if (previous.is_some()) Button::set_style(ui, *previous, row_style(false)).unwrap();
        selected = key;
        Button::set_style(ui, row, row_style(true)).unwrap();
    }
};

struct LevelRows {
    rstd::rc::Rc<LevelSelection> selection;

    LevelRows();

    auto make(UiSession &session, VirtualItem item) -> Result<UiId, UiError> {
        auto &ui = session.document();
        auto made = Button::create(session, fill().padding(12).build(), row_style(item.key == selection->selected));
        if (made.is_err()) return Err(made.unwrap_err().ui.unwrap_or(UiError::InvalidValue));
        auto row = made.unwrap();
        auto fail = [&](UiError error) -> Result<UiId, UiError> {
            ui.destroy(row).unwrap();
            return Err(error);
        };
        auto number = rstd::format("Level {:04}", item.key);
        auto label = append_label(ui, row, number.as_str(), label_style().build(), text_format(15, true), foreground);
        if (label.is_err()) return fail(label.unwrap_err());
        auto properties = Button::default_semantics();
        properties.label = rstd::move(number);
        auto described = session.semantics().set(row, rstd::move(properties));
        if (described.is_err()) return fail(described.unwrap_err());
        auto clicked = Button::on_clicked(ui, row,
            [owner = selection.clone(), row, key = item.key](EventContext &context, const ClickEvent &) mutable {
                owner.get()->select(context.document(), row, key);
            });
        if (clicked.is_err()) return fail(clicked.unwrap_err().ui.unwrap_or(UiError::InvalidValue));
        auto kept = ui.attach_state(row, clicked.unwrap(), NodeStateKey::of<Binding>());
        if (kept.is_err()) return fail(kept.unwrap_err());
        return Ok(row);
    }
};

LevelRows::LevelRows() : selection(rstd::rc::make_rc<LevelSelection>()) {}

using LevelRowsOwner = rstd::rc::Rc<LevelRows>;

auto mount_levels(UiSession &session, UiId parent, LevelRowsOwner &rows, UiId scrollbar) -> Result<empty, UiError> {
    auto &ui = session.document();
    VirtualListOptions options;
    options.item_extent = 48;
    options.spacing = 3;
    options.line_step = 51;
    options.motion = touch_scroll().motion;
    auto list = VirtualList::create(session, fill().build(), options, rows.downgrade());
    if (list.is_err()) return Err(list.unwrap_err().ui.unwrap_or(UiError::InvalidValue));
    auto content = list.unwrap();
    rows.get()->selection.get()->list = content;
    auto attached = ui.attach(parent, content);
    if (attached.is_err()) return attached;
    auto bound = Scrollbar::bind_scroll_view(ui, scrollbar, content);
    if (bound.is_err()) return bound;
    Vec<Serial> keys;
    keys.reserve(usize(level_count));
    for (Index i = 1; i <= level_count; ++i) keys.push(Serial(i));
    return VirtualList::set_items(ui, content, keys.as_slice());
}

[[vui::compose]]
auto lists(cx::Borrowed<LevelRowsOwner> rows) -> cx::ComposeResult<cx::Element> {
    auto made = cx::node();
    if (made.is_err()) return Err(made.unwrap_err());
    auto scrollbar = made.unwrap();
    auto content = native_part([rows, scrollbar](UiSession &session, UiId host) -> Result<empty, UiError> {
        auto id = scrollbar.id();
        if (id.is_err()) return Err(UiError::InvalidId);
        return mount_levels(session, host, rows.get(), *id);
    }).style(fill().hit_self(false).build());

    auto thumb = cx::native(LayoutKind::Leaf)
        .style(NodeStyleBuilder{}.background(muted).radius(5).hit_test_visible(false).build());
    auto bar = control_finish(cx::scrollbar(rstd::move(thumb),
        {.axis = ScrollbarAxis::Vertical, .minimum_handle = 36, .handle_insets = {10, 0, 10, 0}})
        .style(fill().background(white(.02f)).radius(10).build()).reference(scrollbar), false);
    auto list = layout_part(LevelLayout{}, cx::elements({rstd::move(content), rstd::move(bar)}))
        .style(fill().hit_self(false).build());
    return Ok(glass(LayoutKind::Stack, cx::elements({rstd::move(list)}), {.opacity = .74f})
        .style(fill().padding(5).radius(panel_radius).hit_self(false).build()));
}

auto lists_status(UiDocument &ui, const LevelRows &rows) -> Result<empty, UiError> {
    if (!rows.selection->list.valid()) return Ok(empty{});
    auto status = VirtualList::status(ui, rows.selection->list);
    if (status.is_err()) return Err(status.unwrap_err());
    if (status->error.is_some()) return Err(*status->error);
    return Ok(empty{});
}
} // namespace examples::gallery::detail
