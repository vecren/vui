export module vui.render:renderer;
import vui;
import vrento.device_session;
import :content;
import rstd;
using namespace rstd::prelude;

export namespace vui::gpu {
enum class Stage {
    State,
    Instance,
    Device,
    Allocation,
    Shader,
    Pipeline,
    Graph,
    Record,
    Submit,
    Timeout,
    Lost,
    Readback,
    Unsupported,
    Surface,
    Acquire,
    Present,
    TextRaster,
    TextCacheLimit,
    DrawLimit,
    ImageCacheLimit,
    ExternalContent,
    FrameResourceLimit
};
struct Error {
    Stage stage;
    VkResult result{VK_SUCCESS};
    Option<TextError> text{};
    Option<vrento::resource::SampledFrameError> content{};
};
struct Completion {
    Serial submission;
    FrameIdentity frame;
};
struct Pixels {
    Index width{}, height{};
    Vec<unsigned char> rgba;
};
using SurfaceLifetime = vrento::vulkan::SurfaceLifetime;
using NativeSurface = vrento::vulkan::SessionSurface;
class SurfaceHost {
  public:
    virtual ~SurfaceHost() = default;
    virtual auto instance_extensions() -> Result<Vec<vrento::vulkan::Extension>, Error> = 0;
    virtual auto create_surface(VkInstance) -> Result<NativeSurface, Error> = 0;
    virtual auto pixel_extent() const -> VkExtent2D = 0;
};
enum class PresentStatus { Queued, Suboptimal, OutOfDate };
struct PresentResult {
    FrameIdentity frame;
    PresentStatus status{};
};
struct TextCacheLimits {
    Index page_extent{1024}, max_pages{16}, max_entries{65536};
    Serial max_bytes{64 * 1024 * 1024};
};
struct TextCacheStats {
    Serial hits{}, misses{}, rasterizations{}, uploaded_bytes{}, uploads{}, clears{};
    Index pages{}, entries{};
    Serial cpu_bytes{}, gpu_bytes{}, retained_gpu_bytes{}, staging_bytes{};
};
struct ImageCacheLimits {
    Index max_entries{1024};
    Serial max_bytes{64 * 1024 * 1024};
};
struct ImageCacheStats {
    Serial hits{}, misses{}, uploads{}, uploaded_bytes{}, clears{};
    Index entries{};
    Serial cpu_bytes{}, gpu_bytes{}, retained_gpu_bytes{}, staging_bytes{};
};
struct DrawOptions {
    bool batching{true}, culling{true}, timings{false};
};
struct DrawStats {
    FrameIdentity frame;
    Serial items{}, batches{}, merged_items{}, instance_bytes{};
    Serial resolved_items{}, surface_culled{}, clip_culled{}, used_external_frames{};
    Serial pass_starts{}, pipeline_breaks{}, texture_breaks{}, sampler_breaks{}, disabled_breaks{};
};
using CpuTiming = vui::CpuTiming;
enum class GpuTimingStatus { Unsupported, Prepared, Pending, Available, Failed, Ambiguous };
struct FrameTimings {
    FrameIdentity frame;
    Serial submission{};
    CpuTiming prepare, resolve, visibility, record, acquire, present_reuse, queue_submit, poll,
        wait, present;
    Serial acquire_unavailable{}, poll_pending{}, wait_timeouts{};
    GpuTimingStatus gpu_status{GpuTimingStatus::Unsupported};
    Index timestamp_valid_bits{};
    double timestamp_period_nanoseconds{};
    Option<double> gpu_nanoseconds{};
    VkResult query_result{VK_SUCCESS};
};
struct FrameResourceLimits {
    Serial max_target_bytes{512 * 1024 * 1024}, max_buffer_bytes{256 * 1024 * 1024};
    Serial max_cached_bytes{64 * 1024 * 1024};
    Index max_cached_entries{128};
};
struct FrameResourceStats {
    Serial target_allocations{}, target_hits{}, buffer_allocations{}, buffer_hits{};
    Serial evictions{}, budget_failures{}, allocation_failures{};
    Serial target_bytes{}, buffer_bytes{}, cached_target_bytes{}, cached_buffer_bytes{};
    Serial peak_target_bytes{}, peak_buffer_bytes{};
    Index cached_entries{};
};
class Renderer {
    struct Impl;
    Box<Impl> impl_;
    auto initialize_impl(SurfaceHost *, bool validation, bool readback) -> Result<empty, Error>;
    auto clear_frame_resource_cache_impl() -> Result<empty, Error>;
    auto set_frame_resource_limits_impl(FrameResourceLimits limits) -> Result<empty, Error>;
    auto clear_image_cache_impl() -> Result<empty, Error>;
    auto set_image_cache_limits_impl(ImageCacheLimits limits) -> Result<empty, Error>;
    auto clear_text_cache_impl() -> Result<empty, Error>;
    auto set_text_cache_limits_impl(TextCacheLimits limits) -> Result<empty, Error>;
    auto finish_initialize() -> Result<empty, Error>;
    auto initialize_session_impl(vrento::vulkan::DeviceSession, bool) -> Result<empty, Error>;
    auto prepare_impl(FramePlan plan, DrawOptions options) -> Result<empty, Error>;
    auto submit_impl() -> Result<Serial, Error>;
    auto poll_impl() -> Result<Option<Completion>, Error>;
    auto wait_impl(Serial timeout_nanoseconds) -> Result<Completion, Error>;
    auto readback_impl() -> Result<Pixels, Error>;
    auto present_impl() -> Result<PresentResult, Error>;
    auto discard_prepared_impl(Serial timeout_nanoseconds) -> Result<empty, Error>;
    auto resize_impl(VkExtent2D extent, Serial timeout_nanoseconds) -> Result<empty, Error>;
    auto shutdown_impl(Serial timeout_nanoseconds) -> Result<empty, Error>;
    auto release_completed_frame_impl() -> Result<empty, Error>;

