module vui.compose;
import :saveable;
using namespace rstd::prelude;

namespace vui::compose {
auto SavedItemValues::find(const SavedAddress &address) const -> const MemoryPayload * {
    for (const auto &entry : values)
        if (entry.address == address) return entry.payload.get();
    return nullptr;
}

void SaveableStateHolder::trim() {
    while (inactive_.len() > usize(capacity_)) inactive_.remove(usize{});
}

void SaveableStateHolder::discard(const Key &key) {
    inactive_.retain([&](const Entry &entry) { return entry.key != key; });
}

void SaveableStateHolder::set_capacity(Index capacity) {
    capacity_ = capacity;
    trim();
}

void SaveableStateHolder::forget(const Key &key) {
    discard(key);
    active_.retain([&](const rstd::rc::Weak<SavedItemActivation> &entry) {
        auto active = entry.upgrade();
        if (!active) return false;
        if (active->get()->key != key) return active->get()->valid;
        active->get()->valid = false;
        return false;
    });
}

auto SaveableStateHolder::peek(const Key &key) const -> rstd::rc::Rc<SavedItemValues> {
    for (const auto &entry : inactive_)
        if (entry.key == key) return entry.values.clone();
    return {};
}

void SaveableStateHolder::activate(const rstd::rc::Rc<SavedItemActivation> &item) {
    forget(item->key);
    active_.push(item.downgrade());
}

void SaveableStateHolder::save(SavedItemActivation &item, rstd::rc::Rc<SavedItemValues> values) {
    if (!item.valid) return;
    item.valid = false;
    if (!capacity_ || !values || values->values.is_empty()) return;
    discard(item.key);
    inactive_.push({Key(item.key), rstd::move(values)});
    trim();
}
}
