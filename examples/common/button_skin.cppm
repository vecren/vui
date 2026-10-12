export module vui.examples:button_skin;
import vui.controls;
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
struct SkinnedButton {
    UiId id;
    rstd::rc::Rc<ButtonSkin> skin;
    Binding paint, state;
    static auto create(UiSession &session, UiId parent, NodeStyle style, ImageRegion frame,
                       NinePatchSpec border, Option<ImageRegion> icon = {})
        -> Result<SkinnedButton, ControlError> {
        auto created = Button::create(session, style, {}, {.appearance = ButtonAppearance::Custom});
        if (created.is_err())
            return Err(created.unwrap_err());
        auto id = created.unwrap();
        auto &ui = session.document();
        auto attached = ui.attach(parent, id);
        if (attached.is_err()) {
            ui.destroy(id);
            return Err(ControlError{Some(attached.unwrap_err()), {}});
        }
        auto skin =
            rstd::rc::make_rc<ButtonSkin>(ButtonSkin{rstd::move(frame), border, rstd::move(icon)});
        auto paint = ui.bind_paint(id, skin.downgrade());
        if (paint.is_err()) {
            ui.destroy(id);
            return Err(ControlError{Some(paint.unwrap_err()), {}});
        }
        auto state = Button::bind_status_changed(
            ui, id, skin.downgrade(),
            [](ButtonSkin &skin, UiDocument &ui, UiId id, const ButtonStatus &status) {
                skin.status = status;
                ui.invalidate_paint(id).unwrap();
            });
        if (state.is_err()) {
            ui.destroy(id);
            return Err(state.unwrap_err());
        }
        return Ok(SkinnedButton{id, rstd::move(skin), rstd::move(paint).unwrap(),
                                rstd::move(state).unwrap()});
    }
};
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
