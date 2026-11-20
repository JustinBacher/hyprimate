#pragma once

#include <cstdint>
#include <vector>

#include "animations/paper/PaperCrumple.hpp"

namespace bench {
    // Capture the compact halves of independently seeded production trajectories before starting
    // GPU timers. No simulation, fixture I/O, fabrication of folds, or mesh copying in the timed
    // loop.
    inline std::vector<std::vector<hyprimate::PaperSurface>> paper_pose_bank(
        int windows, hyprimate::Vec2 size, float shading) {
        using namespace hyprimate;
        std::vector<std::vector<PaperSurface>> bank(static_cast<std::size_t>(windows));
        PaperParams params;
        params.fold_density = 1;
        params.duration = 0.5;
        params.shading = shading;
        for (int index = 0; index < windows; ++index) {
            auto &poses = bank[static_cast<std::size_t>(index)];
            poses.reserve(30);
            for (const auto kind : {Transition::Close, Transition::Open}) {
                PaperCrumple model;
                const auto seed = static_cast<std::uint64_t>(
                    441 + index + (kind == Transition::Open ? windows : 0));
                model.begin(kind, params, seed, size);
                for (int tick = 0; tick < 120; ++tick) {
                    const bool compact = kind == Transition::Close ? tick >= 60 : tick < 60;
                    if (compact && tick % 4 == 0) {
                        model.frame(poses.emplace_back());
                    }
                    if (!model.advance(1.0 / 240.0)) {
                        break;
                    }
                }
            }
        }
        return bank;
    }
}  // namespace bench
