export module vui.examples:composition;
import vui.compose;
using namespace rstd::prelude;

export namespace examples {
auto composition_status(vui::compose::Composition &composition) -> Result<empty, vui::UiError> {
    auto diagnostics = composition.take_diagnostics();
    Option<vui::UiError> failure;
    auto report = [&](const vui::compose::ComposeError &error) {
        auto file = rstd::ffi::CStr::from_ptr(error.source.file_name()).to_str().unwrap();
        rstd::io::eprintln("composition stage={} operation={} path={} revision={} committed={} at={}:{}",
            unsigned(error.stage), error.operation.as_str(), error.path.as_str(), error.revision,
            error.committed, file, error.source.line());
        if (failure.is_none()) failure = Some(error.ui.is_some() ? *error.ui : vui::UiError::InvalidValue);
    };
    for (const auto &error : diagnostics.entries) report(error);
    if (diagnostics.omitted) rstd::io::eprintln("composition omitted {} diagnostics", diagnostics.omitted);
    if (diagnostics.entries.is_empty()) {
        auto result = composition.last_result();
        if (result.is_some() && result->is_err()) report(result->unwrap_err());
    }
    return failure.is_some() ? Result<empty, vui::UiError>(Err(*failure)) : Result<empty, vui::UiError>(Ok(empty{}));
}
} // namespace examples
