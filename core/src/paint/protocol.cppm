export module vui:paint.protocol;
import :paint.display_list;
import :node.types;
using namespace rstd::prelude;

export namespace vui {
struct PaintDiagnostic {
    UiId target;
    Option<RecordingError> recording;
    UiError error{UiError::PaintFailed};
};
class PaintContext {
    UiId id_;
    Size size_;
    DisplayListRecorder &recorder_;
    bool repaint_{};
    NodeStyle style_;
    Option<Transform> children_mapping_;

  public:
    PaintContext(UiId id, Size size, DisplayListRecorder &recorder, NodeStyle style,
                 Option<Transform> children_mapping = {})
        : id_(id), size_(size), recorder_(recorder), style_(style), children_mapping_(children_mapping) {}
    auto style() const -> const NodeStyle & { return style_; }
    auto id() const -> UiId { return id_; }
    auto size() const -> Size { return size_; }
    auto local_to_node() const -> Transform { return recorder_.local_transform(); }
    void save() { recorder_.save(); }
    void restore() { recorder_.restore(); }
    void transform(Transform value) { recorder_.transform(value); }
    void clip(Clip value) { recorder_.clip(value); }
    void rect(Rect value, Color color, Scalar radius = 0, DrawColorSource color_source = DrawColorSource::Fixed) {
        recorder_.rect(value, color, radius, color_source);
    }
    void text(TextLayout value, Point origin, Color color, TextEffects effects = {}, TextColors colors = {}, TextDecorations decorations = {}, TextInlineImages inline_images = {}) {
        recorder_.text(rstd::move(value), origin, color, effects, rstd::move(colors), rstd::move(decorations), rstd::move(inline_images));
    }
    void image(ImageData value, Rect destination, Rect source,
               ImageSampling sampling = ImageSampling::Linear, Color tint = {1, 1, 1, 1},
               ImageEdges edges = ImageEdges::All, bool clamp_source = false,
               ImageBlend blend = ImageBlend::SourceOver, ImageRadialFill radial = {},
               DrawColorSource color_source = DrawColorSource::Fixed) {
        recorder_.image(rstd::move(value), destination, source, sampling, tint, edges,
                        clamp_source, blend, radial, color_source);
    }
    void nine_patch(const ImageRegion &region, const NinePatchSpec &spec, Rect destination,
                    Color tint = {1, 1, 1, 1}, ImageSampling sampling = ImageSampling::Linear,
                    ImageBlend blend = ImageBlend::SourceOver,
                    DrawColorSource color_source = DrawColorSource::Fixed) {
        recorder_.nine_patch(region, spec, destination, tint, sampling, blend, color_source);
    }
    void external(ContentSource source, Rect destination, ImageFit fit = ImageFit::Contain,
                  ImageSampling sampling = ImageSampling::Linear,
                  Color placeholder = {0.15, 0.15, 0.15, 1},
                  ImageBlend blend = ImageBlend::SourceOver) {
        recorder_.external(rstd::move(source), destination, fit, sampling, placeholder, blend);
    }
    void children() {
        if (children_mapping_.is_some()) recorder_.children(*children_mapping_);
        else recorder_.children();
    }
    void request_repaint() { repaint_ = true; }
    auto repaint_requested() const -> bool { return repaint_; }
};
struct Paint {
    template <class Self, class Delegate = void> struct Api {
        using Trait = Paint;
        auto paint(PaintContext &context) -> Result<empty, UiError> {
            return rstd::trait_call<0>(this, context);
        }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::paint>;
};
} // namespace vui