  public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer &) = delete;
    auto operator=(const Renderer &) -> Renderer & = delete;
    auto initialize(bool validation = false) -> Result<empty, Error>;
    auto initialize(SurfaceHost &, bool validation = false, bool readback = false)
        -> Result<empty, Error>;
    auto initialize(vrento::vulkan::DeviceSession, bool readback = true) -> Result<empty, Error>;
    auto device_lost() const -> bool;
    auto device_epoch() const -> Serial;
    auto device_session() const -> Result<vrento::vulkan::DeviceSession, Error>;
    auto prepare(FramePlan plan, DrawOptions options = {}) -> Result<empty, Error>;
    auto draw_stats() const -> Option<DrawStats>;
    auto frame_timings() const -> Option<FrameTimings>;
    auto frame_resource_stats() const -> FrameResourceStats;
    auto set_frame_resource_limits(FrameResourceLimits) -> Result<empty, Error>;
    auto clear_frame_resource_cache() -> Result<empty, Error>;
    auto release_completed_frame() -> Result<empty, Error>;
    auto submit() -> Result<Serial, Error>;
    auto poll() -> Result<Option<Completion>, Error>;
    auto wait(Serial timeout_nanoseconds = 1'000'000'000) -> Result<Completion, Error>;
    auto readback() -> Result<Pixels, Error>;
    auto present() -> Result<PresentResult, Error>;
    auto discard_prepared(Serial timeout_nanoseconds = 0) -> Result<empty, Error>;
    auto resize(VkExtent2D, Serial timeout_nanoseconds = 1'000'000'000) -> Result<empty, Error>;
    auto shutdown(Serial timeout_nanoseconds = 1'000'000'000) -> Result<empty, Error>;
    auto pixel_extent() const -> VkExtent2D;
    auto validation_errors() const -> unsigned;
    auto image_cache_stats() const -> ImageCacheStats;
    auto clear_image_cache() -> Result<empty, Error>;
    auto set_image_cache_limits(ImageCacheLimits) -> Result<empty, Error>;
    auto text_cache_stats() const -> TextCacheStats;
    auto clear_text_cache() -> Result<empty, Error>;
    auto set_text_cache_limits(TextCacheLimits) -> Result<empty, Error>;
};
} // namespace vui::gpu
