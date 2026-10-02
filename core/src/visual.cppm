export module vui:visual;
import :document;
import :endpoint;
using namespace rstd::prelude;

export namespace vui {
enum class PublishError { Busy, ForeignDocument, StaleUpdate, WrongAcknowledgement, Limit };
struct BuildTimings {
    Serial document{}, ui_revision{}, scene_session{}, base_revision{};
    CpuTiming build;
    Option<TransactionHeader> transaction;
    Option<PublishError> error;
};
class VisualBuilder {
    struct Published {
        UiUpdate update;
        Vec<RenderId> ids;
        Serial revision;
    };
    Serial session_, document_{}, applied_revision_{}, next_generation_{1};
    Option<Published> applied_, pending_;
    Option<BuildTimings> last_timings_;

    static auto id(const Published &state, UiId ui) -> RenderId {
        if (usize(ui.slot) >= state.ids.len())
            return {};
        return state.ids[usize(ui.slot)];
    }
    static auto children(const UiVisual &visual, const Published &state) -> Children {
        Vec<RenderId> result;
        for (auto child : visual.children)
            result.push(id(state, child));
        return Children(rstd::move(result));
    }
    static auto same_children(const UiVisual &a, const Published &a_state, const UiVisual &b,
                              const Published &b_state) -> bool {
        if (a.children.len() != b.children.len())
            return false;
        for (usize i{}; i < a.children.len(); ++i)
            if (id(a_state, a.children[i]) != id(b_state, b.children[i]))
                return false;
        return true;
    }

  public:
    explicit VisualBuilder(Serial session) : session_(session) {}
    auto applied_revision() const -> Serial { return applied_revision_; }
    auto pending() const -> bool { return pending_.is_some(); }
    auto render_id(UiId ui) const -> Option<RenderId> {
        if (applied_.is_none() || !applied_->update.get(ui))
            return None();
        return Some(id(*applied_, ui));
    }
    auto last_build_timings() const -> Option<BuildTimings> { return last_timings_; }
    auto build(UiUpdate update, DiagnosticsOptions options = {})
        -> Result<RenderTransaction, PublishError> {
        BuildTimings sample{
            update.document(), update.revision(), session_, applied_revision_, {}, {}, {}};
        CpuTimer timer(options.timings ? &sample.build : nullptr);
        auto result = build_impl(rstd::move(update));
        timer.pause();
        last_timings_ = None();
        if (result.is_err()) {
            auto error = result.unwrap_err();
            sample.error = Some(error);
            if (options.timings)
                last_timings_ = Some(sample);
            return Err(error);
        }
        auto transaction = result.unwrap();
        sample.transaction = Some(transaction.header());
        if (options.timings)
            last_timings_ = Some(sample);
        return Ok(rstd::move(transaction));
    }

  private:
    auto build_impl(UiUpdate update) -> Result<RenderTransaction, PublishError> {
        if (pending_.is_some())
            return Err(PublishError::Busy);
        if (document_ && document_ != update.document())
            return Err(PublishError::ForeignDocument);
        if (applied_.is_some() && update.revision() < applied_->update.revision())
            return Err(PublishError::StaleUpdate);
        if (applied_revision_ == ~Serial{})
            return Err(PublishError::Limit);
        document_ = update.document();
        Published next{rstd::move(update), {}, applied_revision_ + 1};
        usize capacity{};
        for (const auto &visual : next.update.visuals()) {
            auto required = usize(visual.id.slot) + usize(1);
            if (required > capacity)
                capacity = required;
        }
        next.ids.reserve(capacity);
        for (usize i{}; i < capacity; ++i)
            next.ids.push(RenderId{});
        TransactionBuilder transaction(
            {session_, applied_revision_, next.revision, next.update.revision()});
        for (const auto &visual : next.update.visuals()) {
            auto previous = applied_.is_some() ? applied_->update.get(visual.id) : nullptr;
            if (previous && previous->mount_generation == visual.mount_generation) {
                next.ids[usize(visual.id.slot)] = id(*applied_, visual.id);
            } else {
                if (next_generation_ == ~Serial{})
                    return Err(PublishError::Limit);
                next.ids[usize(visual.id.slot)] =
                    RenderId{session_, visual.id.slot, next_generation_++};
            }
        }
        if (applied_.is_some())
            for (const auto &previous : applied_->update.visuals())
                if (!next.update.get(previous.id) ||
                    next.update.get(previous.id)->mount_generation != previous.mount_generation)
                    transaction.destroy(id(*applied_, previous.id));
        for (const auto &visual : next.update.visuals()) {
            auto render = id(next, visual.id);
            const auto *previous = applied_.is_some() ? applied_->update.get(visual.id) : nullptr;
            if (previous && previous->mount_generation != visual.mount_generation)
                previous = nullptr;
            if (!previous) {
                transaction.create(render, visual.properties, visual.content,
                                   children(visual, next));
            } else {
                if (previous->properties != visual.properties)
                    transaction.properties(render, visual.properties);
                if (!previous->content.same_content(visual.content))
                    transaction.content(render, visual.content);
                if (!same_children(*previous, *applied_, visual, next))
                    transaction.children(render, children(visual, next));
            }
        }
        transaction.root(next.update.root().is_some() ? Some(id(next, *next.update.root()))
                                                      : None<RenderId>());
        pending_ = Some(rstd::move(next));
        return Ok(rstd::move(transaction).finish());
    }

  public:
    auto acknowledge(const ApplyEvent &event) -> Result<empty, PublishError> {
        if (pending_.is_none())
            return Err(PublishError::WrongAcknowledgement);
        Serial session{}, revision{}, ui_revision{};
        if (event.is_Applied()) {
            const auto &applied = event.as_Applied().value;
            session = applied.session;
            revision = applied.revision;
            ui_revision = applied.ui_revision;
        } else {
            auto header =
                event.is_Rejected() ? event.as_Rejected().header : event.as_Cancelled().header;
            session = header.session;
            revision = header.revision;
            ui_revision = header.ui_revision;
        }
        if (session != session_ || revision != pending_->revision ||
            ui_revision != pending_->update.revision())
            return Err(PublishError::WrongAcknowledgement);
        if (event.is_Applied()) {
            applied_revision_ = revision;
            applied_ = pending_.take();
        } else
            pending_ = None();
        return Ok(empty{});
    }
};
} // namespace vui
