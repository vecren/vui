module;
#include <cstring>
module vui.render:sampled;
import :renderer;
using namespace rstd::prelude;
namespace vui::gpu::detail {
namespace vk = ::vrento::vulkan;
struct SampledImage {
    vvk::AllocatedImage image;
    vvk::ImageView view;
    VkExtent2D extent;
};
auto make_sampled(const vk::Device &device, VkExtent2D extent, VkFormat format)
    -> Result<rstd::sync::Arc<SampledImage>, Error> {
    auto features = device.gpu().GetFormatProperties(format).optimalTilingFeatures;
    constexpr auto required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                              VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((features & required) != required)
        return Err(Error{Stage::Unsupported});
    SampledImage gpu;
    gpu.extent = extent;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto image = device.memory_allocator().create_image(info);
    if (image.is_err())
        return Err(Error{Stage::Allocation, image.unwrap_err().api_result});
    gpu.image = image.unwrap();
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = gpu.image.handle();
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto result = device.handle().CreateImageView(view, gpu.view);
    if (result != VK_SUCCESS)
        return Err(Error{Stage::Allocation, result});
    return Ok(rstd::sync::Arc<SampledImage>::make(rstd::move(gpu)));
}
auto make_upload(const vk::Device &device, slice<unsigned char> pixels)
    -> Result<vvk::AllocatedBuffer, Error> {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = pixels.len().to_primitive();
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    auto allocation = device.memory_allocator().create_buffer(info, vvk::MemoryRequest::Upload());
    if (allocation.is_err())
        return Err(Error{Stage::Allocation, allocation.unwrap_err().api_result});
    auto buffer = allocation.unwrap();
    auto memory = buffer.allocation();
    auto mapped = memory.map();
    if (mapped.is_err())
        return Err(Error{Stage::Allocation, mapped.unwrap_err().api_result});
    auto mapping = mapped.unwrap();
    std::memcpy(mapping.data(), pixels.as_raw_ptr(), info.size);
    auto flushed = memory.flush();
    if (flushed.is_err())
        return Err(Error{Stage::Allocation, flushed.unwrap_err().api_result});
    return Ok(rstd::move(buffer));
}

} // namespace vui::gpu::detail
