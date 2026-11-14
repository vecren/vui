export module vui.examples.game2048;
export import :game;
export import :view;
import vui.examples;
import rstd;

int main(int argc, char **argv) {
    unsigned limit{};
    if (!examples::frame_limit(argc, argv, limit))
        return 2;
    examples::Application app;
    if (!app.open("vui | 2048", 520, 720))
        return 1;
    auto seed = rstd::time::SystemTime::now().as_unix_time().seconds.to_primitive();
    game2048::View view(app.session, game2048::Game(static_cast<rstd::uint32_t>(seed)));
    return app.run(
        limit, [&](const vui::SurfaceInfo &surface) { view.resize(surface); },
        [&](const vui::AdvanceResult &) { return view.check(); }, [] {});
}
