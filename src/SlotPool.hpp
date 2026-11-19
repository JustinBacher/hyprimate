#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hyprimate {

// Handles are valid only in the pool that created them. Erase and clear invalidate them;
// a slot whose generation reaches UINT32_MAX is retired rather than wrapping. Pool storage
// and generations survive clear, so no stale handle can name a later occupant. Fixed-address
// blocks keep even non-movable T objects stable as capacity grows. The pool itself is not
// movable or copyable. Structural mutation during for_each is not supported.
template <class T> class CSlotPool {
  public:
    struct SHandle {
        std::uint32_t index;
        std::uint32_t generation;
        bool operator==(const SHandle &) const = default;
    };

    static constexpr SHandle INVALID_HANDLE{std::numeric_limits<std::uint32_t>::max(), 0};

    CSlotPool() = default;
    ~CSlotPool() = default;
    CSlotPool(const CSlotPool &) = delete;
    CSlotPool &operator=(const CSlotPool &) = delete;
    CSlotPool(CSlotPool &&) = delete;
    CSlotPool &operator=(CSlotPool &&) = delete;

    // Allocates blocks up to the requested total slot capacity; retired slots count towards it.
    void reserve(std::size_t capacity) {
        if (capacity > INVALID_HANDLE.index) {
            throw std::length_error("slot pool capacity exceeds handle range");
        }
        while (m_capacity < capacity) {
            grow();
        }
    }

    template <class... Args> SHandle emplace(Args &&...args) {
        if (m_free == INVALID_HANDLE.index) {
            grow();
        }
        const auto INDEX = m_free;
        auto &slot = slot_at(INDEX);
        // Leave the free list untouched if T's constructor throws.
        slot.value.emplace(std::forward<Args>(args)...);
        m_free = slot.next;
        ++m_size;
        return {INDEX, slot.generation};
    }

    T *get(SHandle handle) {
        if (handle.index >= m_capacity) {
            return nullptr;
        }
        auto &slot = slot_at(handle.index);
        return slot.generation == handle.generation && slot.value ? &*slot.value : nullptr;
    }

    [[nodiscard]] const T *get(SHandle handle) const {
        if (handle.index >= m_capacity) {
            return nullptr;
        }
        const auto &slot = slot_at(handle.index);
        return slot.generation == handle.generation && slot.value ? &*slot.value : nullptr;
    }

    bool erase(SHandle handle) {
        if (!get(handle)) {
            return false;
        }
        auto &slot = slot_at(handle.index);
        slot.value.reset();
        --m_size;
        if (slot.generation != std::numeric_limits<std::uint32_t>::max()) {
            ++slot.generation;
            slot.next = m_free;
            m_free = handle.index;
        }
        return true;
    }

    [[nodiscard]] std::size_t size() const { return m_size; }

    template <class F> void for_each(F &&callback) {
        auto &&callable = std::forward<F>(callback);
        for (std::size_t i = 0; i < m_capacity; ++i) {
            auto &slot = slot_at(static_cast<std::uint32_t>(i));
            if (slot.value) {
                callable(SHandle{static_cast<std::uint32_t>(i), slot.generation}, *slot.value);
            }
        }
    }

    template <class F> void for_each(F &&callback) const {
        auto &&callable = std::forward<F>(callback);
        for (std::size_t i = 0; i < m_capacity; ++i) {
            const auto &slot = slot_at(static_cast<std::uint32_t>(i));
            if (slot.value) {
                callable(SHandle{static_cast<std::uint32_t>(i), slot.generation}, *slot.value);
            }
        }
    }

    void clear() {
        for (std::size_t i = 0; i < m_capacity; ++i) {
            const auto &slot = slot_at(static_cast<std::uint32_t>(i));
            if (slot.value) {
                (void)erase({static_cast<std::uint32_t>(i), slot.generation});
            }
        }
    }

  private:
    static constexpr std::size_t BLOCK_SIZE = 64;
    struct SSlot {
        std::optional<T> value;
        std::uint32_t generation = 1;
        std::uint32_t next = INVALID_HANDLE.index;
    };
    using SBlock = std::array<SSlot, BLOCK_SIZE>;

    SSlot &slot_at(std::uint32_t index) { return (*m_blocks[index / BLOCK_SIZE])[index % BLOCK_SIZE]; }
    [[nodiscard]] const SSlot &slot_at(std::uint32_t index) const { return (*m_blocks[index / BLOCK_SIZE])[index % BLOCK_SIZE]; }

    void grow() {
        if (m_capacity == INVALID_HANDLE.index) {
            throw std::length_error("slot pool exhausted");
        }
        auto block = std::make_unique<SBlock>();
        m_blocks.push_back(std::move(block));
        const auto COUNT = std::min(BLOCK_SIZE, static_cast<std::size_t>(INVALID_HANDLE.index) - m_capacity);
        for (std::size_t i = COUNT; i > 0; --i) {
            const auto INDEX = static_cast<std::uint32_t>(m_capacity + i - 1);
            auto &slot = slot_at(INDEX);
            slot.next = m_free;
            m_free = INDEX;
        }
        m_capacity += COUNT;
    }

    std::vector<std::unique_ptr<SBlock>> m_blocks;
    std::size_t m_capacity = 0;
    std::size_t m_size = 0;
    std::uint32_t m_free = INVALID_HANDLE.index;
};

} // namespace hyprimate
