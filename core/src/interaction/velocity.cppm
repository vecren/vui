export module vui:interaction.velocity;
import :interaction.arena;
using namespace rstd::prelude;

export namespace vui {
class VelocityTracker {
    struct Track {
        PointerKey key;
        SequenceId sequence;
        Vec<PointerSample> samples;
        bool active{true};
    };
    Vec<Track> tracks_;
    Serial window_ns_{100'000'000};
    Serial rejected_{};
    auto track(PointerKey key) -> Track * {
        for (auto &value : tracks_)
            if (value.key == key)
                return &value;
        return nullptr;
    }
    void append(Track &track, PointerSample sample) {
        if (!sample.time.clock || !finite(sample.position.x) || !finite(sample.position.y)) {
            ++rejected_;
            return;
        }
        if (!track.samples.is_empty()) {
            const auto &last = track.samples[track.samples.len() - usize(1)];
            if (sample.time.clock != last.time.clock ||
                sample.time.nanoseconds < last.time.nanoseconds) {
                ++rejected_;
                return;
            }
            if (sample.time.nanoseconds == last.time.nanoseconds) {
                track.samples[track.samples.len() - usize(1)] = sample;
                return;
            }
        }
        track.samples.retain([&](const PointerSample &value) {
            return sample.time.nanoseconds - value.time.nanoseconds <= window_ns_;
        });
        if (track.samples.len() >= usize(128))
            track.samples.remove(usize());
        track.samples.push(rstd::move(sample));
    }

  public:
    explicit VelocityTracker(Serial window_ns = 100'000'000) : window_ns_(window_ns) {}
    void add(const InputEvent &event) {
        PointerKey key{event.device, event.pointer};
        auto value = track(key);
        if (!value) {
            if (tracks_.len() >= usize(32)) {
                for (usize i{}; i < tracks_.len(); ++i)
                    if (!tracks_[i].active) {
                        tracks_.remove(i);
                        break;
                    }
                if (tracks_.len() >= usize(32)) {
                    ++rejected_;
                    return;
                }
            }
            tracks_.push({key, event.sequence, {}});
            value = track(key);
        }
        if (value->sequence != event.sequence || event.type == EventType::PointerDown) {
            value->samples.clear();
            value->sequence = event.sequence;
        }
        for (const auto &sample : event.history)
            append(*value, PointerSample(sample));
        append(*value, {event.sample_time, event.position, event.pressure});
        value->active =
            event.type != EventType::PointerUp && event.type != EventType::PointerCancel;
    }
    auto velocity(PointerKey key) const -> Option<Point> {
        for (const auto &value : tracks_)
            if (value.key == key && value.samples.len() >= usize(2)) {
                const auto &first = value.samples[usize()];
                Scalar sum_t{}, sum_tt{}, sum_x{}, sum_y{}, sum_tx{}, sum_ty{};
                auto count = Scalar(value.samples.len().to_primitive());
                for (const auto &sample : value.samples) {
                    auto t =
                        Scalar(sample.time.nanoseconds - first.time.nanoseconds) / 1'000'000'000;
                    auto x = sample.position.x - first.position.x,
                         y = sample.position.y - first.position.y;
                    sum_t += t;
                    sum_tt += t * t;
                    sum_x += x;
                    sum_y += y;
                    sum_tx += t * x;
                    sum_ty += t * y;
                }
                auto denominator = count * sum_tt - sum_t * sum_t;
                if (denominator <= 0)
                    return None();
                return Some(Point{(count * sum_tx - sum_t * sum_x) / denominator,
                                  (count * sum_ty - sum_t * sum_y) / denominator});
            }
        return None();
    }
    auto rejected_samples() const -> Serial { return rejected_; }
    void remove(PointerKey key) {
        tracks_.retain([&](const Track &value) { return value.key != key; });
    }
};
enum class PanAxis { Free, Horizontal, Vertical };
struct PanOptions {
    Scalar threshold{8};
    PanAxis axis{PanAxis::Free};
    int priority{};
};
class PanRecognizer {
    PanOptions options_;
    struct Start {
        PointerKey key;
        Point position;
    };
    Vec<Start> starts_;

  public:
    explicit PanRecognizer(PanOptions options = {}) : options_(options) {}
    auto observe(InputContext &, const InputPacket &packet) -> InputDecision {
        PointerKey key{packet.input.device, packet.input.pointer};
        if (packet.input.type == EventType::PointerDown) {
            starts_.retain([&](const Start &start) { return start.key != key; });
            if (starts_.len() >= usize(32))
                return InputDecision::reject();
            starts_.push({key, packet.input.position});
            return {};
        }
        for (const auto &start : starts_)
            if (start.key == key) {
                auto dx = __builtin_fabs(packet.input.position.x - start.position.x);
                auto dy = __builtin_fabs(packet.input.position.y - start.position.y);
                bool enough = options_.axis == PanAxis::Free
                                  ? dx * dx + dy * dy >= options_.threshold * options_.threshold
                              : options_.axis == PanAxis::Horizontal
                                  ? dx >= options_.threshold && dx >= dy
                                  : dy >= options_.threshold && dy >= dx;
                if (packet.input.type == EventType::PointerMove && enough)
                    return InputDecision::accept(options_.priority);
            }
        if (packet.input.type == EventType::PointerUp ||
            packet.input.type == EventType::PointerCancel)
            starts_.retain([&](const Start &start) { return start.key != key; });
        return {};
    }
    void cancel(InputContext &context, CancelReason) {
        auto key = context.pointer();
        starts_.retain([&](const Start &start) { return start.key != key; });
    }
};
enum class DeltaSource { Pointer, Wheel, User };
struct DeltaOffer {
    Point original, remaining;
    ScrollUnit unit{ScrollUnit::Pixels};
    SequenceId sequence{};
    DeltaSource source{DeltaSource::User};
    // An invalid node identifies surface coordinates.
    UiId coordinate_space;
    explicit DeltaOffer(Point amount, ScrollUnit units = ScrollUnit::Pixels,
                        SequenceId sequence_id = 0, DeltaSource origin = DeltaSource::User,
                        UiId coordinates = {})
        : original(amount), remaining(amount), unit(units), sequence(sequence_id), source(origin),
          coordinate_space(coordinates) {}
    auto consume(Point amount) -> Result<empty, InputError> {
        auto valid = [](Scalar value, Scalar remaining) {
            return finite(value) && (remaining >= 0 ? value >= 0 && value <= remaining
                                                    : value <= 0 && value >= remaining);
        };
        if (!valid(amount.x, remaining.x) || !valid(amount.y, remaining.y))
            return Err(InputError::InvalidEvent);
        remaining.x -= amount.x;
        remaining.y -= amount.y;
        return Ok(empty{});
    }
};
} // namespace vui
