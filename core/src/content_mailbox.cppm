export module vui:content_mailbox;
import :content;
import rstd;
using namespace rstd::prelude;

export namespace vui {
enum class ContentSendError { Busy, Closed, Empty, WrongProducer, ThreadUnsafe };
class ContentMailbox;
class ContentSender {
    struct State {
        rstd::sync::Mutex<empty> mutex;
        Option<TextureFrame> pending;
        Option<FrameWaker> waker;
        Serial source{}, producer{};
        bool closed{};
        State(Serial source, Serial producer, Option<FrameWaker> waker)
            : waker(rstd::move(waker)), source(source), producer(producer) {}
    };
    rstd::sync::Arc<State> state_;
    explicit ContentSender(rstd::sync::Arc<State> state) : state_(rstd::move(state)) {}
    friend class ContentMailbox;
  public:
    ContentSender(const ContentSender &other) : state_(other.state_.clone()) {}
    ContentSender(ContentSender &&) = default;
    auto operator=(ContentSender &&) -> ContentSender & = default;
    // Rejection retains the producer's frame. Successful transfer permits final release on the UI.
    auto try_send(Option<TextureFrame> &frame) const -> Result<empty, ContentSendError> {
        if (frame.is_none()) return Err(ContentSendError::Empty);
        if (frame->info().producer_session != state_->producer) return Err(ContentSendError::WrongProducer);
        if (!frame->concurrent_read()) return Err(ContentSendError::ThreadUnsafe);
        {
            auto lock = state_->mutex.lock().unwrap();
            if (state_->closed) return Err(ContentSendError::Closed);
            if (state_->pending.is_some()) return Err(ContentSendError::Busy);
            state_->pending = frame.take();
        }
        if (state_->waker.is_some()) (void)state_->waker->request();
        return Ok(empty{});
    }
};
// The receiver is UI-owned; producer handles never mutate ContentSource.
class ContentMailbox {
    rstd::sync::Arc<ContentSender::State> state_;
    rstd::thread::ThreadId owner_{rstd::thread::current_id()};
  public:
    explicit ContentMailbox(const ContentSource &source, Option<FrameWaker> waker = {})
        : state_(rstd::sync::Arc<ContentSender::State>::make(source.id(), source.producer_session(), rstd::move(waker))) {}
    ContentMailbox(const ContentMailbox &) = delete;
    ContentMailbox(ContentMailbox &&) = delete;
    ~ContentMailbox() { close(); }
    auto sender() const -> ContentSender { return ContentSender(state_.clone()); }
    auto publish_pending(ContentSource &source) -> Result<bool, ContentError> {
        if (rstd::thread::current_id() != owner_ || source.id() != state_->source)
            return Err(ContentError::InvalidValue);
        Option<TextureFrame> frame;
        {
            auto lock = state_->mutex.lock().unwrap();
            if (state_->closed) return Err(ContentError::Closed);
            frame = state_->pending.take();
        }
        if (frame.is_none()) return Ok(false);
        auto published = source.publish(rstd::move(frame).unwrap());
        if (published.is_err()) return Err(published.unwrap_err());
        return Ok(true);
    }
    void close() {
        // Closing the receiver drains the last payload on its owner before senders can outlive it.
        Option<TextureFrame> pending;
        {
            auto lock = state_->mutex.lock().unwrap();
            state_->closed = true;
            pending = state_->pending.take();
        }
    }
};
} // namespace vui
