export module vui:interaction.navigation;
import :events;
using namespace rstd::prelude;

export namespace vui {
struct TabNavigation {
    void operator()(EventRouter &router, const InputEvent &input) const {
        if (input.type != EventType::KeyDown || input.key != Key::Tab || input.repeat)
            return;
        auto candidates = router.focus_candidates();
        if (candidates.is_empty())
            return;
        auto focused = router.focused();
        usize index{};
        while (index < candidates.len() && (focused.is_none() || candidates[index] != *focused))
            ++index;
        if (input.modifiers.shift)
            index = index == usize() || index == candidates.len() ? candidates.len() - usize(1)
                                                                  : index - usize(1);
        else
            index = index == candidates.len() ? usize() : (index + usize(1)) % candidates.len();
        router.request_focus(Some(UiId(candidates[index])));
    }
};
} // namespace vui
