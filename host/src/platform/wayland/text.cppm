module vui.host:platform.wayland.text;
import :platform.wayland.ffi;
import vui.render;

namespace vui::host::detail {
using namespace rstd::prelude;
using namespace rstd::literals;

class WaylandText {
    friend struct WaylandTextTest;
    void *sink_;
    void (*emit_)(void *, InputEvent);
    void (*drop_)(void *);
    ffi::zwp_text_input_manager_v3 *manager_{};
    ffi::zwp_text_input_v3 *input_{};
    ffi::wl_seat *seat_{};
    ffi::wl_surface *surface_{};
    rstd::uint32_t manager_name_{}, serial_{};
    Serial epoch_{};
    bool focused_{}, enabled_{}, failed_{}, sent_surrounding_{}, preedit_active_{};
    Option<TextInputState> desired_, sent_state_;
    struct Commit { rstd::uint32_t serial; Serial session, epoch; TextSurrounding surrounding; };
    Vec<Commit> commits_;
    TextInputTransaction pending_;
    bool pending_any_{}, pending_invalid_{};

    void clear_pending() { pending_ = {}; pending_any_ = pending_invalid_ = false; }
    void destroy_input();
    void create_input();
    void record_commit(const TextInputState *state, bool has_surrounding);
    static auto rect(Rect area, rstd::int32_t &x, rstd::int32_t &y, rstd::int32_t &w, rstd::int32_t &h) -> bool;
    auto surrounding(const TextInputState &state) -> Option<TextSurroundingRange>;
    void apply_state();
    static void enter(void *data, ffi::zwp_text_input_v3 *, ffi::wl_surface *surface);
    static void leave(void *data, ffi::zwp_text_input_v3 *, ffi::wl_surface *surface);
    static void preedit(void *data, ffi::zwp_text_input_v3 *, const char *text, rstd::int32_t begin, rstd::int32_t end);
    static void commit_string(void *data, ffi::zwp_text_input_v3 *, const char *text);
    static void delete_surrounding(void *data, ffi::zwp_text_input_v3 *, rstd::uint32_t before, rstd::uint32_t after) {
        auto &self = *static_cast<WaylandText *>(data);
        self.pending_any_ = true; self.pending_.delete_before = before; self.pending_.delete_after = after;
    }
    static void done(void *data, ffi::zwp_text_input_v3 *, rstd::uint32_t serial);
    ffi::zwp_text_input_v3_listener listener_{enter, leave, preedit, commit_string, delete_surrounding, done};

  public:
    WaylandText(void *sink, void (*emit)(void *, InputEvent), void (*drop)(void *))
        : sink_(sink), emit_(emit), drop_(drop) {}
    WaylandText(const WaylandText &) = delete;
    ~WaylandText() { close(); }
    void bind(ffi::wl_registry *registry, rstd::uint32_t name, rstd::uint32_t version);
    void remove(rstd::uint32_t name) {
        if (!manager_ || name != manager_name_) return;
        destroy_input(); ffi::manager_destroy(manager_); manager_ = nullptr; manager_name_ = 0;
    }
    void set_seat(ffi::wl_seat *seat) {
        if (seat_ == seat) return;
        destroy_input(); seat_ = seat; create_input();
    }
    void set_surface(ffi::wl_surface *surface) { surface_ = surface; }
    void close();
    auto set_state(Option<TextInputState> state) -> NativeTextInputResult;
    auto failed() const -> bool { return failed_; }
    auto available() const -> bool { return input_ != nullptr; }
    auto session() const -> Option<Serial> {
        return desired_.is_some() ? Some(Serial(desired_->request.session)) : None<Serial>();
    }
    auto composing() const -> bool {
        return preedit_active_ || (pending_.preedit.present() && !pending_.preedit.text()->is_empty());
    }
};
} // namespace vui::host::detail
