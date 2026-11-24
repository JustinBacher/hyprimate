// Bubble opening, closing and movement workloads, including the rupture debris queries a renderer
// issues each frame.
#include <array>

#include "Bench.hpp"
#include "window/bubble/Bubble.hpp"

namespace {
    using namespace hyprimate;
    constexpr double FRAME = 1.0 / 60.0;
    constexpr Vec2 SIZE{.x = 900, .y = 600};
    constexpr Box BODY{.x = -440, .y = -290, .w = 880, .h = 580};

    double surface_sum(const BubbleFrame &frame) {
        return frame.window.thickness + frame.shell.thickness + frame.window.ripple +
               static_cast<double>(frame.formation_count + frame.micro_count);
    }

    double bubble_open() {
        Bubble bubble;
        bubble.begin(Transition::Open, BubbleDuration{1.4}, 21, SIZE);
        double sum = 0;
        for (int i = 0; i < 90; ++i) {
            bubble.advance(FRAME);
            const auto &FRAME_OUT = bubble.frame(SIZE);
            sum += surface_sum(FRAME_OUT) + bubble.bounds(SIZE).w;
        }
        return sum;
    }

    double bubble_close() {
        Bubble bubble;
        bubble.begin(Transition::Close, BubbleDuration{1.0}, 9, SIZE);
        std::array<BubbleParticle, K_BUBBLE_MAX_PARTICLES> particles{};
        std::array<BubbleImpact, K_BUBBLE_MAX_IMPACTS> impacts{};
        double sum = 0;
        for (int i = 0; i < 80; ++i) {
            bubble.advance(FRAME);
            const auto &FRAME_OUT = bubble.frame(SIZE);
            sum += surface_sum(FRAME_OUT);
            sum += static_cast<double>(bubble_particles(FRAME_OUT.window, particles));
            sum += static_cast<double>(bubble_impacts(FRAME_OUT.window, impacts));
        }
        return sum;
    }

    double bubble_movement() {
        Bubble bubble;
        bubble.seed_motion(3);
        const BubbleMotionParams PARAMS{.strength = .65, .microbubbles = .5, .trail = .5, .roundness = .6};
        double sum = 0;
        for (int i = 0; i < 240; ++i) {
            // 1 s of dragging right and down, then 3 s of settling.
            const double SPEED = i < 60 ? 900.0 : 0.0;
            bubble.drive({.x = SPEED * FRAME, .y = SPEED * FRAME * 0.2});
            bubble.advance(FRAME, SIZE, BODY, PARAMS);
            sum += surface_sum(bubble.frame(SIZE));
            bubble.clear_emissions();
        }
        return sum;
    }
}  // namespace

namespace bench {
    void register_bubble(Registry &registry) {
        registry.push_back({"bubble.open", "Bubble formation opening + frame()", 90, bubble_open});
        registry.push_back({"bubble.close", "Bubble rupture + particle/impact queries", 80, bubble_close});
        registry.push_back({"bubble.move_settle", "Bubble drag 1 s then settle, microbubbles on", 240, bubble_movement});
    }
}  // namespace bench
