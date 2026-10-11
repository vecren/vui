export module vui:layout.parameters;
import :layout.protocol;
using namespace rstd::prelude;

export namespace vui {
template <class T> class LayoutParameters {
    struct Entry {
        UiRelation relation;
        T value;
    };
    Vec<Entry> entries_;

  public:
    void set(UiRelation relation, T value) {
        for (auto &entry : entries_)
            if (entry.relation == relation) {
                entry.value = rstd::move(value);
                return;
            }
        entries_.push({relation, rstd::move(value)});
    }
    auto get(UiRelation relation) -> T * {
        for (auto &entry : entries_)
            if (entry.relation == relation)
                return &entry.value;
        return nullptr;
    }
    template <class Context> void retain(const Context &context) {
        entries_.retain([&](const Entry &entry) {
            for (usize i{}; i < context.child_count(); ++i)
                if (context.child_relation(i).unwrap() == entry.relation)
                    return true;
            return false;
        });
    }
};
} // namespace vui
