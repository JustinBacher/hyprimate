#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "window/bubble/BubbleFoam.hpp"
#include "window/bubble/BubbleMotion.hpp"

namespace hyprimate {

    inline constexpr std::size_t K_BUBBLE_WAKE_STRANDS = 6;
    inline constexpr std::size_t K_BUBBLE_WAKE_STRAND_NODES = 40;
    inline constexpr std::size_t K_BUBBLE_WAKE_NODES = K_BUBBLE_WAKE_STRANDS * K_BUBBLE_WAKE_STRAND_NODES;
    inline constexpr std::size_t K_BUBBLE_WAKE_SEGMENTS = 24;
    inline constexpr std::size_t K_BUBBLE_WAKE_BUBBLES = 24;

    // A wake as a renderer draws it: positions in logical px from the centre of its bounds.
    struct BubbleWakeView {
        struct Node {
            Vec2 position{};
            Vec2 direction{};  // unit tangent of the strand
            double half_width = 0;  // logical px
            double arc = 0;  // logical px along its strand from the strand's first point
            double thickness = 0;  // nm of liquid laid down along the strand's middle
            double evaporated = 0;  // nm lost to drying since
        };
        // A run of film between breaks, drawn as one strip over nodes [first, first + count):
        // only the arc range [from, to] holds film, so ends retract smoothly between nodes.
        struct Segment {
            std::size_t first = 0, count = 0;
            double from = 0, to = 0;
        };
        Vec2 size{};
        std::array<Node, K_BUBBLE_WAKE_NODES> nodes{};
        std::size_t node_count = 0;
        std::array<Segment, K_BUBBLE_WAKE_SEGMENTS> segments{};
        std::size_t segment_count = 0;
        std::array<BubbleMicro, K_BUBBLE_WAKE_BUBBLES> bubbles{};
        std::size_t bubble_count = 0;
        double seed = 0;
    };

    // Soap film a moving bubble left behind, in screen space (monitor-local logical px): the strands
    // drawn out of its trailing side, and the microbubbles that pinched off into it. Strands keep a
    // little of the window's motion, straighten, and dry as real film does: thinnest at their
    // borders, so they dry from the edges inward while the interference colours march in with the
    // shrinking thickness, until the last of it turns black and breaks into holes. Threads break up
    // into pieces and the odd droplet-sized bubble. The bubbles are a foam: they drift to a stop,
    // touch, coalesce and pop as their films drain. Independent of the window once laid down.
    class BubbleWake {
      public:
        explicit BubbleWake(std::uint64_t seed = 0) noexcept;
        // Lays a strand point; false when a new strand finds no room in this wake.
        bool extend(const BubbleTrailPoint &point, Vec2 origin) noexcept;
        // Takes a released microbubble; false when the wake carries as many as it can.
        bool release(const BubbleRelease &bubble, Vec2 origin) noexcept;
        void advance(double elapsed) noexcept;
        // Dries the film faster, for a wake that has to make room.
        void hurry() noexcept;
        [[nodiscard]] bool done() const noexcept;
        // Every pixel the wake may draw, monitor-local logical px; empty when done.
        [[nodiscard]] Box bounds() const noexcept;
        void view(BubbleWakeView &out) const noexcept;

      private:
        struct Node {
            Vec2 position{}, velocity{};
            double half_width = 0;
            double thickness = 0;  // nm
            double evaporation = 0;  // nm/s
            double evaporated = 0;  // nm
            double cut = 0;  // half length, px, of a gap a thread has broken open here
            bool provisional = false;
        };
        struct Strand {
            std::uint32_t id = 0;
            std::array<Node, K_BUBBLE_WAKE_STRAND_NODES> nodes{};
            std::size_t count = 0;
            double age = 0;
            double tail = 0, head = 0;  // px its free ends have retracted
            bool open = false;  // still drawn out of the window: its head is not free
            double idle = 0;  // s since the window last sent it a point
            bool thread = false;
        };
        struct Neck {
            Vec2 anchor{};
            double attach = 0;
        };
        Strand *strand(std::uint32_t strand_id) noexcept;
        static bool dry(const Node &node) noexcept;

        std::array<Strand, K_BUBBLE_WAKE_STRANDS> strands{};
        BubbleFoam foam{};
        std::array<Neck, K_BUBBLE_WAKE_BUBBLES> necks{};  // per foam slot: the neck it pinched off on
        std::uint64_t random;
        double seed = 0;
        double pace = 1;
        double substep = 0;
    };

}  // namespace hyprimate
