export module vui:interaction.navigation;
import :input.router;
using namespace rstd::prelude;

export namespace vui {
struct TabNavigation {
    void operator()(EventRouter &router, const InputEvent &input) const {
        if (input.type != EventType::KeyDown || input.key != Key::Tab || input.repeat)
            return;
        (void)router.move_focus(input.modifiers.shift);
    }
};
} // namespace vui
