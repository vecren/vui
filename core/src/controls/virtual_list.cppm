export module vui.controls:virtual_list;
import vui;
import :types;
import :scroll_view;
using namespace rstd::prelude;

export namespace vui {
struct VirtualItem { Serial key; Index index; };
struct VirtualListItem { VirtualItem item; UiId node; };
struct VirtualListOptions {
    bool horizontal{};
    Scalar item_extent{40}, spacing{};
    Insets padding;
    Option<Scalar> item_cross_extent;
    Index overscan{1}, max_materialized{4096};
    Scalar line_step{40};
    ScrollWheelMode wheel_mode{ScrollWheelMode::Direct};
    Point wheel_direction{1,1};
    ScrollMotionOptions motion;
    auto valid() const -> bool;
};
struct VirtualItemFactory {
    template <class Self, class Delegate = void> struct Api {
        using Trait = VirtualItemFactory;
        auto make(UiSession &session, VirtualItem item) -> Result<UiId, UiError> { return rstd::trait_call<0>(this, session, item); }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::make>;
};
struct VirtualListStatus { Index count{}, materialized{}; Option<UiError> error; };
class VirtualListState {
    UiSession &session_;
    Box<rstd::dyn<VirtualItemFactory>> factory_;
    Vec<Serial> keys_;
    Vec<VirtualListItem> cells_;
    UiId content_, scroll_;
    VirtualListOptions options_;
    ViewportMetrics metrics_;
    Binding lifetime_;
    Binding metrics_binding_;
    rstd::rc::Weak<VirtualListState> self_;
    Option<UiError> error_;
    bool scheduled_{}, closed_{}, reset_{}, refreshing_{};
    auto ui() -> UiDocument & { return session_.document(); }
    auto axis(Point value) const -> Scalar { return options_.horizontal ? value.x : value.y; }
    auto axis(Size value) const -> Scalar { return options_.horizontal ? value.width : value.height; }
    auto start() const -> Scalar { return options_.horizontal ? options_.padding.left : options_.padding.top; }
    auto end() const -> Scalar { return options_.horizontal ? options_.padding.right : options_.padding.bottom; }
    struct Range { Index first{}, last{}; auto operator==(const Range &) const -> bool = default; };
    auto range() const -> Range;
    void remove_cells() {
        for (auto cell : cells_) if (ui().valid(cell.node)) ui().destroy(cell.node).unwrap();
        cells_.clear();
    }
    auto refresh() -> Result<empty, UiError>;
  public:
    VirtualListState(UiSession &session, Box<rstd::dyn<VirtualItemFactory>> factory, UiId content, VirtualListOptions options)
        : session_(session), factory_(rstd::move(factory)), content_(content), options_(options), lifetime_(session.document().bindings().make()) {}
    void connect(rstd::rc::Weak<VirtualListState> self, UiId scroll);
    auto reset() -> Result<empty, UiError> {
        if (refreshing_) return Err(UiError::Routing);
        reset_ = true; return request();
    }
    auto request() -> Result<empty, UiError>;
    auto set_items(slice<Serial> keys) -> Result<empty, UiError>;
    auto extent(usize count) const -> Scalar { return start() + end() + (count == usize() ? 0 : Scalar(count.to_primitive()) * options_.item_extent + Scalar(count.to_primitive() - 1) * options_.spacing); }
    auto items() const -> slice<VirtualListItem> { return cells_.as_slice(); }
    auto status() const -> VirtualListStatus { return {Index(keys_.len().to_primitive()), Index(cells_.len().to_primitive()), error_}; }
    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError>;
    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError>;
};
struct VirtualListContentLayout {
    rstd::rc::Rc<VirtualListState> state;
    auto measure(LayoutMeasureContext &context, LayoutConstraints constraints) -> Result<Size, UiError> { return state.get()->measure(context, constraints); }
    auto arrange(LayoutArrangeContext &context, Size size) -> Result<empty, UiError> { return state.get()->arrange(context, size); }
};
struct VirtualListHandle { rstd::rc::Rc<VirtualListState> state; };
class VirtualList {
    static auto handle(UiDocument &ui, UiId id) -> VirtualListHandle * {
        auto content = ScrollView::content(ui, id);
        return content.is_ok() && content.unwrap().is_some() ? ui.state<VirtualListHandle>(*content.unwrap()) : nullptr;
    }
  public:
    static auto reset(UiDocument &ui, UiId id) -> Result<empty, UiError> {
        auto value = handle(ui, id); return value ? value->state.get()->reset() : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    static auto refresh(UiDocument &ui, UiId id) -> Result<empty, UiError> {
        auto handle = VirtualList::handle(ui, id); return handle ? handle->state.get()->request() : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    template <class T> requires rstd::Impled<T, VirtualItemFactory>
    static auto create(UiSession &session, NodeStyle style, VirtualListOptions options, rstd::rc::Weak<T> factory) -> Result<UiId, ControlError> {
        auto &ui = session.document(); if (!options.valid()) return Err(ControlError{Some(UiError::InvalidValue), {}});
        struct Adapter {
            rstd::rc::Weak<T> owner;
            auto make(UiSession &session, VirtualItem item) -> Result<UiId, UiError> {
                auto value = owner.upgrade(); if (value.is_none()) return Err(UiError::Cancelled);
                return rstd::as<VirtualItemFactory>(*value->get()).make(session, item);
            }
        };
        NodeStyle content_style; content_style.layout.horizontal = options.horizontal ? Alignment::Start : Alignment::Stretch;
        content_style.layout.vertical = options.horizontal ? Alignment::Stretch : Alignment::Start;
        if (options.item_cross_extent.is_some()) {
            if (options.horizontal) content_style.layout.vertical = Alignment::Start; else content_style.layout.horizontal = Alignment::Start;
        }
        auto made = ui.create<Stack>(content_style); if (made.is_err()) return Err(ControlError{Some(made.unwrap_err()), {}});
        auto content = made.unwrap();
        auto state = rstd::rc::make_rc<VirtualListState>(session, Box<rstd::dyn<VirtualItemFactory>>::make(Adapter{rstd::move(factory)}), content, options);
        auto scroll = ScrollView::create(session, content, style, {{options.horizontal, !options.horizontal}, options.line_step,options.wheel_mode,options.wheel_direction,options.motion});
        if (scroll.is_err()) { ui.destroy(content); return Err(scroll.unwrap_err()); }
        auto id = scroll.unwrap();
        ui.install_layout(content, VirtualListContentLayout{state}).unwrap(); ui.attach_state(content, VirtualListHandle{state}).unwrap(); state.get()->connect(state.downgrade(), id);
        return Ok(id);
    }
    static auto set_items(UiDocument &ui, UiId id, slice<Serial> keys) -> Result<empty, UiError> {
        auto handle = VirtualList::handle(ui, id); return handle ? handle->state.get()->set_items(keys) : Result<empty, UiError>(Err(UiError::InvalidId));
    }
    static auto items(UiDocument &ui, UiId id) -> Result<slice<VirtualListItem>, UiError> {
        auto handle = VirtualList::handle(ui, id); return handle ? Result<slice<VirtualListItem>, UiError>(Ok(handle->state->items())) : Result<slice<VirtualListItem>, UiError>(Err(UiError::InvalidId));
    }
    static auto status(UiDocument &ui, UiId id) -> Result<VirtualListStatus, UiError> {
        auto handle = VirtualList::handle(ui, id); return handle ? Result<VirtualListStatus, UiError>(Ok(handle->state->status())) : Result<VirtualListStatus, UiError>(Err(UiError::InvalidId));
    }
    static auto item(UiDocument &ui, UiId id, Serial key) -> Result<Option<UiId>, UiError> {
        auto active = items(ui, id); if (active.is_err()) return Err(active.unwrap_err());
        for (auto cell : active.unwrap()) if (cell.item.key == key) return Ok(Some(UiId(cell.node))); return Ok(None<UiId>());
    }
};
} // namespace vui
