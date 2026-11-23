export module vui.compose:saveable;
import :types;
using namespace rstd::prelude;

export namespace vui::compose {
template <class T> concept SaveableStateValue = rstd::mtp::is_arithmetic<T> ||
    rstd::mtp::same_as<T, bool> || rstd::mtp::same_as<T, String>;

struct MemoryPayload {
    virtual ~MemoryPayload() = default;
    virtual auto type() const -> rstd::any::TypeId = 0;
};

template <class T> struct MemoryValue final : MemoryPayload {
    T value;
    explicit MemoryValue(T source) : value(rstd::move(source)) {}
    auto type() const -> rstd::any::TypeId override { return rstd::any::TypeId::of<T>(); }
};

struct SavedPathSegment {
    Key key;
    Index kind;
    Index layout;
    rstd::any::TypeId type;
    friend auto operator==(const SavedPathSegment &, const SavedPathSegment &) -> bool = default;
};

struct SavedAddress {
    Key hook;
    Vec<SavedPathSegment> path;

    explicit SavedAddress(Key key) : hook(rstd::move(key)) {}
    SavedAddress(SavedAddress &&) = default;
    SavedAddress(const SavedAddress &other) : hook(other.hook), path(Vec<SavedPathSegment>::from(other.path.as_slice())) {}
    auto operator=(SavedAddress &&) -> SavedAddress & = default;
    friend auto operator==(const SavedAddress &a, const SavedAddress &b) -> bool {
        if (a.hook != b.hook || a.path.len() != b.path.len()) return false;
        for (usize i{}; i < a.path.len(); ++i)
            if (a.path[i] != b.path[i]) return false;
        return true;
    }
};

struct SavedValue {
    SavedAddress address;
    rstd::rc::Rc<MemoryPayload> payload;
};

struct SavedItemValues {
    Vec<SavedValue> values;

    auto find(const SavedAddress &address) const -> const MemoryPayload *;
};

struct SavedItemActivation {
    Key key;
    bool valid{true};
};

class SaveableStateHolder {
    struct Entry {
        Key key;
        rstd::rc::Rc<SavedItemValues> values;
    };
    Vec<Entry> inactive_;
    Vec<rstd::rc::Weak<SavedItemActivation>> active_;
    Index capacity_{128};

    void trim();
    void discard(const Key &key);

  public:
    void set_capacity(Index capacity);
    void forget(const Key &key);
    auto peek(const Key &key) const -> rstd::rc::Rc<SavedItemValues>;
    void activate(const rstd::rc::Rc<SavedItemActivation> &item);
    void save(SavedItemActivation &item, rstd::rc::Rc<SavedItemValues> values);
};

struct SavedItemSource {
    rstd::rc::Rc<SaveableStateHolder> holder;
    Key key;

    auto same(const SavedItemSource &other) const -> bool {
        return holder.get() == other.holder.get() && key == other.key;
    }
};
}
