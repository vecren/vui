module vui.host;
import :platform.win32.text;
import :platform.win32.ffi;
import vui;
import rstd;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

auto Win32Text::read(ffi::HIMC context, unsigned kind) -> Option<Vec<u16>> {
    auto bytes = ffi::ImmGetCompositionStringW(context, kind, nullptr, 0);
    if (bytes < 0 || bytes > 65536 || bytes % sizeof(wchar_t)) return None();
    Vec<wchar_t> buffer;
    buffer.resize(usize(bytes / sizeof(wchar_t)), wchar_t{});
    if (bytes && ffi::ImmGetCompositionStringW(context, kind, buffer.as_mut_slice().as_raw_ptr(),
                                               unsigned(bytes)) != bytes) return None();
    Vec<u16> result;
    for (auto value : buffer) result.push(u16(value));
    return Some(rstd::move(result));
}

auto Win32Text::text(slice<u16> units) -> Option<TextInput> {
    auto native = rstd::os::windows::ffi::OsStringExt::from_wide(units);
    auto decoded = rstd::move(native).into_string();
    if (decoded.is_err()) return None();
    auto value = TextInput::create(decoded->as_str());
    return value.is_ok() ? Some(rstd::move(value).unwrap()) : None<TextInput>();
}

auto Win32Text::cursor(slice<u16> units, long offset) -> Option<Index> {
    if (offset < 0 || usize(offset) > units.len()) return None();
    auto prefix = slice<u16>::from_raw_parts(units.as_raw_ptr(), usize(offset));
    auto native = rstd::os::windows::ffi::OsStringExt::from_wide(prefix);
    auto decoded = rstd::move(native).into_string();
    if (decoded.is_err() || decoded->len() > usize(~Index{})) return None();
    return Some(Index(decoded->len().to_primitive()));
}

void Win32Text::publish(TextInputTransaction value) {
    if (!focused_ || state_.is_none()) return;
    auto transaction = TextTransaction::create(rstd::move(value));
    if (transaction.is_err()) return;
    InputEvent event; event.type = EventType::TextTransaction;
    event.text_session = state_->request.session;
    event.transaction = rstd::move(transaction).unwrap();
    emit_(sink_, rstd::move(event));
}

void Win32Text::clear_preedit() {
    if (!preedit_) return;
    TextInputTransaction value;
    value.preedit = TextInput::create(""_str).unwrap();
    publish(rstd::move(value));
    preedit_ = false;
}

void Win32Text::cancel(ffi::HWND window) {
    if (state_.is_none() && !composing_ && !preedit_) return;
    clear_preedit(); composing_ = false;
    cancelling_ = true;
    auto context = ffi::ImmGetContext(window);
    if (context) {
        ffi::ImmNotifyIME(context, ffi::NI_COMPOSITIONSTR, ffi::CPS_CANCEL, 0);
        ffi::ImmReleaseContext(window, context);
    }
    cancelling_ = false;
}

auto Win32Text::position_valid(Rect area, rstd::uint32_t dpi) -> bool {
    auto scale = Scalar(dpi ? dpi : 96) / Scalar(96);
    auto x = area.x * scale, y = area.y * scale, right = (area.x + area.width) * scale;
    auto bottom = (area.y + area.height) * scale;
    return finite(x) && finite(y) && finite(right) && finite(bottom) &&
           x >= -32768 && y >= -32768 && right <= 32767 && bottom <= 32767;
}

auto Win32Text::place(ffi::HWND window, Rect area, rstd::uint32_t dpi) -> NativeTextInputResult {
    auto scale = Scalar(dpi ? dpi : 96) / Scalar(96);
    auto x = area.x * scale, y = area.y * scale, right = (area.x + area.width) * scale;
    auto bottom = (area.y + area.height) * scale;
    if (!position_valid(area, dpi)) return NativeTextInputResult::Failed;
    auto context = ffi::ImmGetContext(window);
    if (!context) return NativeTextInputResult::Unsupported;
    ffi::COMPOSITIONFORM composition{};
    composition.dwStyle = ffi::CFS_POINT;
    composition.ptCurrentPos = {long(x), long(bottom)};
    ffi::CANDIDATEFORM candidate{};
    candidate.dwStyle = ffi::CFS_EXCLUDE;
    candidate.ptCurrentPos = {long(x), long(bottom)};
    candidate.rcArea = {long(x), long(y), long(right), long(bottom)};
    auto result = ffi::ImmSetCompositionWindow(context, &composition) &&
                  ffi::ImmSetCandidateWindow(context, &candidate);
    ffi::ImmReleaseContext(window, context);
    return result ? NativeTextInputResult::Succeeded : NativeTextInputResult::Failed;
}

