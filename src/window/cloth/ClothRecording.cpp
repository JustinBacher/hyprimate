#include "window/cloth/ClothRecording.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>

#include "window/cloth/ClothPrivate.hpp"

namespace hyprimate::cloth_detail {
    namespace {
        bool outside(const Run &run, std::span<const cloth::Point> positions) noexcept {
            const double WIDTH = run.window.w;
            const double HEIGHT = run.window.h;
            const double LENGTH_SCALE = std::sqrt(WIDTH * HEIGHT);
            const double FOCAL_LENGTH = 3 * LENGTH_SCALE;
            // NOLINTNEXTLINE(readability-use-anyofallof): GCC native algorithm alternatives add
            // setup and branch work.
            for (const auto position : positions) {
                const double SCALE = FOCAL_LENGTH / std::max(0.25 * FOCAL_LENGTH,
                                                        FOCAL_LENGTH - (position.z * LENGTH_SCALE));
                const double POS_X = run.window.x + (WIDTH / 2) + (position.x * WIDTH * SCALE);
                const double POS_Y =
                    run.window.y + (HEIGHT / 2) + (((-position.y * HEIGHT) - (HEIGHT / 2)) * SCALE);
                if ((run.edge == Edge::Bottom && POS_Y <= run.screen.y + run.screen.h) ||
                    (run.edge == Edge::Top && POS_Y >= run.screen.y) ||
                    (run.edge == Edge::Left && POS_X >= run.screen.x) ||
                    (run.edge == Edge::Right && POS_X <= run.screen.x + run.screen.w)) {
                    return false;
                }
            }
            return true;
        }
    }  // namespace

    void allocate_recording(Run &run) {
        // Recording length is determined at runtime.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        run.samples = std::make_unique<Run::Frame[]>(static_cast<std::size_t>(run.frames));
    }

    bool record(Run &run,
        int frame,
        std::span<const cloth::Point> positions,
        std::span<const cloth::Point> velocities) noexcept {
        const double TRAVEL = std::hypot(run.travel.x, run.travel.y);
        const double POSITION_LIMIT = 16 + TRAVEL + (GRAVITY * run.length * run.length) +
                                      (run.max_initial_speed * run.length);
        const double VELOCITY_LIMIT = 50 + (8 * TRAVEL / run.length) +
                                      (3 * GRAVITY * run.length) +
                                      (1.5 * run.max_initial_speed);
        const auto bounded = [](cloth::Point point, double limit) {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
                   std::abs(point.x) <= limit && std::abs(point.y) <= limit &&
                   std::abs(point.z) <= limit;
        };
        auto &samples = run.samples[static_cast<std::size_t>(frame)];
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const auto p = positions[i];
            const auto v = velocities[i];
            if (!bounded(p, POSITION_LIMIT) || !bounded(v, VELOCITY_LIMIT)) {
                return false;
            }
            samples[i] = {.position = {p.x, p.y, p.z}, .velocity = {v.x, v.y, v.z}};
        }
        if (run.kind == Transition::Close &&
            run.outside_frame.load(std::memory_order_relaxed) == run.frames &&
            outside(run, positions)) {
            run.outside_frame.store(frame, std::memory_order_release);
        }
        run.recorded.store(frame + 1, std::memory_order_release);
        return true;
    }
}  // namespace hyprimate::cloth_detail
