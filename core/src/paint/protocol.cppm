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
    auto canvas() -> Canvas & { return recorder_; }
    void children() {
        recorder_.children(children_mapping_.unwrap_or(Transform{}));
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
