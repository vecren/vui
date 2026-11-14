export module vui.examples.interaction;
import vui.examples;
import vui.examples.interaction.view;

int main(int argc, char **argv) {
    unsigned limit{};
    if (!examples::frame_limit(argc, argv, limit))
        return 2;
    examples::Application app;
    if (!app.open("vui interaction", 640, 520))
        return 1;
    auto panel = interaction_example::Panel::make(app.session);
    app.session.document().mount(panel.get()->root()).unwrap();
    app.session.input().focus_when_ready(panel.get()->root(), rstd::None<vui::ScopeId>(),
        app.session.document().lifetime()).unwrap();
    return app.run(
        limit, [&](const vui::SurfaceInfo &surface) { panel.get()->resize(surface); },
        [&](const vui::AdvanceResult &) { return panel.get()->check(); }, [] {});
}
