module vui.render;
import :frame_resources;
using namespace rstd::prelude;

namespace vui::gpu::detail {
auto FrameResources::can_cache(Serial bytes) const -> bool {
    return stats.cached_entries < limits.max_cached_entries &&
           fits(bytes, stats.cached_target_bytes + stats.cached_buffer_bytes,
                limits.max_cached_bytes);
}

void FrameResources::clear_targets() {
    for (const auto &target : targets) {
        stats.target_bytes -= target.bytes();
        stats.cached_target_bytes -= target.bytes();
        --stats.cached_entries;
        ++stats.evictions;
    }
    targets.clear();
}

void FrameResources::clear_buffers() {
    for (const auto &buffer : buffers) {
        stats.buffer_bytes -= buffer.capacity;
        stats.cached_buffer_bytes -= buffer.capacity;
        --stats.cached_entries;
        ++stats.evictions;
    }
    buffers.clear();
}

void FrameResources::release(Target target, bool reusable) {
    if (!target.managed)
        return;
    reusable &= target.image.check().is_ok();
    if (reusable && can_cache(target.bytes())) {
        stats.cached_target_bytes += target.bytes();
        ++stats.cached_entries;
        targets.push(rstd::move(target));
    } else {
        stats.target_bytes -= target.bytes();
        ++stats.evictions;
    }
}

void FrameResources::release(FrameBuffer buffer, bool reusable) {
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

auto FrameResources::target(vk::TextureContext &textures, VkExtent2D extent) -> Result<Target, Error> {
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
    vrento::texture::TextureDesc image;
    image.extent = {extent.width, extent.height, 1};
    image.format = vrento::TextureFormat::RGBA8_SRGB;
    image.usage = vrento::texture::Usage::ColorAttachment | vrento::texture::Usage::Sampled |
                  vrento::texture::Usage::TransferSource;
    auto allocated = textures.CreateTexture(image);
    if (allocated.is_err())
        return Err(allocation_failure(VkResult(allocated.unwrap_err().native)));
    result.image = allocated.unwrap();
    auto view = textures.CreateView(result.image, {});
    if (view.is_err())
        return Err(allocation_failure(VkResult(view.unwrap_err().native)));
    result.view = view.unwrap();
    ++stats.target_allocations;
    stats.target_bytes += bytes;
    if (stats.target_bytes > stats.peak_target_bytes)
        stats.peak_target_bytes = stats.target_bytes;
    return Ok(rstd::move(result));
}

auto FrameResources::buffer(const vk::Device &device, Serial bytes, FrameBufferKind kind)
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
} // namespace vui::gpu::detail
