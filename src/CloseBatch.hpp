#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
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

// A ticket does not keep its owner alive. Invalidation also rejects work queued before a
// teardown/reinstall of the same object, without allocating on the callback path.
class CallbackLifetime {
  public:
    CallbackLifetime() = default;
    CallbackLifetime(const CallbackLifetime &) = delete;
    CallbackLifetime &operator=(const CallbackLifetime &) = delete;
    CallbackLifetime(CallbackLifetime &&) = delete;
    CallbackLifetime &operator=(CallbackLifetime &&) = delete;
    class Ticket {
      public:
        [[nodiscard]] bool valid() const {
            const auto state = m_state.lock();
            return state && *state == m_generation;
        }
        [[nodiscard]] bool alive() const { return !m_state.expired(); }
      private:
        friend class CallbackLifetime;
        std::weak_ptr<std::uint64_t> m_state;
        std::uint64_t m_generation = 0;
    };
    [[nodiscard]] Ticket ticket() const {
        Ticket result;
        result.m_state = m_state;
        result.m_generation = *m_state;
        return result;
    }
    void invalidate() { ++*m_state; }
  private:
    std::shared_ptr<std::uint64_t> m_state = std::make_shared<std::uint64_t>(0);
};

// Only preparation is safe to retry. The renderer can reenter or throw after accepting a
// pass; neither a backdrop nor the content may be handed to it twice.
template<class Valid, class Prepare, class Backdrop, class Submit>
bool submit_close(Valid &&valid, Prepare &&prepare, Backdrop &&backdrop, Submit &&submit) {
    if (!valid()) { return false; }
    decltype(prepare()) content{};
    try {
        content = prepare();
    } catch (const std::bad_alloc &) {
        if (!valid()) { return false; }
        content = prepare();
    }
    if (!content || !valid()) { return false; }
    try {
        backdrop();
    } catch (const std::bad_alloc &) {
        // Dim/blur is optional. Keep any effect already queued and the prepared pixels.
    }
    if (!valid()) { return false; }
    submit(std::move(content));
    return true;
}

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