auto Win32Text::transaction(Option<slice<u16>> result, Option<slice<u16>> preedit,
                        long position, bool previous_preedit) -> Option<TextInputTransaction> {
    if (result.is_none() && preedit.is_none()) return None();
    TextInputTransaction value;
    if (result.is_some()) {
        auto decoded = text(*result);
        if (decoded.is_none()) return None();
        value.commit = rstd::move(decoded).unwrap();
    }
    if (preedit.is_some()) {
        auto decoded = text(*preedit);
        auto caret = position == -1 ? Some(Index(0)) : cursor(*preedit, position);
        if (decoded.is_none() || caret.is_none()) return None();
        value.preedit = rstd::move(decoded).unwrap();
        value.preedit_cursor = Some(TextPreeditCursor{*caret, *caret, position != -1, false});
    } else if (result.is_some() && previous_preedit) {
        value.preedit = TextInput::create(""_str).unwrap();
    }
    return Some(rstd::move(value));
}

auto Win32Text::set_state(ffi::HWND window, Option<TextInputState> value, rstd::uint32_t dpi)
    -> NativeTextInputResult {
    if (value.is_some() && !position_valid(value->request.area, dpi))
        return NativeTextInputResult::Failed;
    auto next = value.is_some() ? Some(Serial(value->request.session)) : None<Serial>();
    auto outcome = NativeTextInputResult::Succeeded;
    if (value.is_some() && (state_.is_none() || session() != next ||
        state_->request.area != value->request.area || placement_dpi_ != dpi)) {
        outcome = place(window, value->request.area, dpi);
        if (outcome == NativeTextInputResult::Failed) return outcome;
        placement_dpi_ = outcome == NativeTextInputResult::Succeeded ? dpi : 0;
    }
    if (session() != next) cancel(window);
    state_ = rstd::move(value);
    if (state_.is_none()) placement_dpi_ = 0;
    return outcome;
}

void Win32Text::focus(ffi::HWND window, bool focused, rstd::uint32_t dpi) {
    if (focused_ && !focused) cancel(window);
    focused_ = focused;
    if (focused && state_.is_some())
        placement_dpi_ = place(window, state_->request.area, dpi) == NativeTextInputResult::Succeeded ? dpi : 0;
}

auto Win32Text::compose(ffi::HWND window, ffi::LPARAM flags) -> bool {
    if (cancelling_) return true;
    if (!focused_ || state_.is_none()) return false;
    if (!composing_) return true;
    auto context = ffi::ImmGetContext(window);
    if (!context) { clear_preedit(); composing_ = false; return true; }
    auto has_result = (flags & ffi::GCS_RESULTSTR) != 0;
    auto has_preedit = (flags & ffi::ime_preedit_updates) != 0;
    auto result = has_result ? read(context, ffi::GCS_RESULTSTR) : None<Vec<u16>>();
    auto preedit = has_preedit ? read(context, ffi::GCS_COMPSTR) : None<Vec<u16>>();
    auto position = has_preedit ? ffi::ImmGetCompositionStringW(context, ffi::GCS_CURSORPOS, nullptr, 0) : -1;
    ffi::ImmReleaseContext(window, context);
    if ((has_result && result.is_none()) || (has_preedit && preedit.is_none())) {
        clear_preedit(); composing_ = false; return true;
    }
    if (!has_result && !has_preedit) {
        if (!(flags & ffi::ime_all_updates)) clear_preedit();
        return true;
    }
    auto value = transaction(has_result ? Some(result->as_slice()) : None<slice<u16>>(),
                             has_preedit ? Some(preedit->as_slice()) : None<slice<u16>>(),
                             position, preedit_);
    if (value.is_none()) { clear_preedit(); return true; }
    if (value->preedit.present()) preedit_ = !value->preedit.text()->is_empty();
    publish(rstd::move(value).unwrap());
    return true;
}
} // namespace vui::host::detail
