export module vui.examples.gallery.android;
import vui.examples.gallery;
import vui.examples;
import rstd;

using namespace vui;
using namespace rstd::prelude;

namespace {
class AndroidGallery final : public host::AndroidApplication {
    examples::gallery::View gallery_{session()};

  public:
    auto configure(UiSession &session) -> Result<empty, UiError> override {
        set_system_bar_icons(host::AndroidSystemBarIcons::Light, host::AndroidSystemBarIcons::Light);
        session.input().set_navigation(TabNavigation{});
        return gallery_.mount();
    }

    auto can_handle_back() const -> bool override { return true; }
    auto on_back_requested() -> bool override { return gallery_.dismiss_dialog(); }

    auto before_advance(const host::HostEvents &events) -> Result<empty, UiError> override {
        const auto size = events.surface.logical_size;
        const auto safe = metrics().safe_area;
        const auto width = rstd::cmp::max(Scalar(1), size.width - safe.left - safe.right);
        const auto height = rstd::cmp::max(Scalar(1), size.height - safe.top - safe.bottom);
        return gallery_.resize(size, {safe.left, safe.top, width, height});
    }

    auto after_advance(const AdvanceResult &) -> Result<empty, UiError> override {
        (void)session().document().poll_animation_events();
        (void)session().motion().poll_events();
        return gallery_.status();
    }
};
} // namespace

extern "C" auto vui_android_create_application() -> host::AndroidApplication * { return new AndroidGallery; }
