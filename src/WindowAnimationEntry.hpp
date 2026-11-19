#pragma once

#include <array>
#include <cstdint>
#include <utility>

#include "SlotPool.hpp"
#include "StyleSettingsCache.hpp"
#include "WindowAnimationFacts.hpp"

namespace hyprimate::animation {
enum class Life : std::uint8_t { Mapped, PendingCapture, DyingOwned, DyingNative };
struct EntryFacts {
    Life life = Life::Mapped;
    WindowId origin = 0;
    MonitorId monitor = 0;
    WorkspaceId workspace = 0;
    Place closing_place{};
    Frame frame{};
    Clock::time_point last_step{};
    FrameId last_frame = 0;
    std::array<Clock::time_point, STALL_LIMIT> stalls{};
    std::uint8_t stall_count = 0;
};
template<class State> struct Slot {
    template<class Factory> Slot(std::in_place_t, Factory &&factory)
        : state(std::forward<Factory>(factory)()) {}
    template<class... Args> explicit Slot(Args &&...args) : state(std::forward<Args>(args)...) {}
    State state;
    EntryFacts entry;
};
template<class State, class Settings, class Prepared>
struct ResourcesBase : SStyleSettingsCache<Settings, Prepared> {
    using AnimationPool = CSlotPool<Slot<State>>;
};
struct Handle {
    config::Style style{};
    std::uint32_t index = 0, generation = 0;
    bool operator==(const Handle &) const = default;
};
} // namespace hyprimate::animation
