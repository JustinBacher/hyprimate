#pragma once

#include <cstdint>
#include <vector>

namespace hyprimate::workspace {
// Weak references distinguish object lifetimes, even when an allocator reuses an address.
// IDs are never recycled while the registry exists.
template<class Strong, class Weak> class CompositionIdentities {
  public:
    std::int64_t get(const Strong &normal, const Strong &special) {
        std::erase_if(m_entries, [](const Entry &entry) {
            return (entry.has_normal && !entry.normal.lock()) ||
                (entry.has_special && !entry.special.lock());
        });
        for (const auto &entry : m_entries) {
            if (entry.has_normal == static_cast<bool>(normal) &&
                entry.has_special == static_cast<bool>(special) &&
                entry.normal.lock() == normal && entry.special.lock() == special) {
                return entry.id;
            }
        }
        const auto IDENTITY = ++m_next;
        m_entries.push_back({normal, special, static_cast<bool>(normal), static_cast<bool>(special), IDENTITY});
        return IDENTITY;
    }
  private:
    struct Entry {
        Weak normal;
        Weak special;
        bool has_normal;
        bool has_special;
        std::int64_t id;
    };
    std::vector<Entry> m_entries;
    std::int64_t m_next = 0;
};
}
