export module vui:interaction.arena;
import :document.interface;
import :input.protocol;
import :input.scopes;
using namespace rstd::prelude;

export namespace vui {
struct PointerKey {
    DeviceId device{};
    Index pointer{};
    friend auto operator==(const PointerKey &, const PointerKey &) -> bool = default;
};
struct PointerContact {
    PointerKey key;
    SequenceId sequence{};
    Point position;
    PointerKind kind{};
    bool active{};
};
using ParticipantId = Identity<struct ParticipantTag>;
enum class InputDecisionState { Pending, Accept, Reject };
enum class ClaimMode { Exclusive, Shared };
struct InputDecision {
    InputDecisionState state{InputDecisionState::Pending};
    ClaimMode mode{ClaimMode::Exclusive};
    Serial group{};
    int priority{};
    Vec<PointerKey> pointers;
    static auto accept(int priority = 0) -> InputDecision {
        return {InputDecisionState::Accept, ClaimMode::Exclusive, 0, priority};
    }
    static auto reject() -> InputDecision { return {InputDecisionState::Reject}; }
};
class InputContext {
    const InputSnapshot &snapshot_;
    slice<PointerContact> contacts_;
    PointerKey pointer_;

  public:
    InputContext(const InputSnapshot &snapshot, slice<PointerContact> contacts, PointerKey pointer)
        : snapshot_(snapshot), contacts_(contacts), pointer_(pointer) {}
    auto snapshot() const -> const InputSnapshot & { return snapshot_; }
    auto contacts() const -> slice<PointerContact> { return contacts_; }
    auto pointer() const -> PointerKey { return pointer_; }
};
struct InputParticipant {
    template <class Self, class Delegate = void> struct Api {
        using Trait = InputParticipant;
        auto observe(InputContext &context, const InputPacket &packet) -> InputDecision {
            return rstd::trait_call<0>(this, context, packet);
        }
        void cancel(InputContext &context, CancelReason reason) {
            rstd::trait_call<1>(this, context, reason);
        }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::observe, &T::cancel>;
};
struct ClaimRequest {
    ParticipantId participant;
    UiId target;
    ClaimMode mode{};
    Serial group{};
    int priority{};
    Vec<PointerKey> pointers;
};
struct ClaimResolution {
    Vec<ParticipantId> winners;
};
struct ArbitrationPolicy {
    template <class Self, class Delegate = void> struct Api {
        using Trait = ArbitrationPolicy;
        auto resolve(InputContext &context, slice<ClaimRequest> requests)
            -> Result<ClaimResolution, InputError> {
            return rstd::trait_call<0>(this, context, requests);
        }
    };
    template <class T> using Funcs = rstd::TraitFuncs<&T::resolve>;
};
struct PriorityArbitration {
    auto resolve(InputContext &context, slice<ClaimRequest> requests)
        -> Result<ClaimResolution, InputError> {
        ClaimResolution result;
        if (requests.is_empty())
            return Ok(rstd::move(result));
        usize chosen{};
        for (usize i{1}; i < requests.len(); ++i)
            if (requests[i].priority > requests[chosen].priority ||
                (requests[i].priority==requests[chosen].priority && requests[i].target!=requests[chosen].target &&
                 input_descendant(context.snapshot(),requests[chosen].target,requests[i].target)))
                chosen = i;
        const auto &winner = requests[chosen];
        result.winners.push(ParticipantId(winner.participant));
        if (winner.mode == ClaimMode::Shared && winner.group)
            for (usize i{}; i < requests.len(); ++i)
                if (i != chosen && requests[i].mode == ClaimMode::Shared &&
                    requests[i].group == winner.group)
                    result.winners.push(ParticipantId(requests[i].participant));
        return Ok(rstd::move(result));
    }
};
struct ParticipantRegistration {
    ParticipantId id;
    Binding binding;
};
struct OwnershipChange {
    PointerKey pointer;
    UiId target;
    bool additional{};
    Vec<UiId> previous;
};
class InputArena {
    struct Entry {
        UiId target;
        BindingGuard guard;
        Box<rstd::dyn<InputParticipant>> participant;
        bool global{};
    };
    struct Slot {
        Serial generation{1};
        Option<Entry> entry;
    };
    struct Candidate {
        ParticipantId id;
        InputDecisionState state{InputDecisionState::Pending};
    };
    struct Stream {
        PointerKey key;
        SequenceId sequence;
        UiId original;
        Vec<Candidate> candidates;
        Vec<ParticipantId> owners;
        ClaimMode mode{ClaimMode::Exclusive};
        Serial group{};
    };
    UiDocument &document_;
    Serial owner_{new_session()};
    Vec<Slot> slots_;
    Vec<Stream> streams_;
    Box<rstd::dyn<ArbitrationPolicy>> policy_{
        Box<rstd::dyn<ArbitrationPolicy>>::make(PriorityArbitration{})};
    bool busy_{};
    Option<UiId> scope_root_;
    InputWork work_;
    auto entry(ParticipantId id) -> Entry * {
        if (id.session != owner_ || usize(id.slot) >= slots_.len())
            return nullptr;
        auto &slot = slots_[usize(id.slot)];
        return slot.generation == id.generation && slot.entry.is_some() ? &*slot.entry : nullptr;
    }
    auto stream(PointerKey key) -> Stream * {
        for (auto &value : streams_)
            if (value.key == key)
                return &value;
        return nullptr;
    }
    static auto ancestor(const InputSnapshot &snapshot, UiId target, UiId leaf) -> bool {
        auto cursor = Some(leaf);
        for (unsigned depth{}; cursor.is_some() && depth < 256; ++depth) {
            if (*cursor == target)
                return true;
            auto node = snapshot.get(*cursor);
            if (!node)
                return false;
            cursor = node->parent;
        }
        return false;
    }
    auto eligible(const InputSnapshot &snapshot, UiId target) -> bool {
        if (scope_root_.is_some() && !input_descendant(snapshot, *scope_root_, target))
            return false;
        if (!input_current_mount(document_, snapshot, target))
            return false;
        auto cursor = Some(target);
        for (unsigned depth{}; cursor.is_some() && depth < 256; ++depth) {
            auto visual = snapshot.get(*cursor);
            if (!visual || !visual->enabled || !visual->hit_test_visible ||
                !visual->properties.visible || visual->properties.transform.inverse().is_none())
                return false;
            if (cursor == snapshot.root())
                return true;
            cursor = visual->parent;
        }
        return false;
    }
    void cancel_candidate(Candidate &candidate, InputContext &context, CancelReason reason) {
        if (candidate.state == InputDecisionState::Reject)
            return;
        candidate.state = InputDecisionState::Reject;
        auto value = entry(candidate.id);
        if (value && value->guard.alive())
            value->participant->cancel(context, reason);
    }
    template <class Match>
    auto cancel_matching(const InputSnapshot &previous, slice<PointerContact> contacts, Match match,
                         CancelReason reason = CancelReason::Detached)
        -> Result<empty, InputError> {
        if (busy_) return Err(InputError::Reentrant);
        busy_ = true;
        struct Guard { bool &busy; ~Guard() { busy = false; } } guard{busy_};
        for (auto &stream : streams_) {
            InputContext context(previous, contacts, stream.key);
            for (auto &candidate : stream.candidates) {
                auto value = entry(candidate.id);
                if (!value || !value->guard.alive()) cancel_candidate(candidate, context, CancelReason::ReceiverLost);
                else if (match(value->target)) cancel_candidate(candidate, context, reason);
            }
        }
        return Ok(empty{});
    }

