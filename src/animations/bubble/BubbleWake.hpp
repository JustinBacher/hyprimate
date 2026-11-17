#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "animations/bubble/BubbleMotion.hpp"

namespace hyprimate {

    inline constexpr std::size_t K_BUBBLE_WAKE_NODES = 48;
    inline constexpr std::size_t K_BUBBLE_WAKE_BUBBLES = 12;

    // A wake as a renderer draws it: positions in logical px from the centre of its bounds.
    struct BubbleWakeView {
        struct Node {
            Vec2 position{};
            Vec2 direction{};  // unit tangent of the ribbon
            double half_width = 0;  // logical px
            double arc = 0;  // logical px along the ribbon from its first point
            double fade = 0;  // 0 fresh .. 1 gone
            double density = 0;
        };
        Vec2 size{};
        std::array<Node, K_BUBBLE_WAKE_NODES> nodes{};
        std::size_t node_count = 0;
        std::array<BubbleMicro, K_BUBBLE_WAKE_BUBBLES> bubbles{};
        std::size_t bubble_count = 0;
        double seed = 0;
    };

    // Soap film a moving bubble left behind, in screen space (monitor-local logical px): a ribbon
    // along the window's path that drains, breaks up and retracts, and the microbubbles that
    // pinched off into it, drifting to a stop before their film collapses. Independent of the
    // window once laid down.
    class BubbleWake {
      public:
        explicit BubbleWake(std::uint64_t seed = 0) noexcept;
        // Appends a point of the ribbon; false when the wake is full.
        bool extend(const BubbleTrailPoint &point, Vec2 origin) noexcept;
        // Takes a released microbubble; false when the wake carries as many as it can.
        bool release(const BubbleRelease &bubble, Vec2 origin) noexcept;
        void advance(double elapsed) noexcept;
        // Ages the film faster, for a wake that has to make room.
        void hurry() noexcept;
        [[nodiscard]] bool done() const noexcept;
        [[nodiscard]] bool full() const noexcept;
        // Every pixel the wake may draw, monitor-local logical px; empty when done.
        [[nodiscard]] Box bounds() const noexcept;
        void view(BubbleWakeView &out) const noexcept;
        // The newest ribbon point, to continue a stroke in another wake.
        [[nodiscard]] const BubbleTrailPoint *last() const noexcept;

      private:
        struct Node {
            BubbleTrailPoint point{};
            double arc = 0;
            double age = 0;
            double life = 0;
        };
        struct Free {
            BubbleMicro micro{};
            Vec2 velocity{};
            double age = 0;
            double life = 0;
            double thickness = 0;
        };
        std::array<Node, K_BUBBLE_WAKE_NODES> nodes{};
        std::size_t first = 0, count = 0;  // live nodes: [first, first + count)
        std::array<Free, K_BUBBLE_WAKE_BUBBLES> bubbles{};
        std::size_t bubble_count = 0;
        std::uint64_t random;
        double seed = 0;
        double pace = 1;
    };

}  // namespace hyprimate
