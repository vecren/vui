export module vui.controls:types;
import vui;
using namespace rstd::prelude;
export namespace vui {
struct ControlError {
    Option<UiError> ui;
    Option<InputError> input;
};
struct ClickEvent {
    UiId target;
    InputEvent input;
};
struct ButtonStatus {
    bool pressed{}, focused{}, hovered{}, held{};
    friend auto operator==(const ButtonStatus &, const ButtonStatus &) -> bool = default;
};
enum class ButtonVisualState { Normal, Hovered, Pressed, Focused, Disabled };
inline auto button_visual_state(ButtonStatus status, bool enabled, bool retain_press_outside=false) -> ButtonVisualState {
    return !enabled ? ButtonVisualState::Disabled
        : (retain_press_outside ? status.held : status.pressed) ? ButtonVisualState::Pressed
        : status.focused ? ButtonVisualState::Focused
        : status.hovered ? ButtonVisualState::Hovered : ButtonVisualState::Normal;
}
struct ButtonTintStyle {
    Color normal{1, 1, 1, 1}, hovered{1, 1, 1, 1}, pressed{1, 1, 1, 1},
        focused{1, 1, 1, 1}, disabled{.5, .5, .5, .5};
    Serial duration_ns{100'000'000}, frame_interval_ns{16'666'667};
    bool retain_press_outside{};
    auto valid() const -> bool {
        return normal.valid() && hovered.valid() && pressed.valid() && focused.valid() &&
               disabled.valid() && frame_interval_ns > 0;
    }
};
struct ButtonTintStatus {
    Color value{1, 1, 1, 1};
    bool transitioning{};
    Option<UiError> error;
};
enum class ButtonAppearance { Default, Custom };
struct ButtonOptions {
    bool focus_on_press{true}, capture_on_press{true}, pointer_enabled{true};
    Key first_key{Key::Space}, second_key{Key::Enter};
    ButtonAppearance appearance{ButtonAppearance::Default};
};
} // namespace vui
