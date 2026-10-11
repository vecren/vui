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
import :shaders;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace vui::gpu {
namespace rg = ::vrento::rg;
namespace vk = ::vrento::vulkan;
namespace {
using namespace detail;
using CpuSample = vui::CpuTimer;
enum class DrawKind { Rectangle, Composite, Glyph, Image, External };
auto pipeline_kind(DrawKind kind) -> DrawKind {
    return kind == DrawKind::External ? DrawKind::Image : kind;
}
struct PaintItem {
    DrawData data{};
    DrawKind kind{DrawKind::Rectangle};
    Index clip{no_clip}, source_pass{}, resource_index{};
    ImageSampling sampling{ImageSampling::Linear};
};
struct SampleBinding {
    DrawKind kind;
    Index cache_index;
    rstd::sync::Arc<SampledImage> gpu;
    Option<vvk::AllocatedBuffer> upload;
    rg::TextureNodeRef texture;
};
struct ExternalBinding {
    TextureFrame frame;
    vrento::vulkan::TextureRead prepared;
    rg::TextureNodeRef texture;
};
struct UploadPass : rg::Pass {
    struct Source {
        Index binding;
        rg::TextureNodeRef texture;
        bool upload;
    };
    struct Desc {
        Vec<Source> pages;
    };
    Desc desc;
    explicit UploadPass(Desc value) : desc(rstd::move(value)) {}
};
struct UiPass : rg::Pass {
    struct Source {
        Index index;
        rg::TextureNodeRef texture;
        DrawKind kind{DrawKind::Composite};
    };
    struct Desc {
        Index index{};
        rg::TextureNodeRef target;
        Vec<Source> sources;
        Vec<Source> samples;
    };
    Desc desc;
    explicit UiPass(Desc value) : desc(rstd::move(value)) {}
};
struct OwnedFrame;
struct PrepareContext {
    OwnedFrame &frame;
    const vk::Device &device;
    VkDescriptorSetLayout layout;
    VkSampler sampler, glyph_sampler;
};
struct RecordContext {
    VkCommandBuffer command;
    const vvk::DeviceDispatch &dispatch;
    VkRenderPass render_pass;
    VkPipelineLayout layout;
    VkPipeline rectangle, composite, glyph, image;
};
struct PreparedDraw {
    VkDescriptorSet descriptor{};
    DrawKind kind;
    Index first{}, count{1};
};
struct UiExecutor {
    VkFramebuffer framebuffer{};
    VkExtent2D extent{};
    Vec<PreparedDraw> draws;
    auto prepare(const UiPass &, const rg::DeclaredTextures &, PrepareContext &)
        -> Result<empty, rg::ExecutionError>;
    auto record(RecordContext &) -> Result<empty, rg::ExecutionError>;
};
struct OwnedFrame {
    const vk::Device &device;
    FrameResources &resources;
    bool recyclable{true};
    VkResult uncertain_submission{VK_SUCCESS};
    FramePlan plan;
    rg::RenderGraph graph;
    rg::GraphExecutor<PrepareContext, RecordContext> executor;
    Vec<Vec<PaintItem>> items;
    Vec<Index> item_offsets;
    DrawOptions options;
    DrawStats stats;
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
    Vec<Target> targets;
    Vec<rg::TextureNodeRef> textures;
    FrameBuffer clips, instances, readback;
    VkDescriptorPool pool{};
    VkCommandBuffer command{};
    vvk::Fence fence, acquire_fence;
    vvk::Semaphore acquire_semaphore;
    Index image_index{};
    bool acquired{}, presented{}, suboptimal{};
    VkExtent2D extent{};
    VkResult error{VK_SUCCESS};
    explicit OwnedFrame(const vk::Device &device, FrameResources &resources, FramePlan plan)
        : device(device), resources(resources), plan(rstd::move(plan)) {}
    void release_uploads() {
        for (auto &page : samples)
            page.upload = None();
    }
    ~OwnedFrame() {
        const auto &d = device.handle().Dispatch();
        if (command)
            d.vkFreeCommandBuffers(*device.handle(), *device.cmd_pool(), 1, &command);
        if (pool)
            d.vkDestroyDescriptorPool(*device.handle(), pool, nullptr);
        if (queries)
            d.vkDestroyQueryPool(*device.handle(), queries, nullptr);
        for (auto &target : targets)
            resources.release(rstd::move(target), recyclable);
        resources.release(rstd::move(clips), recyclable);
        resources.release(rstd::move(instances), recyclable);
        resources.release(rstd::move(readback), recyclable);
    }
};
auto shader(const vvk::Device &device, const unsigned *code, rstd::size_t bytes,
            vvk::ShaderModule &output) -> VkResult {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes;
    info.pCode = code;
    return device.CreateShaderModule(info, output);
}
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
auto UiExecutor::prepare(const UiPass &pass, const rg::DeclaredTextures &resources,
                         PrepareContext &context) -> Result<empty, rg::ExecutionError> {
    auto &frame = context.frame;
    if (resources.write(pass.desc.target).is_none())
        return Err(rg::ExecutionError::Prepare);
    for (const auto &source : pass.desc.sources)
        if (resources.read(source.texture).is_none())
            return Err(rg::ExecutionError::Prepare);
    framebuffer = *frame.targets[usize(pass.desc.index)].framebuffer;
    extent = frame.extent;
    const auto &d = context.device.handle().Dispatch();
    draws.clear();
    for (const auto &source : pass.desc.samples)
        if (resources.read(source.texture).is_none())
            return Err(rg::ExecutionError::Prepare);
    VkImageView previous_view{};
    VkSampler previous_sampler{};
    Index instance = frame.item_offsets[usize(pass.desc.index)];
    for (const auto &paint : frame.items[usize(pass.desc.index)]) {
        PreparedDraw item{{}, paint.kind, instance++, 1};
        VkImageView source_view{};
        if (paint.kind == DrawKind::Composite) {
            bool declared{};
            for (const auto &source : pass.desc.sources)
                if (source.index == paint.source_pass)
                    declared = true;
            if (!declared || usize(paint.source_pass) >= frame.targets.len())
                return Err(rg::ExecutionError::Prepare);
            source_view = *frame.targets[usize(paint.source_pass)].view;
        } else if (paint.kind == DrawKind::Glyph || paint.kind == DrawKind::Image ||
                   paint.kind == DrawKind::External) {
            bool declared{};
            for (const auto &source : pass.desc.samples)
                if (source.index == paint.resource_index && source.kind == paint.kind)
                    declared = true;
            if (!declared)
                return Err(rg::ExecutionError::Prepare);
            for (const auto &page : frame.samples)
                if (page.cache_index == paint.resource_index && page.kind == paint.kind)
                    source_view = *page.gpu->view;
            if (paint.kind == DrawKind::External &&
                usize(paint.resource_index) < frame.externals.len())
                source_view = frame.externals[usize(paint.resource_index)]
                                  .prepared.native;
            if (!source_view)
                return Err(rg::ExecutionError::Prepare);
        }
        auto sampler =
            paint.kind == DrawKind::Glyph ||
                    ((paint.kind == DrawKind::Image || paint.kind == DrawKind::External) &&
                     paint.sampling == ImageSampling::Linear)
                ? context.glyph_sampler
                : context.sampler;
        if (draws.is_empty())
            ++frame.stats.pass_starts;
        else {
            auto &previous = draws[draws.len() - usize(1)];
            if (!frame.options.batching)
                ++frame.stats.disabled_breaks;
            else if (pipeline_kind(previous.kind) != pipeline_kind(item.kind))
                ++frame.stats.pipeline_breaks;
            else if (previous_view != source_view)
                ++frame.stats.texture_breaks;
            else if (previous_sampler != sampler)
                ++frame.stats.sampler_breaks;
            else {
                ++previous.count;
                ++frame.stats.merged_items;
                continue;
            }
        }
        previous_view = source_view;
        previous_sampler = sampler;
        ++frame.stats.batches;
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = frame.pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &context.layout;
        frame.error =
            d.vkAllocateDescriptorSets(*context.device.handle(), &allocate, &item.descriptor);
        if (frame.error != VK_SUCCESS)
            return Err(rg::ExecutionError::Prepare);
        VkDescriptorBufferInfo buffer{frame.clips.handle(), 0, VK_WHOLE_SIZE};
        VkDescriptorBufferInfo instances{frame.instances.handle(), 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet writes[3]{};
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
        if (item.kind != DrawKind::Rectangle) {
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = item.descriptor;
            writes[2].dstBinding = 0;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].pImageInfo = &image;
        }
        d.vkUpdateDescriptorSets(*context.device.handle(), item.kind != DrawKind::Rectangle ? 3 : 2,
                                 writes, 0, nullptr);
        draws.push(rstd::move(item));
    }
    return Ok(empty{});
}
auto UiExecutor::record(RecordContext &context) -> Result<empty, rg::ExecutionError> {
    const auto &d = context.dispatch;
    VkClearValue clear{};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = context.render_pass;
    begin.framebuffer = framebuffer;
    begin.renderArea.extent = extent;
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    d.vkCmdBeginRenderPass(context.command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height),
                        0, 1};
    VkRect2D scissor{{0, 0}, extent};
    d.vkCmdSetViewport(context.command, 0, 1, &viewport);
    d.vkCmdSetScissor(context.command, 0, 1, &scissor);
    for (const auto &draw : draws) {
        d.vkCmdBindPipeline(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            draw.kind == DrawKind::Image || draw.kind == DrawKind::External
                                ? context.image
                            : draw.kind == DrawKind::Glyph     ? context.glyph
                            : draw.kind == DrawKind::Composite ? context.composite
                                                               : context.rectangle);
        d.vkCmdBindDescriptorSets(context.command, VK_PIPELINE_BIND_POINT_GRAPHICS, context.layout,
                                  0, 1, &draw.descriptor, 0, nullptr);
        d.vkCmdDraw(context.command, 6, draw.count, 0, draw.first);
    }
    d.vkCmdEndRenderPass(context.command);
    return Ok(empty{});
}
struct UploadExecutor {
    struct Upload {
        VkImage image;
        VkBuffer buffer;
        VkExtent2D extent;
    };
    Vec<Upload> uploads;
    auto prepare(const UploadPass &pass, const rg::DeclaredTextures &resources,
                 PrepareContext &context) -> Result<empty, rg::ExecutionError> {
        for (const auto &source : pass.desc.pages) {
            const auto &binding = context.frame.samples[usize(source.binding)];
            if (source.upload) {
                if (resources.write(source.texture).is_none() || binding.upload.is_none())
                    return Err(rg::ExecutionError::Prepare);
                uploads.push(Upload{binding.gpu->image.handle(), binding.upload->handle(),
                                    binding.gpu->extent});
            } else if (resources.read(source.texture).is_none())
                return Err(rg::ExecutionError::Prepare);
        }
        return Ok(empty{});
    }
    auto record(RecordContext &context) -> Result<empty, rg::ExecutionError> {
        const auto &d = context.dispatch;
        for (const auto &upload : uploads) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = upload.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            d.vkCmdPipelineBarrier(context.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &barrier);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {upload.extent.width, upload.extent.height, 1};
            d.vkCmdCopyBufferToImage(context.command, upload.buffer, upload.image,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            d.vkCmdPipelineBarrier(context.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                                   1, &barrier);
        }
        return Ok(empty{});
    }
};
auto resolve_items(OwnedFrame &frame, GlyphCache &cache, ImageCache &images, Index max_extent,
                   const vk::DeviceSession &session) -> Result<empty, Error> {
    auto surface = frame.plan.surface();
    auto raster_scale = std::max(Scalar(frame.extent.width) / surface.logical_size.width,
                                 Scalar(frame.extent.height) / surface.logical_size.height);
    if (!finite(raster_scale) || raster_scale <= 0)
        return Err(Error{Stage::State});
    auto coverage_scale = std::min(Scalar(frame.extent.width) / surface.logical_size.width,
                                   Scalar(frame.extent.height) / surface.logical_size.height);
    Serial total{};
    for (usize pass{}; pass < frame.plan.pass_count(); ++pass) {
        Vec<PaintItem> items;
        for (const auto &draw : frame.plan.draws(static_cast<Index>(pass.to_primitive()))) {
            PaintItem item;
            item.data.surface[0] = static_cast<float>(surface.logical_size.width);
            item.data.surface[1] = static_cast<float>(surface.logical_size.height);
            item.data.surface[2] = static_cast<float>(coverage_scale);
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
            } else if (draw.is_Composite()) {
                const auto &value = draw.as_Composite();
                item.kind = DrawKind::Composite;
                item.source_pass = value.source_pass;
                item.data.params[3] = 1;
                item.data.params[1] = static_cast<float>(value.opacity);
                rectangle_values(item.data.rect,
                                 {0, 0, surface.logical_size.width, surface.logical_size.height});
                transform_values(item.data, {});
                item.clip = value.clip;
            } else if (draw.is_Image()) {
                const auto &value = draw.as_Image();
                if (!value.destination.width || !value.destination.height || !value.source.width ||
                    !value.source.height || value.transform.inverse().is_none())
                    continue;
                auto resolved = images.resolve(value.data, max_extent);
                if (resolved.is_err())
                    return Err(resolved.unwrap_err());
                item.kind = DrawKind::Image;
                item.resource_index = resolved.unwrap();
                item.sampling = value.sampling;
                item.clip = value.clip;
                transform_values(item.data, value.transform);
                rectangle_values(item.data.rect, value.destination);
                item.data.uv[0] = value.source.x / value.data.width();
                item.data.uv[1] = value.source.y / value.data.height();
                item.data.uv[2] = value.source.width / value.data.width();
                item.data.uv[3] = value.source.height / value.data.height();
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
                auto prepared = vrento::vulkan::prepare_texture_read(
                    session, native->native(),
                    vrento::texture::Stages::Fragment,
                    value.sampling == ImageSampling::Linear);
                if (prepared.is_err()) {
                    Error error{Stage::ExternalContent};
                    error.content = Some(prepared.unwrap_err());
                    return Err(error);
                }
                Index index = static_cast<Index>(frame.externals.len().to_primitive());
                for (usize i{}; i < frame.externals.len(); ++i)
                    if (frame.externals[i].frame.payload<NativeTextureFrame>() == native) {
                        index = static_cast<Index>(i.to_primitive());
                        break;
                    }
                if (usize(index) == frame.externals.len())
                    frame.externals.push(ExternalBinding{value.frame, prepared.unwrap(), {}});
                item.kind = DrawKind::External;
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
                if (value.color.a == 0 || value.transform.inverse().is_none())
                    continue;
                auto scaled = glyph_scale(value.transform, raster_scale);
                if (scaled.is_err())
                    return Err(scaled.unwrap_err());
                Scalar scale = scaled.unwrap();
                item.kind = DrawKind::Glyph;
                item.data.params[3] = 2;
                item.clip = value.clip;
                transform_values(item.data, value.transform);
                color(value.color);
                for (const auto &glyph : value.layout.glyphs()) {
                    if (!glyph.drawable)
                        continue;
                    auto resolved = cache.resolve(value.layout.fonts()[usize(glyph.font)],
                                                  glyph.glyph, scale, max_extent);
                    if (resolved.is_err())
                        return Err(resolved.unwrap_err());
                    auto entry = resolved.unwrap();
                    if (!entry.width || !entry.height)
                        continue;
                    item.resource_index = entry.page;
                    const auto &page = cache.pages[usize(entry.page)];
                    rectangle_values(
                        item.data.rect,
                        {value.origin.x + glyph.position.x + (entry.bearing.x - 1) / scale,
                         value.origin.y + glyph.position.y + (entry.bearing.y - 1) / scale,
                         (entry.width + 2) / scale, (entry.height + 2) / scale});
                    item.data.uv[0] = float(entry.x - 1) / page.width;
                    item.data.uv[1] = float(entry.y - 1) / page.height;
                    item.data.uv[2] = float(entry.width + 2) / page.width;
                    item.data.uv[3] = float(entry.height + 2) / page.height;
                    if (++total > 65536)
                        return Err(Error{Stage::DrawLimit});
                    items.push(PaintItem(item));
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
            ++frame.stats.resolved_items;
            bool discard{};
            if (frame.options.culling &&
                !(item.clip == no_clip && visibility::center_on_surface(item.data))) {
                if (mapping.is_none() || !mapping->matches(item.data))
                    mapping = Some(visibility::QuadMapping(item.data));
                auto bounds = mapping->quad(item.data, frame.extent);
                if (bounds.is_some() && visibility::outside(*bounds, surface)) {
                    ++frame.stats.surface_culled;
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
                    if (visibility::clipped(pixels, value,
                                            frame.extent.width / item.data.surface[0],
                                            frame.extent.height / item.data.surface[1])) {
                        discard = true;
                        ++frame.stats.clip_culled;
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
auto prepare_atlas(OwnedFrame &frame, GlyphCache &cache) -> Result<empty, Error> {
    for (usize i{}; i < cache.pages.len(); ++i) {
        bool used{};
        for (const auto &items : frame.items)
            for (const auto &item : items)
                used |= item.kind == DrawKind::Glyph && item.resource_index == i.to_primitive();
        if (!used)
            continue;
        auto &page = cache.pages[i];
        if (page.gpu.is_none()) {
            auto created =
                make_sampled(frame.device, {page.width, page.height}, VK_FORMAT_R8_UNORM);
            if (created.is_err())
                return Err(created.unwrap_err());
            page.gpu = Some(rstd::move(created).unwrap());
            cache.stats.gpu_bytes += Serial(page.width) * page.height;
        }
        SampleBinding binding{
            DrawKind::Glyph, static_cast<Index>(i.to_primitive()), page.gpu->clone(), {}, {}};
        if (!page.resident) {
            auto uploaded = make_upload(frame.device, page.pixels.as_slice());
            if (uploaded.is_err())
                return Err(uploaded.unwrap_err());
            binding.upload = Some(rstd::move(uploaded).unwrap());
        }
        frame.samples.push(rstd::move(binding));
    }
    return Ok(empty{});
}
auto prepare_images(OwnedFrame &frame, ImageCache &cache) -> Result<empty, Error> {
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
                make_sampled(frame.device, {entry.data.width(), entry.data.height()}, format);
            if (created.is_err())
                return Err(created.unwrap_err());
            entry.gpu = Some(rstd::move(created).unwrap());
            cache.stats.gpu_bytes += entry.data.pixels().len().to_primitive();
        }
        SampleBinding binding{
            DrawKind::Image, static_cast<Index>(i.to_primitive()), entry.gpu->clone(), {}, {}};
        if (!entry.resident) {
            auto pixels = premultiply_srgb(entry.data.pixels());
            auto uploaded = make_upload(frame.device, pixels.as_slice());
            if (uploaded.is_err())
                return Err(uploaded.unwrap_err());
            binding.upload = Some(rstd::move(uploaded).unwrap());
        }
        frame.samples.push(rstd::move(binding));
    }
    return Ok(empty{});
}
} // namespace

struct CompletionTimeline {
    vk::DeviceSession session;
    vvk::Semaphore semaphore;
    explicit CompletionTimeline(vk::DeviceSession owner)
        : session(rstd::move(owner)) {}
};

struct Renderer::Impl {
    Option<vk::DeviceSession> session;
    auto device() const -> const vk::Device & { return session->device(); }
    vvk::RenderPass render_pass;
    vvk::DescriptorSetLayout descriptor_layout;
    vvk::PipelineLayout layout;
    vvk::Pipeline rectangle, composite, glyph, image;
    vvk::Sampler sampler, glyph_sampler;
    Option<rstd::sync::Arc<CompletionTimeline>> completion_timeline;
    auto completion_semaphore() const -> VkSemaphore {
        return *(*completion_timeline)->semaphore;
    }
    GlyphCache glyph_cache;
    ImageCache image_cache;
    FrameResources frame_resources;
    Option<Box<OwnedFrame>> frame;
    struct PresentSync {
        vvk::Semaphore semaphore;
        vvk::Fence fence;
        bool pending{};
    };
    Vec<PresentSync> presents;
    PFN_vkReleaseSwapchainImagesEXT release_images{};
    bool windowed{}, initialized{}, readback_enabled{true};
    auto swapchain_options() const -> vk::SwapchainOptions {
        return {true, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          (readback_enabled ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u)};
    }
    auto presentation_sync() -> Result<empty, Error> {
        Vec<PresentSync> next;
        for (usize i{}; i < device().swapchain().images().len(); ++i) {
            PresentSync sync;
            auto result = device().handle().CreateSemaphore(
                {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}, sync.semaphore);
            if (result == VK_SUCCESS)
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
        if (frame.is_some() && (**frame).acquired && !(**frame).presented) {
            auto &active = **frame;
            auto result = session->observe_result(active.acquire_fence.Wait(timeout));
            if (result != VK_SUCCESS)
                return Err(Error{Stage::Acquire, result});
            VkReleaseSwapchainImagesInfoEXT release{
                VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT};
            release.swapchain = *device().swapchain().handle();
            release.imageIndexCount = 1;
            release.pImageIndices = &active.image_index;
            result = release_images(*device().handle(), &release);
            if (result != VK_SUCCESS)
                return Err(Error{Stage::Surface, result});
            active.acquired = false;
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
            if (frame.is_some() && (**frame).acquired) {
                result = session->observe_result((**frame).acquire_fence.Wait(timeout));
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    return Err(
                        Error{result == VK_TIMEOUT ? Stage::Timeout : Stage::Acquire, result});
            }
            for (auto &sync : presents) {
                if (!sync.pending)
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
                return Err(
                    Error{Stage::Submit, (**frame).uncertain_submission});
            auto result = session->wait_idle();
            if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                return Err(Error{Stage::Submit, result});
            (**frame).uncertain_submission = VK_SUCCESS;
        }
        if (state == State::Submitted) {
            auto result = session->observe_result((**frame).fence.Wait(timeout));
            if (result != VK_SUCCESS) {
                if (result != VK_TIMEOUT)
                    (**frame).fail_timestamps(result);
                return Err(Error{result == VK_TIMEOUT ? Stage::Timeout : Stage::Lost, result});
            }
            (**frame).collect_timestamps();
            if (session->observe_result((**frame).timings.query_result) == VK_ERROR_DEVICE_LOST)
                return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
            (**frame).recyclable = true;
            state = State::Complete;
        }
        if (frame.is_some() && (**frame).acquired && (**frame).presented) {
            auto result = session->observe_result((**frame).acquire_fence.Wait(timeout));
            if (result != VK_SUCCESS)
                return Err(Error{Stage::Acquire, result});
        }
        auto released = release_acquired(timeout);
        if (released.is_err())
            return released;
        for (auto &sync : presents) {
            if (!sync.pending)
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
        clear_resources();
        if (windowed && session.is_some())
            session->release_surface();
    }
    void clear_resources() {
        frame = None();
        frame_resources.clear();
        completion_timeline = None();
        presents.clear();
        glyph_cache.clear();
        image_cache.clear();
        image.reset();
        glyph_sampler.reset();
        glyph.reset();
        sampler.reset();
        composite.reset();
        rectangle.reset();
        layout.reset();
        descriptor_layout.reset();
        render_pass.reset();
    }
    auto pipelines() -> Result<empty, Error> {
        const auto &handle = device().handle();
        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        VkSubpassDependency dependencies[2]{};
        dependencies[0] = {VK_SUBPASS_EXTERNAL,
                           0,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_ACCESS_SHADER_READ_BIT,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_DEPENDENCY_BY_REGION_BIT};
        dependencies[1] = {0,
                           VK_SUBPASS_EXTERNAL,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                           VK_DEPENDENCY_BY_REGION_BIT};
        VkRenderPassCreateInfo pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass_info.attachmentCount = 1;
        pass_info.pAttachments = &attachment;
        pass_info.subpassCount = 1;
        pass_info.pSubpasses = &subpass;
        pass_info.dependencyCount = 2;
        pass_info.pDependencies = dependencies;
        auto result = handle.CreateRenderPass(pass_info, render_pass);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Pipeline, result});
        VkDescriptorSetLayoutBinding bindings[3]{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                       VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                       nullptr};
        bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo descriptor_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor_info.bindingCount = 3;
        descriptor_info.pBindings = bindings;
        result = handle.CreateDescriptorSetLayout(descriptor_info, descriptor_layout);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Pipeline, result});
        auto set_layout = *descriptor_layout;
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &set_layout;
        result = handle.CreatePipelineLayout(layout_info, layout);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Pipeline, result});
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxLod = 0;
        result = handle.CreateSampler(sampler_info, sampler);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Pipeline, result});
        sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_LINEAR;
        result = handle.CreateSampler(sampler_info, glyph_sampler);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Pipeline, result});
        vvk::ShaderModule vertex, rect, compose, glyph_fragment, image_fragment;
        result = shader(handle, shaders::quad.words, sizeof(shaders::quad.words), vertex);
        if (result == VK_SUCCESS)
            result =
                shader(handle, shaders::rectangle.words, sizeof(shaders::rectangle.words), rect);
        if (result == VK_SUCCESS)
            result =
                shader(handle, shaders::composite.words, sizeof(shaders::composite.words), compose);
        if (result == VK_SUCCESS)
            result =
                shader(handle, shaders::glyph.words, sizeof(shaders::glyph.words), glyph_fragment);
        if (result == VK_SUCCESS)
            result =
                shader(handle, shaders::image.words, sizeof(shaders::image.words), image_fragment);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Shader, result});
        auto make_pipeline = [&](VkShaderModule fragment, vvk::Pipeline &output) {
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                         nullptr,
                         0,
                         VK_SHADER_STAGE_VERTEX_BIT,
                         *vertex,
                         "main",
                         nullptr};
            stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                         nullptr,
                         0,
                         VK_SHADER_STAGE_FRAGMENT_BIT,
                         fragment,
                         "main",
                         nullptr};
            VkPipelineVertexInputStateCreateInfo input{
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo assembly{
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{
                VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo multisample{
                VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState blend{};
            blend.blendEnable = VK_TRUE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;
            blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend_info{
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blend_info.attachmentCount = 1;
            blend_info.pAttachments = &blend;
            VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{
                VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dynamic_states;
            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            info.stageCount = 2;
            info.pStages = stages;
            info.pVertexInputState = &input;
            info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport;
            info.pRasterizationState = &raster;
            info.pMultisampleState = &multisample;
            info.pColorBlendState = &blend_info;
            info.pDynamicState = &dynamic;
            info.layout = *layout;
            info.renderPass = *render_pass;
            return handle.CreateGraphicsPipeline(info, output);
        };
        result = make_pipeline(*rect, rectangle);
        if (result == VK_SUCCESS)
            result = make_pipeline(*compose, composite);
        if (result == VK_SUCCESS)
            result = make_pipeline(*glyph_fragment, glyph);
        if (result == VK_SUCCESS)
            result = make_pipeline(*image_fragment, image);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Pipeline, result});
        return Ok(empty{});
    }
};

Renderer::Renderer() : impl_(Box<Impl>::make()) {}
Renderer::~Renderer() = default;
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
auto session_error(vk::DeviceSessionError error) -> Error {
    switch (error.stage) {
    case vk::DeviceSessionStage::Invalid:
        return {Stage::State, error.result};
    case vk::DeviceSessionStage::Instance:
        return {Stage::Instance, error.result};
    case vk::DeviceSessionStage::Surface:
        return {Stage::Surface, error.result};
    case vk::DeviceSessionStage::Device:
        return {Stage::Device, error.result};
    case vk::DeviceSessionStage::Lost:
        return {Stage::Lost, error.result};
    }
    return {Stage::State, error.result};
}
struct SurfaceAdapter : vk::SessionSurfaceFactory {
    SurfaceHost &host;
    Option<Error> error;
    explicit SurfaceAdapter(SurfaceHost &host) : host(host) {}
    auto create(VkInstance instance)
        -> Result<vk::SessionSurface, vk::DeviceSessionError> override {
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
        instance_extensions.push({true, "VK_KHR_get_surface_capabilities2"_str});
        instance_extensions.push({true, "VK_EXT_surface_maintenance1"_str});
    }
    vk::Extension required[]{{true, "VK_KHR_swapchain"_str},
                             {true, "VK_EXT_swapchain_maintenance1"_str}};
    vk::DeviceSessionOptions options;
    options.instance_extensions = instance_extensions.as_slice();
    options.device_extensions = slice<vk::Extension>::from_raw_parts(required, usize(host ? 2 : 0));
    options.extent = host ? host->pixel_extent() : VkExtent2D{1, 1};
    options.swapchain = host ? self.swapchain_options() : vk::SwapchainOptions{};
    options.validation = validation;
    Option<SurfaceAdapter> adapter;
    if (host)
        adapter = Some(SurfaceAdapter(*host));
    auto created = vk::DeviceSession::create(options, adapter.is_some() ? &*adapter : nullptr);
    if (created.is_err()) {
        if (adapter.is_some() && adapter->error.is_some())
            return Err(*adapter->error);
        return Err(session_error(created.unwrap_err()));
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
    auto features = self.device().gpu().GetFormatProperties(format).optimalTilingFeatures;
    if ((features &
         (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) !=
        (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        return Err(Error{Stage::Unsupported});
    if (self.windowed) {
        self.release_images = reinterpret_cast<PFN_vkReleaseSwapchainImagesEXT>(
            self.device().handle().Dispatch().vkGetDeviceProcAddr(*self.device().handle(),
                                                                  "vkReleaseSwapchainImagesEXT"));
        auto destination = self.device()
                               .gpu()
                               .GetFormatProperties(self.device().swapchain().format())
                               .optimalTilingFeatures;
        if (!self.release_images || !(features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
            !(destination & VK_FORMAT_FEATURE_BLIT_DST_BIT))
            return Err(Error{Stage::Unsupported});
        auto sync = self.presentation_sync();
        if (sync.is_err())
            return sync;
    }
    auto result = self.pipelines();
    if (result.is_err())
        return result;
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semaphore.pNext = &type;
    auto completion = rstd::sync::Arc<CompletionTimeline>::make(*self.session);
    auto status = self.device().handle().CreateSemaphore(semaphore,
                                                         completion->semaphore);
    if (status != VK_SUCCESS)
        return Err(Error{Stage::Allocation, status});
    self.completion_timeline = Some(rstd::move(completion));
    self.initialized = true;
    self.state = Impl::State::Ready;
    return Ok(empty{});
}

auto Renderer::prepare(FramePlan plan, DrawOptions options) -> Result<empty, Error> {
    if (impl_->is_lost())
        return Err(Error{Stage::Lost, VK_ERROR_DEVICE_LOST});
    return impl_->observe_result(prepare_impl(rstd::move(plan), options));
}
auto Renderer::prepare_impl(FramePlan plan, DrawOptions options) -> Result<empty, Error> {
    auto &self = *impl_;
    if (self.state != Impl::State::Ready && self.state != Impl::State::Complete)
        return Err(Error{Stage::State});
    if (self.frame.is_some() && (**self.frame).acquired && !(**self.frame).presented)
        return Err(Error{Stage::State});
    if (self.frame.is_some() && (**self.frame).acquired) {
        auto result = (**self.frame).acquire_fence.Wait(0);
        if (result != VK_SUCCESS)
            return Err(Error{Stage::Timeout, result});
    }
    Option<rstd::time::Instant> prepare_start;
    if (options.timings)
        prepare_start = Some(rstd::time::Instant::now());
    auto surface = plan.surface();
    auto width = std::ceil(surface.logical_size.width * surface.scale);
    auto height = std::ceil(surface.logical_size.height * surface.scale);
    if (width < 1 || height < 1 || width > self.device().limits().maxImageDimension2D ||
        height > self.device().limits().maxImageDimension2D || plan.pass_count() > usize(1024) ||
        plan.clips().len() > usize(65535))
        return Err(Error{Stage::Unsupported});
    auto candidate = Box<OwnedFrame>::make(self.device(), self.frame_resources, rstd::move(plan));
    auto &frame = *candidate;
    frame.options = options;
    frame.stats.frame = frame.plan.identity();
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
    auto atlas_ready = prepare_atlas(frame, self.glyph_cache);
    if (atlas_ready.is_err())
        return atlas_ready;
    auto images_ready = prepare_images(frame, self.image_cache);
    if (images_ready.is_err())
        return images_ready;
    for (usize i{}; i < frame.plan.pass_count(); ++i) {
        auto target = self.frame_resources.target(self.device(), *self.render_pass, frame.extent);
        if (target.is_err())
            return Err(target.unwrap_err());
        frame.targets.push(target.unwrap());
        frame.textures.push(rg::TextureNodeRef{});
    }
    if (clip_data.is_empty())
        clip_data.push(GpuClip{});
    auto buffer_bytes = clip_data.len().to_primitive() * sizeof(GpuClip);
    auto allocation =
        self.frame_resources.buffer(self.device(), buffer_bytes, FrameBufferKind::Storage);
    if (allocation.is_err())
        return Err(allocation.unwrap_err());
    frame.clips = allocation.unwrap();
    {
        auto memory = frame.clips.allocation();
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
    frame.stats.items = instance_data.len().to_primitive();
    if (instance_data.is_empty())
        instance_data.push(DrawData{});
    buffer_bytes = instance_data.len().to_primitive() * sizeof(DrawData);
    if (buffer_bytes > self.device().limits().maxStorageBufferRange)
        return Err(Error{Stage::DrawLimit});
    frame.stats.instance_bytes = buffer_bytes;
    allocation = self.frame_resources.buffer(self.device(), buffer_bytes, FrameBufferKind::Storage);
    if (allocation.is_err())
        return Err(allocation.unwrap_err());
    frame.instances = allocation.unwrap();
    {
        auto memory = frame.instances.allocation();
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
            self.frame_resources.buffer(self.device(), buffer_bytes, FrameBufferKind::Readback);
        if (allocation.is_err())
            return Err(allocation.unwrap_err());
        frame.readback = allocation.unwrap();
    }
    unsigned count{};
    for (usize pass{}; pass < frame.plan.pass_count(); ++pass)
        count += static_cast<unsigned>(frame.items[pass].len().to_primitive());
    if (!count)
        count = 1;
    VkDescriptorPoolSize pool_sizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count * 2},
                                      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = count;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;
    auto result = d.vkCreateDescriptorPool(*device, &pool_info, nullptr, &frame.pool);
    if (result != VK_SUCCESS)
        return fail(Stage::Allocation, result);
    VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = *self.device().cmd_pool();
    command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_info.commandBufferCount = 1;
    result = d.vkAllocateCommandBuffers(*device, &command_info, &frame.command);
    if (result != VK_SUCCESS)
        return fail(Stage::Allocation, result);
    result = device.CreateFence({VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}, frame.fence);
    if (result != VK_SUCCESS)
        return fail(Stage::Allocation, result);
    if (self.windowed) {
        result = device.CreateSemaphore({VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO},
                                        frame.acquire_semaphore);
        if (result == VK_SUCCESS)
            result = device.CreateFence({VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}, frame.acquire_fence);
        if (result != VK_SUCCESS)
            return fail(Stage::Allocation, result);
    }
    if (!frame.samples.is_empty()) {
        auto pass = frame.graph.addPass<UploadPass>(
            "ui.sample_upload"_str, rg::PassNode::Type::Extension,
            [&](rg::RenderGraphBuilder &builder, UploadPass::Desc &desc) {
                for (usize i{}; i < frame.samples.len(); ++i) {
                    auto &page = frame.samples[i];
                    auto name = rstd::format("ui.sample.{}", i.to_primitive());
                    page.texture = builder.createTexture(
                        {name.clone(), name.clone(), rg::TextureKind::Imported},
                        page.upload.is_some());
                    if (page.upload.is_some())
                        builder.write(page.texture);
                    else
                        builder.read(page.texture);
                    desc.pages.push({static_cast<Index>(i.to_primitive()), page.texture,
                                     page.upload.is_some()});
                }
            });
        if (frame.executor.bind<UploadPass>(frame.graph, pass, UploadExecutor{}).is_err())
            return Err(Error{Stage::Graph});
    }
    for (auto i = frame.plan.pass_count(); i > usize(); --i) {
        auto index = static_cast<Index>((i - usize(1)).to_primitive());
        auto name = rstd::format("ui.{}", index);
        auto node = frame.graph.addPass<UiPass>(
            name.as_str(), rg::PassNode::Type::Extension,
            [&](rg::RenderGraphBuilder &builder, UiPass::Desc &desc) {
                desc.index = index;
                desc.target = builder.createTexture(
                    {name.clone(), name.clone(), rg::TextureKind::Temp}, true);
                builder.write(desc.target);
                frame.textures[usize(index)] = desc.target;
                for (const auto &page : frame.samples) {
                    bool used{};
                    for (const auto &item : frame.items[usize(index)])
                        used |= item.kind == page.kind && item.resource_index == page.cache_index;
                    if (used) {
                        builder.read(page.texture);
                        desc.samples.push({page.cache_index, page.texture, page.kind});
                    }
                }
                for (usize i{}; i < frame.externals.len(); ++i) {
                    bool used{};
                    for (const auto &item : frame.items[usize(index)])
                        used |= item.kind == DrawKind::External && usize(item.resource_index) == i;
                    if (!used)
                        continue;
                    auto &external = frame.externals[i];
                    if (!external.texture.valid()) {
                        auto key = rstd::format("ui.external.{}", i.to_primitive());
                        ++frame.stats.used_external_frames;
                        external.texture = builder.createTexture(
                            {key.clone(), key.clone(), rg::TextureKind::Imported}, false);
                    }
                    builder.read(external.texture);
                    desc.samples.push({static_cast<Index>(i.to_primitive()), external.texture,
                                       DrawKind::External});
                }
                for (const auto &draw : frame.items[usize(index)]) {
                    if (draw.kind != DrawKind::Composite)
                        continue;
                    auto source = draw.source_pass;
                    auto texture = frame.textures[usize(source)];
                    builder.read(texture);
                    desc.sources.push({source, texture});
                }
            });
        auto bound = frame.executor.bind<UiPass>(frame.graph, node, UiExecutor{});
        if (bound.is_err())
            return Err(Error{Stage::Graph});
    }
    PrepareContext context{frame, self.device(), *self.descriptor_layout, *self.sampler,
                           *self.glyph_sampler};
    if (frame.executor.prepare(frame.graph, context).is_err())
        return fail(Stage::Graph, frame.error);
    if (options.timings) {
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
    for (auto &external : frame.externals)
        if (!external.texture.valid())
            vk::cancel_texture_read(external.prepared);
    self.frame = Some(rstd::move(candidate));
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
    CpuSample record(frame.metric(&FrameTimings::record));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    auto result = d.vkResetCommandBuffer(frame.command, 0);
    if (result != VK_SUCCESS)
        return Err(Error{Stage::Record, result});
    result = d.vkBeginCommandBuffer(frame.command, &begin);
    if (result != VK_SUCCESS)
        return Err(Error{Stage::Record, result});
    if (frame.queries) {
        frame.reset_queries(frame.command, frame.queries, 0, 2);
        frame.write_timestamp(frame.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.queries, 0);
    }
    RecordContext context{
        frame.command,   d,           *self.render_pass, *self.layout, *self.rectangle,
        *self.composite, *self.glyph, *self.image};
    if (frame.executor.record(context).is_err())
        return Err(Error{Stage::Graph});
    VkImageMemoryBarrier image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    image.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    image.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image.srcQueueFamilyIndex = image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image.image = frame.targets[usize()].image.handle();
    image.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    d.vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &image);
    auto capture_image = [&](VkImage source) {
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {frame.extent.width, frame.extent.height, 1};
        d.vkCmdCopyImageToBuffer(frame.command, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 frame.readback.handle(), 1, &copy);
        VkBufferMemoryBarrier memory{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        memory.srcQueueFamilyIndex = memory.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        memory.buffer = frame.readback.handle();
        memory.size = VK_WHOLE_SIZE;
        d.vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &memory, 0, nullptr);
    };
    if (self.readback_enabled && !self.windowed)
        capture_image(image.image);
    if (self.windowed) {
        record.pause();
        if (!frame.acquired) {
            CpuSample acquire(frame.metric(&FrameTimings::acquire));
            result = d.vkAcquireNextImageKHR(
                *self.device().handle(), *self.device().swapchain().handle(), 0,
                *frame.acquire_semaphore, *frame.acquire_fence, &frame.image_index);
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                if (frame.options.timings && (result == VK_NOT_READY || result == VK_TIMEOUT))
                    ++frame.timings.acquire_unavailable;
                return Err(Error{Stage::Acquire, result});
            }
            frame.acquired = true;
            frame.suboptimal = result == VK_SUBOPTIMAL_KHR;
        }
        auto &sync = self.presents[usize(frame.image_index)];
        if (sync.pending) {
            CpuSample reuse(frame.metric(&FrameTimings::present_reuse));
            result = sync.fence.Wait(0);
            if (result != VK_SUCCESS)
                return Err(Error{Stage::Timeout, result});
            sync.pending = false;
        }
        record.resume();
        VkImageMemoryBarrier destination{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        destination.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        destination.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        destination.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        destination.srcQueueFamilyIndex = destination.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        destination.image = self.device().swapchain().images()[usize(frame.image_index)].handle;
        destination.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        // Chain the layout transition after the acquire semaphore's transfer-stage wait.
        d.vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                               &destination);
        VkImageBlit blit{};
        blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = blit.dstOffsets[1] = {static_cast<int>(frame.extent.width),
                                                   static_cast<int>(frame.extent.height), 1};
        d.vkCmdBlitImage(frame.command, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         destination.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                         VK_FILTER_NEAREST);
        if (self.readback_enabled) {
            destination.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            destination.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            destination.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            destination.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            d.vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                   &destination);
            capture_image(destination.image);
        }
        destination.srcAccessMask =
            self.readback_enabled ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        destination.dstAccessMask = 0;
        destination.oldLayout = self.readback_enabled ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                                      : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        destination.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        d.vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                               &destination);
    }
    if (frame.queries)
        frame.write_timestamp(frame.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.queries,
                              1);
    result = d.vkEndCommandBuffer(frame.command);
    record.pause();
    if (result != VK_SUCCESS)
        return Err(Error{Stage::Record, result});
    VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    info.commandBufferCount = 1;
    info.pCommandBuffers = &frame.command;
    VkSemaphore acquire = *frame.acquire_semaphore;
    VkSemaphore finished =
        self.windowed ? *self.presents[usize(frame.image_index)].semaphore : VK_NULL_HANDLE;
    Vec<VkSemaphore> waits;
    Vec<rstd::uint64_t> values;
    Vec<VkPipelineStageFlags> stages;
    if (self.windowed) {
        waits.push(VkSemaphore(acquire));
        values.push(rstd::uint64_t(0));
        stages.push(VkPipelineStageFlags(VK_PIPELINE_STAGE_TRANSFER_BIT));
    }
    // Preserve a GPU memory dependency when completed allocations are reused.
    if (self.submission) {
        waits.push(VkSemaphore(self.completion_semaphore()));
        values.push(rstd::uint64_t(self.submission));
        stages.push(VkPipelineStageFlags(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
    }
    VkSemaphore signals[]{self.completion_semaphore(), finished};
    rstd::uint64_t signal_values[]{self.submission + 1, 0};
    info.signalSemaphoreCount = self.windowed ? 2 : 1;
    info.pSignalSemaphores = signals;
    for (const auto &external : frame.externals) {
        if (!external.texture.valid())
            continue;
        auto prepared_wait = vrento::vulkan::dependency_wait(
            *self.session, external.prepared.dependency,
            external.prepared.stages);
        if (prepared_wait.is_err()) {
            Error error{Stage::ExternalContent};
            error.content = Some(prepared_wait.unwrap_err());
            return Err(error);
        }
        const auto native = prepared_wait.unwrap();
        if (!native.semaphore)
            continue;
        usize index{};
        while (index < waits.len() && waits[index] != native.semaphore)
            ++index;
        if (index == waits.len()) {
            waits.push(VkSemaphore(native.semaphore));
            values.push(rstd::uint64_t(native.value));
            stages.push(VkPipelineStageFlags(native.stages));
        } else {
            if (values[index] < native.value)
                values[index] = native.value;
            stages[index] |= native.stages;
        }
    }
    info.waitSemaphoreCount = static_cast<unsigned>(waits.len().to_primitive());
    info.pWaitSemaphores = waits.data();
    info.pWaitDstStageMask = stages.data();
    VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    timeline.waitSemaphoreValueCount = info.waitSemaphoreCount;
    timeline.pWaitSemaphoreValues = values.data();
    timeline.signalSemaphoreValueCount = info.signalSemaphoreCount;
    timeline.pSignalSemaphoreValues = signal_values;
    info.pNext = &timeline;
    auto completion = vk::import_dependency(
        *self.session, self.completion_semaphore(), self.submission + 1,
        rstd::sync::Arc<dyn<rstd::any::Any>>::make(
            self.completion_timeline->clone()));
    if (completion.is_err())
        return Err(
            Error{Stage::Submit, VkResult(completion.unwrap_err().native)});
    auto completion_token = completion.unwrap();
    frame.recyclable = false;
    if (frame.queries)
        frame.submitted_at = Some(rstd::time::Instant::now());
    {
        CpuSample submit(frame.metric(&FrameTimings::queue_submit));
        result = self.device().graphics_queue().handle.Submit(info, *frame.fence);
    }
    if (result != VK_SUCCESS) {
        frame.uncertain_submission = result;
        if (result == VK_ERROR_DEVICE_LOST)
            self.state = Impl::State::Lost;
        if (result == VK_ERROR_DEVICE_LOST)
            frame.fail_timestamps(result);
        return Err(
            Error{result == VK_ERROR_DEVICE_LOST ? Stage::Lost : Stage::Submit,
                  result});
    }
    for (const auto &external : frame.externals) {
        if (!external.texture.valid())
            continue;
        auto committed =
            vk::commit_texture_read(external.prepared, completion_token);
        if (committed.is_err())
            rstd::panic("cannot commit submitted texture read");
    }
    for (const auto &page : frame.samples) {
        if (page.upload.is_some() && page.kind == DrawKind::Glyph) {
            self.glyph_cache.pages[usize(page.cache_index)].resident = true;
            ++self.glyph_cache.stats.uploads;
            self.glyph_cache.stats.uploaded_bytes +=
                Serial(page.gpu->extent.width) * page.gpu->extent.height;
        } else if (page.upload.is_some() && page.kind == DrawKind::Image) {
            self.image_cache.entries[usize(page.cache_index)].resident = true;
            ++self.image_cache.stats.uploads;
            self.image_cache.stats.uploaded_bytes +=
                Serial(page.gpu->extent.width) * page.gpu->extent.height * 4;
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
            result = device.Dispatch().vkGetFenceStatus(*device, *frame.fence);
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
            result = frame.fence.Wait(timeout_nanoseconds);
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
    auto memory = frame.readback.allocation();
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
    if (!frame.acquired || frame.presented)
        return Err(Error{Stage::State});
    auto &sync = self.presents[usize(frame.image_index)];
    const auto &device = self.device().handle();
    VkFence fence = *sync.fence;
    auto result = device.Dispatch().vkResetFences(*device, 1, &fence);
    if (result != VK_SUCCESS)
        return Err(Error{Stage::Present, result});
    VkSwapchainPresentFenceInfoEXT finish{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};
    finish.swapchainCount = 1;
    finish.pFences = &fence;
    VkSemaphore semaphore = *sync.semaphore;
    VkSwapchainKHR swapchain = *self.device().swapchain().handle();
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.pNext = &finish;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain;
    info.pImageIndices = &frame.image_index;
    {
        CpuSample present(frame.metric(&FrameTimings::present));
        result = device.Dispatch().vkQueuePresentKHR(*self.device().present_queue().handle, &info);
    }
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
        return Err(Error{Stage::Present, result});
    frame.presented = true;
    sync.pending = true;
    if (result == VK_ERROR_DEVICE_LOST) {
        self.state = Impl::State::Lost;
        frame.fail_timestamps(result);
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
        return Err(Error{Stage::Present, result});
    return Ok(PresentResult{frame.plan.identity(), result == VK_ERROR_OUT_OF_DATE_KHR
                                                       ? PresentStatus::OutOfDate
                                                   : result == VK_SUBOPTIMAL_KHR || frame.suboptimal
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
auto Renderer::resize_impl(VkExtent2D extent, Serial timeout_nanoseconds) -> Result<empty, Error> {
    auto &self = *impl_;
    if (!self.windowed || !self.initialized || self.state == Impl::State::Lost ||
        self.state == Impl::State::Closed)
        return Err(Error{Stage::State});
    auto retired = self.retire(timeout_nanoseconds);
    if (retired.is_err())
        return retired;
    self.frame = None();
    self.presents.clear();
    self.state = Impl::State::Unavailable;
    if (!extent.width || !extent.height)
        return Ok(empty{});
    auto resized = self.session->resize_surface(extent, self.swapchain_options());
    if (resized.is_err())
        return Err(session_error(resized.unwrap_err()));
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
auto Renderer::frame_resource_stats() const -> FrameResourceStats {
    return impl_->frame_resources.stats;
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
    self.frame_resources.clear();
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
    auto &pool = self.frame_resources;
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
    if (self.frame.is_some() && (**self.frame).acquired) {
        if (!(**self.frame).presented)
            return Err(Error{Stage::State});
        auto result = (**self.frame).acquire_fence.Wait(0);
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
auto Renderer::draw_stats() const -> Option<DrawStats> {
    if (impl_->frame.is_none())
        return None();
    return Some(DrawStats((**impl_->frame).stats));
}
auto Renderer::image_cache_stats() const -> ImageCacheStats {
    auto stats = impl_->image_cache.stats;
    if (impl_->frame.is_some()) {
        for (const auto &binding : (**impl_->frame).samples) {
            if (binding.kind != DrawKind::Image)
                continue;
            auto bytes = Serial(binding.gpu->extent.width) * binding.gpu->extent.height * 4;
            if (binding.upload.is_some())
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
    auto stats = impl_->glyph_cache.stats;
    if (impl_->frame.is_some()) {
        for (const auto &binding : (**impl_->frame).samples) {
            if (binding.kind != DrawKind::Glyph)
                continue;
            auto bytes = Serial(binding.gpu->extent.width) * binding.gpu->extent.height;
            if (binding.upload.is_some())
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
