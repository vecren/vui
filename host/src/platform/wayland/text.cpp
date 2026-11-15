module vui.host;
import :platform.wayland.text;
import :ffi.wayland;
import :platform.wayland.protocol.text_input;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

void WaylandText::destroy_input() {
    if (input_) ffi::destroy(input_);
    input_ = nullptr; focused_ = enabled_ = sent_surrounding_ = preedit_active_ = false;
    serial_ = 0; epoch_ = 0; commits_.clear(); sent_state_ = None(); clear_pending();
}

void WaylandText::create_input() {
    if (input_ || !manager_ || !seat_) return;
    input_ = ffi::get_text_input(manager_, seat_);
    if (!input_ || ffi::add_listener(input_, &listener_, this)) failed_ = true;
}

void WaylandText::record_commit(const TextInputState *state, bool has_surrounding) {
    ffi::commit(input_);
    ++serial_;
    if (commits_.len() >= usize(256)) commits_.remove(usize());
    commits_.push({serial_, state ? state->request.session : 0, epoch_,
                   state && has_surrounding ? TextSurrounding(state->surrounding) : TextSurrounding{}});
}

auto WaylandText::rect(Rect area, rstd::int32_t &x, rstd::int32_t &y, rstd::int32_t &w, rstd::int32_t &h) -> bool {
    auto left = __builtin_floor(area.x), top = __builtin_floor(area.y);
    auto right = __builtin_ceil(area.x + area.width), bottom = __builtin_ceil(area.y + area.height);
    auto valid = [](Scalar value) { return finite(value) && value >= -2147483648.0 && value <= 2147483647.0; };
    if (!valid(left) || !valid(top) || !valid(right) || !valid(bottom) ||
        right - left > 2147483647.0 || bottom - top > 2147483647.0) return false;
    x = rstd::int32_t(left); y = rstd::int32_t(top);
    w = rstd::int32_t(rstd::cmp::max(Scalar(1), right - left));
    h = rstd::int32_t(rstd::cmp::max(Scalar(1), bottom - top));
    return true;
}

auto WaylandText::surrounding(const TextInputState &state) -> Option<TextSurroundingRange> {
    auto range = state.surrounding.window(4000);
    if (range.is_none()) return None();
    auto chars = range->view()->chars();
    for (auto cp = chars.next(); cp.is_some(); cp = chars.next())
        if (cp->to_primitive() == 0) return None();
    return range;
}

void WaylandText::apply_state() {
    if (!input_ || !focused_) return;
    if (desired_.is_none()) {
        if (enabled_) { ffi::disable(input_); record_commit(nullptr, false); }
        enabled_ = sent_surrounding_ = preedit_active_ = false; sent_state_ = None(); clear_pending();
        return;
    }
    auto &state = *desired_;
    auto window = surrounding(state);
    bool supports_surrounding = window.is_some();
    bool fresh = !enabled_ || sent_state_.is_none() ||
        sent_state_->request.session != state.request.session ||
        (sent_surrounding_ && !supports_surrounding);
    if (fresh && enabled_) {
        ffi::disable(input_); record_commit(nullptr, false);
        enabled_ = preedit_active_ = false; clear_pending();
    }
    if (fresh) {
        if (epoch_ == ~Serial{}) { failed_ = true; return; }
        ++epoch_; ffi::enable(input_); enabled_ = true; sent_surrounding_ = false;
    }
    rstd::int32_t x{}, y{}, w{}, h{};
    if (!rect(state.request.area, x, y, w, h)) { failed_ = true; return; }
    if (fresh || sent_state_.is_none() || sent_state_->request.area != state.request.area)
        ffi::rectangle(input_, x, y, w, h);
    bool text_changed = fresh || sent_state_.is_none() || !(sent_state_->surrounding == state.surrounding);
    if (text_changed && supports_surrounding) {
        String text = String::make(window->view()); text.push(U'\0');
        ffi::surrounding(input_, reinterpret_cast<const char *>(text.as_str()->as_bytes().as_ptr().as_raw_ptr()),
                        rstd::int32_t(window->cursor), rstd::int32_t(window->anchor));
        ffi::cause(input_, state.change_cause == TextChangeCause::Other ? ffi::ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER
                                                                      : ffi::ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD);
        sent_surrounding_ = true;
    }
    record_commit(&state, sent_surrounding_);
    sent_state_ = Some(TextInputState(state));
}

void WaylandText::enter(void *data, ffi::zwp_text_input_v3 *, ffi::wl_surface *surface) {
    auto &self = *static_cast<WaylandText *>(data);
    self.focused_ = surface && surface == self.surface_;
    self.enabled_ = self.sent_surrounding_ = self.preedit_active_ = false; self.sent_state_ = None(); self.clear_pending();
    if (self.focused_) self.apply_state();
}

void WaylandText::leave(void *data, ffi::zwp_text_input_v3 *, ffi::wl_surface *surface) {
    auto &self = *static_cast<WaylandText *>(data);
    if (!self.focused_ || surface != self.surface_) return;
    if (self.desired_.is_some()) {
        TextInputTransaction transaction;
        transaction.preedit = TextInput::create(""_str).unwrap();
        InputEvent event; event.type = EventType::TextTransaction;
        event.text_session = self.desired_->request.session;
        event.transaction = TextTransaction::create(rstd::move(transaction)).unwrap();
        self.emit_(self.sink_, rstd::move(event));
    }
    self.focused_ = self.enabled_ = self.sent_surrounding_ = self.preedit_active_ = false;
    self.sent_state_ = None(); self.clear_pending();
}