  public:
    explicit InputArena(UiDocument &document) : document_(document) {}
    void set_scope_root(Option<UiId> root) { scope_root_ = root; }
    auto work() const -> InputWork { return work_; }
    auto has_stream(PointerKey key) -> bool { return stream(key) != nullptr; }
    template <class T>
        requires rstd::Impled<T, InputParticipant>
    auto bind(UiId target, rstd::rc::Weak<T> receiver, bool global = false)
        -> Result<ParticipantRegistration, InputError> {
        if (busy_)
            return Err(InputError::Reentrant);
        if (!document_.valid(target))
            return Err(InputError::InvalidId);
        usize index{};
        while (index < slots_.len() && slots_[index].entry.is_some()) {
            if (!slots_[index].entry->guard.alive()) {
                slots_[index].entry = None();
                ++slots_[index].generation;
                break;
            }
            ++index;
        }
        if (index >= usize(4096))
            return Err(InputError::Limit);
        if (index == slots_.len())
            slots_.push(Slot{});
        auto binding = document_.bindings().make(receiver);
        struct Adapter {
            rstd::rc::Weak<T> receiver;
            BindingGuard guard;
            auto observe(InputContext &context, const InputPacket &packet) -> InputDecision {
                auto value = receiver.upgrade();
                if (!guard.alive() || value.is_none())
                    return InputDecision::reject();
                return rstd::as<InputParticipant>(*value->get()).observe(context, packet);
            }
            void cancel(InputContext &context, CancelReason reason) {
                auto value = receiver.upgrade();
                if (guard.alive() && value.is_some())
                    rstd::as<InputParticipant>(*value->get()).cancel(context, reason);
            }
        };
        slots_[index].entry = Some(Entry{
            target, binding.guard(),
            Box<rstd::dyn<InputParticipant>>::make(Adapter{rstd::move(receiver), binding.guard()}),
            global});
        return Ok(ParticipantRegistration{
            {owner_, static_cast<Index>(index.to_primitive()), slots_[index].generation},
            rstd::move(binding)});
    }
    template <class T>
        requires rstd::Impled<T, ArbitrationPolicy>
    auto set_policy(T policy) -> Result<empty, InputError> {
        if (busy_)
            return Err(InputError::Reentrant);
        policy_ = Box<rstd::dyn<ArbitrationPolicy>>::make(rstd::move(policy));
        return Ok(empty{});
    }
    void owners(PointerKey key, Vec<UiId> &result) {
        result.clear();
        auto state = stream(key);
        if (state)
            for (auto id : state->owners) {
                auto value = entry(id);
                if (value && value->guard.alive()) {
                    bool duplicate{};
                    for (auto target : result)
                        duplicate |= target == value->target;
                    if (!duplicate)
                        result.push(UiId(value->target));
                }
            }
    }
    auto owners(PointerKey key) -> Vec<UiId> {
        Vec<UiId> result;
        owners(key, result);
        return result;
    }
    auto ownership_alive(PointerKey key) -> bool {
        auto state = stream(key);
        if (!state || state->owners.is_empty())
            return false;
        for (auto id : state->owners) {
            auto value = entry(id);
            if (!value || !value->guard.alive())
                return false;
        }
        return true;
    }
    auto has_participants() const -> bool {
        for (const auto &slot : slots_)
            if (slot.entry.is_some() && slot.entry->guard.alive() &&
                document_.valid(slot.entry->target))
                return true;
        return false;
    }
    auto has_global_participants() const -> bool {
        for (const auto &slot : slots_)
            if (slot.entry.is_some() && slot.entry->global && slot.entry->guard.alive() &&
                document_.valid(slot.entry->target))
                return true;
        return false;
    }
    auto cancel_detached(const InputSnapshot &previous, slice<PointerContact> contacts)
        -> Result<empty, InputError> {
        return cancel_matching(previous, contacts,
            [&](UiId target) { return !input_current_mount(document_, previous, target); });
    }
    auto cancel_subtree(UiId root, const InputSnapshot &previous, slice<PointerContact> contacts, bool current = false)
        -> Result<empty, InputError> {
        if (!document_.valid(root)) return Err(InputError::InvalidId);
        return cancel_matching(previous, contacts, [&](UiId target) {
            if (!current) return input_descendant(previous, root, target);
            auto descendant = document_.is_descendant(root, target);
            return descendant.is_ok() && descendant.unwrap();
        }, CancelReason::Explicit);
    }
    auto synchronize(const InputSnapshot &previous, const InputSnapshot &next,
                     slice<PointerContact> contacts) -> Result<empty, InputError> {
        if (busy_)
            return Err(InputError::Reentrant);
        busy_ = true;
        struct Guard {
            bool &busy;
            ~Guard() { busy = false; }
        } guard{busy_};
        for (auto &stream : streams_) {
            InputContext context(previous, contacts, stream.key);
            for (auto &candidate : stream.candidates) {
                auto value = entry(candidate.id);
                if (!value || !value->guard.alive()) {
                    cancel_candidate(candidate, context, CancelReason::ReceiverLost);
                    continue;
                }
                auto before = previous.get(value->target), after = next.get(value->target);
                if (!before || !after || before->mount_generation != after->mount_generation ||
                    !eligible(next, value->target))
                    cancel_candidate(candidate, context,
                                     scope_root_.is_some() &&
                                             !input_descendant(next, *scope_root_, value->target)
                                         ? CancelReason::ScopeChanged
                                         : CancelReason::Detached);
            }
        }
        return Ok(empty{});
    }
    void prune() {
        if (busy_)
            return;
        for (auto &slot : slots_)
            if (slot.entry.is_some() &&
                (!document_.valid(slot.entry->target) || !slot.entry->guard.alive())) {
                slot.entry->guard.disconnect();
                slot.entry = None();
                ++slot.generation;
            }
    }
    auto transfer(ParticipantId participant, slice<PointerKey> pointers,
                  const InputSnapshot &snapshot, slice<PointerContact> contacts)
        -> Result<Vec<OwnershipChange>, InputError> {
        if (busy_)
            return Err(InputError::Reentrant);
        auto receiver = entry(participant);
        if (!receiver || !receiver->guard.alive() || !eligible(snapshot, receiver->target) ||
            pointers.is_empty())
            return Err(InputError::InvalidId);
        for (usize i{}; i < pointers.len(); ++i) {
            auto state = stream(pointers[i]);
            if (!state ||
                (!receiver->global && !ancestor(snapshot, receiver->target, state->original)))
                return Err(InputError::InvalidSequence);
            for (usize j{}; j < i; ++j)
                if (pointers[j] == pointers[i])
                    return Err(InputError::InvalidSequence);
        }
        busy_ = true;
        struct Guard {
            bool &busy;
            ~Guard() { busy = false; }
        } guard{busy_};
        Vec<OwnershipChange> changes;
        for (auto pointer : pointers) {
            auto state = stream(pointer);
            auto previous = owners(pointer);
            InputContext context(snapshot, contacts, pointer);
            for (auto &candidate : state->candidates)
                if (candidate.id != participant)
                    cancel_candidate(candidate, context, CancelReason::Superseded);
            state->owners.clear();
            if (receiver->guard.alive()) {
                state->mode = ClaimMode::Exclusive;
                state->group = 0;
                state->owners.push(ParticipantId(participant));
                bool known{};
                for (const auto &candidate : state->candidates)
                    known |= candidate.id == participant;
                if (!known)
                    state->candidates.push({participant});
                for (auto &candidate : state->candidates)
                    if (candidate.id == participant)
                        candidate.state = InputDecisionState::Accept;
                changes.push({pointer, receiver->target, false, rstd::move(previous)});
            }
        }
        return Ok(rstd::move(changes));
    }
    auto release(PointerKey pointer, const InputSnapshot &snapshot, slice<PointerContact> contacts)
        -> Result<empty, InputError> {
        auto state = stream(pointer);
        if (busy_)
            return Err(InputError::Reentrant);
        if (!state)
            return Err(InputError::InvalidSequence);
        busy_ = true;
        struct Guard {
            bool &busy;
            ~Guard() { busy = false; }
        } guard{busy_};
        InputContext context(snapshot, contacts, pointer);
        for (auto &candidate : state->candidates)
            cancel_candidate(candidate, context, CancelReason::Explicit);
        state->owners.clear();
        return Ok(empty{});
    }
    auto process(const InputPacket &packet, const InputSnapshot &snapshot,
                 slice<PointerContact> contacts) -> Result<Vec<OwnershipChange>, InputError> {
        if (busy_)
            return Err(InputError::Reentrant);
        busy_ = true;
        struct Guard {
            bool &busy;
            ~Guard() { busy = false; }
        } guard{busy_};
        PointerKey key{packet.input.device, packet.input.pointer};
        work_ = {};
        InputContext context(snapshot, contacts, key);
        auto state = stream(key);
        if (!state || state->sequence != packet.input.sequence) {
            streams_.retain([&](const Stream &value) { return value.key != key; });
            if (streams_.len() >= usize(32))
                return Err(InputError::Limit);
            Stream next{key, packet.input.sequence, packet.target};
            for (usize i{}; i < slots_.len(); ++i) {
                const auto &slot = slots_[i];
                if (slot.entry.is_some() && slot.entry->guard.alive() &&
                    eligible(snapshot, slot.entry->target) &&
                    (slot.entry->global || ancestor(snapshot, slot.entry->target, packet.target)))
                    next.candidates.push(
                        {{owner_, static_cast<Index>(i.to_primitive()), slot.generation}});
            }
            streams_.push(rstd::move(next));
            state = stream(key);
        }
        Vec<ClaimRequest> requests;
        for (auto &candidate : state->candidates) {
            auto value = entry(candidate.id);
            if (candidate.state == InputDecisionState::Reject || !value || !value->guard.alive() ||
                !eligible(snapshot, value->target))
                continue;
            ++work_.observations;
            auto decision = value->participant->observe(context, packet);
            if (decision.state == InputDecisionState::Reject) {
                candidate.state = InputDecisionState::Reject;
                continue;
            }
            if (decision.state != InputDecisionState::Accept ||
                candidate.state == InputDecisionState::Accept)
                continue;
            if (!eligible(snapshot, value->target))
                continue;
            if (decision.pointers.is_empty())
                decision.pointers.push(PointerKey(key));
            bool valid{true};
            for (usize i{}; i < decision.pointers.len(); ++i) {
                auto affected = stream(decision.pointers[i]);
                valid &= affected &&
                         (value->global || ancestor(snapshot, value->target, affected->original));
                if (affected && !affected->owners.is_empty())
                    valid &= affected->mode == ClaimMode::Shared &&
                             decision.mode == ClaimMode::Shared && affected->group &&
                             affected->group == decision.group;
                for (usize j{}; j < i; ++j)
                    valid &= decision.pointers[i] != decision.pointers[j];
            }
            if (!valid)
                continue;
            requests.push({candidate.id, value->target, decision.mode, decision.group,
                           decision.priority, rstd::move(decision.pointers)});
            ++work_.claims;
        }
        auto resolved = policy_->resolve(context, requests.as_slice());
        if (resolved.is_err())
            return Err(resolved.unwrap_err());
        auto resolution = rstd::move(resolved).unwrap();
        for (usize i{}; i < resolution.winners.len(); ++i) {
            const ClaimRequest *request{};
            for (const auto &value : requests)
                if (value.participant == resolution.winners[i])
                    request = &value;
            if (!request)
                return Err(InputError::InvalidSequence);
            for (usize j{}; j < i; ++j) {
                const ClaimRequest *previous{};
                for (const auto &value : requests)
                    if (value.participant == resolution.winners[j])
                        previous = &value;
                bool overlapping{};
                for (auto a : request->pointers)
                    for (auto b : previous->pointers)
                        overlapping |= a == b;
                if (overlapping &&
                    (request->mode != ClaimMode::Shared || previous->mode != ClaimMode::Shared ||
                     !request->group || request->group != previous->group))
                    return Err(InputError::InvalidSequence);
                if (resolution.winners[i] == resolution.winners[j])
                    return Err(InputError::InvalidSequence);
            }
        }
        Vec<OwnershipChange> changes;
        for (auto winner : resolution.winners) {
            const ClaimRequest *request{};
            for (const auto &value : requests)
                if (value.participant == winner)
                    request = &value;
            if (!request)
                return Err(InputError::InvalidSequence);
            auto receiver = entry(winner);
            if (!receiver || !receiver->guard.alive())
                continue;
            for (auto pointer : request->pointers) {
                auto affected = stream(pointer);
                bool first = affected->owners.is_empty();
                if (!first &&
                    (affected->mode != ClaimMode::Shared || request->mode != ClaimMode::Shared ||
                     affected->group != request->group))
                    continue;
                if (first) {
                    affected->mode = request->mode;
                    affected->group = request->group;
                    InputContext affected_context(snapshot, contacts, pointer);
                    for (auto &candidate : affected->candidates) {
                        bool selected{};
                        for (auto selected_id : resolution.winners)
                            selected |= candidate.id == selected_id;
                        if (!selected)
                            cancel_candidate(candidate, affected_context, CancelReason::Superseded);
                    }
                }
                bool known{};
                for (auto id : affected->owners)
                    known |= id == winner;
                if (!known)
                    affected->owners.push(ParticipantId(winner));
                for (auto &candidate : affected->candidates)
                    if (candidate.id == winner)
                        candidate.state = InputDecisionState::Accept;
                if (!known) {
                    bool duplicate{};
                    for (const auto &change : changes)
                        duplicate |= change.pointer == pointer && change.target == receiver->target;
                    if (!duplicate)
                        changes.push({pointer, receiver->target, !first});
                }
            }
        }
        return Ok(rstd::move(changes));
    }
    auto end(PointerKey key, const InputSnapshot &snapshot, slice<PointerContact> contacts,
             CancelReason reason = CancelReason::None) -> Result<empty, InputError> {
        if (busy_)
            return Err(InputError::Reentrant);
        busy_ = true;
        struct Guard {
            bool &busy;
            ~Guard() { busy = false; }
        } guard{busy_};
        auto state = stream(key);
        if (state && reason != CancelReason::None) {
            InputContext context(snapshot, contacts, key);
            for (auto &candidate : state->candidates)
                cancel_candidate(candidate, context, reason);
        }
        streams_.retain([&](const Stream &value) { return value.key != key; });
        return Ok(empty{});
    }
    auto close() -> Result<empty, InputError> {
        if (busy_)
            return Err(InputError::Reentrant);
        streams_.clear();
        for (auto &slot : slots_)
            if (slot.entry.is_some())
                slot.entry->guard.disconnect();
        slots_.clear();
        return Ok(empty{});
    }
};
} // namespace vui
