export module vui:interaction.navigation;
import :input.router;
using namespace rstd::prelude;

export namespace vui {
struct TabNavigation {
    void operator()(EventRouter &router, const InputEvent &input) const {
        if (input.type != EventType::KeyDown || input.repeat) return;
        if (input.key == Key::Tab) {
            (void)router.move_focus(input.modifiers.shift);
            return;
        }
        if (input.modifiers.control || input.modifiers.alt || input.modifiers.meta || input.modifiers.shift) return;
        switch (input.key) {
        case Key::Left: (void)router.move_focus(FocusDirection::Left); break;
        case Key::Right: (void)router.move_focus(FocusDirection::Right); break;
        case Key::Up: (void)router.move_focus(FocusDirection::Up); break;
        case Key::Down: (void)router.move_focus(FocusDirection::Down); break;
        default: break;
        }
    }
};
} // namespace vui
