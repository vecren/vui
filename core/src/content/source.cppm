export module vui:content.source;
export import :image.types;
import :metadata;
import :runtime.wakeup;
using namespace rstd::prelude;

export namespace vui {
enum class ContentColorSpace { LinearSrgb, Srgb };
enum class ContentAlpha { Opaque, Premultiplied, Straight };
enum class ContentOrigin { TopLeft, BottomLeft };
enum class ContentFormat { Rgba8Unorm, Rgba8Srgb, Bgra8Unorm, Bgra8Srgb, Rgba16Float, R11G11B10Float };
enum class ContentError { InvalidValue, WrongProducer, StaleFrame, Closed, Limit, Busy };
struct TextureFrameInfo {
    Serial producer_session{}, frame_version{}, device_epoch{};
    Index width{}, height{};
    ContentFormat format{ContentFormat::Rgba8Unorm};
    ContentColorSpace color_space{ContentColorSpace::LinearSrgb};
    ContentAlpha alpha{ContentAlpha::Premultiplied};
    ContentOrigin origin{ContentOrigin::TopLeft};
    auto valid() const -> bool {
        return producer_session && frame_version && device_epoch && width && height &&
               (color_space == ContentColorSpace::LinearSrgb ||
                color_space == ContentColorSpace::Srgb) &&
               (alpha == ContentAlpha::Opaque || alpha == ContentAlpha::Premultiplied ||
                alpha == ContentAlpha::Straight) &&
               (format == ContentFormat::Rgba8Unorm || format == ContentFormat::Rgba8Srgb ||
                format == ContentFormat::Bgra8Unorm || format == ContentFormat::Bgra8Srgb ||
                format == ContentFormat::Rgba16Float || format == ContentFormat::R11G11B10Float) &&
               (format != ContentFormat::R11G11B10Float || alpha == ContentAlpha::Opaque) &&
               ((format != ContentFormat::Rgba8Srgb && format != ContentFormat::Bgra8Srgb) ||
                color_space == ContentColorSpace::Srgb) &&
               (origin == ContentOrigin::TopLeft || origin == ContentOrigin::BottomLeft);
    }
    friend auto operator==(const TextureFrameInfo &, const TextureFrameInfo &) -> bool = default;
};
struct ScenePlacement {
    TextureFrameInfo frame;
    ImagePlacement image;
    auto content_position(Point local, ContentBounds bounds = ContentBounds::RequireInside) const -> Option<Point> {
        return image.content_position(local,bounds);
    }
};
class TextureFrame {
    struct Data {
        TextureFrameInfo info;
        detail::FramePayload payload;
        bool concurrent_read{};
        Data(TextureFrameInfo i, detail::FramePayload p, bool shared) : info(i), payload(rstd::move(p)), concurrent_read(shared) {}
    };
    rstd::sync::Arc<Data> data_;
    Option<detail::FrameMetadata> metadata_;
    TextureFrame(TextureFrameInfo info, detail::FramePayload payload, bool shared)
        : data_(rstd::sync::Arc<Data>::make(info, rstd::move(payload), shared)) {}

  public:
    TextureFrame(const TextureFrame &other) : data_(other.data_.clone()) {
        if (other.metadata_) metadata_ = Some(other.metadata_->clone());
    }
    auto operator=(const TextureFrame &other) -> TextureFrame & {
        if (this != &other) {
            data_ = other.data_.clone();
            metadata_ = other.metadata_ ? Some(other.metadata_->clone()) : Option<detail::FrameMetadata>{};
        }
        return *this;
    }
    TextureFrame(TextureFrame &&) = default;
    auto operator=(TextureFrame &&) -> TextureFrame & = default;
    template <class T>
    // Shared payloads permit concurrent const access; the UI retains the final frame reference.
    static auto make(TextureFrameInfo info, T payload, bool concurrent_read = false) -> Result<TextureFrame, ContentError> {
        if (!info.valid())
            return Err(ContentError::InvalidValue);
        return Ok(TextureFrame(info,detail::FramePayload::make(rstd::move(payload)),concurrent_read));
    }
    auto concurrent_read() const -> bool { return data_->concurrent_read && (!metadata_ || metadata_->concurrent_read()); }
    auto valid() const -> bool { return bool(data_); }
    auto info() const -> TextureFrameInfo { return data_->info; }
    template <class T> auto payload() const -> const T * {
        return valid() ? data_->payload.get<T>():nullptr;
    }
    template <class T> auto with_metadata(T value, bool concurrent_read = false) const -> TextureFrame {
        auto result = TextureFrame(*this);
        result.metadata_ = Some(detail::FrameMetadata::make(rstd::move(value),concurrent_read));
        return result;
    }
    template <class T> auto metadata() const -> const T * {
        return metadata_ ? metadata_->get<T>():nullptr;
    }
};
class ContentSubscription {
    friend class ContentSource;
    rstd::sync::Arc<FrameWaker> listener_;
    explicit ContentSubscription(rstd::sync::Arc<FrameWaker> value)
        : listener_(rstd::move(value)) {}

