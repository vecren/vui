export module vui.examples:button_skin;
import vui.controls;
import vui.compose;
using namespace rstd::prelude;

export namespace examples {
using namespace vui;
struct ButtonSkin {
    ImageRegion frame;
    NinePatchSpec border;
    Option<ImageRegion> icon;
    ButtonStatus status;
    Color normal{.06f, .2f, .4f, 1}, pressed{.03f, .1f, .2f, 1}, focused{.12f, .35f, .65f, 1};
    auto paint(PaintContext &context) -> Result<empty, UiError> {
        auto size = context.size();
        auto color = status.pressed ? pressed : status.focused ? focused : normal;
        context.nine_patch(frame, border, {0, 0, size.width, size.height}, color);
        if (icon.is_some()) {
            auto width = rstd::min(Scalar(24), rstd::min(size.width, size.height));
            context.image(ImageData(icon->image),
                          {(size.width - width) / 2, (size.height - width) / 2, width, width},
                          icon->source);
        }
        context.children();
        return Ok(empty{});
    }
};
struct SkinRegistration {
    rstd::rc::Rc<ButtonSkin> skin;
    Option<Binding> paint, status;
    explicit SkinRegistration(ButtonSkin value) : skin(rstd::rc::make_rc<ButtonSkin>(rstd::move(value))) {}
};
struct SkinnedButtonElement {
    vui::compose::ButtonElement button;
    ImageRegion frame;
    NinePatchSpec border;
    auto prepare(vui::compose::PrepareContext &context, Option<UiId> previous, NodeStyle style)
        -> vui::compose::ComposeResult<UiNodeRef> {
        namespace cx = vui::compose;
        auto prepared = button.prepare(context, previous, style);
        if (prepared.is_err()) return Err(prepared.unwrap_err());
        auto state = context.runtime<SkinRegistration>([this]() -> cx::ComposeResult<rstd::rc::Rc<SkinRegistration>> {
            return Ok(rstd::rc::make_rc<SkinRegistration>(ButtonSkin{ImageRegion(frame), border, {}}));
        });
        if (state.is_err()) return Err(state.unwrap_err());
        auto skin = state.unwrap(); auto target = context.target();
        auto &current = *skin.get()->skin.get();
        if (current.frame.image.id() != frame.image.id() || current.frame.source != frame.source ||
            current.border.source_insets != border.source_insets || current.border.destination_insets != border.destination_insets ||
            current.border.draw_center != border.draw_center)
            context.on_commit([skin, target, frame = ImageRegion(frame), border = border](vui::compose::CommittedContext &context) mutable -> vui::compose::ComposeResult<empty> {
                auto id = context.resolve(target); if (id.is_err()) return Err(id.unwrap_err());
                skin.get()->skin.get()->frame = rstd::move(frame); skin.get()->skin.get()->border = border;
                auto result = context.session().document().invalidate_paint(id.unwrap());
                return result.is_ok() ? vui::compose::ComposeResult<empty>(Ok(empty{}))
                    : vui::compose::ComposeResult<empty>(Err(vui::compose::ComposeError(vui::compose::ComposeStage::Effect, Some(result.unwrap_err()))));
            });
        context.on_mount([skin, target](cx::CommittedContext &context) mutable -> cx::ComposeResult<cx::EffectLease> {
            auto node = context.resolve(target); if (node.is_err()) return Err(node.unwrap_err());
            cx::EffectLease cleanup([skin]() mutable { skin.get()->status = None(); skin.get()->paint = None(); });
            auto &ui = context.session().document();
            auto paint = ui.bind_paint(node.unwrap(), skin.get()->skin.downgrade());
            if (paint.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(paint.unwrap_err())));
            skin.get()->paint = Some(paint.unwrap());
            auto status = Button::bind_status_changed(ui, node.unwrap(), skin.get()->skin.downgrade(),
                [](ButtonSkin &skin, UiDocument &ui, UiId id, const ButtonStatus &status) {
                    skin.status = status; ui.invalidate_paint(id).unwrap();
                });
            if (status.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, status.unwrap_err().ui));
            skin.get()->status = Some(status.unwrap()); return Ok(rstd::move(cleanup));
        });
        return Ok(prepared.unwrap());
    }
};

template <class F>
auto skinned_button(vui::compose::Element content, F clicked, ImageRegion frame, NinePatchSpec border)
    -> vui::compose::Element {
    namespace cx = vui::compose;
    Vec<cx::Element> children; children.push(rstd::move(content));
    auto options = ButtonOptionsBuilder{}.appearance(ButtonAppearance::Custom).build();
    return cx::element(SkinnedButtonElement{cx::ButtonElement{rstd::rc::make_rc<cx::ClickFunction>(rstd::move(clicked)), {}, options},
        rstd::move(frame), border}, rstd::move(children));
}
inline auto make_button_frame() -> ImageData {
    unsigned char pixels[36];
    for (unsigned i = 0; i < 9; ++i) {
        auto value = static_cast<unsigned char>(i == 4 ? 200 : 255);
        pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = value;
        pixels[i * 4 + 3] = 255;
    }
    return ImageData::rgba8_srgb(3, 3, slice<unsigned char>::from_raw_parts(pixels, usize(36)))
        .unwrap();
}
} // namespace examples
