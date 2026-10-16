export module vui:text_edit;
import :text;
import :state;
import :input.protocol;
using namespace rstd::prelude;

export namespace vui {
struct TextSelection {
    Index anchor{}, cursor{};
    auto range() const -> TextRange { return {rstd::cmp::min(anchor, cursor), rstd::cmp::max(anchor, cursor)}; }
    auto operator==(const TextSelection &) const -> bool = default;
};
struct TextEditOptions {
    bool single_line{true};
    Index max_graphemes{}, max_bytes{1048576}, history_limit{64};
};
struct TextEditError { Option<TextError> text; Option<state::StateError> state; };
enum class TextEditMove { Previous, Next, PreviousWord, NextWord, Start, End };
struct TextEditState {
    TextBoundaries text, display;
    TextSelection selection;
    TextRange composition;
    Option<TextInputSelection> preedit_selection;
    Index caret{};
    bool composing{}, can_undo{}, can_redo{};
    auto operator==(const TextEditState &) const -> bool = default;
    auto selected_text() const -> ref<str> {
        auto range = selection.range(); return text.text()->get(usize(range.start), usize(range.end)).unwrap();
    }
    auto committed_index(Index index, TextBoundaryBias bias = TextBoundaryBias::After) const -> Result<Index, TextError> {
        if (!display.contains(index)) return Err(TextError::InvalidIndex);
        if (composing) {
            auto range = selection.range();
            if (index >= composition.end) index = range.end + index - composition.end;
            else if (index > composition.start) index = bias == TextBoundaryBias::Before ? range.start : range.end;
        }
        return text.snap(index, bias);
    }
};
class TextEdit {
    struct Impl;
    Box<Impl> data_;
    explicit TextEdit(Box<Impl>);
  public:
    ~TextEdit();
    TextEdit(const TextEdit &) = delete;
    auto operator=(const TextEdit &) -> TextEdit & = delete;
    TextEdit(TextEdit &&);
    auto operator=(TextEdit &&) -> TextEdit &;
    static auto create(ref<str> text, TextEditOptions options = {}) -> Result<TextEdit, TextEditError>;
    auto value() const -> state::Value<TextEditState>;
    auto state() const -> Result<TextEditState, TextEditError>;
    auto set_text(ref<str>) -> Result<bool, TextEditError>;
    auto select(TextSelection) -> Result<bool, TextEditError>;
    auto select_all() -> Result<bool, TextEditError>;
    auto move(TextEditMove, bool extend = false) -> Result<bool, TextEditError>;
    auto insert(ref<str>) -> Result<bool, TextEditError>;
    auto erase_backwards() -> Result<bool, TextEditError>;
    auto erase_forwards() -> Result<bool, TextEditError>;
    auto preedit(const TextInput &) -> Result<bool, TextEditError>;
    auto cancel_composition() -> Result<bool, TextEditError>;
    auto undo() -> Result<bool, TextEditError>;
    auto redo() -> Result<bool, TextEditError>;
};
} // namespace vui
