#pragma once

#include <cstdint>
#include <utility>

#include "SlotPool.hpp"
#include "WindowAnimationFacts.hpp"
#include "shared/StyleSettingsCache.hpp"

namespace hyprimate::animation {
    enum class Life : std::uint8_t { Mapped, PendingCapture, DyingOwned, DyingNative };
    template <class State>
    struct Slot {
        template <class Factory>
        Slot(std::in_place_t /*tag*/, Factory &&factory) : state(std::forward<Factory>(factory)()) {
        }
        template <class... Args>
        explicit Slot(Args &&...args) : state(std::forward<Args>(args)...) {
        }
        State state;
    };
    template <class State, class Settings, class Prepared>
    struct ResourcesBase : SStyleSettingsCache<Settings, Prepared> {
        using AnimationPool = CSlotPool<Slot<State>>;
    };
    struct Handle {
        config::Style style{};
        std::uint32_t index = 0, generation = 0;
        bool operator==(const Handle &) const = default;
    };
}  // namespace hyprimate::animation
