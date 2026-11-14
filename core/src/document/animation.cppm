export module vui:document.animation;
import :animation.types;
import :node.ownership;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
struct DocumentAnimatedStyle {
    Option<Transform> transform;
    Option<Scalar> opacity, width, height;
    Option<Color> background;
};

struct DocumentAnimationCallback {
    virtual ~DocumentAnimationCallback() = default;
    virtual void invoke(UiDocument &, const AnimationEvent &) = 0;
};

template <class F> struct DocumentAnimationFunction : DocumentAnimationCallback {
    F callback;
    explicit DocumentAnimationFunction(F value) : callback(rstd::move(value)) {}
    void invoke(UiDocument &document, const AnimationEvent &event) override {
        callback(document, event);
    }
};

struct DocumentRunningAnimation {
    AnimationState state;
    AnimationValue from;
    Option<Box<DocumentAnimationCallback>> callback;
};

struct DocumentAnimationCompletion {
    AnimationEvent event;
    Option<Box<DocumentAnimationCallback>> callback;
};
} // namespace vui
