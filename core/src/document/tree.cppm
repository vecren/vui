export module vui:document.tree;
import :document.layout;
import :document.text;
import :document.content;
import :document.animation;
import :document.edit;
import :document.modifier;
import :document.subcomposition;
import :layout.protocol;
import :paint.protocol;
import :scene.composition;
import :motion.properties;
import :state.targets;
import :document.phase;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui {
struct DocumentNode {
    LayoutKind kind;
    NodeStyle style;
    Option<UiId> parent;
    Vec<UiId> children;
    Size measured, desired;
    Option<LayoutConstraints> measure_constraints;
    bool measure_dirty{true}, arrange_dirty{true}, subtree_arrange_dirty{true};
    bool pending_layout{}, constraint_conflict{};
    Option<Rect> arranged_slot;
    Option<Box<rstd::dyn<Layout>>> layout_strategy;
    Option<BindingGuard> layout_binding;
    rstd::rc::Weak<DocumentSubcomposition> subcomposition;
    Option<Box<rstd::dyn<Paint>>> painter;
    Option<BindingGuard> paint_binding;
    Vec<DocumentModifier> modifiers;
    Size modifier_content_desired, modifier_content_size;
    Transform modifier_content_transform;
    Point position;
    Transform layout_mapping;
    Option<Size> layout_local_size;
    Clip layout_clip;
    Transform layout_clip_transform;
    bool layout_has_text_input_area{};
    Option<Rect> layout_text_input_area;
    bool paint_dirty{true};
    rstd::rc::Rc<DocumentPhaseObservation> phase_observations[3];
    DisplayList content;
    Option<Box<RenderProperties>> frozen_paint;
    Serial mount_generation{1}, relation_generation{1};
    Vec<UiNodeStateSlot> states;
    Option<UiTreePermit> children_owner;
    Option<Box<DocumentViewportLayout>> viewport;
    Option<Box<DocumentTextContent>> text;
    Option<Box<DocumentImageContent>> image;
    Option<Box<DocumentSceneContent>> scene;
    DocumentAnimatedStyle animated;
    rstd::rc::Weak<PropertyLeaseState> property_owner[static_cast<unsigned>(PresentationProperty::Count)];
    rstd::rc::Weak<UiValueWriterState> value_writer[3];
    DocumentNode(LayoutKind value, NodeStyle properties) : kind(value), style(properties) {}
    DocumentNode(DocumentNode &&) = default;
    auto operator=(DocumentNode &&) -> DocumentNode & = default;
    ~DocumentNode() { while (!states.is_empty()) (void)states.pop(); }
};

struct DocumentSlot {
    Serial generation{1};
    Option<DocumentNode> node;
};
} // namespace vui
