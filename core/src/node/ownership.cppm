export module vui:node.ownership;
import :types;
import :binding.scope;
using namespace rstd::prelude;

export namespace vui {
class UiDocument;
class UiTreeOwner;
class UiTreePermit {
    Serial document_{};
    BindingGuard guard_;
    friend class UiDocument;
    friend class UiTreeOwner;
    UiTreePermit(Serial document, BindingGuard guard) : document_(document), guard_(rstd::move(guard)) {}
  public:
    UiTreePermit() = default;
    auto alive() const -> bool { return guard_.alive(); }
    auto document() const -> Serial { return document_; }
    auto same_owner(const UiTreePermit &other) const -> bool {
        return document_ == other.document_ && guard_.same_binding(other.guard_);
    }
};
class UiTreeAccess {
    Vec<UiTreePermit> permits_;
  public:
    UiTreeAccess() = default;
    UiTreeAccess(UiTreePermit permit) { permits_.push(rstd::move(permit)); }
    UiTreeAccess(initializer_list<UiTreePermit> permits) { for (auto permit : permits) allow(rstd::move(permit)); }
    UiTreeAccess(const UiTreeAccess &other) { for (auto permit : other.permits_) permits_.push(rstd::move(permit)); }
    UiTreeAccess(UiTreeAccess &&) = default;
    auto operator=(UiTreeAccess &&) -> UiTreeAccess & = default;
    void allow(UiTreePermit permit) {
        for (const auto &previous : permits_) if (previous.same_owner(permit)) return;
        permits_.push(rstd::move(permit));
    }
    auto valid(Serial document) const -> bool {
        for (const auto &permit : permits_) if (permit.document() != document || !permit.alive()) return false;
        return true;
    }
    auto allows(const UiTreePermit &owner) const -> bool {
        for (const auto &permit : permits_) if (permit.alive() && permit.same_owner(owner)) return true;
        return false;
    }
};
class UiTreeOwner {
    Serial document_{};
    Binding binding_;
    friend class UiDocument;
    UiTreeOwner(Serial document, Binding binding) : document_(document), binding_(rstd::move(binding)) {}
  public:
    UiTreeOwner() = default;
    UiTreeOwner(const UiTreeOwner &) = delete;
    UiTreeOwner(UiTreeOwner &&) = default;
    auto operator=(UiTreeOwner &&) -> UiTreeOwner & = default;
    auto permit() const -> UiTreePermit { return {document_, binding_.guard()}; }
    auto access() const -> UiTreeAccess { return UiTreeAccess(permit()); }
    auto alive() const -> bool { return binding_.alive(); }
    void release() { binding_.disconnect(); }
};
} // namespace vui
