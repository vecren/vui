export module vui:document.layout;
import :node.types;
import :document.snapshot;
import :binding.signal;
import :document.reveal;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
struct DocumentGeometryObserver {
    UiId source, reference;
    Signal<void(Option<RelativeGeometry>)> changed;
    BindingGuard subscriber;
    Option<RelativeGeometry> published;
    bool initialized{};
    DocumentGeometryObserver(UiId from, UiId to) : source(from), reference(to) {}
};

struct DocumentViewportObserver {
    Signal<void(ViewportMetrics)> changed;
    Vec<BindingGuard> subscribers;
    Option<ViewportMetrics> published;
    Serial mount{};
};

struct DocumentViewportLayout {
    ScrollAxes axes;
    Point requested;
    ViewportMetrics metrics;
    Option<rstd::rc::Rc<DocumentViewportObserver>> observer;
    bool requires_bounds{};
};
} // namespace vui
