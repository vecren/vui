export module vui.examples.game2048.android;
import vui.examples.game2048;
import vui.examples;
import rstd;

using namespace vui;
using namespace rstd::prelude;

namespace {
class Android2048 final : public host::AndroidApplication {
    Option<Box<game2048::View>> view_;

  public:
    auto configure(UiSession &session) -> Result<empty, UiError> override {
        set_system_bar_icons(host::AndroidSystemBarIcons::Dark, host::AndroidSystemBarIcons::Dark);
        session.input().set_navigation(TabNavigation{});
        auto seed = rstd::time::SystemTime::now().as_unix_time().seconds.to_primitive();
        view_ = Some(Box<game2048::View>::make(session, game2048::Game(static_cast<rstd::uint32_t>(seed)),
                                              game2048::InputHint::Swipe));
        return Ok(empty{});
    }

    auto before_advance(const host::HostEvents &events) -> Result<empty, UiError> override {
        const auto size = events.surface.logical_size;
        const auto safe = metrics().safe_area;
        const auto width = rstd::cmp::max(Scalar(1), size.width - safe.left - safe.right);
        const auto height = rstd::cmp::max(Scalar(1), size.height - safe.top - safe.bottom);
        (*view_)->resize(size, {safe.left, safe.top, width, height});
        return Ok(empty{});
    }

    auto after_advance(const AdvanceResult &) -> Result<empty, UiError> override {
        (void)session().document().poll_animation_events();
        (void)session().motion().poll_events();
        return (*view_)->check();
    }
};
} // namespace

extern "C" auto vui_android_create_application() -> host::AndroidApplication * { return new Android2048; }
