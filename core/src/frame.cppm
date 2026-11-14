module;
#include <rstd/enum.hpp>
export module vui:frame;
import :composition;
using namespace rstd::prelude;

export namespace vui {
constexpr Index no_clip = ~Index{};
struct FrameIdentity {
    Serial id{}, scene_session{}, scene_revision{}, ui_revision{}, surface_generation{};
};
struct PixelSize {
    Index width{}, height{};
    friend auto operator==(const PixelSize &, const PixelSize &) -> bool = default;
};
struct SurfaceInfo {
    Size logical_size;
    Scalar scale{1};
    Serial generation{1};
    Option<PixelSize> pixel_size;
    friend auto operator==(const SurfaceInfo &, const SurfaceInfo &) -> bool = default;
    auto valid() const -> bool {
        return logical_size.valid() && finite(scale) && scale > 0 && generation != 0 &&
               finite(logical_size.width * scale) && finite(logical_size.height * scale);
    }
};
struct ClipInstance {
    Clip shape;
    Transform transform;
    Index parent{no_clip};
};
class FrameDraw {
    RSTD_ENUM(FrameDraw,
              (Rectangle, (Rect rect; Color color; Scalar radius; Transform transform; Index clip;
                           RenderId node;)),
              (Text, (TextLayout layout; Point origin; Color color; Transform transform; Index clip;
                      RenderId node; TextEffects effects{}; TextColors colors{}; Color tint{1, 1, 1, 1};)),
              (Image, (ImageData data; Rect destination; Rect source; ImageSampling sampling;
                       Color tint; ImageEdges edges; bool clamp_source; Transform transform;
                       Index clip; RenderId node; ImageBlend blend{ImageBlend::SourceOver};
                       ImageRadialFill radial{};)),
              (External, (TextureFrame frame; Serial source_id; Rect destination; Rect source;
                          ImageSampling sampling; Transform transform; Index clip; RenderId node; Color tint{1, 1, 1, 1};
                          ImageBlend blend{ImageBlend::SourceOver};)),
              (Composite, (Index source_pass; Scalar opacity; Index clip; RenderId node;)))
};
struct FramePass {
    Vec<FrameDraw> draws;
};
enum class FrameError { InvalidSurface, InvalidTransform, MissingNode, Limit, InvalidContent, Busy, ThreadUnsafe };
struct ContentSelection {
    Serial source_id{}, source_revision{};
    Option<TextureFrame> frame;
};

struct FrameContent {
    FrameIdentity frame;
    Serial source_id{};
    TextureFrame image;
    ScenePlacement placement;
    Transform local_to_surface;
    auto content_position(Point surface, ContentBounds bounds = ContentBounds::RequireInside) const -> Option<Point> {
        auto inverse = local_to_surface.inverse();
        return inverse ? placement.content_position(inverse->map(surface), bounds) : None<Point>();
    }
};

class FramePlanner;
class FramePlan {
    struct Data {
        FrameIdentity identity;
        SurfaceInfo surface;
        Vec<ClipInstance> clips;
        Vec<FramePass> passes;
        Vec<DisplayList> contents;
        Vec<ContentSelection> sources;
    };
    rstd::sync::Arc<Data> data_;
    explicit FramePlan(Data data) : data_(rstd::sync::Arc<Data>::make(rstd::move(data))) {}
    friend class FramePlanner;

  public:
    FramePlan(const FramePlan &other) : data_(other.data_.clone()) {}
    auto operator=(const FramePlan &other) -> FramePlan & {
        if (this != &other)
            data_ = other.data_.clone();
        return *this;
    }
    FramePlan(FramePlan &&) = default;
    auto operator=(FramePlan &&) -> FramePlan & = default;
    // Only immutable draws are consumed remotely. The producer retains this plan until release.
    auto concurrent_rendering() const -> bool {
        for (const auto &pass : data_->passes) for (const auto &draw : pass.draws) {
            if (draw.is_Text()) for (const auto &font : draw.as_Text().layout.fonts())
                if (!font.concurrent_rasterization()) return false;
            if (draw.is_External() && !draw.as_External().frame.concurrent_read()) return false;
        }
        return true;
    }
    auto identity() const -> FrameIdentity { return data_->identity; }
    auto surface() const -> SurfaceInfo { return data_->surface; }
    auto clips() const -> slice<ClipInstance> { return data_->clips.as_slice(); }
    auto pass_count() const -> usize { return data_->passes.len(); }
    auto draws(Index pass) const -> slice<FrameDraw> {
        return data_->passes[usize(pass)].draws.as_slice();
    }
    auto contents() const -> slice<DisplayList> { return data_->contents.as_slice(); }
    auto sources() const -> slice<ContentSelection> { return data_->sources.as_slice(); }
    auto content(RenderId node, Serial source_id) const -> Result<Option<FrameContent>, FrameError> {
        Option<FrameContent> result;
        for (const auto &pass : data_->passes) for (const auto &draw : pass.draws) if (draw.is_External()) {
            const auto &external = draw.as_External();
            if (external.node != node || external.source_id != source_id) continue;
            if (result) return Err(FrameError::InvalidContent);
            result = Some(FrameContent{identity(), source_id, TextureFrame(external.frame),
                {external.frame.info(), {external.destination, external.source}}, external.transform});
        }
        return Ok(rstd::move(result));
    }
};

class FramePlanner {
    struct State {
        Transform transform;
        Index clip;
    };
    FramePlan::Data output_;
    Index depth_limit_;

