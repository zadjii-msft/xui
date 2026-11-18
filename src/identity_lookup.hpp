#pragma once
#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>

namespace xui::detail {
// Borrowed, bounded index. Input order remains the admission order; duplicates
// resolve to one entry so consumers can share one retained slot.
template<class Entry, std::size_t Capacity>
class IdentityLookup {
public:
    explicit IdentityLookup(std::span<Entry> entries) : size_(entries.size()) {
        if (size_ > Capacity) throw std::length_error("Identity lookup capacity exceeded");
        for (std::size_t i = 0; i < size_; ++i) entries_[i] = &entries[i];
        std::sort(entries_.begin(), entries_.begin() + size_,
            [](const auto* a, const auto* b) { return a->identity() < b->identity(); });
    }
    template<class Key> Entry* find(const Key& key) const {
        const auto end = entries_.begin() + size_;
        const auto found = std::lower_bound(entries_.begin(), end, key,
            [](const auto* entry, const auto& value) { return entry->identity() < value; });
        return found != end && (*found)->identity() == key ? *found : nullptr;
    }
private:
    std::array<Entry*, Capacity> entries_;
    std::size_t size_{};
};
}
