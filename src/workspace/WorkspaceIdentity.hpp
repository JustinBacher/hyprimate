#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

// Durable composition slots outlive compositor objects. Numbered workspaces use even slots;
// named/special composition pairs retain compact, collision-resolved odd slots for this
// registry's entire plugin lifetime. Existing-key lookup neither copies nor allocates strings.
class CompositionPlacements {
    struct View {
        std::string_view normal;
        std::optional<std::string_view> special;
    };
    struct Entry {
        std::string normal;
        std::optional<std::string> special;
        std::uint64_t placement;
    };
    static std::uint64_t hash(View key) {
        std::uint64_t result = UINT64_C(14695981039346656037);
        const auto APPEND = [&result](std::string_view text) {
            for (const unsigned char BYTE : text) {
                result = (result ^ BYTE) * UINT64_C(1099511628211);
            }
        };
        APPEND(key.normal);
        result = (result ^ static_cast<unsigned>(key.special.has_value())) * UINT64_C(1099511628211);
        if (key.special) {
            APPEND(*key.special);
        }
        return result;
    }
    static std::string_view normalized(std::string_view name, std::string_view prefix) {
        return name.starts_with(prefix) ? name.substr(prefix.size()) : name;
    }

  public:
    [[nodiscard]] std::uint64_t get(std::optional<std::uint32_t> numbered,
        std::string_view normal, std::optional<std::string_view> special) {
        if (numbered && !special) {
            return static_cast<std::uint64_t>(*numbered) * 2;
        }
        const View KEY{.normal = normalized(normal, "name:"),
            .special = special ? std::optional{normalized(*special, "special:")} : std::nullopt};
        const auto HASH = hash(KEY);
        const auto [FIRST, LAST] = m_entries.equal_range(HASH);
        for (auto entry = FIRST; entry != LAST; ++entry) {
            if (entry->second.normal == KEY.normal && entry->second.special == KEY.special) {
                return entry->second.placement;
            }
        }
        // Probe from a small deterministic range so new names never renumber existing slots.
        auto candidate = HASH % 64;
        while (m_occupied.contains(candidate)) {
            ++candidate;
        }
        const auto PLACEMENT = (candidate * 2) + 1;
        m_occupied.insert(candidate);
        m_entries.emplace(HASH, Entry{.normal = std::string(KEY.normal),
            .special = KEY.special ? std::optional{std::string(*KEY.special)} : std::nullopt,
            .placement = PLACEMENT});
        return PLACEMENT;
    }

  private:
    std::unordered_multimap<std::uint64_t, Entry> m_entries;
    std::unordered_set<std::uint64_t> m_occupied;
};
}