  public:
    ContentSubscription(const ContentSubscription &) = delete;
    auto operator=(const ContentSubscription &) -> ContentSubscription & = delete;
    ContentSubscription(ContentSubscription &&) = default;
    auto operator=(ContentSubscription &&) -> ContentSubscription & = default;
};
class ContentSource {
    struct State {
        Serial id{new_session()}, producer, revision{1}, last_version{};
        bool closed{};
        Option<TextureFrame> latest;
        Vec<rstd::sync::Weak<FrameWaker>> listeners;
        Serial wake_failures{};
        explicit State(Serial p) : producer(p) {}
        void notify() {
            listeners.retain([&](const auto &weak) {
                auto listener = weak.upgrade();
                if (!listener)
                    return false;
                if (listener->request() == FrameWakeResult::Failed && wake_failures != ~Serial{})
                    ++wake_failures;
                return true;
            });
        }
    };
    rstd::sync::Arc<State> state_;
    explicit ContentSource(Serial producer) : state_(rstd::sync::Arc<State>::make(producer)) {}

  public:
    ContentSource(const ContentSource &other) : state_(other.state_.clone()) {}
    auto operator=(const ContentSource &other) -> ContentSource & {
        if (this != &other)
            state_ = other.state_.clone();
        return *this;
    }
    ContentSource(ContentSource &&) = default;
    auto operator=(ContentSource &&) -> ContentSource & = default;
    static auto create(Serial producer_session) -> Result<ContentSource, ContentError> {
        if (!producer_session)
            return Err(ContentError::InvalidValue);
        return Ok(ContentSource(producer_session));
    }
    auto valid() const -> bool { return bool(state_); }
    auto id() const -> Serial { return state_->id; }
    auto producer_session() const -> Serial { return state_->producer; }
    auto revision() const -> Serial { return state_->revision; }
    auto closed() const -> bool { return state_->closed; }
    auto wake_failures() const -> Serial { return state_->wake_failures; }
    // Source access and subscription lifetime are serialized by the source owner.
    auto subscribe(FrameWaker waker) const -> Result<ContentSubscription, ContentError> {
        if (!valid() || !waker.valid())
            return Err(ContentError::InvalidValue);
        if (closed())
            return Err(ContentError::Closed);
        state_->listeners.retain([](const auto &weak) { return !weak.expired(); });
        auto listener = rstd::sync::Arc<FrameWaker>::make(rstd::move(waker));
        state_->listeners.push(listener.downgrade());
        return Ok(ContentSubscription(rstd::move(listener)));
    }
    auto latest() const -> Option<TextureFrame> {
        return state_->latest.is_some() ? Some(TextureFrame(*state_->latest)) : None();
    }
    auto publish(TextureFrame frame) -> Result<empty, ContentError> {
        if (!valid() || !frame.valid())
            return Err(ContentError::InvalidValue);
        if (state_->closed)
            return Err(ContentError::Closed);
        if (frame.info().producer_session != state_->producer)
            return Err(ContentError::WrongProducer);
        if (frame.info().frame_version <= state_->last_version)
            return Err(ContentError::StaleFrame);
        if (state_->revision == ~Serial{})
            return Err(ContentError::Limit);
        state_->last_version = frame.info().frame_version;
        state_->latest = Some(rstd::move(frame));
        ++state_->revision;
        state_->notify();
        return Ok(empty{});
    }
    auto clear() -> Result<empty, ContentError> {
        if (!valid())
            return Err(ContentError::InvalidValue);
        if (state_->closed)
            return Err(ContentError::Closed);
        if (state_->latest.is_none())
            return Ok(empty{});
        if (state_->revision == ~Serial{})
            return Err(ContentError::Limit);
        state_->latest = None();
        ++state_->revision;
        state_->notify();
        return Ok(empty{});
    }
    auto close() -> Result<empty, ContentError> {
        if (!valid())
            return Err(ContentError::InvalidValue);
        if (state_->closed)
            return Ok(empty{});
        if (state_->revision == ~Serial{})
            return Err(ContentError::Limit);
        state_->latest = None();
        state_->closed = true;
        ++state_->revision;
        state_->notify();
        state_->listeners.clear();
        return Ok(empty{});
    }
};
} // namespace vui