void WaylandText::preedit(void *data, ffi::zwp_text_input_v3 *, const char *text, rstd::int32_t begin, rstd::int32_t end) {
    auto &self = *static_cast<WaylandText *>(data);
    self.pending_any_ = true;
    auto decoded = rstd::ffi::CStr::from_ptr(text ? text : "").to_str();
    if (decoded.is_err() || ((begin < 0 || end < 0) && (begin != -1 || end != -1))) { self.pending_invalid_ = true; return; }
    auto payload = TextInput::create(decoded.unwrap());
    if (payload.is_err()) { self.pending_invalid_ = true; return; }
    self.pending_.preedit = rstd::move(payload).unwrap();
    self.pending_.preedit_cursor = begin == -1 ? Some(TextPreeditCursor{0, 0, false})
        : Some(TextPreeditCursor{Index(end), Index(begin), true, begin != end});
    if (!self.pending_.valid()) self.pending_invalid_ = true;
}

void WaylandText::commit_string(void *data, ffi::zwp_text_input_v3 *, const char *text) {
    auto &self = *static_cast<WaylandText *>(data);
    self.pending_any_ = true;
    if (!text) { self.pending_.commit = TextInput{}; return; }
    auto decoded = rstd::ffi::CStr::from_ptr(text).to_str();
    if (decoded.is_err()) { self.pending_invalid_ = true; return; }
    auto payload = TextInput::create(decoded.unwrap());
    if (payload.is_err()) { self.pending_invalid_ = true; return; }
    self.pending_.commit = rstd::move(payload).unwrap();
}

void WaylandText::done(void *data, ffi::zwp_text_input_v3 *, rstd::uint32_t serial) {
    auto &self = *static_cast<WaylandText *>(data);
    if (!self.pending_any_) {
        if (!self.preedit_active_) return;
        self.pending_.preedit = TextInput::create(""_str).unwrap();
        self.pending_any_ = true;
    }
    const Commit *matched{};
    for (const auto &entry : self.commits_) if (entry.serial == serial) matched = &entry;
    bool valid = !self.pending_invalid_ && self.focused_ && self.enabled_ && self.desired_.is_some() &&
                 matched && matched->session && matched->session == self.desired_->request.session &&
                 matched->epoch == self.epoch_;
    if (valid && (self.pending_.delete_before || self.pending_.delete_after)) {
        auto range = matched->surrounding.window(4000);
        if (range.is_none()) valid = false;
        else {
            auto low = rstd::cmp::min(range->anchor, range->cursor), high = rstd::cmp::max(range->anchor, range->cursor);
            auto view = range->view();
            if (self.pending_.delete_before > low || self.pending_.delete_after > view->len().to_primitive() - high ||
                !view->is_char_boundary(usize(low - self.pending_.delete_before)) ||
                !view->is_char_boundary(usize(high + self.pending_.delete_after))) valid = false;
            else self.pending_.expected_surrounding = Some(TextSurroundingRevision{
                matched->surrounding.text.identity(), matched->surrounding.anchor, matched->surrounding.cursor});
        }
    }
    if (valid) {
        auto transaction = TextTransaction::create(rstd::move(self.pending_));
        if (transaction.is_ok()) {
            auto payload = transaction->get();
            if (payload->preedit.present()) self.preedit_active_ = !payload->preedit.text()->is_empty();
            else if (payload->commit.present() || payload->delete_before || payload->delete_after)
                self.preedit_active_ = false;
            InputEvent event; event.type = EventType::TextTransaction;
            event.text_session = matched->session; event.transaction = rstd::move(transaction).unwrap();
            self.emit_(self.sink_, rstd::move(event));
        } else valid = false;
    }
    if (!valid) self.drop_(self.sink_);
    self.clear_pending();
}

void WaylandText::bind(ffi::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version) {
    if (manager_ || !version) return;
    manager_ = static_cast<ffi::zwp_text_input_manager_v3 *>(ffi::wl_registry_bind(registry, name, &ffi::zwp_text_input_manager_v3_interface, 1));
    if (!manager_) { failed_ = true; return; }
    manager_name_ = name; create_input();
}

void WaylandText::close() {
    destroy_input();
    if (manager_) ffi::manager_destroy(manager_);
    manager_ = nullptr; manager_name_ = 0; seat_ = nullptr; surface_ = nullptr; desired_ = None(); failed_ = false;
}

auto WaylandText::set_state(Option<TextInputState> state) -> NativeTextInputResult {
    if (!state.is_some()) { desired_ = None(); apply_state(); return NativeTextInputResult::Succeeded; }
    rstd::int32_t x{}, y{}, w{}, h{};
    if (!state->valid() || !rect(state->request.area, x, y, w, h)) return NativeTextInputResult::Failed;
    desired_ = Some(TextInputState(*state));
    if (!input_) return NativeTextInputResult::Unsupported;
    apply_state();
    return failed_ ? NativeTextInputResult::Failed : NativeTextInputResult::Succeeded;
}
} // namespace vui::host::detail
