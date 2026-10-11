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
struct ButtonOptions {
    bool focus_on_press{true}, capture_on_press{true}, pointer_enabled{true};
    Key first_key{Key::Space}, second_key{Key::Enter};
};
} // namespace vui
