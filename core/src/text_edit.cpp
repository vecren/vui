module vui;
import :text_edit;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
namespace {
auto edit_error(TextError error) -> TextEditError { return {Some(error), {}}; }
auto edit_error(state::StateError error) -> TextEditError { return {{}, Some(error)}; }
auto normalized(ref<str> text, TextEditOptions options) -> Result<String, TextEditError> {
    if (text->len() > usize(options.max_bytes)) return Err(edit_error(TextError::Limit));
    if (rstd::str_::from_utf8(text->as_bytes()).is_err()) return Err(edit_error(TextError::InvalidUtf8));
    String result; bool cr{}; auto chars = text->chars();
    for (auto item = chars.next(); item.is_some(); item = chars.next()) {
        auto cp = item->to_primitive();
        if (cp == 0 || (cp < 32 && cp != '\r' && cp != '\n' && cp != '\t') || cp == 127)
            return Err(edit_error(TextError::InvalidValue));
        if (options.single_line) {
            if (cp == '\n' && cr) { cr = false; continue; }
            cr = cp == '\r';
            if (cp == '\r' || cp == '\n' || cp == '\t' || cp == 0x85 || cp == 0x2028 || cp == 0x2029) cp = ' ';
        }
        result.push(char32_t(cp));
    }
    return Ok(rstd::move(result));
}
auto analyze(ref<str> text, TextEditOptions options, bool limit = true) -> Result<TextBoundaries, TextEditError> {
    if (text->len() > usize(options.max_bytes)) return Err(edit_error(TextError::Limit));
    auto result = TextBoundaries::create(text);
    if (result.is_err()) return Err(edit_error(result.unwrap_err()));
    if (limit && options.max_graphemes && result->graphemes().len() - usize(1) > usize(options.max_graphemes))
        return Err(edit_error(TextError::Limit));
    return Ok(rstd::move(result).unwrap());
}
void clear_composition(TextEditState &state) {
    state.display = state.text; state.composing = false; state.composition = {}; state.preedit_selection = None(); state.caret = state.selection.cursor;
}
}
struct TextEdit::Impl {
    struct Saved { TextBoundaries text; TextSelection selection; };
    enum class History { None, Reset, Insert, Undo, Redo };
    TextEditOptions options;
    state::Graph graph;
    Option<state::Cell<TextEditState>> cell;
    Vec<Saved> undo, redo;
    explicit Impl(TextEditOptions options) : options(options) {}
    auto current() const -> Result<TextEditState, TextEditError> {
        auto result = state::read(cell->value());
        if (result.is_err()) return Err(edit_error(result.unwrap_err()));
        return Ok(TextEditState(result.unwrap().value()));
    }
    auto publish(TextEditState next, History history = History::None) -> Result<bool, TextEditError> {
        auto read = current();
        if (read.is_err()) return Err(read.unwrap_err());
        auto previous = rstd::move(read).unwrap();
        if (history == History::Reset) next.can_undo = next.can_redo = false;
        else if (history == History::Insert) { next.can_undo = options.history_limit != 0; next.can_redo = false; }
        else if (history == History::Undo) { next.can_undo = undo.len() > usize(1); next.can_redo = true; }
        else if (history == History::Redo) { next.can_undo = true; next.can_redo = redo.len() > usize(1); }
        if (next == previous) return Ok(false);
        auto result = cell->set(rstd::move(next));
        if (result.is_err()) return Err(edit_error(result.unwrap_err()));
        auto remember = [&](Vec<Saved> &list) {
            if (!options.history_limit) return;
            if (list.len() == usize(options.history_limit)) list.remove(usize());
            list.push(Saved{previous.text, previous.selection});
        };
        if (history == History::Reset) { undo.clear(); redo.clear(); }
        if (history == History::Insert) { remember(undo); redo.clear(); }
        if (history == History::Undo) { remember(redo); undo.pop(); }
        if (history == History::Redo) { remember(undo); redo.pop(); }
        return Ok(true);
    }
    auto replace(TextRange range, ref<str> text) -> Result<bool, TextEditError> {
        auto clean = normalized(text, options);
        if (clean.is_err()) return Err(clean.unwrap_err());
        auto read = current();
        if (read.is_err()) return Err(read.unwrap_err());
        auto next = rstd::move(read).unwrap();
        String value = String::make(next.text.text()); value.replace_range(usize(range.start), usize(range.end), clean->as_str());
        bool changed = value.as_str() != next.text.text();
        if (changed) {
            auto result = analyze(value.as_str(), options);
            if (result.is_err()) return Err(result.unwrap_err());
            next.text = rstd::move(result).unwrap();
        }
        auto cursor = next.text.snap(range.start + Index(clean->len().to_primitive()), TextBoundaryBias::After).unwrap();
        next.selection = {cursor, cursor}; clear_composition(next);
        return publish(rstd::move(next), changed ? History::Insert : History::None);
    }
};
TextEdit::TextEdit(Box<Impl> data) : data_(rstd::move(data)) {}
TextEdit::~TextEdit() = default;
TextEdit::TextEdit(TextEdit &&) = default;
auto TextEdit::operator=(TextEdit &&) -> TextEdit & = default;
auto TextEdit::create(ref<str> text, TextEditOptions options) -> Result<TextEdit, TextEditError> {
    if (!options.max_bytes || options.max_bytes > 1048576 || options.history_limit > 4096)
        return Err(edit_error(TextError::InvalidValue));
    auto clean = normalized(text, options);
    if (clean.is_err()) return Err(clean.unwrap_err());
    auto result = analyze(clean->as_str(), options);
    if (result.is_err()) return Err(result.unwrap_err());
    auto prepared = rstd::move(result).unwrap(); auto end = Index(prepared.text()->len().to_primitive());
    auto data = Box<Impl>::make(options);
    auto cell = data->graph.cell(TextEditState{prepared, prepared, {end, end}, {}, {}, end});
    if (cell.is_err()) return Err(edit_error(cell.unwrap_err()));
    data->cell = Some(rstd::move(cell).unwrap()); return Ok(TextEdit(rstd::move(data)));
}
auto TextEdit::value() const -> state::Value<TextEditState> { return data_->cell->value(); }
auto TextEdit::state() const -> Result<TextEditState, TextEditError> { return data_->current(); }
auto TextEdit::set_text(ref<str> text) -> Result<bool, TextEditError> {
    auto clean = normalized(text, data_->options);
    if (clean.is_err()) return Err(clean.unwrap_err());
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    auto next = rstd::move(read).unwrap();
    if (clean->as_str() == next.text.text()) return Ok(false);
    auto result = analyze(clean->as_str(), data_->options);
    if (result.is_err()) return Err(result.unwrap_err());
    next.text = rstd::move(result).unwrap(); auto end = Index(next.text.text()->len().to_primitive());
    next.selection = {end, end}; clear_composition(next); return data_->publish(rstd::move(next), Impl::History::Reset);
}
auto TextEdit::select(TextSelection selection) -> Result<bool, TextEditError> {
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    auto next = rstd::move(read).unwrap();
    if (!next.text.contains(selection.anchor) || !next.text.contains(selection.cursor)) return Err(edit_error(TextError::InvalidIndex));
    next.selection = selection; clear_composition(next); return data_->publish(rstd::move(next));
}
auto TextEdit::select_all() -> Result<bool, TextEditError> {
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    return select({0, Index(read->text.text()->len().to_primitive())});
}
auto TextEdit::move(TextEditMove direction, bool extend) -> Result<bool, TextEditError> {
    if (unsigned(direction) > unsigned(TextEditMove::End)) return Err(edit_error(TextError::InvalidValue));
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    auto next = rstd::move(read).unwrap(); auto cursor = next.selection.cursor; auto range = next.selection.range();
    if (!extend && range.start != range.end && (direction == TextEditMove::Previous || direction == TextEditMove::Next))
        cursor = direction == TextEditMove::Previous ? range.start : range.end;
    else if (direction == TextEditMove::Previous) cursor = next.text.previous(cursor).unwrap();
    else if (direction == TextEditMove::Next) cursor = next.text.next(cursor).unwrap();
    else if (direction == TextEditMove::Start) cursor = 0;
    else if (direction == TextEditMove::End) cursor = Index(next.text.text()->len().to_primitive());
    else if (direction == TextEditMove::PreviousWord) {
        cursor = 0;
        for (const auto &word : next.text.words()) if (word.word && word.range.start < next.selection.cursor) cursor = word.range.start;
    } else {
        cursor = Index(next.text.text()->len().to_primitive());
        for (const auto &word : next.text.words()) if (word.word && word.range.end > next.selection.cursor) { cursor = word.range.end; break; }
    }
    cursor = next.text.snap(cursor, direction == TextEditMove::PreviousWord ? TextBoundaryBias::Before : TextBoundaryBias::After).unwrap();
    next.selection = {extend ? next.selection.anchor : cursor, cursor}; clear_composition(next); return data_->publish(rstd::move(next));
}
auto TextEdit::insert(ref<str> text) -> Result<bool, TextEditError> {
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    return data_->replace(read->selection.range(), text);
}
auto TextEdit::erase_backwards() -> Result<bool, TextEditError> {
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    auto next = rstd::move(read).unwrap(); if (next.composing) return cancel_composition(); auto range = next.selection.range();
    if (range.start == range.end) range.start = next.text.previous(range.start).unwrap(); return data_->replace(range, ""_str);
}
auto TextEdit::erase_forwards() -> Result<bool, TextEditError> {
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    auto next = rstd::move(read).unwrap(); if (next.composing) return cancel_composition(); auto range = next.selection.range();
    if (range.start == range.end) range.end = next.text.next(range.end).unwrap(); return data_->replace(range, ""_str);
}
auto TextEdit::preedit(const TextInput &input) -> Result<bool, TextEditError> {
    if (!input.present()) return Err(edit_error(TextError::InvalidValue));
    if (input.text()->is_empty()) return cancel_composition();
    auto clean = normalized(input.text(), data_->options);
    if (clean.is_err()) return Err(clean.unwrap_err());
    if (clean->as_str() != input.text()) return Err(edit_error(TextError::InvalidValue));
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    auto next = rstd::move(read).unwrap(); auto range = next.selection.range();
    if (next.composing && next.preedit_selection == input.selection() &&
        next.display.text()->get(usize(next.composition.start), usize(next.composition.end)).unwrap() == input.text())
        return Ok(false);
    String value = String::make(next.text.text()); value.replace_range(usize(range.start), usize(range.end), input.text());
    auto result = analyze(value.as_str(), data_->options, false);
    if (result.is_err()) return Err(result.unwrap_err());
    next.display = rstd::move(result).unwrap(); next.composing = true;
    next.composition = {range.start, range.start + Index(input.text()->len().to_primitive())};
    next.preedit_selection = input.selection();
    auto caret = next.preedit_selection.is_some() ? next.composition.start + next.preedit_selection->start : next.composition.end;
    next.caret = next.display.snap(caret, TextBoundaryBias::After).unwrap(); return data_->publish(rstd::move(next));
}
auto TextEdit::cancel_composition() -> Result<bool, TextEditError> { auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    auto next = rstd::move(read).unwrap(); clear_composition(next); return data_->publish(rstd::move(next)); }
auto TextEdit::undo() -> Result<bool, TextEditError> {
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    auto next = rstd::move(read).unwrap(); if (next.composing) return cancel_composition(); if (data_->undo.is_empty()) return Ok(false);
    const auto &saved = data_->undo[data_->undo.len() - usize(1)]; next.text = saved.text; next.selection = saved.selection;
    clear_composition(next); return data_->publish(rstd::move(next), Impl::History::Undo);
}
auto TextEdit::redo() -> Result<bool, TextEditError> {
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    auto next = rstd::move(read).unwrap(); if (next.composing) return cancel_composition(); if (data_->redo.is_empty()) return Ok(false);
    const auto &saved = data_->redo[data_->redo.len() - usize(1)]; next.text = saved.text; next.selection = saved.selection;
    clear_composition(next); return data_->publish(rstd::move(next), Impl::History::Redo);
}
} // namespace vui
