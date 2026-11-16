#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "animations/bubble/BubbleMotion.hpp"

namespace hyprimate {

    inline constexpr std::size_t kBubbleWakeNodes = 48;
    inline constexpr std::size_t kBubbleWakeBubbles = 12;

    // A wake as a renderer draws it: positions in logical px from the centre of its bounds.
    struct BubbleWakeView {
        struct Node {
            Vec2 position{};
            Vec2 direction{};  // unit tangent of the ribbon
            double halfWidth = 0;  // logical px
            double arc = 0;  // logical px along the ribbon from its first point
            double fade = 0;  // 0 fresh .. 1 gone
            double density = 0;
        };
        Vec2 size{};
        std::array<Node, kBubbleWakeNodes> nodes{};
        std::size_t nodeCount = 0;
        std::array<BubbleMicro, kBubbleWakeBubbles> bubbles{};
        std::size_t bubbleCount = 0;
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
        bool done() const noexcept;
        bool full() const noexcept;
        // Every pixel the wake may draw, monitor-local logical px; empty when done.
        Box bounds() const noexcept;
        void view(BubbleWakeView &out) const noexcept;
        // The newest ribbon point, to continue a stroke in another wake.
        const BubbleTrailPoint *last() const noexcept;

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
        std::array<Node, kBubbleWakeNodes> nodes_{};
        std::size_t first_ = 0, count_ = 0;  // live nodes: [first_, first_ + count_)
        std::array<Free, kBubbleWakeBubbles> bubbles_{};
        std::size_t bubbleCount_ = 0;
        std::uint64_t random_;
        double seed_ = 0;
        double pace_ = 1;
    };

}  // namespace hyprimate
