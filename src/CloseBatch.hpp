#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace hyprimate::animation {
// The source tag separates native object identities from retained close IDs. Equal-z items
// have a deterministic order in both the collected and allocation-free paths.
struct CloseBatchKey {
    int z_index = 0;
    bool retained = false;
    std::uintptr_t identity = 0;
    auto operator<=>(const CloseBatchKey &) const = default;
};

// Enumerate emits values, never borrowed records. It must be repeatable without allocating.
// Only collection can fall back: once drawing begins, an exception must never replay a batch.
// Invocation-local storage also permits renderer callbacks to reenter on the same thread.
template<class Item, std::size_t InlineCapacity = 16, class Spill = std::vector<Item>,
    class Enumerate, class Draw>
void draw_close_batch(Enumerate enumerate, Draw draw) {
    static_assert(InlineCapacity > 0);
    std::array<Item, InlineCapacity> local{};
    std::size_t size = 0;
    Spill spill;
    bool spilled = false;
    bool fallback = false;
    try {
        enumerate([&](Item item) {
            if (!spilled && size < InlineCapacity) {
                local[size++] = std::move(item);
                return;
            }
            if (!spilled) {
                spill.reserve(InlineCapacity * 2);
                for (auto &value : local) { spill.push_back(std::move(value)); }
                spilled = true;
            }
            spill.push_back(std::move(item));
        });
    } catch (const std::bad_alloc &) {
        fallback = true;
    }
    if (!fallback) {
        const auto LESS = [](const Item &left, const Item &right) { return left.key < right.key; };
        if (spilled) {
            std::ranges::sort(spill, LESS);
            for (const auto &item : spill) { draw(item); }
        } else {
            std::sort(local.begin(), local.begin() + size, LESS);
            for (std::size_t index = 0; index < size; ++index) { draw(local[index]); }
        }
        return;
    }
    // Memory pressure must not delegate a suppressed close to the host, nor swallow unrelated
    // native fadeouts. Select the next value without scratch allocation. No source iterator or
    // record survives draw(), which may recursively collect or mutate the source.
    std::optional<CloseBatchKey> previous;
    for (;;) {
        std::optional<Item> next;
        enumerate([&](Item item) {
            if ((!previous || item.key > *previous) && (!next || item.key < next->key)) {
                next = std::move(item);
            }
        });
        if (!next) { return; }
        previous = next->key;
        draw(*next);
    }
}
} // namespace hyprimate::animation
