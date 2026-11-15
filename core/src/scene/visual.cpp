module vui;
import :scene.visual;
using namespace rstd::prelude;

namespace vui {
auto UiFrame::image(UiId node, Serial image_id) const -> Result<Option<FrameImage>, FrameError> {
    if (!data_->update.get(node)) return Ok(None<FrameImage>());
    return data_->plan.image(data_->ids[usize(node.slot)], image_id);
}
}
