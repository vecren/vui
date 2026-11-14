export module vui.examples.composition;
import vui.examples;
import vui.compose;
import vui.examples.composition.view;
using namespace rstd::prelude;

int main(int argc, char **argv) {
    unsigned limit{};
    if (!examples::frame_limit(argc, argv, limit)) return 2;
    examples::Application app;
    if (!app.open("vui composition", 800, 720)) return 1;
    auto &ui = app.session.document();
    auto service = Box<vectext::SystemTextService>::make();
    ui.set_text_service(Box<vui::TextService>::from_raw(
        rstd::move(service).into_raw().template cast<vui::TextService>())).unwrap();
    auto root = ui.create<vui::CustomLayout>(vui::NodeStyleBuilder{
        examples::block(800, 720, examples::theme::canvas)}.build()).unwrap();
    ui.install_layout(root, vui::ReferenceCanvas{{800, 720}, vui::CanvasFit::Contain,
        {.5, .5}, true, Some(vui::Scalar(1.2))}).unwrap();
    ui.mount(root).unwrap();
    auto composition = vui::compose::Composition::create(app.session, root).unwrap();
    auto content = composition_example::Screen();
    if (content.is_err()) return 1;
    composition.set_content(content.unwrap()).unwrap();
    vui::Serial reported{};
    auto result = app.run(limit, [&](const vui::SurfaceInfo &surface) {
        ui.set_layout(root, vui::LayoutStyleBuilder{}.width(surface.logical_size.width)
            .height(surface.logical_size.height).build()).unwrap();
    }, [&](const vui::AdvanceResult &) -> Result<empty, vui::UiError> {
        auto status = examples::composition_status(composition);
        if (status.is_err()) return status;
        auto committed = composition.last_result();
        if (committed.is_some() && committed->is_ok()) {
            auto report = committed->unwrap();
            if (report.attempt != reported) {
                reported = report.attempt;
                rstd::io::eprintln("composition committed attempt={} evaluations={} comparisons={} skipped={} created={} updated={} destroyed={}",
                    report.attempt, report.evaluations, report.comparisons, report.skipped,
                    report.created, report.updated, report.destroyed);
            }
        }
        return Ok(empty{});
    }, [] {});
    if (result) (void)examples::composition_status(composition);
    return result;
}
