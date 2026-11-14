module vui;
import :text.edit;
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
    state.display = state.text; state.composing = false; state.composition = {}; state.preedit_selection = None(); state.preedit_cursor = None(); state.caret = state.selection.cursor;
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
    TextInputTransaction transaction;
    transaction.preedit = TextInput::create(input.text()).unwrap();
    if (input.selection().is_some()) {
        auto selection = *input.selection();
        transaction.preedit_cursor = Some(TextPreeditCursor{
            selection.end.is_some() ? *selection.end : selection.start, selection.start, true,
            selection.end.is_some()});
    }
    return apply_input_transaction(transaction);
}
auto TextEdit::apply_input_transaction(const TextInputTransaction &transaction) -> Result<bool, TextEditError> {
    if (!transaction.valid()) return Err(edit_error(TextError::InvalidValue));
    auto read = state();
    if (read.is_err()) return Err(read.unwrap_err());
    auto next = rstd::move(read).unwrap();
    if (transaction.expected_surrounding.is_some() &&
        (transaction.expected_surrounding->text != next.text.identity() ||
         transaction.expected_surrounding->anchor != next.selection.anchor ||
         transaction.expected_surrounding->cursor != next.selection.cursor))
        return Err(edit_error(TextError::InvalidValue));
    if (!transaction.commit.present() && !transaction.delete_before && !transaction.delete_after &&
        transaction.preedit.present() && next.composing &&
        next.preedit_cursor == transaction.preedit_cursor &&
        next.display.text()->get(usize(next.composition.start), usize(next.composition.end)).unwrap() == transaction.preedit.text())
        return Ok(false);
    auto range = next.selection.range();
    auto text = next.text.text();
    auto length = Index(text->len().to_primitive());
    if (transaction.delete_before > range.start || transaction.delete_after > length - range.end ||
        !text->is_char_boundary(usize(range.start - transaction.delete_before)) ||
        !text->is_char_boundary(usize(range.end + transaction.delete_after)))
        return Err(edit_error(TextError::InvalidIndex));

    String value = String::make(text);
    value.replace_range(usize(range.end), usize(range.end + transaction.delete_after), ""_str);
    value.replace_range(usize(range.start - transaction.delete_before), usize(range.start), ""_str);
    next.selection.anchor -= transaction.delete_before;
    next.selection.cursor -= transaction.delete_before;
    range = next.selection.range();
    if (transaction.commit.present()) {
        auto clean = normalized(transaction.commit.text(), data_->options);
        if (clean.is_err()) return Err(clean.unwrap_err());
        value.replace_range(usize(range.start), usize(range.end), clean->as_str());
        auto index = range.start + Index(clean->len().to_primitive());
        next.selection = {index, index};
    }
    bool changed = value.as_str() != text;
    if (changed) {
        auto analyzed = analyze(value.as_str(), data_->options);
        if (analyzed.is_err()) return Err(analyzed.unwrap_err());
        next.text = rstd::move(analyzed).unwrap();
        next.selection.anchor = next.text.snap(next.selection.anchor, TextBoundaryBias::After).unwrap();
        next.selection.cursor = next.text.snap(next.selection.cursor, TextBoundaryBias::After).unwrap();
    }
    if (transaction.commit.present() || transaction.delete_before || transaction.delete_after || transaction.preedit.present())
        clear_composition(next);
    if (transaction.preedit.present() && !transaction.preedit.text()->is_empty()) {
        auto clean = normalized(transaction.preedit.text(), data_->options);
        if (clean.is_err()) return Err(clean.unwrap_err());
        if (clean->as_str() != transaction.preedit.text()) return Err(edit_error(TextError::InvalidValue));
        auto selected = next.selection.range();
        String display = String::make(next.text.text());
        display.replace_range(usize(selected.start), usize(selected.end), transaction.preedit.text());
        auto analyzed = analyze(display.as_str(), data_->options, false);
        if (analyzed.is_err()) return Err(analyzed.unwrap_err());
        next.display = rstd::move(analyzed).unwrap();
        next.composing = true;
        next.composition = {selected.start, selected.start + Index(transaction.preedit.text()->len().to_primitive())};
        next.preedit_cursor = transaction.preedit_cursor;
        if (transaction.preedit_cursor.is_some() && transaction.preedit_cursor->visible) {
            auto cursor = *transaction.preedit_cursor;
            auto start = rstd::cmp::min(cursor.anchor, cursor.cursor);
            auto end = rstd::cmp::max(cursor.anchor, cursor.cursor);
            next.preedit_selection = Some(TextInputSelection{start, start == end && !cursor.has_range ? None<Index>() : Some(Index(end))});
            next.caret = next.display.snap(selected.start + cursor.cursor, TextBoundaryBias::After).unwrap();
        } else next.caret = next.composition.end;
    }
    return data_->publish(rstd::move(next), changed ? Impl::History::Insert : Impl::History::None);
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
