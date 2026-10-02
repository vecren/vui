module vui.render:frame_resources;
import :renderer;
import :data;
using namespace rstd::prelude;
namespace vui::gpu::detail {
namespace vk = ::vrento::vulkan;
struct Target {
    vvk::AllocatedImage image;
    vvk::ImageView view;
    vvk::Framebuffer framebuffer;
    VkExtent2D extent{};
    auto bytes() const -> Serial { return Serial(extent.width) * extent.height * 4; }
};
enum class FrameBufferKind { Storage, Readback };
struct FrameBuffer {
    vvk::AllocatedBuffer buffer;
    Serial capacity{};
    FrameBufferKind kind{FrameBufferKind::Storage};
    auto handle() const { return buffer.handle(); }
    auto allocation() const { return buffer.allocation(); }
};
struct FrameResources {
    FrameResourceLimits limits;
    FrameResourceStats stats;
    Vec<Target> targets;
    Vec<FrameBuffer> buffers;

    template <class T> static auto take(Vec<T> &values, usize index) -> T {
        auto result = rstd::move(values[index]);
        auto last = values.pop().unwrap();
        if (index < values.len())
            values[index] = rstd::move(last);
        return result;
    }
    static auto fits(Serial bytes, Serial used, Serial limit) -> bool {
        return bytes <= limit && used <= limit - bytes;
    }
    auto can_cache(Serial bytes) const -> bool {
        return stats.cached_entries < limits.max_cached_entries &&
               fits(bytes, stats.cached_target_bytes + stats.cached_buffer_bytes,
                    limits.max_cached_bytes);
    }
    void clear_targets() {
        for (const auto &target : targets) {
            stats.target_bytes -= target.bytes();
            stats.cached_target_bytes -= target.bytes();
            --stats.cached_entries;
            ++stats.evictions;
        }
        targets.clear();
    }
    void clear_buffers() {
        for (const auto &buffer : buffers) {
            stats.buffer_bytes -= buffer.capacity;
            stats.cached_buffer_bytes -= buffer.capacity;
            --stats.cached_entries;
            ++stats.evictions;
        }
        buffers.clear();
    }
    void clear() {
        clear_targets();
        clear_buffers();
    }
    void release(Target target, bool reusable) {
        if (reusable && can_cache(target.bytes())) {
            stats.cached_target_bytes += target.bytes();
            ++stats.cached_entries;
            targets.push(rstd::move(target));
        } else {
            stats.target_bytes -= target.bytes();
            ++stats.evictions;
        }
    }
    void release(FrameBuffer buffer, bool reusable) {
        if (!buffer.capacity)
            return;
        if (reusable && can_cache(buffer.capacity)) {
            stats.cached_buffer_bytes += buffer.capacity;
            ++stats.cached_entries;
            buffers.push(rstd::move(buffer));
        } else {
            stats.buffer_bytes -= buffer.capacity;
            ++stats.evictions;
        }
    }
    auto budget_failure() -> Error {
        ++stats.budget_failures;
        return Error{Stage::FrameResourceLimit};
    }
    auto allocation_failure(VkResult result) -> Error {
        ++stats.allocation_failures;
        return Error{Stage::Allocation, result};
    }
    auto target(const vk::Device &device, VkRenderPass pass, VkExtent2D extent)
        -> Result<Target, Error> {
        for (usize i{}; i < targets.len(); ++i) {
            if (targets[i].extent.width != extent.width ||
                targets[i].extent.height != extent.height)
                continue;
            auto result = take(targets, i);
            stats.cached_target_bytes -= result.bytes();
            --stats.cached_entries;
            ++stats.target_hits;
            return Ok(rstd::move(result));
        }
        auto bytes = Serial(extent.width) * extent.height * 4;
        if (!fits(bytes, stats.target_bytes, limits.max_target_bytes))
            clear_targets();
        if (!fits(bytes, stats.target_bytes, limits.max_target_bytes))
            return Err(budget_failure());
        Target result;
        result.extent = extent;
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = format;
        image.extent = {extent.width, extent.height, 1};
        image.mipLevels = image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        auto allocated = device.memory_allocator().create_image(image);
        if (allocated.is_err())
            return Err(allocation_failure(allocated.unwrap_err().api_result));
        result.image = allocated.unwrap();
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = result.image.handle();
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        auto status = device.handle().CreateImageView(view, result.view);
        if (status != VK_SUCCESS)
            return Err(allocation_failure(status));
        auto attachment = *result.view;
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = pass;
        framebuffer.attachmentCount = 1;
        framebuffer.pAttachments = &attachment;
        framebuffer.width = extent.width;
        framebuffer.height = extent.height;
        framebuffer.layers = 1;
        status = device.handle().CreateFramebuffer(framebuffer, result.framebuffer);
        if (status != VK_SUCCESS)
            return Err(allocation_failure(status));
        ++stats.target_allocations;
        stats.target_bytes += bytes;
        if (stats.target_bytes > stats.peak_target_bytes)
            stats.peak_target_bytes = stats.target_bytes;
        return Ok(rstd::move(result));
    }
    auto buffer(const vk::Device &device, Serial bytes, FrameBufferKind kind)
        -> Result<FrameBuffer, Error> {
        auto best = buffers.len();
        for (usize i{}; i < buffers.len(); ++i)
            if (buffers[i].kind == kind && buffers[i].capacity >= bytes &&
                (best == buffers.len() || buffers[i].capacity < buffers[best].capacity))
                best = i;
        if (best != buffers.len()) {
            auto result = take(buffers, best);
            stats.cached_buffer_bytes -= result.capacity;
            --stats.cached_entries;
            ++stats.buffer_hits;
            return Ok(rstd::move(result));
        }
        Serial capacity = 256;
        while (capacity < bytes)
            capacity *= 2;
        if (kind == FrameBufferKind::Storage && capacity > device.limits().maxStorageBufferRange)
            capacity = bytes;
        if (!fits(capacity, stats.buffer_bytes, limits.max_buffer_bytes))
            clear_buffers();
        if (!fits(capacity, stats.buffer_bytes, limits.max_buffer_bytes))
            return Err(budget_failure());
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = capacity;
        info.usage = kind == FrameBufferKind::Storage ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                                      : VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        auto allocation = device.memory_allocator().create_buffer(
            info, kind == FrameBufferKind::Storage ? vvk::MemoryRequest::Upload()
                                                   : vvk::MemoryRequest::Readback());
        if (allocation.is_err())
            return Err(allocation_failure(allocation.unwrap_err().api_result));
        FrameBuffer result{allocation.unwrap(), capacity, kind};
        ++stats.buffer_allocations;
        stats.buffer_bytes += capacity;
        if (stats.buffer_bytes > stats.peak_buffer_bytes)
            stats.peak_buffer_bytes = stats.buffer_bytes;
        return Ok(rstd::move(result));
    }
};

} // namespace vui::gpu::detail
