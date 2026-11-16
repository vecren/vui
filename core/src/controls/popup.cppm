export module vui.controls:popup;
import vui;
import :types;
using namespace rstd::prelude;

export namespace vui {
enum class PopupCloseReason { Escape, OutsidePress };
enum class EscapePolicy { Ignore, RequestClose };
enum class OutsidePressPolicy { Ignore, RequestClose };
enum class RestoreFocus { None, Previous };
struct PopupOptions {
    bool modal{true}, owns_content{};
    Option<UiId> initial_focus;
    Option<ScopeId> parent_scope;
    RestoreFocus restore_focus{RestoreFocus::Previous};
    EscapePolicy escape{EscapePolicy::Ignore};
    OutsidePressPolicy outside_press{OutsidePressPolicy::Ignore};
    Color backdrop{0, 0, 0, 0};
    auto operator==(const PopupOptions &) const -> bool = default;
};
class Popup {
    struct State {
        UiSession &session;
        UiId root, content, host;
        PopupOptions options;
        Binding lifetime, focus_lifetime;
        Binding key_bubble, key_target, outside, global_outside_capture, global_outside_target;
        InputScopeRegistration scope;
        FocusBookmark bookmark;
        Signal<void(PopupCloseReason)> requested;
        rstd::rc::Weak<State> self;
        Serial identity{new_session()};
        bool opened{}, owns_root{};
        State(UiSession &s, UiId r, UiId c, PopupOptions o, bool owned)
            : session(s), root(r), content(c), options(o),
              lifetime(s.document().bindings().make()), owns_root(owned) {}
        auto connect() -> Result<empty, ControlError>;
        auto connect_outside() -> Result<empty, ControlError>;
        auto configure(PopupOptions) -> Result<empty, ControlError>;
        auto replace_content(UiId) -> Result<empty, ControlError>;
        auto open() -> Result<empty, ControlError>;
        auto restore() -> Option<FocusBookmark>;
        void outside_press(EventContext &, const RoutedEvent &event);
        auto close() -> Result<empty, ControlError>;
        void key(EventContext &context, const RoutedEvent &event);
        void press(EventContext &context, const RoutedEvent &event);
    };
    struct RootBinding { rstd::rc::Weak<State> receiver; Serial identity{}; };
    rstd::rc::Rc<State> state_;
    explicit Popup(rstd::rc::Rc<State> state) : state_(rstd::move(state)) {}
    auto change(bool open) -> Result<empty, ControlError>;
    static auto initialize(UiSession &, UiId, UiId, PopupOptions, bool) -> Result<Popup, ControlError>;
    void dispose();

  public:
    Popup() = default;
    Popup(const Popup &) = delete;
    Popup(Popup &&) = default;
    auto operator=(Popup &&other) -> Popup &;
    ~Popup() { dispose(); }
    static auto create(UiSession &session, UiId parent, UiId content, PopupOptions options = {})
        -> Result<Popup, ControlError>;
    static auto bind(UiSession &session, UiId root, UiId content, PopupOptions options = {})
        -> Result<Popup, ControlError>;
    auto set_options(PopupOptions options) -> Result<empty, ControlError>;
    auto set_content(UiId content) -> Result<empty, ControlError>;
    auto root() const -> UiId { return state_ ? state_.get()->root : UiId{}; }
    auto scope_id() const -> ScopeId { return state_ ? state_.get()->scope.id() : ScopeId{}; }
    auto is_open() const -> bool;
    auto open() -> Result<empty, ControlError> { return change(true); }
    auto close() -> Result<empty, ControlError> { return change(false); }
    template <class F> auto on_close_requested(F callback) -> Result<Binding, SignalError> {
        if (!state_ || !state_.get()->lifetime.alive())
            return Err(SignalError::Closed);
        return state_.get()->requested.connect(state_.get()->session.document().bindings(),
                                               rstd::move(callback));
    }
    template <class T, class F>
    auto bind_close_requested(rstd::rc::Weak<T> receiver, F callback)
        -> Result<Binding, SignalError> {
        if (!state_ || !state_.get()->lifetime.alive())
            return Err(SignalError::Closed);
        return state_.get()->requested.bind(state_.get()->session.document().bindings(),
                                            rstd::move(receiver), rstd::move(callback));
    }
};
} // namespace vui
