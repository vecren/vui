module;
#include <cmath>
#include <cstring>
#include <vulkan/vk_platform.h>
module vui.render;
import vrento;
import :atlas;
import :images;
import :frame_resources;
import :timestamps;
import :visibility;
import :effects.backdrop_blur;
import :shaders;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace vui::gpu::detail;

namespace vui::gpu {
namespace rg = ::vrento::vulkan;
namespace vk = ::vrento::vulkan;
namespace tex = ::vrento::texture;

auto Error::session(vrento::vulkan::DeviceSessionError cause) -> Error {
    Stage stage{Stage::State};
    switch (cause.stage) {
    case vrento::vulkan::DeviceSessionStage::Invalid: stage = Stage::State; break;
    case vrento::vulkan::DeviceSessionStage::Instance: stage = Stage::Instance; break;
    case vrento::vulkan::DeviceSessionStage::Surface: stage = Stage::Surface; break;
    case vrento::vulkan::DeviceSessionStage::Device: stage = Stage::Device; break;
    case vrento::vulkan::DeviceSessionStage::Lost: stage = Stage::Lost; break;
    }
    Error result{stage, cause.result};
    if (cause.cause) result.backend = Some(vrento::ErrorDiagnostic::capture(*cause.cause));
    return result;
}

auto Error::graph(vrento::vulkan::GraphExecutionError cause) -> Error {
    Error result{Stage::Graph, cause.native_result()};
    auto texture = cause.texture_error();
    if (texture) result.content = Some(vrento::texture::ErrorSnapshot::capture(**texture));
    result.graph_error = Some(vrento::vulkan::GraphExecutionErrorSnapshot::capture(cause));
    return result;
}

auto Error::graph(vrento::fg::Error cause) -> Error {
    return graph(vrento::vulkan::GraphExecutionError{
        vrento::vulkan::GraphExecutionStage::Graph, cause.pass, cause.resource,
        vrento::vulkan::GraphExecutionCause::Graph(cause)});
}

auto Error::resource(Stage stage, vrento::resource::ResourceError cause) -> Error {
    auto result = graph(vrento::vulkan::GraphExecutionError{
        vrento::vulkan::GraphExecutionStage::Prepare,
        {},
        {},
        vrento::vulkan::GraphExecutionCause::Resource(rstd::move(cause))});
    result.stage = stage;
    return result;
}
namespace {
using CpuSample = vui::CpuTimer;
enum class DrawKind { Rectangle, Composite, Glyph, Image, External, BackdropFilter, BackdropComposite };
auto pipeline_kind(DrawKind kind) -> DrawKind {
    return kind == DrawKind::External ? DrawKind::Image : kind;
}
struct PaintItem {
    DrawData data{};
    DrawKind kind{DrawKind::Rectangle};
    Index clip{no_clip}, source_target{}, resource_index{};
    Index source_version{}, original_target{};
    Scalar sigma{};
    ImageSampling sampling{ImageSampling::Linear};
    ImageBlend blend{ImageBlend::SourceOver};
    VkSampler sampler{};
};
struct DrawSegment {
    Index target{}, version{};
    rg::AttachmentLoad load{rg::AttachmentLoad::Clear};
    bool effects{};
};
struct DrawSources {
    vrento::fg::ResourceVersion primary{}, original{};
};
struct SampleBinding {
    DrawKind kind;
    Index cache_index;
    rstd::sync::Arc<SampledImage> gpu;
    Vec<tex::TextureUpload> uploads;
    rg::ResourceId texture;
};
struct ExternalBinding {
    TextureFrame frame;
    tex::TextureViewRef view;
    rg::ResourceId texture;
};
struct OwnedFrame;
struct UiFrameData;
struct PrepareContext {
    const UiFrameData &frame;
    const vk::Device &device;
    VkDescriptorSetLayout layout;
    VkSampler sampler, glyph_sampler;
};
struct RecordContext {
    VkCommandBuffer command;
    const vvk::DeviceDispatch &dispatch;
    VkPipelineLayout layout;
    VkPipeline rectangle, composite, glyph, image, additive, additive_rgb, plus;
    VkPipeline backdrop_filter{}, backdrop_composite{};
};
struct PreparedDraw {
    VkDescriptorSet descriptor{};
    DrawKind kind;
    Index first{}, count{1};
    ImageBlend blend{ImageBlend::SourceOver};
};
struct FrameGpu {
    vk::DeviceSession session;
    rstd::sync::Arc<FrameResources> resources;
    Option<tex::SubmissionReceipt> receipt;
    bool recyclable{true};
    Vec<Target> targets;
    Vec<tex::SamplerRef> image_samplers;
    FrameBuffer clips, instances, readback;
    VkDescriptorPool pool{};
    FrameGpu(vk::DeviceSession owner, rstd::sync::Arc<FrameResources> cache)
        : session(rstd::move(owner)), resources(rstd::move(cache)) {}
    ~FrameGpu() {
        recyclable &=
            !session.lost() && (!receipt || receipt->status() != tex::SubmissionStatus::Uncertain);
        if (pool)
            session.device().handle().Dispatch().vkDestroyDescriptorPool(*session.device().handle(),
                                                                         pool, nullptr);
        for (auto &target : targets)
            resources->release(rstd::move(target), recyclable);
        resources->release(rstd::move(clips), recyclable);
        resources->release(rstd::move(instances), recyclable);
        resources->release(rstd::move(readback), recyclable);
    }
};
struct UiFrameData {
    rstd::sync::Arc<FrameGpu> gpu;
    Vec<PaintItem> items;
    Vec<DrawSources> sources;
    Index first{};
    DrawOptions options;
    rstd::sync::Arc<DrawStats> stats;
};
struct FrameFence {
    vk::DeviceSession session;
    vvk::Fence value;
    explicit FrameFence(vk::DeviceSession owner) : session(rstd::move(owner)) {}
};
struct UiExecutor {
    Vec<PreparedDraw> draws;
    auto prepare(const rg::PassExecutionInfo &, const vk::GraphBindings &, PrepareContext &)
        -> Result<empty, vk::GraphExecutionError>;
    auto record(RecordContext &) const -> Result<empty, vk::GraphExecutionError>;
};
struct FrameOutput {
    rstd::sync::Arc<FrameFence> fence;
    vvk::Fence acquire_fence;
    vvk::Semaphore acquire_semaphore;
    Index image_index{};
    bool acquired{}, presented{}, suboptimal{};
    explicit FrameOutput(vk::DeviceSession session)
        : fence(rstd::sync::Arc<FrameFence>::make(rstd::move(session))) {}
};
struct OwnedFrame {
    const vk::Device &device;
    bool recyclable{true};
    VkResult uncertain_submission{VK_SUCCESS};
    FramePlan plan;
    rstd::sync::Arc<FrameGpu> gpu;
    struct Execution {
        Option<tex::Submission> owned_submission;
        Option<vk::PreparedGraphProgram> program;
    } execution;
    Vec<Vec<PaintItem>> items;
    Vec<DrawSegment> segments;
    Vec<Index> item_offsets;
    DrawOptions options;
    rstd::sync::Arc<DrawStats> stats{rstd::sync::Arc<DrawStats>::make()};
    FrameTimings timings;
    VkQueryPool queries{};
    PFN_vkCmdResetQueryPool reset_queries{};
    PFN_vkCmdWriteTimestamp write_timestamp{};
    Option<rstd::time::Instant> submitted_at;
    auto metric(CpuTiming FrameTimings::*member) -> CpuTiming * {
        return options.timings ? &(timings.*member) : nullptr;
    }
    void fail_timestamps(VkResult result) {
        if (queries && timings.gpu_status != GpuTimingStatus::Available) {
            timings.gpu_status = GpuTimingStatus::Failed;
            timings.query_result = result;
        }
    }
    void collect_timestamps() {
        if (timings.gpu_status != GpuTimingStatus::Pending)
            return;
        rstd::uint64_t ticks[2]{};
        const auto &handle = device.handle();
        timings.query_result = handle.Dispatch().vkGetQueryPoolResults(
            *handle, queries, 0, 2, sizeof(ticks), ticks, sizeof(ticks[0]), VK_QUERY_RESULT_64_BIT);
        if (timings.query_result != VK_SUCCESS) {
            timings.gpu_status = GpuTimingStatus::Failed;
            return;
        }
        timings.gpu_nanoseconds =
            timestamp_duration(timings.timestamp_valid_bits, timings.timestamp_period_nanoseconds,
                               elapsed_nanoseconds(*submitted_at), ticks[0], ticks[1]);
        if (timings.gpu_nanoseconds.is_none()) {
            timings.gpu_status = GpuTimingStatus::Ambiguous;
            return;
        }
        timings.gpu_status = GpuTimingStatus::Available;
    }
    Vec<SampleBinding> samples;
    Vec<ExternalBinding> externals;
    Vec<rg::ResourceId> textures;
    FrameOutput output;
    VkExtent2D extent{};
    explicit OwnedFrame(vk::DeviceSession session, rstd::sync::Arc<FrameResources> resources,
                        FramePlan plan)
        : device(session.device()), plan(rstd::move(plan)),
          gpu(rstd::sync::Arc<FrameGpu>::make(session, rstd::move(resources))),
          output(rstd::move(session)) {}
    void release_uploads() {
        for (auto &page : samples)
            page.uploads.clear();
    }
    ~OwnedFrame() {
        const auto &d = device.handle().Dispatch();
        if (queries)
            d.vkDestroyQueryPool(*device.handle(), queries, nullptr);
        gpu->recyclable = recyclable;
    }
};
void rectangle_values(float *output, Rect rect) {
    output[0] = static_cast<float>(rect.x);
    output[1] = static_cast<float>(rect.y);
    output[2] = static_cast<float>(rect.width);
    output[3] = static_cast<float>(rect.height);
}
void transform_values(DrawData &output, Transform transform) {
    output.row0[0] = static_cast<float>(transform.a);
    output.row0[1] = static_cast<float>(transform.c);
    output.row0[2] = static_cast<float>(transform.tx);
    output.row1[0] = static_cast<float>(transform.b);
    output.row1[1] = static_cast<float>(transform.d);
    output.row1[2] = static_cast<float>(transform.ty);
}
auto UiExecutor::prepare(const rg::PassExecutionInfo &pass, const vk::GraphBindings &bindings,
                         PrepareContext &context) -> Result<empty, vk::GraphExecutionError> {
    const auto &frame = context.frame;
    const auto &d = context.device.handle().Dispatch();
    draws.clear();
    VkImageView previous_view{};
    VkImageView previous_original{};
    VkSampler previous_sampler{};
    Index instance = frame.first;
    usize paint_index{};
    auto sampled_view = [&](vrento::fg::ResourceVersion source)
        -> Result<VkImageView, vk::GraphExecutionError> {
        bool declared{};
        for (const auto &use : pass.uses)
            declared |= use.resource == source.resource && use.version == source.version &&
                        use.access == rg::GraphAccess::Read && use.usage == rg::PassUsage::Sampled;
        auto bound = bindings.resolve(source.resource, source.version);
        if (!declared || bound.is_none() || (*bound)->Texture().is_none())
            return Err(vk::GraphExecutionError{vk::GraphExecutionStage::Prepare});
        auto native = vk::native_view(*(*bound)->Texture());
        if (native.is_err())
            return Err(vk::GraphExecutionError{
                vk::GraphExecutionStage::Prepare,
                {},
                source.resource,
                vrento::vulkan::GraphExecutionCause::Texture(native.unwrap_err())});
        return Ok(native.unwrap());
    };
    for (const auto &paint : frame.items) {
        PreparedDraw item{{}, paint.kind, instance++, 1, paint.blend};
        VkImageView source_view{}, original_view{};
        auto sources = frame.sources[paint_index++];
        if (item.kind != DrawKind::Rectangle) {
            auto view = sampled_view(sources.primary);
            if (view.is_err())
                return Err(view.unwrap_err());
            source_view = view.unwrap();
        }
        if (item.kind == DrawKind::BackdropComposite) {
            auto view = sampled_view(sources.original);
            if (view.is_err())
                return Err(view.unwrap_err());
            original_view = view.unwrap();
        }
        auto sampler =
            paint.kind == DrawKind::Glyph || paint.kind == DrawKind::BackdropFilter ||
                    paint.kind == DrawKind::BackdropComposite ||
                    ((paint.kind == DrawKind::Image || paint.kind == DrawKind::External) &&
                     paint.sampling == ImageSampling::Linear)
                ? context.glyph_sampler
                : context.sampler;
        if(paint.sampler)sampler=paint.sampler;
        if (draws.is_empty())
            ++frame.stats->pass_starts;
        else {
            auto &previous = draws[draws.len() - usize(1)];
            if (!frame.options.batching)
                ++frame.stats->disabled_breaks;
            else if (pipeline_kind(previous.kind) != pipeline_kind(item.kind) ||
                     previous.blend != item.blend)
                ++frame.stats->pipeline_breaks;
            else if (previous_view != source_view || previous_original != original_view)
                ++frame.stats->texture_breaks;
            else if (previous_sampler != sampler)
                ++frame.stats->sampler_breaks;
            else {
                ++previous.count;
                ++frame.stats->merged_items;
                continue;
            }
        }
        previous_view = source_view;
        previous_original = original_view;
        previous_sampler = sampler;
        ++frame.stats->batches;
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = frame.gpu->pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &context.layout;
        auto allocation_result =
            d.vkAllocateDescriptorSets(*context.device.handle(), &allocate, &item.descriptor);
        if (allocation_result != VK_SUCCESS)
            return Err(vk::GraphExecutionError{
                vk::GraphExecutionStage::Prepare,
                {},
                {},
                vrento::vulkan::GraphExecutionCause::Native(allocation_result)});
        VkDescriptorBufferInfo buffer{frame.gpu->clips.handle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo instances{frame.gpu->instances.handle(), 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet writes[4]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = item.descriptor;
        writes[0].dstBinding = 1;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo = &buffer;
        writes[1] = writes[0];
        writes[1].dstBinding = 2;
        writes[1].pBufferInfo = &instances;
        VkDescriptorImageInfo image{sampler, source_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo original{context.sampler, original_view,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        if (item.kind != DrawKind::Rectangle) {
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = item.descriptor;
            writes[2].dstBinding = 0;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].pImageInfo = &image;
        }
        if (original_view) {
            writes[3] = writes[2];
            writes[3].dstBinding = 3;
            writes[3].pImageInfo = &original;
        }
        d.vkUpdateDescriptorSets(*context.device.handle(),
                                 original_view ? 4 : source_view ? 3 : 2, writes, 0, nullptr);
        draws.push(rstd::move(item));
    }
    return Ok(empty{});
}
auto UiExecutor::record(RecordContext &context) const -> Result<empty, vk::GraphExecutionError> {
    const auto &d = context.dispatch;
    for (const auto &draw : draws) {
        d.vkCmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            draw.kind == DrawKind::Image || draw.kind == DrawKind::External
                                ? (draw.blend == ImageBlend::Plus ? context.plus : draw.blend == ImageBlend::AdditiveRgb ? context.additive_rgb : draw.blend == ImageBlend::Additive ? context.additive : context.image)
                            : draw.kind == DrawKind::Glyph     ? context.glyph
                            : draw.kind == DrawKind::Composite ? context.composite
                            : draw.kind == DrawKind::BackdropFilter ? context.backdrop_filter
                            : draw.kind == DrawKind::BackdropComposite ? context.backdrop_composite
                                                               : context.rectangle);
        d.vkCmdBindDescriptorSets(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, context.layout,
                                  0, 1, &draw.descriptor, 0, nullptr);
        d.vkCmdDraw(context.command, 6, draw.count, 0, draw.first);
    }
    return Ok(empty{});
}
constexpr rg::GraphSerial ui_kind = 0x5655494452415701ull;
constexpr rg::GraphSerial ui_effect_kind = 0x5655494452415702ull;
constexpr usize ui_pipeline_count{7};
struct UiAssets {
    vk::DeviceSession session;
    vrento::resource_registry::DescriptorLayoutRegistry &descriptors;
    vrento::resource_registry::PipelineLayoutRegistry &layouts;
    vrento::resource_registry::RenderPassRegistry &passes;
    vrento::resource_registry::PipelineRegistry &pipelines;
    Option<vrento::resource_registry::PipelineLayoutResult> layout;
    vvk::Sampler sampler, glyph_sampler;
    Vec<vrento::resource::PipelineRecipeHandle> recipes;
    Vec<vrento::resource_registry::PipelineResourceResult> warmed;
    Option<vk::PipelinePreparationQueue> preparation;
    explicit UiAssets(vk::DeviceSession owner)
        : session(rstd::move(owner)), descriptors(session.graphics_resources().descriptors),
          layouts(session.graphics_resources().layouts),
          passes(session.graphics_resources().passes),
          pipelines(session.graphics_resources().pipelines) {}
    ~UiAssets() {
        for (const auto &recipe : recipes)
            pipelines.ReleaseRecipe(recipe);
    }
    auto descriptor() const -> VkDescriptorSetLayout {
        return *(**descriptors.Resolve(layout->physical->descriptor_layouts[usize()])).layout;
    }
    auto initialize(vk::PipelinePreparationMode) -> Result<empty, Error>;
    auto register_backdrop() -> Result<empty, Error>;
    template <class Shader>
    auto register_pipeline(const Shader &, ImageBlend, bool replace)
        -> Result<empty, Error>;
};
struct UiPrepared {
    rstd::sync::Arc<FrameGpu> gpu;
    rstd::sync::Arc<UiAssets> assets;
    rstd::sync::Arc<Vec<vrento::resource_registry::PipelineResourceResult>> pipelines;
    UiExecutor executor;
    RecordContext native;
    auto Record(vk::GraphRecordContext &context) const -> Result<empty, vk::GraphExecutionError> {
        auto command = vk::native_recording(context.submission, context.session);
        if (command.is_err())
            return Err(vk::GraphExecutionError{
                vk::GraphExecutionStage::Record,
                {},
                {},
                vrento::vulkan::GraphExecutionCause::Texture(command.unwrap_err())});
        auto state = native;
        state.command = command.unwrap();
        return executor.record(state);
    }
};
struct UiDrawPassDesc {
    using PassDescriptor = UiDrawPassDesc;
    vk::VulkanPassIO io;
    VkFormat format;
};
struct UiDrawPass {
    using FrameData = UiFrameData;
    rstd::sync::Arc<UiAssets> assets;
    vk::PassExecutionInfo description;
    rstd::sync::Arc<Vec<vrento::resource_registry::PipelineResourceResult>> pipelines;
    auto Instantiate(const vk::GraphPrepareContext &inputs, rstd::sync::Arc<FrameData> frame) const
        -> Result<vrento::Preparation<rstd::sync::Arc<dyn<vk::VulkanPass>>>,
                  vk::GraphExecutionError>;
};
struct UiPassFactory {
    using Pass = UiDrawPass;
    rstd::sync::Arc<UiAssets> assets;
    auto Compile(const UiDrawPassDesc &desc, const vk::PassExecutionInfo &description,
                 const vk::PassCompileContext &)
        -> Result<vrento::Preparation<rstd::sync::Arc<dyn<vk::VulkanPass>>>,
                  vk::GraphExecutionError> {
        Vec<vrento::resource_registry::PipelineResourceResult> pipelines;
        vk::RenderPassResourceDesc signature;
        signature.color_format = desc.format;
        const auto count = description.kind == ui_effect_kind ? assets->recipes.len() : ui_pipeline_count;
        for (usize i{}; i < count; ++i) {
            const auto &recipe = assets->recipes[i];
            auto prepared = assets->preparation->prepare(recipe, signature);
            if (prepared.is_err())
                return Err(vk::GraphExecutionError{
                    vk::GraphExecutionStage::Prepare,
                    description.id,
                    {},
                    vk::GraphExecutionCause::Resource(prepared.unwrap_err())});
            if (prepared->status == vk::PipelineReadiness::Pending)
                return Ok(vrento::PendingPreparation{});
            if (prepared->error)
                return Err(vk::GraphExecutionError{
                    vk::GraphExecutionStage::Prepare,
                    description.id,
                    {},
                    vk::GraphExecutionCause::Resource(prepared->error.take().unwrap())});
            if (!prepared->pipeline)
                return Err(
                    vk::GraphExecutionError{vk::GraphExecutionStage::Prepare, description.id});
            pipelines.push(prepared->pipeline.take().unwrap());
        }
        return Ok(rstd::sync::Arc<dyn<vk::VulkanPass>>::make(UiDrawPass{
            assets.clone(), description.clone(),
            rstd::sync::Arc<Vec<vrento::resource_registry::PipelineResourceResult>>::make(
                rstd::move(pipelines))}));
    }
};
struct ScopeBackend {
    vk::GraphicsScopeCache &cache;
    auto Prepare(const rg::GraphicsDescription &description, const vk::GraphPrepareContext &context)
        -> Result<vrento::vulkan::Preparation<rstd::sync::Arc<dyn<vk::PreparedGraphicsScope>>>,
                  vk::GraphExecutionError> {
        return cache.Prepare(description, context);
    }
};
auto resolve_items(OwnedFrame &frame, GlyphCache &cache, ImageCache &images, Index max_extent,
                   const vk::DeviceSession &session) -> Result<empty, Error> {
    auto surface = frame.plan.surface();
    const auto scale_x = frame.extent.width / surface.logical_size.width;
    const auto scale_y = frame.extent.height / surface.logical_size.height;
    auto raster_scale = rstd::cmp::max(scale_x, scale_y);
    if (!finite(raster_scale) || raster_scale <= 0)
        return Err(Error{Stage::State});
    auto coverage_scale = rstd::cmp::min(scale_x, scale_y);
    const auto raster_x = frame.extent.width / static_cast<float>(surface.logical_size.width);
    const auto raster_y = frame.extent.height / static_cast<float>(surface.logical_size.height);
    Serial total{};
    for (usize pass{}; pass < frame.plan.pass_count(); ++pass) {
        Vec<PaintItem> items;
        for (const auto &draw : frame.plan.draws(static_cast<Index>(pass.to_primitive()))) {
            PaintItem item;
            item.data.surface[0] = static_cast<float>(surface.logical_size.width);
            item.data.surface[1] = static_cast<float>(surface.logical_size.height);
            item.data.surface[2] = static_cast<float>(coverage_scale);
            item.data.row0[3] = raster_x;
            item.data.row1[3] = raster_y;
            auto color = [&](Color c) {
                item.data.color[0] = c.r;
                item.data.color[1] = c.g;
                item.data.color[2] = c.b;
                item.data.color[3] = c.a;
            };
            if (draw.is_Rectangle()) {
                const auto &value = draw.as_Rectangle();
                rectangle_values(item.data.rect, value.rect);
                color(value.color);
                transform_values(item.data, value.transform);
                item.data.params[0] = static_cast<float>(value.radius);
                item.clip = value.clip;
            } else if (draw.is_BackdropBlur()) {
                const auto &value = draw.as_BackdropBlur();
                if (value.transform.inverse().is_none())
                    continue;
                item.kind = DrawKind::BackdropComposite;
                item.sigma = value.sigma;
                rectangle_values(item.data.rect, value.mask.rect);
                transform_values(item.data, value.transform);
                item.data.params[0] = static_cast<float>(value.mask.radius);
                item.clip = value.clip;
            } else if (draw.is_Composite()) {
                const auto &value = draw.as_Composite();
                item.kind = DrawKind::Composite;
                item.source_target = value.source_pass;
                item.data.params[3] = 1;
                item.data.params[1] = static_cast<float>(value.opacity);
                rectangle_values(item.data.rect,
                                 {0, 0, surface.logical_size.width, surface.logical_size.height});
                transform_values(item.data, {});
                item.clip = value.clip;
            } else if (draw.is_Image()) {
                const auto &value = draw.as_Image();
                if (!value.destination.width || !value.destination.height ||
                    value.transform.inverse().is_none())
                    continue;
                auto resolved = images.resolve(value.data, max_extent);
                if (resolved.is_err())
                    return Err(resolved.unwrap_err());
                item.kind = DrawKind::Image;
                item.blend = value.blend;
                item.resource_index = resolved.unwrap();
                item.sampling = value.sampling;
                item.data.image_edges = static_cast<unsigned>(value.edges);
                item.data.params[3] = value.clamp_source ? 1 : 0;
                item.data.color[0] = value.tint.r;
                item.data.color[1] = value.tint.g;
                item.data.color[2] = value.tint.b;
                item.data.color[3] = value.tint.a;
                item.clip = value.clip;
                transform_values(item.data, value.transform);
                rectangle_values(item.data.rect, value.destination);
                item.data.uv[0] = value.source.x / value.data.width();
                item.data.uv[1] = value.source.y / value.data.height();
                item.data.uv[2] = value.source.width / value.data.width();
                item.data.uv[3] = value.source.height / value.data.height();
                item.data.radial[0] = std::cos(value.radial.start_angle);
                item.data.radial[1] = std::sin(value.radial.start_angle);
                item.data.radial[2] = (value.radial.clockwise ? 1 : -1) * value.radial.amount * 6.283185307179586;
                item.data.radial[3] = value.radial.amount;
            } else if (draw.is_External()) {
                const auto &value = draw.as_External();
                if (!value.destination.width || !value.destination.height ||
                    value.transform.inverse().is_none())
                    continue;
                auto native = value.frame.payload<NativeTextureFrame>();
                if (!native || native->info() != value.frame.info())
                    return Err(Error{Stage::ExternalContent});
                auto info = native->info();
                bool hardware_srgb = info.format == ContentFormat::Rgba8Srgb ||
                                     info.format == ContentFormat::Bgra8Srgb;
                bool decode = info.color_space == ContentColorSpace::Srgb && !hardware_srgb;
                if (value.sampling == ImageSampling::Linear &&
                    (decode || info.alpha == ContentAlpha::Straight))
                    return Err(Error{Stage::Unsupported});
                auto prepared = vrento::vulkan::validate_sampled_texture(
                    session, native->native(), vrento::texture::Stages::Fragment,
                    value.sampling == ImageSampling::Linear);
                if (prepared.is_err()) {
                    Error error{Stage::ExternalContent};
                    error.content = Some(tex::ErrorSnapshot::capture(prepared.unwrap_err()));
                    return Err(error);
                }
                Index index = static_cast<Index>(frame.externals.len().to_primitive());
                for (usize i{}; i < frame.externals.len(); ++i)
                    if (frame.externals[i].frame.payload<NativeTextureFrame>() == native) {
                        index = static_cast<Index>(i.to_primitive());
                        break;
                    }
                if (usize(index) == frame.externals.len())
                    frame.externals.push(ExternalBinding{value.frame, native->native(), {}});
                item.kind = DrawKind::External;
                item.blend = value.blend;
                item.data.image_edges = 15;
                color(value.tint);
                item.resource_index = index;
                item.sampling = value.sampling;
                item.clip = value.clip;
                transform_values(item.data, value.transform);
                rectangle_values(item.data.rect, value.destination);
                item.data.params[0] = decode ? 1 : 0;
                item.data.params[1] = info.alpha == ContentAlpha::Straight ? 1
                                      : info.alpha == ContentAlpha::Opaque ? 2
                                                                           : 0;
                item.data.uv[0] = value.source.x / info.width;
                item.data.uv[1] = value.source.y / info.height;
                item.data.uv[2] = value.source.width / info.width;
                item.data.uv[3] = value.source.height / info.height;
                if (info.origin == ContentOrigin::BottomLeft) {
                    item.data.uv[1] = 1 - item.data.uv[1];
                    item.data.uv[3] = -item.data.uv[3];
                }
            } else if (draw.is_Text()) {
                const auto &value = draw.as_Text();
                if (!value.effects.valid()) return Err(Error{Stage::Unsupported});
                if (value.transform.inverse().is_none()) continue;
                auto scaled = glyph_scale(value.transform, raster_scale);
                if (scaled.is_err()) return Err(scaled.unwrap_err());
                Scalar scale = scaled.unwrap();
                item.kind = DrawKind::Glyph;
                item.data.params[3] = 2;
                item.clip = value.clip;
                transform_values(item.data, value.transform);
                struct Pass { Color color; Point offset; Scalar outline; };
                const auto &effect = value.effects;
                Pass passes[]{{effect.shadow_color, effect.shadow_offset, effect.outline_width},
                              {effect.outline_width > 0 ? effect.outline_color : Color{}, {}, effect.outline_width},
                              {value.color, {}, 0}};
                for (unsigned pass{}; pass < 3; ++pass) {
                    const auto &paint = passes[pass];
                    if ((pass < 2 && !paint.color.a) || (pass == 1 && !effect.outline_width)) continue;
                    for (const auto &glyph : value.layout.glyphs()) {
                        if (!glyph.drawable) continue;
                        auto fill = pass == 2 ? value.colors.color(glyph.byte_start, paint.color) : paint.color;
                        if (!fill.a) continue;
                        color(fill.modulate(value.tint));
                        auto resolved = cache.resolve(value.layout.fonts()[usize(glyph.font)],
                                                      glyph.glyph, scale, max_extent, paint.outline * scale);
                        if (resolved.is_err()) return Err(resolved.unwrap_err());
                        auto entry = resolved.unwrap();
                        if (!entry.width || !entry.height) continue;
                        item.resource_index = entry.page;
                        const auto &page = cache.pages[usize(entry.page)];
                        rectangle_values(item.data.rect,
                            {value.origin.x + paint.offset.x + glyph.position.x + (entry.bearing.x - 1) / scale,
                             value.origin.y + paint.offset.y + glyph.position.y + (entry.bearing.y - 1) / scale,
                             (entry.width + 2) / scale, (entry.height + 2) / scale});
                        item.data.uv[0] = float(entry.x - 1) / page.width;
                        item.data.uv[1] = float(entry.y - 1) / page.height;
                        item.data.uv[2] = float(entry.width + 2) / page.width;
                        item.data.uv[3] = float(entry.height + 2) / page.height;
                        if (++total > 65536) return Err(Error{Stage::DrawLimit});
                        items.push(PaintItem(item));
                    }
                }
                continue;
            } else
                return Err(Error{Stage::Unsupported});
            if (++total > 65536)
                return Err(Error{Stage::DrawLimit});
            items.push(rstd::move(item));
        }
        frame.items.push(rstd::move(items));
    }
    return Ok(empty{});
}
auto cull_items(OwnedFrame &frame, slice<GpuClip> clips) -> Result<empty, Error> {
    RasterBounds surface{0, 0, double(frame.extent.width), double(frame.extent.height)};
    for (auto &items : frame.items) {
        Option<visibility::QuadMapping> mapping;
        Vec<PaintItem> visible;
        for (auto &item : items) {
            float values[sizeof(DrawData) / sizeof(float)];
            std::memcpy(values, &item.data, sizeof(DrawData));
            for (auto value : values)
                if (!std::isfinite(value))
                    return Err(Error{Stage::Unsupported});
            ++frame.stats->resolved_items;
            bool discard{};
            if (frame.options.culling &&
                !(item.clip == no_clip && visibility::center_on_surface(item.data))) {
                if (mapping.is_none() || !mapping->matches(item.data))
                    mapping = Some(visibility::QuadMapping(item.data));
                auto bounds = mapping->quad(item.data, frame.extent);
                if (bounds.is_some() && visibility::outside(*bounds, surface)) {
                    ++frame.stats->surface_culled;
                    continue;
                }
                auto pixels = surface;
                if (bounds.is_some()) {
                    pixels = {std::fmax(surface.left, bounds->left),
                              std::fmax(surface.top, bounds->top),
                              std::fmin(surface.right, bounds->right),
                              std::fmin(surface.bottom, bounds->bottom)};
                }
                auto clip = item.clip;
                while (clip != no_clip) {
                    const auto &value = clips[usize(clip)];
                    if (visibility::clipped(pixels, value, item.data.row0[3], item.data.row1[3])) {
                        discard = true;
                        ++frame.stats->clip_culled;
                        break;
                    }
                    clip = value.params[1] < 0 ? no_clip : static_cast<Index>(value.params[1]);
                }
            }
            if (!discard)
                visible.push(rstd::move(item));
        }
        items = rstd::move(visible);
    }
    return Ok(empty{});
}

auto segment_draws(OwnedFrame &frame, vk::TextureContext &textures, slice<GpuClip> clips,
                   rg::AttachmentLoad root_load) -> Result<empty, Error> {
    auto logical_items = rstd::move(frame.items);
    Vec<Index> final_versions;
    for (usize i{}; i < logical_items.len(); ++i)
        final_versions.push(0u);
    auto emit = [&](DrawSegment segment, Vec<PaintItem> items) {
        frame.segments.push(rstd::move(segment));
        frame.items.push(rstd::move(items));
    };
    auto emit_filter = [&](DrawData data, Index target, Index source, Index version) {
        PaintItem paint;
        paint.data = data;
        paint.kind = DrawKind::BackdropFilter;
        paint.source_target = source;
        paint.source_version = version;
        Vec<PaintItem> items;
        items.push(rstd::move(paint));
        emit({target, 1, rg::AttachmentLoad::Clear, true}, rstd::move(items));
    };
    Serial remaining_samples = backdrop_sample_budget;
    for (auto i = logical_items.len(); i > usize(); --i) {
        auto index = i - usize(1);
        auto target = static_cast<Index>(index.to_primitive());
        const auto &attachment = frame.gpu->targets[index];
        const auto desc = attachment.image.description();
        const auto view = attachment.view.description();
        const bool sampled = tex::has(desc.usage, tex::Usage::Sampled) &&
                             view.dimension == tex::Dimension::D2 &&
                             view.range.mip_count == 1 && view.range.layer_count == 1;
        DrawSegment segment{target, 1, target ? rg::AttachmentLoad::Clear : root_load};
        bool has_blur{};
        for (auto &paint : logical_items[index]) {
            if (paint.kind == DrawKind::Composite)
                paint.source_version = final_versions[usize(paint.source_target)];
            has_blur |= paint.kind == DrawKind::BackdropComposite;
        }
        if (!has_blur) {
            final_versions[index] = segment.version;
            emit(segment, rstd::move(logical_items[index]));
            continue;
        }
        Vec<PaintItem> items;
        for (auto &paint : logical_items[index]) {
            if (paint.kind != DrawKind::BackdropComposite) {
                items.push(rstd::move(paint));
                continue;
            }
            if (!sampled) {
                ++frame.stats->backdrop_unavailable;
                continue;
            }
            auto blur = BackdropBlur::plan(paint.data, paint.sigma, frame.plan.surface().logical_size,
                                          frame.extent, clips, paint.clip);
            if (!blur)
                continue;
            if (blur->samples > remaining_samples) {
                ++frame.stats->backdrop_budget_skips;
                continue;
            }
            Vec<Target> allocated;
            VkExtent2D extents[]{blur->capture, blur->filtered, blur->filtered};
            for (auto extent : extents) {
                auto scratch = frame.gpu->resources->target(textures, extent, desc.format);
                if (scratch.is_err()) {
                    auto error = scratch.unwrap_err();
                    for (auto &entry : allocated)
                        frame.gpu->resources->release(rstd::move(entry), true);
                    allocated.clear();
                    if (error.stage != Stage::FrameResourceLimit)
                        return Err(error);
                    ++frame.stats->backdrop_budget_skips;
                    break;
                }
                allocated.push(scratch.unwrap());
            }
            if (allocated.is_empty())
                continue;
            auto original = static_cast<Index>(frame.gpu->targets.len().to_primitive());
            for (auto &entry : allocated) {
                frame.gpu->targets.push(rstd::move(entry));
                frame.textures.push(rg::ResourceId{});
            }
            emit(segment, rstd::move(items));
            items = {};
            DrawData snapshot{}, horizontal{}, vertical{};
            blur->snapshot(snapshot);
            blur->filter(horizontal, false);
            blur->filter(vertical, true);
            emit_filter(snapshot, original, target, segment.version);
            emit_filter(horizontal, original + 1, original, 1);
            emit_filter(vertical, original + 2, original + 1, 1);
            ++segment.version;
            segment.load = rg::AttachmentLoad::Load;
            segment.effects = true;
            blur->composite(paint.data);
            paint.original_target = original;
            paint.source_target = original + 2;
            paint.source_version = 1;
            items.push(rstd::move(paint));
            remaining_samples -= blur->samples;
            ++frame.stats->backdrop_captures;
            frame.stats->backdrop_capture_pixels += Serial(blur->capture.width) * blur->capture.height;
            frame.stats->backdrop_filter_pixels += Serial(blur->filtered.width) * blur->filtered.height * 2;
            frame.stats->backdrop_filter_passes += 2;
            if (blur->filtered.width != blur->capture.width || blur->filtered.height != blur->capture.height)
                ++frame.stats->backdrop_downsampled;
        }
        final_versions[index] = segment.version;
        emit(segment, rstd::move(items));
    }
    return Ok(empty{});
}
auto prepare_atlas(OwnedFrame &frame, GlyphCache &cache, vk::TextureContext &textures)
    -> Result<empty, Error> {
    for (usize i{}; i < cache.pages.len(); ++i) {
        bool used{};
        for (const auto &items : frame.items)
            for (const auto &item : items)
                used |= item.kind == DrawKind::Glyph && item.resource_index == i.to_primitive();
        if (!used)
            continue;
        auto &page = cache.pages[i];
        if (page.gpu.is_none()) {
            auto created = make_sampled(textures, {page.width, page.height}, VK_FORMAT_R8_UNORM);
            if (created.is_err())
                return Err(created.unwrap_err());
            page.gpu = Some(rstd::move(created).unwrap());
            cache.stats.gpu_bytes += Serial(page.width) * page.height;
        }
        SampleBinding binding{
            DrawKind::Glyph, static_cast<Index>(i.to_primitive()), page.gpu->clone(), {}, {}};
        if (!page.resident) {
            auto uploaded = make_upload(page.gpu->clone(), page.pixels.as_slice());
            if (uploaded.is_err())
                return Err(uploaded.unwrap_err());
            binding.uploads.push(rstd::move(uploaded).unwrap());
        }
        frame.samples.push(rstd::move(binding));
    }
    return Ok(empty{});
}
auto prepare_images(OwnedFrame &frame, ImageCache &cache, vk::TextureContext &textures)
    -> Result<empty, Error> {
    for (usize i{}; i < cache.entries.len(); ++i) {
        bool used{};
        for (const auto &items : frame.items)
            for (const auto &item : items)
                used |= item.kind == DrawKind::Image && item.resource_index == i.to_primitive();
        if (!used)
            continue;
        auto &entry = cache.entries[i];
        if (entry.gpu.is_none()) {
            auto created =
                make_sampled(textures, {entry.data.width(), entry.data.height()}, format,entry.data.mip_count());
            if (created.is_err())
                return Err(created.unwrap_err());
            auto image=rstd::move(created).unwrap();image->bytes=entry.data.byte_size();
            if(entry.data.texture_options()!=ImageTextureOptions{}){
                auto nearest=make_image_sampler(textures,entry.data,false);if(nearest.is_err())return Err(nearest.unwrap_err());
                auto linear=make_image_sampler(textures,entry.data,true);if(linear.is_err())return Err(linear.unwrap_err());
                image->nearest=Some(rstd::move(nearest).unwrap());image->linear=Some(rstd::move(linear).unwrap());
            }
            entry.gpu=Some(rstd::move(image));cache.stats.gpu_bytes+=entry.data.byte_size();
        }
        SampleBinding binding{
            DrawKind::Image, static_cast<Index>(i.to_primitive()), entry.gpu->clone(), {}, {}};
        for(auto &items:frame.items)for(auto &item:items)
            if(item.kind==DrawKind::Image && item.resource_index==i.to_primitive()){
                const auto &sampler=item.sampling==ImageSampling::Linear?(*entry.gpu)->linear:(*entry.gpu)->nearest;
                if(sampler.is_some()){
                    auto native=vk::native_sampler(*sampler);if(native.is_err())return Err(Error::texture(Stage::Allocation,native.unwrap_err()));
                    item.sampler=native.unwrap();frame.gpu->image_samplers.push(tex::SamplerRef(*sampler));
                }
            }
        if (!entry.resident) {
            for(Index mip{};mip<entry.data.mip_count();++mip){
                auto pixels=premultiply_srgb(entry.data.mip_level(mip).unwrap().pixels);
                auto uploaded=make_upload(entry.gpu->clone(),pixels.as_slice(),mip);
                if(uploaded.is_err())return Err(uploaded.unwrap_err());
                binding.uploads.push(rstd::move(uploaded).unwrap());
            }
        }
        frame.samples.push(rstd::move(binding));
    }
    return Ok(empty{});
}
} // namespace

} // namespace vui::gpu
namespace rstd {
template <>
struct Impl<vrento::vulkan::GraphicsScopeBackend, vui::gpu::ScopeBackend>
    : ImplBase<vui::gpu::ScopeBackend> {
    auto Prepare(const vrento::vulkan::GraphicsDescription &description,
                 const vrento::vulkan::GraphPrepareContext &context)
        -> Result<
            vrento::vulkan::Preparation<sync::Arc<dyn<vrento::vulkan::PreparedGraphicsScope>>>,
            vrento::vulkan::GraphExecutionError> {
        return self().Prepare(description, context);
    }
};
} // namespace rstd
namespace vui::gpu {
namespace {
auto UiDrawPass::Instantiate(const vk::GraphPrepareContext &inputs,
                             rstd::sync::Arc<FrameData> frame) const
    -> Result<vrento::Preparation<rstd::sync::Arc<dyn<vk::VulkanPass>>>, vk::GraphExecutionError> {
    if (!inputs.scope)
        return Err(vk::GraphExecutionError{vk::GraphExecutionStage::Schema});
    PrepareContext context{*frame, inputs.session.device(), assets->descriptor(), *assets->sampler,
                           *assets->glyph_sampler};
    UiExecutor executor;
    auto prepared = executor.prepare(description, inputs.bindings, context);
    if (prepared.is_err())
        return Err(prepared.unwrap_err());
    RecordContext native{VK_NULL_HANDLE,
                         inputs.session.device().handle().Dispatch(),
                         *assets->layout->physical->layout,
                         *(*pipelines)[usize()].pipeline->pipeline.handle,
                         *(*pipelines)[usize(1)].pipeline->pipeline.handle,
                         *(*pipelines)[usize(2)].pipeline->pipeline.handle,
                         *(*pipelines)[usize(3)].pipeline->pipeline.handle,
                         *(*pipelines)[usize(4)].pipeline->pipeline.handle,
                         *(*pipelines)[usize(5)].pipeline->pipeline.handle,
                         *(*pipelines)[usize(6)].pipeline->pipeline.handle};
    if (description.kind == ui_effect_kind) {
        native.backdrop_filter = *(*pipelines)[usize(7)].pipeline->pipeline.handle;
        native.backdrop_composite = *(*pipelines)[usize(8)].pipeline->pipeline.handle;
    }
    return Ok(rstd::sync::Arc<dyn<vk::VulkanPass>>::make(
        UiPrepared{context.frame.gpu.clone(), assets.clone(), pipelines.clone(),
                   rstd::move(executor), native}));
}
} // namespace

namespace {
template <rstd::size_t N>
auto ui_shader(const unsigned (&words)[N], vrento::ShaderType type) -> vk::Uni_ShaderSpv {
    auto result = Box<vk::ShaderSpv>::make();
    result->stage = type;
    result->spirv = vrento::ShaderCode::from(
        slice<rstd::uint32_t>::from_raw_parts(words, usize(N)));
    return result;
}
auto UiAssets::initialize(vk::PipelinePreparationMode mode) -> Result<empty, Error> {
    auto worker = vk::PipelinePreparationQueue::create(session, mode);
    if (worker.is_err())
        return Err(Error::resource(Stage::Pipeline, worker.unwrap_err()));
    preparation = Some(worker.unwrap());
    vrento::resource_registry::PipelineLayoutRequest request;
    vk::DescriptorSetInfo set;
    set.bindings.push(
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    set.bindings.push(
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    set.bindings.push({2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    set.bindings.push(
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    request.descriptor_sets.push(rstd::move(set));
    auto created = layouts.Ensure(session.device(), request, descriptors);
    if (created.is_err())
        return Err(Error::resource(Stage::Pipeline, created.unwrap_err()));
    layout = Some(created.unwrap());
    VkSamplerCreateInfo sample{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sample.magFilter = sample.minFilter = VK_FILTER_NEAREST;
    sample.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sample.addressModeU = sample.addressModeV = sample.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    auto result = session.device().handle().CreateSampler(sample, sampler);
    if (result != VK_SUCCESS)
        return Err(Error{Stage::Pipeline, result});
    sample.magFilter = sample.minFilter = VK_FILTER_LINEAR;
    result = session.device().handle().CreateSampler(sample, glyph_sampler);
    if (result != VK_SUCCESS)
        return Err(Error{Stage::Pipeline, result});
    auto add = [&](const auto &fragment, ImageBlend blend = ImageBlend::SourceOver) {
        return register_pipeline(fragment, blend, false);
    };
    auto rectangle = add(shaders::rectangle);
    if (rectangle.is_err())
        return rectangle;
    auto composite = add(shaders::composite);
    if (composite.is_err())
        return composite;
    auto glyph = add(shaders::glyph);
    if (glyph.is_err())
        return glyph;
    auto image = add(shaders::image);
    if (image.is_err())
        return image;
    auto additive = add(shaders::image, ImageBlend::Additive);
    if (additive.is_err()) return additive;
    auto rgb = add(shaders::image, ImageBlend::AdditiveRgb);
    if (rgb.is_err()) return rgb;
    return add(shaders::image, ImageBlend::Plus);
}

template <class Shader>
auto UiAssets::register_pipeline(const Shader &fragment, ImageBlend blend, bool replace)
    -> Result<empty, Error> {
    vk::GraphicsPipeline defaults;
    vk::PipelineResourceRequest description;
    description.pipeline_layout = layout->handle;
    description.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    description.depth = defaults.depth;
    description.raster = defaults.raster;
    description.multisample = defaults.multisample;
    description.color_format = format;
    description.color_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    description.color_initial_layout = description.color_final_layout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    description.color_blend.blendEnable = replace ? VK_FALSE : VK_TRUE;
    description.color_blend.srcColorBlendFactor = description.color_blend.srcAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE;
    description.color_blend.dstColorBlendFactor = description.color_blend.dstAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (blend == ImageBlend::Additive || blend == ImageBlend::AdditiveRgb || blend == ImageBlend::Plus) {
        description.color_blend.dstColorBlendFactor = description.color_blend.dstAlphaBlendFactor =
            VK_BLEND_FACTOR_ONE;
        if (blend != ImageBlend::Plus) description.color_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    }
    description.color_blend.colorBlendOp = description.color_blend.alphaBlendOp =
        VK_BLEND_OP_ADD;
    description.color_blend.colorWriteMask = 15;
    if (blend == ImageBlend::AdditiveRgb) description.color_blend.colorWriteMask = 7;
    description.shader_stages.push(ui_shader(shaders::quad.words, vrento::ShaderType::VERTEX));
    description.shader_stages.push(ui_shader(fragment.words, vrento::ShaderType::FRAGMENT));
    const auto signature = vk::MakeRenderPassResourceDesc(description);
    auto recipe = pipelines.RegisterRecipe(rstd::move(description), layouts);
    if (recipe.is_none())
        return Err(Error{Stage::Pipeline});
    auto handle = recipe.unwrap();
    recipes.push(vrento::resource::PipelineRecipeHandle(handle));
    if (replace)
        return Ok(empty{});
    auto pipeline = preparation->prepare(handle, signature);
    if (pipeline.is_err())
        return Err(Error::resource(Stage::Pipeline, pipeline.unwrap_err()));
    auto value = pipeline.unwrap();
    if (value.error)
        return Err(Error::resource(Stage::Pipeline, value.error.take().unwrap()));
    if (value.status == vk::PipelineReadiness::Failed)
        return Err(Error{Stage::Pipeline});
    if (value.pipeline)
        warmed.push(value.pipeline.take().unwrap());
    return Ok(empty{});
}

auto UiAssets::register_backdrop() -> Result<empty, Error> {
    if (recipes.len() != ui_pipeline_count)
        return Ok(empty{});
    auto filter = register_pipeline(shaders::backdrop_filter, ImageBlend::SourceOver, true);
    if (filter.is_err())
        return Err(filter.unwrap_err());
    auto composite = register_pipeline(shaders::backdrop_composite, ImageBlend::SourceOver, true);
    if (composite.is_err())
        pipelines.ReleaseRecipe(recipes.pop().unwrap());
    return composite;
}
} // namespace

struct Renderer::Impl {
    vk::PipelinePreparationMode pipeline_mode{vk::PipelinePreparationMode::Synchronous};
    Option<vk::DeviceSession> session;
    Option<vk::TextureContext> textures;
    Option<vk::Renderer> compiler;
    struct GraphShape {
        struct Resource {
            rg::ResourceId id;
            rg::GraphResourceKind kind;
            Serial bytes{};
            Index mips{1}, layers{1};
            bool imported{};
            VkFormat format{VK_FORMAT_UNDEFINED};
            Index width{}, height{};
            friend bool operator==(const Resource &, const Resource &) = default;
        };
        Vec<Resource> resources;
        Vec<vk::PassExecutionInfo> passes;
        friend bool operator==(const GraphShape &, const GraphShape &) = default;
    };
    Option<GraphShape> graph_shape;
    vk::GraphicsScopeCache scopes;
    auto device() const -> const vk::Device & { return session->device(); }
    Option<rstd::sync::Arc<UiAssets>> assets;
    mutable GlyphCache glyph_cache;
    mutable ImageCache image_cache;
    void acknowledge_uploads() const {
        for (auto &page : glyph_cache.pages)
            if (!page.resident && page.gpu && (*page.gpu)->image.ready().valid()) {
                page.resident = true;
                ++glyph_cache.stats.uploads;
                glyph_cache.stats.uploaded_bytes += Serial(page.width) * page.height;
            }
        for (auto &entry : image_cache.entries)
            if (!entry.resident && entry.gpu && (*entry.gpu)->image.ready().valid()) {
                entry.resident = true;
                ++image_cache.stats.uploads;
                image_cache.stats.uploaded_bytes += entry.data.byte_size();
            }
    }
    rstd::sync::Arc<FrameResources> frame_resources{rstd::sync::Arc<FrameResources>::make()};
    Option<rstd::sync::Arc<OwnedFrame>> frame;
    Option<DrawStats> recorded_stats;
    struct PresentSync {
        vvk::Semaphore semaphore;
        vvk::Fence fence;
        bool pending{};
    };
    Vec<PresentSync> presents;
    PFN_vkReleaseSwapchainImagesEXT release_images{};
    bool windowed{}, initialized{}, readback_enabled{true};
    auto maintenance() const -> bool {
        return session.is_some() && device().capabilities().swapchain_maintenance1;
    }
    PresentMode presentation{PresentMode::Fifo};
    auto swapchain_options() const -> vk::SwapchainOptions {
        return {true, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          (readback_enabled ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u), presentation};
    }
    auto presentation_sync() -> Result<empty, Error> {
        Vec<PresentSync> next;
        for (usize i{}; i < device().swapchain().images().len(); ++i) {
            PresentSync sync;
            auto result = device().handle().CreateSemaphore(
                {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}, sync.semaphore);
            if (result == VK_SUCCESS && maintenance())
                result = device().handle().CreateFence({VK_STRUCTURE_TYPE_FENCE_CREATE_INFO},
                                                       sync.fence);
            if (result != VK_SUCCESS)
                return Err(Error{Stage::Allocation, result});
            next.push(rstd::move(sync));
        }
        presents = rstd::move(next);
        return Ok(empty{});
    }
    auto release_acquired(Serial timeout) -> Result<empty, Error> {
        if (frame.is_some() && (**frame).output.acquired && !(**frame).output.presented) {
            auto &active = **frame;
            auto result = session->observe_result(active.output.acquire_fence.Wait(timeout));
            if (result != VK_SUCCESS)
                return Err(Error{result == VK_TIMEOUT ? Stage::Timeout : Stage::Acquire, result});
            if (!maintenance())
                return Ok(empty{});
            VkReleaseSwapchainImagesInfoEXT release{
                VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT};
            release.swapchain = *device().swapchain().handle();
            release.imageIndexCount = 1;
            release.pImageIndices = &active.output.image_index;
            result = release_images(*device().handle(), &release);
            if (result != VK_SUCCESS)
                return Err(Error{Stage::Surface, result});
            active.output.acquired = false;
        }
        return Ok(empty{});
    }
    void mark_lost() {
        if (session.is_some())
            session->observe_result(VK_ERROR_DEVICE_LOST);
        state = State::Lost;
        if (frame.is_some()) {
            (**frame).recyclable = false;
            (**frame).fail_timestamps(VK_ERROR_DEVICE_LOST);
        }
    }
    auto is_lost() -> bool {
        if (state == State::Closed)
            return false;
        if (state == State::Lost || (session.is_some() && session->lost())) {
            mark_lost();
            return true;
        }
        return false;
    }
    template <class T> auto observe_result(Result<T, Error> result) -> Result<T, Error> {
        if (result.is_err()) {
            auto error = result.unwrap_err();
            if (error.result == VK_ERROR_DEVICE_LOST && session.is_some())
                mark_lost();
            return Err(error);
        }
        return result;
    }
    auto retire(Serial timeout) -> Result<empty, Error> {
        if (state == State::Closed)
            return Ok(empty{});
        if (is_lost()) {
            auto result = session->wait_idle();
            if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                return Err(Error{Stage::Lost, result});
            if (frame.is_some() && (**frame).output.acquired) {
                result = session->observe_result((**frame).output.acquire_fence.Wait(timeout));
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    return Err(
                        Error{result == VK_TIMEOUT ? Stage::Timeout : Stage::Acquire, result});
            }
            for (auto &sync : presents) {
                if (!sync.pending || !maintenance())
                    continue;
                result = session->observe_result(sync.fence.Wait(timeout));
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    return Err(Error{result == VK_TIMEOUT ? Stage::Timeout : Stage::Lost, result});
                sync.pending = false;
            }
            return Ok(empty{});
        }
        if (frame.is_some() && (**frame).uncertain_submission != VK_SUCCESS) {
            if (!timeout)
                return Err(Error{Stage::Submit, (**frame).uncertain_submission});
            auto result = session->wait_idle();
            if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                return Err(Error{Stage::Submit, result});
            if (result == VK_ERROR_DEVICE_LOST)
                return retire(timeout);
            (**frame).uncertain_submission = VK_SUCCESS;
        }
        if (state == State::Submitted) {
            auto result = session->observe_result((**frame).output.fence->value.Wait(timeout));
            if (result != VK_SUCCESS) {
                if (result != VK_TIMEOUT)
                    (**frame).fail_timestamps(result);
                return Err(Error{result == VK_TIMEOUT ? Stage::Timeout : Stage::Lost, result});
            }
            auto retired = textures->Poll();
            if (retired.is_err())
                return Err(Error{Stage::Submit, VkResult(retired.unwrap_err().native)});
            (**frame).collect_timestamps();
            if (session->observe_result((**frame).timings.query_result) == VK_ERROR_DEVICE_LOST)
                return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
            (**frame).recyclable = true;
            state = State::Complete;
        }
        if (frame.is_some() && (**frame).output.acquired && (**frame).output.presented) {
            auto result = session->observe_result((**frame).output.acquire_fence.Wait(timeout));
            if (result != VK_SUCCESS)
                return Err(Error{Stage::Acquire, result});
        }
        auto released = release_acquired(timeout);
        if (released.is_err())
            return released;
        for (auto &sync : presents) {
            if (!sync.pending || !maintenance())
                continue;
            auto result = session->observe_result(sync.fence.Wait(timeout));
            if (result != VK_SUCCESS)
                return Err(Error{result == VK_TIMEOUT ? Stage::Timeout : Stage::Lost, result});
            sync.pending = false;
        }
        return Ok(empty{});
    }
    enum class State {
        Empty,
        Ready,
        Prepared,
        Submitted,
        Complete,
        Unavailable,
        Closed,
        Lost
    } state{State::Empty};
    Serial submission{};
    bool reported{};
    ~Impl() {
        if (session.is_some()) {
            auto retired = retire(~Serial{});
            if (retired.is_err()) {
                auto result = session->wait_idle();
                if ((result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) ||
                    retire(~Serial{}).is_err())
                    rstd::panic("cannot retire renderer device resources");
            }
        }
        if (windowed && session.is_some() && state != State::Closed) {
            auto result = session->wait_idle();
            if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                rstd::panic("cannot retire renderer device resources");
        }
        clear_resources();
        if (windowed && session.is_some())
            session->release_surface();
    }
    void clear_resources() {
        textures = None();
        frame = None();
        recorded_stats = None();
        scopes = vk::GraphicsScopeCache{};
        frame_resources->clear();
        presents.clear();
        glyph_cache.clear();
        image_cache.clear();
        compiler = None();
        graph_shape = None();
        assets = None();
    }
};

Renderer::Renderer() : impl_(Box<Impl>::make()) {}
Renderer::~Renderer() = default;
auto Renderer::set_pipeline_preparation_mode(vk::PipelinePreparationMode mode)
    -> Result<empty, Error> {
    if (impl_->initialized)
        return Err(Error{Stage::State});
    impl_->pipeline_mode = mode;
    return Ok(empty{});
}
auto Renderer::pipeline_cache_stats() const -> vrento::resource_registry::PipelineCacheStats {
    return impl_->assets ? (*impl_->assets)->pipelines.stats()
                         : vrento::resource_registry::PipelineCacheStats{};
}
auto Renderer::initialize(bool validation) -> Result<empty, Error> {
    if (impl_->state == Impl::State::Closed)
        impl_ = Box<Impl>::make();
    return impl_->observe_result(initialize_impl(nullptr, validation, true));
}
auto Renderer::initialize(SurfaceHost &host, bool validation, bool readback)
    -> Result<empty, Error> {
    if (impl_->state == Impl::State::Closed)
        impl_ = Box<Impl>::make();
    return impl_->observe_result(initialize_impl(&host, validation, readback));
}
namespace {
struct SurfaceAdapter {
    SurfaceHost &host;
    Option<Error> error;
    explicit SurfaceAdapter(SurfaceHost &host) : host(host) {}
    auto create(const vvk::Instance &instance) -> Result<vk::SessionSurface, vk::DeviceSessionError> {
        auto result = host.create_surface(instance);
        if (result.is_err()) {
            error = Some(result.unwrap_err());
            return Err(vk::DeviceSessionError{vk::DeviceSessionStage::Surface, error->result});
        }
        return Ok(result.unwrap());
    }
};
} // namespace
auto Renderer::initialize_impl(SurfaceHost *host, bool validation, bool readback)
    -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Empty)
        return Err(Error{Stage::State});
    self.state = Impl::State::Unavailable;
    self.windowed = host != nullptr;
    self.readback_enabled = readback;
    Vec<vk::Extension> instance_extensions;
    if (host) {
        auto extensions = host->instance_extensions();
        if (extensions.is_err())
            return Err(extensions.unwrap_err());
        instance_extensions = extensions.unwrap();
        instance_extensions.push({false, "VK_KHR_get_surface_capabilities2"_str});
        instance_extensions.push({false, "VK_EXT_surface_maintenance1"_str});
    }
    vk::Extension required[]{{true, "VK_KHR_swapchain"_str},
                             {false, "VK_EXT_swapchain_maintenance1"_str}};
    vk::DeviceSessionOptions options;
    options.instance_extensions = instance_extensions.as_slice();
    options.device_extensions = slice<vk::Extension>::from_raw_parts(required, usize(host ? 2 : 0));
    options.extent = host ? host->pixel_extent() : VkExtent2D{1, 1};
    options.swapchain = host ? self.swapchain_options() : vk::SwapchainOptions{};
    options.validation = validation;
    Option<SurfaceAdapter> adapter;
    if (host)
        adapter = Some(SurfaceAdapter(*host));
    Option<mut_ref<dyn<vk::SessionSurfaceFactory>>> surface;
    if (adapter.is_some())
        surface = Some(dyn<vk::SessionSurfaceFactory>::from_ref(*adapter).as_mut_ref());
    auto created = vk::DeviceSession::create(options, surface);
    if (created.is_err()) {
        if (adapter.is_some() && adapter->error.is_some())
            return Err(*adapter->error);
        return Err(Error::session(created.unwrap_err()));
    }
    self.session = Some(created.unwrap());
    return finish_initialize();
}
auto Renderer::initialize(vk::DeviceSession session, bool readback) -> Result<empty, Error> {
    if (impl_->state == Impl::State::Closed)
        impl_ = Box<Impl>::make();
    return impl_->observe_result(initialize_session_impl(rstd::move(session), readback));
}
auto Renderer::initialize_session_impl(vk::DeviceSession session, bool readback)
    -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Empty || !session.valid())
        return Err(Error{Stage::State});
    if (session.lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    self.state = Impl::State::Unavailable;
    self.readback_enabled = readback;
    self.session = Some(rstd::move(session));
    return finish_initialize();
}
auto Renderer::device_epoch() const -> Serial {
    return impl_->session.is_some() ? impl_->session->epoch() : 0;
}
auto Renderer::device_lost() const -> bool {
    return impl_->state == Impl::State::Lost ||
           (impl_->session.is_some() && impl_->session->lost());
}
auto Renderer::device_session() const -> Result<vk::DeviceSession, Error> {
    if (device_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    if (!impl_->initialized || impl_->state == Impl::State::Closed ||
        impl_->state == Impl::State::Lost)
        return Err(Error{Stage::State});
    return Ok(vk::DeviceSession(*impl_->session));
}
auto Renderer::finish_initialize() -> Result<empty, Error> {
    auto &self = *impl_;
    auto textures = vk::TextureContext::create(*self.session);
    if (textures.is_err())
        return Err(Error::texture(Stage::Allocation, textures.unwrap_err()));
    self.textures = Some(rstd::move(textures).unwrap());
    auto features = self.device().gpu().GetFormatProperties(format).optimalTilingFeatures;
    if ((features &
         (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) !=
        (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        return Err(Error{Stage::Unsupported});
    if (self.windowed) {
        if (self.maintenance())
            self.release_images = reinterpret_cast<PFN_vkReleaseSwapchainImagesEXT>(
                self.device().handle().Dispatch().vkGetDeviceProcAddr(*self.device().handle(),
                                                                      "vkReleaseSwapchainImagesEXT"));
        if (self.maintenance() && !self.release_images)
            return Err(Error::session(vk::DeviceSessionError::from(
                vk::DeviceSessionStage::Device,
                {"load swapchain maintenance"_Str,
                 vk::BackendCause::Unsupported("vkReleaseSwapchainImagesEXT"_Str)})));
        auto destination = self.device()
                               .gpu()
                               .GetFormatProperties(self.device().swapchain().format())
                               .optimalTilingFeatures;
        if (!(features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
            !(destination & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            return Err(Error{Stage::Unsupported});
        auto sync = self.presentation_sync();
        if (sync.is_err())
            return sync;
    }
    auto assets = rstd::sync::Arc<UiAssets>::make(*self.session);
    auto result = assets->initialize(self.pipeline_mode);
    if (result.is_err())
        return result;
    self.assets = Some(rstd::move(assets));
    self.initialized = true;
    self.state = Impl::State::Ready;
    return Ok(empty{});
}

auto Renderer::prepare(FramePlan plan, DrawOptions options) -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(prepare_impl(rstd::move(plan), options));
}
auto Renderer::record(tex::Submission &work, FramePlan plan, tex::TextureViewRef target,
                      rg::AttachmentLoad load, DrawOptions options) -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    if (self.windowed)
        return Err(Error{Stage::Unsupported});
    auto prepared = prepare_impl(rstd::move(plan), options, Some(rstd::move(target)), load, &work);
    if (prepared.is_err())
        return self.observe_result(rstd::move(prepared));
    auto frame = self.frame->clone();
    frame->gpu->receipt = Some(work.receipt());
    work.retain(frame.clone());
    auto recorded = frame->execution.program->record(work);
    self.frame = None();
    self.state = Impl::State::Ready;
    if (recorded.is_err()) {
        auto error = recorded.unwrap_err();
        auto result = Error::graph(rstd::move(error));
        result.stage = Stage::Record;
        Result<empty, Error> failed = Err(result);
        return self.observe_result(rstd::move(failed));
    }
    self.recorded_stats = Some(DrawStats(*frame->stats));
    return Ok(empty{});
}
auto Renderer::prepare_impl(FramePlan plan, DrawOptions options,
                            Option<tex::TextureViewRef> target_override, rg::AttachmentLoad load,
                            tex::Submission *external_work) -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Ready && self.state != Impl::State::Complete)
        return Err(Error{Stage::State});
    VkFormat formats[]{format, target_override
                                   ? vk::ToVkType(target_override->texture().description().format)
                                   : format};
    bool pending{};
    for (auto target_format : formats) {
        vk::RenderPassResourceDesc signature;
        signature.color_format = target_format;
        for (const auto &recipe : (**self.assets).recipes) {
            auto result = (**self.assets).preparation->prepare(recipe, signature);
            if (result.is_err())
                return Err(Error::resource(Stage::Pipeline, result.unwrap_err()));
            auto value = result.unwrap();
            if (value.error)
                return Err(Error::resource(Stage::Pipeline, value.error.take().unwrap()));
            pending |= value.status == vk::PipelineReadiness::Pending;
            if (value.pipeline) {
                bool retained{};
                for (const auto &cached : (**self.assets).warmed)
                    retained |= cached.handle == value.pipeline->handle;
                if (!retained)
                    (**self.assets).warmed.push(value.pipeline.take().unwrap());
            }
        }
    }
    if (pending)
        return Err(Error{Stage::Pending, VK_NOT_READY});
    self.acknowledge_uploads();
    self.scopes.collect();
    if (self.assets)
        (*self.assets)->pipelines.PruneExpired();
    if (self.frame.is_some() && (**self.frame).output.acquired && !(**self.frame).output.presented)
        return Err(Error{Stage::State});
    if (self.frame.is_some() && (**self.frame).output.acquired) {
        auto result = (**self.frame).output.acquire_fence.Wait(0);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Timeout, result});
    }
    Option<rstd::time::Instant> prepare_start;
    if (options.timings)
        prepare_start = Some(rstd::time::Instant::now());
    auto surface = plan.surface();
    auto width = surface.pixel_size.is_some() ? Scalar(surface.pixel_size->width)
                                              : std::ceil(surface.logical_size.width * surface.scale);
    auto height = surface.pixel_size.is_some() ? Scalar(surface.pixel_size->height)
                                               : std::ceil(surface.logical_size.height * surface.scale);
    if (width < 1 || height < 1 || width > self.device().limits().maxImageDimension2D ||
        height > self.device().limits().maxImageDimension2D || plan.pass_count() > usize(1024) ||
        plan.clips().len() > usize(65535))
        return Err(Error{Stage::Unsupported});
    auto candidate = rstd::sync::Arc<OwnedFrame>::make(*self.session, self.frame_resources.clone(),
                                                       rstd::move(plan));
    auto &frame = *candidate;
    frame.options = options;
    frame.stats->frame = frame.plan.identity();
    frame.timings.frame = frame.plan.identity();
    frame.extent = {static_cast<unsigned>(width), static_cast<unsigned>(height)};
    if (self.windowed) {
        auto extent = self.device().swapchain().extent();
        if (extent.width != frame.extent.width || extent.height != frame.extent.height)
            return Err(Error{Stage::Surface, VK_ERROR_OUT_OF_DATE_KHR});
    }
    const auto &device = self.device().handle();
    const auto &d = device.Dispatch();
    auto fail = [&](Stage stage, VkResult result) -> Result<empty, Error> {
        return Err(Error{stage, result});
    };
    Vec<GpuClip> clip_data;
    for (const auto &source : frame.plan.clips()) {
        GpuClip clip{};
        rectangle_values(clip.rect, source.shape.rect);
        auto inverse = source.transform.inverse();
        if (inverse.is_some()) {
            clip.row0[0] = static_cast<float>(inverse->a);
            clip.row0[1] = static_cast<float>(inverse->c);
            clip.row0[2] = static_cast<float>(inverse->tx);
            clip.row1[0] = static_cast<float>(inverse->b);
            clip.row1[1] = static_cast<float>(inverse->d);
            clip.row1[2] = static_cast<float>(inverse->ty);
        }
        clip.params[0] = static_cast<float>(source.shape.radius);
        clip.params[1] = source.parent == no_clip ? -1 : static_cast<float>(source.parent);
        clip.params[2] = inverse.is_some() ? 1 : 0;
        float values[sizeof(GpuClip) / sizeof(float)];
        std::memcpy(values, &clip, sizeof(GpuClip));
        for (unsigned i = 0; i < sizeof(GpuClip) / sizeof(float); ++i)
            if (!std::isfinite(values[i]))
                return Err(Error{Stage::Unsupported});
        clip_data.push(rstd::move(clip));
    }
    CpuSample resolve(frame.metric(&FrameTimings::resolve));
    auto reserved =
        self.image_cache.reserve_frame(frame.plan, self.device().limits().maxImageDimension2D);
    if (reserved.is_err())
        return reserved;
    auto resolved = resolve_items(frame, self.glyph_cache, self.image_cache,
                                  self.device().limits().maxImageDimension2D, *self.session);
    resolve.pause();
    if (resolved.is_err())
        return resolved;
    CpuSample visibility(frame.metric(&FrameTimings::visibility));
    auto culled = cull_items(frame, clip_data.as_slice());
    visibility.pause();
    if (culled.is_err())
        return culled;
    auto atlas_ready = prepare_atlas(frame, self.glyph_cache, *self.textures);
    if (atlas_ready.is_err())
        return atlas_ready;
    auto images_ready = prepare_images(frame, self.image_cache, *self.textures);
    if (images_ready.is_err())
        return images_ready;
    if (target_override) {
        const auto &view = *target_override;
        if (!view.valid())
            return Err(Error{Stage::State});
        const auto &desc = view.texture().description();
        auto extent = tex::mip_extent(desc, view.description().range.first_mip);
        if (extent.width != frame.extent.width || extent.height != frame.extent.height ||
            (desc.format != vrento::TextureFormat::RGBA8 &&
             desc.format != vrento::TextureFormat::RGBA8_SRGB) ||
            desc.samples != 1)
            return Err(Error{Stage::Unsupported});
    }
    for (usize i{}; i < frame.plan.pass_count(); ++i) {
        if (i == usize() && target_override) {
            frame.gpu->targets.push(
                Target{target_override->texture(), *target_override, frame.extent, false});
            frame.textures.push(rg::ResourceId{});
            continue;
        }
        auto target = self.frame_resources->target(*self.textures, frame.extent);
        if (target.is_err())
            return Err(target.unwrap_err());
        frame.gpu->targets.push(target.unwrap());
        frame.textures.push(rg::ResourceId{});
    }
    auto segmented = segment_draws(frame, *self.textures, clip_data.as_slice(),
                                   target_override ? load : rg::AttachmentLoad::Clear);
    if (segmented.is_err())
        return segmented;
    if (frame.stats->backdrop_captures) {
        auto registered = (*self.assets)->register_backdrop();
        if (registered.is_err())
            return registered;
    }
    if (clip_data.is_empty())
        clip_data.push(GpuClip{});
    auto buffer_bytes = clip_data.len().to_primitive() * sizeof(GpuClip);
    auto allocation =
        self.frame_resources->buffer(self.device(), buffer_bytes, FrameBufferKind::Storage);
    if (allocation.is_err())
        return Err(allocation.unwrap_err());
    frame.gpu->clips = allocation.unwrap();
    {
        auto memory = frame.gpu->clips.allocation();
        auto mapped = memory.map();
        if (mapped.is_err())
            return fail(Stage::Allocation, mapped.unwrap_err().api_result);
        auto mapping = mapped.unwrap();
        std::memcpy(mapping.data(), clip_data.as_slice().as_raw_ptr(), buffer_bytes);
        auto flushed = memory.flush();
        if (flushed.is_err())
            return fail(Stage::Allocation, flushed.unwrap_err().api_result);
    }
    Vec<DrawData> instance_data;
    for (const auto &items : frame.items) {
        frame.item_offsets.push(static_cast<Index>(instance_data.len().to_primitive()));
        for (const auto &paint : items) {
            auto data = paint.data;
            data.params[2] = paint.clip == no_clip ? -1 : static_cast<float>(paint.clip);
            instance_data.push(rstd::move(data));
        }
    }
    frame.stats->items = instance_data.len().to_primitive();
    if (instance_data.is_empty())
        instance_data.push(DrawData{});
    buffer_bytes = instance_data.len().to_primitive() * sizeof(DrawData);
    if (buffer_bytes > self.device().limits().maxStorageBufferRange)
        return Err(Error{Stage::DrawLimit});
    frame.stats->instance_bytes = buffer_bytes;
    allocation =
        self.frame_resources->buffer(self.device(), buffer_bytes, FrameBufferKind::Storage);
    if (allocation.is_err())
        return Err(allocation.unwrap_err());
    frame.gpu->instances = allocation.unwrap();
    {
        auto memory = frame.gpu->instances.allocation();
        auto mapped = memory.map();
        if (mapped.is_err())
            return fail(Stage::Allocation, mapped.unwrap_err().api_result);
        auto mapping = mapped.unwrap();
        std::memcpy(mapping.data(), instance_data.as_slice().as_raw_ptr(), buffer_bytes);
        auto flushed = memory.flush();
        if (flushed.is_err())
            return fail(Stage::Allocation, flushed.unwrap_err().api_result);
    }
    if (self.readback_enabled) {
        buffer_bytes = VkDeviceSize(frame.extent.width) * frame.extent.height * 4;
        allocation =
            self.frame_resources->buffer(self.device(), buffer_bytes, FrameBufferKind::Readback);
        if (allocation.is_err())
            return Err(allocation.unwrap_err());
        frame.gpu->readback = allocation.unwrap();
    }
    unsigned count{};
    for (const auto &items : frame.items)
        count += static_cast<unsigned>(items.len().to_primitive());
    if (!count)
        count = 1;
    VkDescriptorPoolSize pool_sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count * 2},
                                      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * 2}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = count;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    auto result = d.vkCreateDescriptorPool(*device, &pool_info, nullptr, &frame.gpu->pool);
    if (result != VK_SUCCESS)
        return fail(Stage::Allocation, result);
    result = device.CreateFence({VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}, frame.output.fence->value);
    if (result != VK_SUCCESS)
        return fail(Stage::Allocation, result);
    if (self.windowed) {
        result = device.CreateSemaphore({VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO},
                                        frame.output.acquire_semaphore);
        if (result == VK_SUCCESS)
            result = device.CreateFence({VK_STRUCTURE_TYPE_FENCE_CREATE_INFO},
                                        frame.output.acquire_fence);
        if (result != VK_SUCCESS)
            return fail(Stage::Allocation, result);
    }
    Impl::GraphShape graph;
    constexpr Serial graph_scope = 1;
    vk::GraphBindings bindings(graph_scope);
    auto texture = [&](tex::TextureViewRef view, bool imported) -> rg::ResourceId {
        rg::ResourceId id{graph.resources.len().to_primitive() + 1, 1};
        const auto &range = view.description().range;
        const auto &desc = view.texture().description();
        auto extent = tex::mip_extent(desc, range.first_mip);
        graph.resources.push({id, rg::GraphResourceKind::Texture, 0, range.mip_count,
                              range.layer_count, imported, vk::ToVkType(desc.format), extent.width,
                              extent.height});
        bindings
            .bind(id, rstd::sync::Arc<dyn<vk::GraphBinding>>::make(
                          vk::GraphTextureBinding(rstd::move(view))))
            .unwrap();
        return id;
    };
    auto host_buffer = [&](const FrameBuffer &buffer) -> rg::ResourceId {
        rg::ResourceId id{graph.resources.len().to_primitive() + 1, 1};
        graph.resources.push({id, rg::GraphResourceKind::Buffer, buffer.capacity, 1, 1, true});
        bindings
            .bind(id, rstd::sync::Arc<dyn<vk::GraphBinding>>::make(vk::GraphHostBufferBinding{
                          *self.session, buffer.buffer.clone(), buffer.capacity}))
            .unwrap();
        return id;
    };
    auto buffer_read = [&](rg::ResourceId id, Serial bytes, rg::PassUsage usage,
                           rg::PassStages stages) {
        rg::PassResourceUse use{id, 0, rg::GraphAccess::Read, usage, stages};
        use.range.size = bytes;
        return use;
    };
    for (usize i{}; i < frame.gpu->targets.len(); ++i)
        frame.textures[i] =
            texture(frame.gpu->targets[i].view,
                    i == usize() && target_override && load == rg::AttachmentLoad::Load);
    auto clips = host_buffer(frame.gpu->clips);
    auto instances = host_buffer(frame.gpu->instances);
    for (auto &page : frame.samples)
        page.texture = texture(page.gpu->view, true);
    for (usize index{}; index < frame.segments.len(); ++index) {
        const auto &segment = frame.segments[index];
        auto target = frame.textures[usize(segment.target)];
        rg::PassExecutionInfo pass;
        pass.id = {index.to_primitive() + 2};
        pass.kind = segment.effects ? ui_effect_kind : ui_kind;
        if (segment.load == rg::AttachmentLoad::Load)
            pass.uses.push({target, segment.version - 1, rg::GraphAccess::Read,
                            rg::PassUsage::ColorAttachment, rg::PassStages::Color});
        pass.uses.push({target, segment.version, rg::GraphAccess::Write,
                        rg::PassUsage::ColorAttachment, rg::PassStages::Color});
        pass.uses.push(buffer_read(clips, frame.gpu->clips.capacity, rg::PassUsage::Storage,
                                   rg::PassStages::Fragment));
        pass.uses.push(buffer_read(instances, frame.gpu->instances.capacity, rg::PassUsage::Storage,
                                   rg::PassStages::Vertex | rg::PassStages::Fragment));
        auto sample = [&](rg::ResourceId id, rg::GraphIndex version) {
            for (const auto &use : pass.uses)
                if (use.resource == id && use.version == version && use.usage == rg::PassUsage::Sampled)
                    return;
            rg::PassResourceUse use{id,version,rg::GraphAccess::Read,rg::PassUsage::Sampled,rg::PassStages::Fragment};
            for(const auto &resource:graph.resources)if(resource.id==id){
                use.range.mip_count=resource.mips;use.range.layer_count=resource.layers;
            }
            pass.uses.push(rstd::move(use));
        };
        for (const auto &page : frame.samples)
            for (const auto &item : frame.items[usize(index)])
                if (item.kind == page.kind && item.resource_index == page.cache_index) {
                    sample(page.texture, 0);
                    break;
                }
        for (usize external_index{}; external_index < frame.externals.len(); ++external_index)
            for (const auto &item : frame.items[usize(index)]) {
                if (item.kind != DrawKind::External || usize(item.resource_index) != external_index)
                    continue;
                auto &external = frame.externals[external_index];
                if (!external.texture.valid()) {
                    external.texture = texture(external.view, true);
                    ++frame.stats->used_external_frames;
                }
                sample(external.texture, 0);
                break;
            }
        for (const auto &item : frame.items[index]) {
            if (item.kind == DrawKind::Composite || item.kind == DrawKind::BackdropFilter ||
                item.kind == DrawKind::BackdropComposite)
                sample(frame.textures[usize(item.source_target)], item.source_version);
            if (item.kind == DrawKind::BackdropComposite)
                sample(frame.textures[usize(item.original_target)], 1);
        }
        rg::GraphicsDescription graphics;
        graphics.color = Some(rg::GraphAttachment{target, segment.version, segment.load});
        pass.graphics = Some(graphics);
        graph.passes.push(rstd::move(pass));
    }
    if (!self.compiler) {
        self.compiler = Some(vk::Renderer(*self.session));
        self.compiler
            ->bind(rstd::sync::Arc<dyn<vk::PassFactory>>::make(
                vk::TypedPassFactory<UiDrawPassDesc, UiPassFactory>{
                    UiPassFactory{self.assets->clone()}}))
            .unwrap();
    }
    if (!self.graph_shape || *self.graph_shape != graph) {
        vrento::fg::FrameGraphBuilder builder(graph_scope);
        for (const auto &resource : graph.resources) {
            vrento::fg::ResourceDescription desc{.id = resource.id,
                                                 .kind = resource.kind,
                                                 .bytes = resource.bytes,
                                                 .mip_levels = resource.mips,
                                                 .layers = resource.layers};
            auto declared = resource.imported ? builder.import_resource(rstd::move(desc))
                                              : builder.create_resource(rstd::move(desc));
            if (declared.is_err())
                return Err(Error::graph(declared.unwrap_err()));
        }
        for (const auto &pass : graph.passes) {
            auto added = builder.add_pass(pass.id);
            if (added.is_err())
                return Err(Error::graph(added.unwrap_err()));
            for (const auto &use : pass.uses)
                if (use.access == rg::GraphAccess::Write) {
                    bool inherits = pass.graphics && pass.graphics->color &&
                                    pass.graphics->color->load == rg::AttachmentLoad::Load;
                    auto output =
                        builder.reserve_write(pass.id, {use.resource, use.version - 1}, inherits, inherits);
                    if (output.is_err())
                        return Err(Error::graph(output.unwrap_err()));
                }
        }
        Vec<vk::PassInput> descriptions;
        for (const auto &pass : graph.passes) {
            UiDrawPassDesc desc;
            auto target_id = pass.graphics->color->resource;
            for (const auto &resource : graph.resources)
                if (resource.id == target_id)
                    desc.format = resource.format;
            desc.io.kind = pass.kind;
            desc.io.graphics = pass.graphics;
            for (const auto &use : pass.uses) {
                auto content = vrento::pass::ContentRef::bind(builder, {use.resource, use.version});
                if (content.is_err())
                    return Err(Error::graph(content.unwrap_err()));
                vk::VulkanResourceUse value{content.unwrap(), use.usage, use.stages, use.range};
                if (use.access == rg::GraphAccess::Read)
                    desc.io.inputs.push(rstd::move(value));
                else
                    desc.io.outputs.push(rstd::move(value));
            }
            if (pass.graphics->color->load == rg::AttachmentLoad::Load) {
                auto version = pass.graphics->color->version;
                auto previous = vrento::pass::ContentRef::bind(builder, {target_id, version - 1}).unwrap();
                auto output = vrento::pass::ContentRef::bind(builder, {target_id, version}).unwrap();
                desc.io.storage.push({rstd::move(output), rstd::move(previous)});
            }
            auto published = vk::declare_input(builder, pass.id, rstd::move(desc));
            if (published.is_err())
                return Err(Error::graph(published.unwrap_err()));
            descriptions.push(published.unwrap());
        }
        auto built = rstd::move(builder).finish();
        if (built.is_err())
            return Err(Error::graph(built.unwrap_err()));
        auto compiled = self.compiler->compile(built.unwrap(), rstd::move(descriptions));
        if (compiled.is_err())
            return Err(Error::graph(compiled.unwrap_err()));
        self.graph_shape = Some(rstd::move(graph));
    }
    if (self.compiler->pending()) {
        auto advanced = self.compiler->continue_compile(self.compiler->status().id);
        if (advanced.is_err())
            return Err(Error::graph(advanced.unwrap_err()));
        if (*advanced == vk::CompileProgress::Pending)
            return Err(Error{Stage::Pending, VK_NOT_READY});
    }
    auto compiled = self.compiler->compiled().unwrap();
    Vec<vk::PassFrameBinding> inputs;
    for (usize index{}; index < frame.segments.len(); ++index) {
        UiFrameData data{
            frame.gpu.clone(),  {}, {}, frame.item_offsets[index], frame.options,
            frame.stats.clone()};
        data.items = rstd::move(frame.items[index]);
        for (const auto &paint : data.items) {
            DrawSources sources;
            if (paint.kind == DrawKind::Composite || paint.kind == DrawKind::BackdropFilter ||
                paint.kind == DrawKind::BackdropComposite)
                sources.primary = {frame.textures[usize(paint.source_target)], paint.source_version};
            else if (paint.kind == DrawKind::External)
                sources.primary.resource = frame.externals[usize(paint.resource_index)].texture;
            else if (paint.kind == DrawKind::Glyph || paint.kind == DrawKind::Image)
                for (const auto &sample : frame.samples)
                    if (sample.kind == paint.kind && sample.cache_index == paint.resource_index)
                        sources.primary.resource = sample.texture;
            if (paint.kind == DrawKind::BackdropComposite)
                sources.original = {frame.textures[usize(paint.original_target)], 1};
            data.sources.push(rstd::move(sources));
        }
        auto input = compiled.frame<UiDrawPass>({index.to_primitive() + 2}, 1, rstd::move(data));
        if (input.is_err())
            return Err(Error::graph(input.unwrap_err()));
        inputs.push(input.unwrap());
    }
    if (options.timings && !external_work) {
        const auto families = self.device().gpu().GetQueueFamilyProperties();
        frame.timings.timestamp_valid_bits =
            families[usize(self.device().graphics_queue().family_index)].timestampValidBits;
        frame.timings.timestamp_period_nanoseconds = self.device().limits().timestampPeriod;
        frame.reset_queries = reinterpret_cast<PFN_vkCmdResetQueryPool>(
            d.vkGetDeviceProcAddr(*device, "vkCmdResetQueryPool"));
        frame.write_timestamp = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
            d.vkGetDeviceProcAddr(*device, "vkCmdWriteTimestamp"));
        if (frame.timings.timestamp_valid_bits && frame.reset_queries && frame.write_timestamp) {
            VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            query.queryType = VK_QUERY_TYPE_TIMESTAMP;
            query.queryCount = 2;
            frame.timings.query_result =
                d.vkCreateQueryPool(*device, &query, nullptr, &frame.queries);
            if (self.session->observe_result(frame.timings.query_result) == VK_ERROR_DEVICE_LOST)
                return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
            frame.timings.gpu_status = frame.timings.query_result == VK_SUCCESS
                                           ? GpuTimingStatus::Prepared
                                           : GpuTimingStatus::Failed;
        }
    }
    if (!external_work) {
        auto begun = self.textures->Begin();
        if (begun.is_err())
            return Err(Error::texture(Stage::Record, begun.unwrap_err()));
        frame.execution.owned_submission = Some(begun.unwrap());
    }
    auto &work = external_work ? *external_work : *frame.execution.owned_submission;
    frame.gpu->receipt = Some(work.receipt());
    if (frame.queries) {
        auto native = vk::native_recording(work, *self.session);
        if (native.is_err())
            return Err(Error::texture(Stage::Record, native.unwrap_err()));
        frame.reset_queries(native.unwrap(), frame.queries, 0, 2);
        frame.write_timestamp(native.unwrap(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queries, 0);
    }
    for (const auto &sample : frame.samples) for(const auto &upload:sample.uploads){
        auto uploaded = work.commands()->Upload(upload.clone());
        if (uploaded.is_err())
            return Err(Error::texture(Stage::Record, uploaded.unwrap_err()));
    }
    auto program = compiled.instantiate(
        bindings, inputs.as_slice(),
        Box<dyn<vk::GraphicsScopeBackend>>::make(ScopeBackend{self.scopes}), true, &work);
    if (program.is_err())
        return Err(Error::graph(program.unwrap_err()));
    if (!program->ready())
        return fail(Stage::Pending, VK_NOT_READY);
    frame.execution.program = Some(program->take_ready());
    self.frame = Some(rstd::move(candidate));
    self.recorded_stats = None();
    if (prepare_start.is_some())
        frame.timings.prepare = {1, elapsed_nanoseconds(*prepare_start)};
    self.state = Impl::State::Prepared;
    self.reported = false;
    return Ok(empty{});
}

auto Renderer::submit() -> Result<Serial, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(submit_impl());
}
auto Renderer::submit_impl() -> Result<Serial, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Prepared || self.submission == ~Serial{})
        return Err(Error{Stage::State});
    auto &frame = **self.frame;
    if (frame.uncertain_submission != VK_SUCCESS)
        return Err(Error{Stage::Submit, frame.uncertain_submission});
    const auto &d = self.device().handle().Dispatch();
    VkResult result = VK_SUCCESS;
    if (self.windowed) {
        if (!frame.output.acquired) {
            CpuSample acquire(frame.metric(&FrameTimings::acquire));
            result = d.vkAcquireNextImageKHR(
                *self.device().handle(), *self.device().swapchain().handle(), ~Serial{},
                *frame.output.acquire_semaphore, *frame.output.acquire_fence,
                &frame.output.image_index);
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                if (frame.options.timings && (result == VK_NOT_READY || result == VK_TIMEOUT))
                    ++frame.timings.acquire_unavailable;
                return Err(Error{Stage::Acquire, result});
            }
            frame.output.acquired = true;
            frame.output.suboptimal = result == VK_SUBOPTIMAL_KHR;
        }
        auto &sync = self.presents[usize(frame.output.image_index)];
        if (sync.pending && self.maintenance()) {
            CpuSample reuse(frame.metric(&FrameTimings::present_reuse));
            result = sync.fence.Wait(0);
            if (result != VK_SUCCESS)
                return Err(Error{Stage::Timeout, result});
            sync.pending = false;
        }
    }
    CpuSample record(frame.metric(&FrameTimings::record));
    if (!frame.execution.owned_submission)
        return Err(Error{Stage::State});
    auto work = frame.execution.owned_submission.take().unwrap();
    work.retain(self.frame->clone());
    auto native = vk::native_recording(work, *self.session);
    if (native.is_err())
        return Err(Error{Stage::Record, VkResult(native.unwrap_err().native)});
    auto command = native.unwrap();
    auto recorded = frame.execution.program->record(work);
    if (recorded.is_err())
        return Err(Error::graph(recorded.unwrap_err()));
    auto source = vk::native_texture(frame.gpu->targets[usize()].image);
    if (source.is_err())
        return Err(Error{Stage::Record, VkResult(source.unwrap_err().native)});
    auto transferred = work.commands()->UseTexture(
        {frame.gpu->targets[usize()].view, tex::Access::TransferSource, tex::Stages::Transfer});
    if (transferred.is_err())
        return Err(Error{Stage::Record, VkResult(transferred.unwrap_err().native)});
    auto capture_image = [&](VkImage source) {
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {frame.extent.width, frame.extent.height, 1};
        d.vkCmdCopyImageToBuffer(command, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 frame.gpu->readback.handle(), 1, &copy);
        VkBufferMemoryBarrier memory{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        memory.srcQueueFamilyIndex = memory.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memory.buffer = frame.gpu->readback.handle();
        memory.size = VK_WHOLE_SIZE;
        d.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                               0, 0, nullptr, 1, &memory, 0, nullptr);
    };
    if (self.readback_enabled && !self.windowed)
        capture_image(source.unwrap());
    if (self.windowed) {
        vrento::FrameSurfaceLease surface;
        surface.identity = {u64(self.session->epoch()), u32(frame.output.image_index),
                            u64(self.submission + 1)};
        surface.reuse = {vrento::FrameSurfaceReuseKind::PresentationAcquired, u64(0)};
        surface.image = self.device().swapchain().images()[usize(frame.output.image_index)];
        surface.format = self.device().swapchain().format();
        surface.acquire = {vrento::FrameSurfaceAcquireKind::BinarySemaphore,
                           *frame.output.acquire_semaphore};
        surface.initial_queue_family = surface.final_queue_family =
            self.device().graphics_queue().family_index;
        surface.final_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        auto acquired = vk::recordAcquire(
            work, *self.session, surface,
            rstd::sync::Arc<dyn<rstd::any::Any>>::make(self.frame->clone()),
            {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
             VK_PIPELINE_STAGE_TRANSFER_BIT, self.device().graphics_queue().family_index});
        if (acquired.is_err())
            return Err(Error{Stage::Record, VkResult(acquired.unwrap_err().native)});
        VkImageBlit blit{};
        blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = blit.dstOffsets[1] = {static_cast<int>(frame.extent.width),
                                                   static_cast<int>(frame.extent.height), 1};
        d.vkCmdBlitImage(command, source.unwrap(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         surface.image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                         VK_FILTER_NEAREST);
        if (self.readback_enabled) {
            VkImageMemoryBarrier destination{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            destination.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            destination.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            destination.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            destination.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            destination.srcQueueFamilyIndex = destination.dstQueueFamilyIndex =
                VK_QUEUE_FAMILY_IGNORED;
            destination.image = surface.image.handle;
            destination.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            d.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &destination);
            capture_image(surface.image.handle);
            auto final = vk::recordSurfaceFinalState(
                work, *self.session, surface,
                {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, self.device().graphics_queue().family_index});
            if (final.is_err())
                return Err(Error{Stage::Record, VkResult(final.unwrap_err().native)});
        }
        auto released = vk::recordRelease(work, *self.session, surface);
        if (released.is_err())
            return Err(Error{Stage::Record, VkResult(released.unwrap_err().native)});
    }
    if (frame.queries)
        frame.write_timestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.queries, 1);
    record.pause();
    auto fence = self.textures->AttachFence(
        work, {*self.device().handle(), *frame.output.fence->value,
               rstd::sync::Arc<dyn<rstd::any::Any>>::make(frame.output.fence.clone())});
    if (fence.is_err())
        return Err(Error{Stage::Submit, VkResult(fence.unwrap_err().native)});
    if (self.windowed) {
        auto signal = self.textures->AttachSignal(
            work,
            {*self.device().handle(), *self.presents[usize(frame.output.image_index)].semaphore,
             vk::NativeSemaphoreKind::Binary, 0,
             rstd::sync::Arc<dyn<rstd::any::Any>>::make(self.frame->clone())});
        if (signal.is_err())
            return Err(Error{Stage::Submit, VkResult(signal.unwrap_err().native)});
    }
    auto receipt = work.receipt();
    frame.recyclable = false;
    if (frame.queries)
        frame.submitted_at = Some(rstd::time::Instant::now());
    auto submitted = [&] {
        CpuSample submit(frame.metric(&FrameTimings::queue_submit));
        return self.textures->Submit(rstd::move(work));
    }();
    if (submitted.is_err()) {
        result = VkResult(submitted.unwrap_err().native);
        if (receipt.status() == tex::SubmissionStatus::Uncertain)
            frame.uncertain_submission = result;
        else
            frame.recyclable = true;
        if (result == VK_ERROR_DEVICE_LOST)
            frame.fail_timestamps(result);
        return Err(Error{result == VK_ERROR_DEVICE_LOST ? Stage::Lost : Stage::Submit, result});
    }
    for (const auto &page : frame.samples) {
        if (!page.uploads.is_empty() && page.kind == DrawKind::Glyph) {
            self.glyph_cache.pages[usize(page.cache_index)].resident = true;
            ++self.glyph_cache.stats.uploads;
            self.glyph_cache.stats.uploaded_bytes +=
                Serial(page.gpu->extent.width) * page.gpu->extent.height;
        } else if (!page.uploads.is_empty() && page.kind == DrawKind::Image) {
            self.image_cache.entries[usize(page.cache_index)].resident = true;
            ++self.image_cache.stats.uploads;
            self.image_cache.stats.uploaded_bytes +=
                page.gpu->bytes;
        }
    }
    ++self.submission;
    frame.timings.submission = self.submission;
    if (frame.queries)
        frame.timings.gpu_status = GpuTimingStatus::Pending;
    self.state = Impl::State::Submitted;
    return Ok(self.submission);
}
auto Renderer::poll() -> Result<Option<Completion>, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(poll_impl());
}
auto Renderer::poll_impl() -> Result<Option<Completion>, Error> {
    auto &self = *impl_;
    if (self.state == Impl::State::Submitted) {
        const auto &device = self.device().handle();
        auto &frame = **self.frame;
        VkResult result;
        {
            CpuSample poll(frame.metric(&FrameTimings::poll));
            result = device.Dispatch().vkGetFenceStatus(*device, *frame.output.fence->value);
        }
        if (result == VK_NOT_READY) {
            if (frame.options.timings)
                ++frame.timings.poll_pending;
            return Ok(None<Completion>());
        }
        if (result != VK_SUCCESS) {
            if (result == VK_ERROR_DEVICE_LOST)
                self.state = Impl::State::Lost;
            frame.fail_timestamps(result);
            return Err(Error{Stage::Lost, result});
        }
        auto retired = self.textures->Poll();
        if (retired.is_err())
            return Err(Error{Stage::Submit, VkResult(retired.unwrap_err().native)});
        (**self.frame).collect_timestamps();
        if (self.session->observe_result((**self.frame).timings.query_result) ==
            VK_ERROR_DEVICE_LOST)
            return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
        (**self.frame).release_uploads();
        (**self.frame).recyclable = true;
        self.state = Impl::State::Complete;
    }
    if (self.state == Impl::State::Lost)
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    if (self.state != Impl::State::Complete || self.reported)
        return Ok(None<Completion>());
    self.reported = true;
    return Ok(Some(Completion{self.submission, (**self.frame).plan.identity()}));
}
auto Renderer::wait(Serial timeout_nanoseconds) -> Result<Completion, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(wait_impl(timeout_nanoseconds));
}
auto Renderer::wait_impl(Serial timeout_nanoseconds) -> Result<Completion, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Submitted && self.state != Impl::State::Complete)
        return Err(Error{Stage::State});
    if (self.state == Impl::State::Submitted) {
        auto &frame = **self.frame;
        VkResult result;
        {
            CpuSample wait(frame.metric(&FrameTimings::wait));
            result = frame.output.fence->value.Wait(timeout_nanoseconds);
        }
        if (result == VK_TIMEOUT) {
            if (frame.options.timings)
                ++frame.timings.wait_timeouts;
            return Err(Error{Stage::Timeout, result});
        }
        if (result != VK_SUCCESS) {
            if (result == VK_ERROR_DEVICE_LOST)
                self.state = Impl::State::Lost;
            frame.fail_timestamps(result);
            return Err(Error{Stage::Lost, result});
        }
        auto retired = self.textures->Poll();
        if (retired.is_err())
            return Err(Error{Stage::Submit, VkResult(retired.unwrap_err().native)});
        (**self.frame).collect_timestamps();
        if (self.session->observe_result((**self.frame).timings.query_result) ==
            VK_ERROR_DEVICE_LOST)
            return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
        (**self.frame).release_uploads();
        (**self.frame).recyclable = true;
        self.state = Impl::State::Complete;
    }
    self.reported = true;
    return Ok(Completion{self.submission, (**self.frame).plan.identity()});
}
auto Renderer::readback() -> Result<Pixels, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(readback_impl());
}
auto Renderer::readback_impl() -> Result<Pixels, Error> {
    auto &self = *impl_;
    if (!self.readback_enabled)
        return Err(Error{Stage::Unsupported});
    if (self.state != Impl::State::Complete)
        return Err(Error{Stage::State});
    auto &frame = **self.frame;
    auto memory = frame.gpu->readback.allocation();
    auto mapped = memory.map();
    if (mapped.is_err())
        return Err(Error{Stage::Readback, mapped.unwrap_err().api_result});
    auto mapping = mapped.unwrap();
    auto invalidated = memory.invalidate();
    if (invalidated.is_err())
        return Err(Error{Stage::Readback, invalidated.unwrap_err().api_result});
    Pixels pixels{frame.extent.width, frame.extent.height};
    auto count = usize(frame.extent.width) * usize(frame.extent.height) * usize(4);
    pixels.rgba.reserve(count);
    const auto *bytes = static_cast<const unsigned char *>(mapping.data());
    for (usize i{}; i < count; ++i)
        pixels.rgba.push(static_cast<unsigned char>(bytes[i.to_primitive()]));
    if (self.windowed && self.device().swapchain().format() == VK_FORMAT_B8G8R8A8_SRGB)
        for (usize i{}; i < count; i += usize(4))
            rstd::swap(pixels.rgba[i], pixels.rgba[i + usize(2)]);
    return Ok(rstd::move(pixels));
}
auto Renderer::present() -> Result<PresentResult, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(present_impl());
}
auto Renderer::present_impl() -> Result<PresentResult, Error> {
    auto &self = *impl_;
    if (!self.windowed ||
        (self.state != Impl::State::Submitted && self.state != Impl::State::Complete))
        return Err(Error{Stage::State});
    auto &frame = **self.frame;
    if (!frame.output.acquired || frame.output.presented)
        return Err(Error{Stage::State});
    auto &sync = self.presents[usize(frame.output.image_index)];
    const auto &device = self.device().handle();
    VkFence fence = self.maintenance() ? *sync.fence : VK_NULL_HANDLE;
    VkResult result = VK_SUCCESS;
    if (self.maintenance()) {
        result = device.Dispatch().vkResetFences(*device, 1, &fence);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Present, result});
    }
    VkSwapchainPresentFenceInfoEXT finish{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
    finish.swapchainCount = 1;
    finish.pFences = &fence;
    VkSemaphore semaphore = *sync.semaphore;
    VkSwapchainKHR swapchain = *self.device().swapchain().handle();
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.pNext = self.maintenance() ? &finish : nullptr;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain;
    info.pImageIndices = &frame.output.image_index;
    {
        CpuSample present(frame.metric(&FrameTimings::present));
        auto access = self.session->queue_access();
        result = device.Dispatch().vkQueuePresentKHR(*self.device().present_queue().handle, &info);
    }
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
        return Err(Error{Stage::Present, result});
    frame.output.presented = true;
    sync.pending = true;
    if (result == VK_ERROR_DEVICE_LOST) {
        self.state = Impl::State::Lost;
        frame.fail_timestamps(result);
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
        return Err(Error{Stage::Present, result});
    return Ok(PresentResult{frame.plan.identity(),
                            result == VK_ERROR_OUT_OF_DATE_KHR ? PresentStatus::OutOfDate
                            : result == VK_SUBOPTIMAL_KHR || frame.output.suboptimal
                                ? PresentStatus::Suboptimal
                                : PresentStatus::Queued});
}
auto Renderer::discard_prepared(Serial timeout_nanoseconds) -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(discard_prepared_impl(timeout_nanoseconds));
}
auto Renderer::discard_prepared_impl(Serial timeout_nanoseconds) -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Prepared || !(**self.frame).recyclable)
        return Err(Error{Stage::State});
    if (self.windowed && !self.maintenance() && (**self.frame).output.acquired)
        return resize_impl(self.device().swapchain().extent(), timeout_nanoseconds);
    auto released = self.release_acquired(timeout_nanoseconds);
    if (released.is_err())
        return released;
    self.frame = None();
    self.state = Impl::State::Ready;
    return Ok(empty{});
}
auto Renderer::resize(VkExtent2D extent, Serial timeout_nanoseconds) -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(resize_impl(extent, timeout_nanoseconds));
}
auto Renderer::resize(VkExtent2D extent, PresentMode mode, Serial timeout_nanoseconds)
    -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    if (unsigned(mode) > unsigned(PresentMode::FifoRelaxed))
        return Err(Error{Stage::State});
    return impl_->observe_result(resize_impl(extent, timeout_nanoseconds, Some(mode)));
}
auto Renderer::resize_impl(VkExtent2D extent, Serial timeout_nanoseconds, Option<PresentMode> mode)
    -> Result<empty, Error> {
    auto &self = *impl_;
    if (!self.windowed || !self.initialized || self.state == Impl::State::Lost ||
        self.state == Impl::State::Closed)
        return Err(Error{Stage::State});
    auto retired = self.retire(timeout_nanoseconds);
    if (retired.is_err())
        return retired;
    if (mode.is_some()) self.presentation = *mode;
    if (!extent.width || !extent.height) {
        self.frame = None();
        self.state = Impl::State::Unavailable;
        return Ok(empty{});
    }
    auto idle = self.session->wait_idle();
    if (idle != VK_SUCCESS)
        return Err(Error{idle == VK_ERROR_DEVICE_LOST ? Stage::Lost : Stage::Device, idle});
    self.frame = None();
    self.presents.clear();
    self.state = Impl::State::Unavailable;
    auto resized = self.session->resize_surface(extent, self.swapchain_options());
    if (resized.is_err())
        return Err(Error::session(resized.unwrap_err()));
    auto features = self.device()
                        .gpu()
                        .GetFormatProperties(self.device().swapchain().format())
                        .optimalTilingFeatures;
    if (!(features & VK_FORMAT_FEATURE_BLIT_DST_BIT))
        return Err(Error{Stage::Unsupported});
    auto sync = self.presentation_sync();
    if (sync.is_err())
        return sync;
    self.state = Impl::State::Ready;
    return Ok(empty{});
}
auto Renderer::detach_surface(Serial timeout_nanoseconds) -> Result<empty, Error> {
    return impl_->observe_result(detach_surface_impl(timeout_nanoseconds));
}
auto Renderer::detach_surface_impl(Serial timeout_nanoseconds) -> Result<empty, Error> {
    auto &self = *impl_;
    if (!self.windowed || !self.initialized || self.state == Impl::State::Closed)
        return Err(Error{Stage::State});
    auto retired = self.retire(timeout_nanoseconds);
    if (retired.is_err())
        return retired;
    auto idle = self.session->wait_idle();
    if (idle != VK_SUCCESS && idle != VK_ERROR_DEVICE_LOST)
        return Err(Error{Stage::Device, idle});
    self.frame = None();
    self.presents.clear();
    self.scopes = vk::GraphicsScopeCache{};
    self.graph_shape = None();
    self.session->detach_surface();
    self.state = self.is_lost() ? Impl::State::Lost : Impl::State::Unavailable;
    return Ok(empty{});
}
auto Renderer::attach_surface(SurfaceHost &host, Serial timeout_nanoseconds) -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(attach_surface_impl(host, timeout_nanoseconds));
}
auto Renderer::attach_surface_impl(SurfaceHost &host, Serial timeout_nanoseconds) -> Result<empty, Error> {
    auto &self = *impl_;
    if (!self.windowed || !self.initialized || self.state == Impl::State::Closed)
        return Err(Error{Stage::State});
    auto extent = host.pixel_extent();
    if (!extent.width || !extent.height)
        return Err(Error{Stage::State});
    auto extensions = host.instance_extensions();
    if (extensions.is_err())
        return Err(extensions.unwrap_err());
    for (const auto &extension : extensions.unwrap()) {
        if (!extension.required)
            continue;
        bool enabled{};
        for (const auto &name : self.session->instance_owner().enabled_extensions())
            enabled |= name.as_ref().to_bytes() == extension.name->as_bytes();
        if (!enabled)
            return Err(Error{Stage::Unsupported});
    }
    auto detached = detach_surface_impl(timeout_nanoseconds);
    if (detached.is_err())
        return detached;
    SurfaceAdapter adapter(host);
    auto attached = self.session->attach_surface(dyn<vk::SessionSurfaceFactory>::from_ref(adapter).as_mut_ref(), extent,
                                                 self.swapchain_options());
    if (attached.is_err()) {
        if (adapter.error.is_some())
            return Err(adapter.error.unwrap());
        return Err(Error::session(attached.unwrap_err()));
    }
    auto features = self.device().gpu().GetFormatProperties(self.device().swapchain().format()).optimalTilingFeatures;
    if (!(features & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        self.session->detach_surface();
        return Err(Error{Stage::Unsupported});
    }
    auto sync = self.presentation_sync();
    if (sync.is_err()) {
        self.session->detach_surface();
        return sync;
    }
    self.state = Impl::State::Ready;
    return Ok(empty{});
}
auto Renderer::shutdown(Serial timeout_nanoseconds) -> Result<empty, Error> {
    return impl_->observe_result(shutdown_impl(timeout_nanoseconds));
}
auto Renderer::shutdown_impl(Serial timeout_nanoseconds) -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state == Impl::State::Closed)
        return Ok(empty{});
    auto retired = self.retire(timeout_nanoseconds);
    if (retired.is_err())
        return retired;
    if (self.windowed && self.session.is_some()) {
        auto result = self.session->wait_idle();
        if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
            return Err(Error{Stage::Device, result});
    }
    self.clear_resources();
    if (self.windowed && self.session.is_some())
        self.session->release_surface();
    self.state = Impl::State::Closed;
    return Ok(empty{});
}
auto Renderer::pixel_extent() const -> VkExtent2D {
    return impl_->windowed && impl_->state != Impl::State::Unavailable &&
                   impl_->state != Impl::State::Closed
               ? impl_->device().swapchain().extent()
               : VkExtent2D{};
}
auto Renderer::present_mode() const -> Option<PresentMode> {
    if (!impl_->windowed || !impl_->initialized || impl_->state == Impl::State::Unavailable ||
        impl_->state == Impl::State::Closed || device_lost()) return None();
    switch (impl_->device().swapchain().presentMode()) {
    case VK_PRESENT_MODE_FIFO_KHR: return Some(PresentMode::Fifo);
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return Some(PresentMode::Immediate);
    case VK_PRESENT_MODE_MAILBOX_KHR: return Some(PresentMode::Mailbox);
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return Some(PresentMode::FifoRelaxed);
    default: return None();
    }
}
auto Renderer::presentation_capabilities() const -> Option<PresentationCapabilities> {
    if (!impl_->windowed || !impl_->initialized)
        return None();
    const bool maintenance = impl_->device().capabilities().swapchain_maintenance1;
    return Some(PresentationCapabilities{
        maintenance ? PresentationPath::Maintenance : PresentationPath::Basic,
        maintenance ? PresentationRetirement::PresentFence
                    : PresentationRetirement::CompatibilityIdle});
}
auto Renderer::frame_resource_stats() const -> FrameResourceStats {
    return impl_->frame_resources->stats;
}
auto Renderer::clear_frame_resource_cache() -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(clear_frame_resource_cache_impl());
}
auto Renderer::clear_frame_resource_cache_impl() -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Ready && self.state != Impl::State::Complete)
        return Err(Error{Stage::State});
    self.frame_resources->clear();
    return Ok(empty{});
}
auto Renderer::set_frame_resource_limits(FrameResourceLimits limits) -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(set_frame_resource_limits_impl(limits));
}
auto Renderer::set_frame_resource_limits_impl(FrameResourceLimits limits) -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Ready && self.state != Impl::State::Complete)
        return Err(Error{Stage::State});
    constexpr Serial maximum = 1ull << 40;
    if (!limits.max_target_bytes || !limits.max_buffer_bytes || limits.max_target_bytes > maximum ||
        limits.max_buffer_bytes > maximum || limits.max_cached_bytes > maximum ||
        limits.max_cached_entries > 65536)
        return Err(Error{Stage::Unsupported});
    auto &pool = *self.frame_resources;
    if (pool.stats.target_bytes - pool.stats.cached_target_bytes > limits.max_target_bytes ||
        pool.stats.buffer_bytes - pool.stats.cached_buffer_bytes > limits.max_buffer_bytes)
        return Err(pool.budget_failure());
    pool.clear();
    pool.limits = limits;
    return Ok(empty{});
}
auto Renderer::release_completed_frame() -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(release_completed_frame_impl());
}
auto Renderer::release_completed_frame_impl() -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Ready && self.state != Impl::State::Complete)
        return Err(Error{Stage::State});
    if (self.frame.is_some() && (**self.frame).output.acquired) {
        if (!(**self.frame).output.presented)
            return Err(Error{Stage::State});
        auto result = (**self.frame).output.acquire_fence.Wait(0);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Timeout, result});
    }
    self.frame = None();
    self.state = Impl::State::Ready;
    return Ok(empty{});
}
auto Renderer::frame_timings() const -> Option<FrameTimings> {
    if (impl_->frame.is_none() || !(**impl_->frame).options.timings)
        return None();
    return Some(FrameTimings((**impl_->frame).timings));
}
auto Renderer::renderer_statistics() const -> vk::RendererStatistics {
    return impl_->compiler ? impl_->compiler->statistics() : vk::RendererStatistics{};
}
auto Renderer::draw_stats() const -> Option<DrawStats> {
    if (impl_->frame.is_none())
        return impl_->recorded_stats;
    return Some(DrawStats(*(**impl_->frame).stats));
}
auto Renderer::image_cache_stats() const -> ImageCacheStats {
    impl_->acknowledge_uploads();
    auto stats = impl_->image_cache.stats;
    if (impl_->frame.is_some()) {
        for (const auto &binding : (**impl_->frame).samples) {
            if (binding.kind != DrawKind::Image)
                continue;
            auto bytes = binding.gpu->bytes;
            if (!binding.uploads.is_empty())
                stats.staging_bytes += bytes;
            bool cached{};
            for (const auto &entry : impl_->image_cache.entries)
                if (entry.gpu.is_some() &&
                    rstd::sync::Arc<SampledImage>::ptr_eq(*entry.gpu, binding.gpu))
                    cached = true;
            if (!cached)
                stats.retained_gpu_bytes += bytes;
        }
    }
    return stats;
}
auto Renderer::clear_image_cache() -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(clear_image_cache_impl());
}
auto Renderer::clear_image_cache_impl() -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Ready && self.state != Impl::State::Complete)
        return Err(Error{Stage::State});
    self.image_cache.clear();
    return Ok(empty{});
}
auto Renderer::set_image_cache_limits(ImageCacheLimits limits) -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(set_image_cache_limits_impl(limits));
}
auto Renderer::set_image_cache_limits_impl(ImageCacheLimits limits) -> Result<empty, Error> {
    if (!limits.max_entries || limits.max_entries > 65536 || !limits.max_bytes ||
        limits.max_bytes > 256ull * 1024 * 1024)
        return Err(Error{Stage::Unsupported});
    auto cleared = clear_image_cache();
    if (cleared.is_err())
        return cleared;
    impl_->image_cache.limits = limits;
    return Ok(empty{});
}
auto Renderer::text_cache_stats() const -> TextCacheStats {
    impl_->acknowledge_uploads();
    auto stats = impl_->glyph_cache.stats;
    if (impl_->frame.is_some()) {
        for (const auto &binding : (**impl_->frame).samples) {
            if (binding.kind != DrawKind::Glyph)
                continue;
            auto bytes = Serial(binding.gpu->extent.width) * binding.gpu->extent.height;
            if (!binding.uploads.is_empty())
                stats.staging_bytes += bytes;
            bool cached{};
            for (const auto &page : impl_->glyph_cache.pages)
                if (page.gpu.is_some() &&
                    rstd::sync::Arc<SampledImage>::ptr_eq(*page.gpu, binding.gpu))
                    cached = true;
            if (!cached)
                stats.retained_gpu_bytes += bytes;
        }
    }
    return stats;
}
auto Renderer::clear_text_cache() -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(clear_text_cache_impl());
}
auto Renderer::clear_text_cache_impl() -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Ready && self.state != Impl::State::Complete)
        return Err(Error{Stage::State});
    self.glyph_cache.clear();
    return Ok(empty{});
}
auto Renderer::set_text_cache_limits(TextCacheLimits limits) -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(set_text_cache_limits_impl(limits));
}
auto Renderer::set_text_cache_limits_impl(TextCacheLimits limits) -> Result<empty, Error> {
    if (limits.page_extent < 4 || !limits.max_pages || limits.max_pages > 256 ||
        !limits.max_entries || limits.max_entries > 1000000 || !limits.max_bytes ||
        limits.max_bytes > 256ull * 1024 * 1024)
        return Err(Error{Stage::Unsupported});
    auto cleared = clear_text_cache();
    if (cleared.is_err())
        return cleared;
    impl_->glyph_cache.limits = limits;
    return Ok(empty{});
}
auto Renderer::validation_errors() const -> unsigned {
    return impl_->session.is_some() ? impl_->session->validation_errors() : 0;
}
} // namespace vui::gpu
