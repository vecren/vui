module vui.render:frame_resources;
import :renderer;
using namespace rstd::prelude;
namespace vui::gpu::detail {
namespace vk = ::vrento::vulkan;
struct Target {
    vrento::texture::TextureRef image;
    vrento::texture::TextureViewRef view;
    VkExtent2D extent{};
    bool managed{true};
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
    auto can_cache(Serial bytes) const -> bool;
    void clear_targets();
    void clear_buffers();
    void clear() {
        clear_targets();
        clear_buffers();
    }
    void release(Target target, bool reusable);
    void release(FrameBuffer buffer, bool reusable);
    auto budget_failure() -> Error {
        ++stats.budget_failures;
        return Error{Stage::FrameResourceLimit};
    }
    auto allocation_failure(VkResult result) -> Error {
        ++stats.allocation_failures;
        return Error{Stage::Allocation, result};
    }
    auto target(vk::TextureContext &textures, VkExtent2D extent,
                vrento::TextureFormat format = vrento::TextureFormat::RGBA8_SRGB)
        -> Result<Target, Error>;
    auto buffer(const vk::Device &device, Serial bytes, FrameBufferKind kind)
        -> Result<FrameBuffer, Error>;
};

} // namespace vui::gpu::detail
