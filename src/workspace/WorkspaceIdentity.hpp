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

// Durable planet slots outlive compositor objects. Numbered workspaces use even slots;
// named/special composition pairs retain compact, collision-resolved odd slots for this
// registry's entire plugin lifetime. Existing-key lookup neither copies nor allocates strings.
class CompositionPlacements {
    struct View {
        std::string_view normal;
        std::optional<std::string_view> special;
    };
    struct Key {
        std::string normal;
        std::optional<std::string> special;
    };
    static View view(const Key &key) {
        return {key.normal, key.special ? std::optional<std::string_view>{*key.special} : std::nullopt};
    }
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(View key) const {
            std::uint64_t result = UINT64_C(14695981039346656037);
            const auto append = [&result](std::string_view text) {
                for (const unsigned char byte : text) {
                    result = (result ^ byte) * UINT64_C(1099511628211);
                }
            };
            append(key.normal);
            result = (result ^ static_cast<unsigned>(key.special.has_value())) * UINT64_C(1099511628211);
            if (key.special) {
                append(*key.special);
            }
            return static_cast<std::size_t>(result);
        }
        std::size_t operator()(const Key &key) const { return (*this)(view(key)); }
    };
    struct Equal {
        using is_transparent = void;
        bool operator()(View first, View second) const {
            return first.normal == second.normal && first.special == second.special;
        }
        bool operator()(const Key &first, const Key &second) const { return (*this)(view(first), view(second)); }
        bool operator()(const Key &first, View second) const { return (*this)(view(first), second); }
        bool operator()(View first, const Key &second) const { return (*this)(first, view(second)); }
    };
    static std::string_view normalized(std::string_view name, std::string_view prefix) {
        return name.starts_with(prefix) ? name.substr(prefix.size()) : name;
    }

  public:
    [[nodiscard]] std::uint64_t get(std::optional<std::uint32_t> numbered,
        std::string_view normal, std::optional<std::string_view> special) {
        if (numbered && !special) {
            return static_cast<std::uint64_t>(*numbered) * 2;
        }
        const View KEY{normalized(normal, "name:"),
            special ? std::optional<std::string_view>{normalized(*special, "special:")} : std::nullopt};
        if (const auto found = m_entries.find(KEY); found != m_entries.end()) {
            return found->second;
        }
        // Small deterministic initial grid range; linear probing resolves actual collisions
        // without moving prior planets. More names grow the range only as occupancy requires.
        auto candidate = static_cast<std::uint64_t>(Hash{}(KEY) % 64);
        while (m_occupied.contains(candidate)) {
            ++candidate;
        }
        const auto PLACEMENT = candidate * 2 + 1;
        m_occupied.insert(candidate);
        m_entries.emplace(Key{std::string(KEY.normal),
            KEY.special ? std::optional<std::string>{std::string(*KEY.special)} : std::nullopt}, PLACEMENT);
        return PLACEMENT;
    }

  private:
    std::unordered_map<Key, std::uint64_t, Hash, Equal> m_entries;
    std::unordered_set<std::uint64_t> m_occupied;
};
}
