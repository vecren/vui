export module vui.examples.gallery:parts;
import vui.compose;
import rstd;

namespace examples::gallery::detail {
using namespace vui;
using namespace rstd::prelude;
namespace cx = vui::compose;

template <class Mount> struct MountedPart {
    LayoutKind kind;
    Mount mount;
    bool owns_children{};

    auto prepare(cx::PrepareContext &context, Option<UiId> previous, NodeStyle style) -> cx::ComposeResult<UiNodeRef> {
        auto node = previous.is_some() ? context.edit().existing(*previous) : context.edit().create(kind, style);
        context.style(node, style);
        if (owns_children) context.owns_children();
        context.on_mount([target = context.target(), mount = mount](cx::CommittedContext &committed) mutable
            -> cx::ComposeResult<cx::EffectLease> {
            auto id = committed.resolve(target);
            if (id.is_err()) return Err(id.unwrap_err());
            auto mounted = mount(committed.session(), *id);
            if (mounted.is_err()) return Err(cx::ComposeError(cx::ComposeStage::Effect, Some(mounted.unwrap_err())));
            return Ok(cx::EffectLease{});
        });
        return Ok(node);
    }
};

template <class Layout>
auto layout_part(Layout layout, Vec<cx::Element> children) -> cx::Element {
    auto mount = [layout](UiSession &session, UiId id) { return session.document().install_layout(id, layout); };
    return cx::element(MountedPart{LayoutKind::Custom, mount}, rstd::move(children));
}

template <class Mount>
auto native_part(Mount mount) -> cx::Element {
    return cx::element(MountedPart{LayoutKind::Stack, rstd::move(mount), true}, {});
}
} // namespace examples::gallery::detail