    auto select(const ContentSource &source) -> const ContentSelection & {
        for (const auto &selected : output_.sources)
            if (selected.source_id == source.id())
                return selected;
        output_.sources.push(ContentSelection{source.id(), source.revision(), source.latest()});
        return output_.sources[output_.sources.len() - usize(1)];
    }

    auto clip(Clip value, Transform transform, Index parent) -> Index {
        if (!value.enabled)
            return parent;
        auto id = static_cast<Index>(output_.clips.len().to_primitive());
        output_.clips.push({value, transform, parent});
        return id;
    }
    auto append(const RenderScene &scene, RenderId id, Transform parent_transform,
                Index inherited_clip, Index destination, Index depth) -> Option<FrameError> {
        if (depth >= depth_limit_)
            return Some(FrameError::Limit);
        auto node = scene.get(id);
        if (!node)
            return Some(FrameError::MissingNode);
        if (!node->properties.visible || node->properties.opacity == 0)
            return None();
        auto world = parent_transform * node->properties.transform;
        if (!world.valid())
            return Some(FrameError::InvalidTransform);
        auto node_clip = clip(node->properties.clip, world, inherited_clip);
        node_clip = clip(node->properties.layout_clip, world, node_clip);
        auto target = destination;
        bool isolated = node->properties.opacity < 1;
        if (isolated) {
            target = static_cast<Index>(output_.passes.len().to_primitive());
            output_.passes.push(FramePass{});
        }
        // An isolated group applies its inherited mask once at composition.
        auto base_clip = isolated ? no_clip : node_clip;
        State state{world, base_clip};
        Vec<State> stack;
        output_.contents.push(DisplayList(node->content));
        for (const auto &op : node->content.operations()) {
            if (op.is_Save())
                stack.push(State(state));
            else if (op.is_Restore())
                state = stack.pop().unwrap();
            else if (op.is_Transform()) {
                state.transform = state.transform * op.as_Transform().value;
                if (!state.transform.valid())
                    return Some(FrameError::InvalidTransform);
            } else if (op.is_Clip()) {
                state.clip = clip(op.as_Clip().value, state.transform, state.clip);
            } else if (op.is_Rect()) {
                const auto &value = op.as_Rect();
                output_.passes[usize(target)].draws.push(FrameDraw::Rectangle(
                    value.rect, (value.color_source == DrawColorSource::Current ? node->properties.paint_color : value.color).modulate(node->properties.content_tint), value.radius, state.transform, state.clip, id));
            } else if (op.is_Text()) {
                const auto &value = op.as_Text();
                output_.passes[usize(target)].draws.push(
                    FrameDraw::Text(TextLayout(value.layout), value.origin, value.color,
                                    state.transform, state.clip, id, value.effects, TextColors(value.colors), node->properties.content_tint));
            } else if (op.is_Image()) {
                const auto &value = op.as_Image();
                output_.passes[usize(target)].draws.push(FrameDraw::Image(
                    ImageData(value.data), value.destination, value.source, value.sampling,
                    (value.color_source == DrawColorSource::Current ? node->properties.paint_color : value.tint).modulate(node->properties.content_tint), value.edges, value.clamp_source, state.transform, state.clip, id,
                    value.blend, value.radial));
            } else if (op.is_External()) {
                const auto &value = op.as_External();
                const auto &selected = select(value.source);
                if (selected.frame.is_some()) {
                    auto info = selected.frame->info();
                    auto placed = place_image(Size{Scalar(info.width), Scalar(info.height)},
                                              value.destination, value.fit);
                    if (placed.is_err())
                        return Some(FrameError::InvalidContent);
                    auto placement = placed.unwrap();
                    output_.passes[usize(target)].draws.push(FrameDraw::External(
                        TextureFrame(*selected.frame), selected.source_id, placement.destination,
                        placement.source, value.sampling, state.transform, state.clip, id, node->properties.content_tint, value.blend));
                } else {
                    output_.passes[usize(target)].draws.push(FrameDraw::Rectangle(
                        value.destination, value.placeholder.modulate(node->properties.content_tint), 0, state.transform, state.clip, id));
                }
            } else if (op.is_Children()) {
                for (auto child : node->children.values()) {
                    auto error = append(scene, child, world, base_clip, target, depth + 1);
                    if (error.is_some())
                        return error;
                }
            }
        }
        if (isolated)
            output_.passes[usize(destination)].draws.push(
                FrameDraw::Composite(target, node->properties.opacity, node_clip, id));
        return None();
    }

  public:
    explicit FramePlanner(Index depth_limit = 256) : depth_limit_(depth_limit) {}
    auto build(const RenderScene &scene, SurfaceInfo surface, Serial frame_id)
        -> Result<FramePlan, FrameError> {
        if (!surface.valid())
            return Err(FrameError::InvalidSurface);
        output_ = FramePlan::Data{};
        output_.identity = {frame_id, scene.session(), scene.revision(), scene.ui_revision(),
                            surface.generation};
        output_.surface = surface;
        output_.passes.push(FramePass{});
        if (scene.root().is_some()) {
            auto error = append(scene, *scene.root(), {}, no_clip, 0, 0);
            if (error.is_some()) {
                output_ = FramePlan::Data{};
                return Err(*error);
            }
        }
        return Ok(FramePlan(rstd::move(output_)));
    }
};
} // namespace vui
