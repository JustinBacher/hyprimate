#pragma once

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

#include "workspace/WorkspaceCapturePlan.hpp"

namespace hyprimate::workspace::test {

    struct FakeCaptureFailure {};
    using FakeCaptureEvent =
        std::variant<CaptureBoundary, CaptureWindowFacts, CaptureLayerFacts, FakeCaptureFailure>;

    // Executes the same ordered capture plans as the adapter without native objects
    // or GL. Native draws remain observable even after a retained capture fails.
    struct FakeWorkspaceCapture {
        bool enabled = false;
        CaptureFeatures features;
        std::vector<FakeCaptureEvent> events;
        std::vector<CapturePlane> boundaries;
        std::vector<CaptureRect> crops;
        std::size_t native_window_draws = 0;
        std::size_t native_layer_draws = 0;
        std::uint64_t retained_bytes = 0;
        std::uint64_t other_bytes = 0;
        bool wide_color = false;
        bool allocation_succeeds = true;
        bool native_succeeds = true;
        bool wallpaper_succeeds = true;
        bool selected_window_fadeout = false;
        struct Target {
            bool present = false;
            double width = 0;
            double height = 0;
            std::uint64_t bytes = 0;
        };
        std::vector<std::array<Target, 2>> targets;

        bool prepare_target(Target &target, const CaptureRect &crop) {
            const auto PLAN = capture_target_plan({.present = target.present,
                .matching_extent = target.width == crop.width && target.height == crop.height});
            if (PLAN.release) {
                retained_bytes -= target.bytes;
                target = {};
            }
            if (PLAN.allocate) {
                const auto BYTES = static_cast<std::uint64_t>(crop.width) *
                                   static_cast<std::uint64_t>(crop.height) * (wide_color ? 8 : 4);
                if (!capture_allocation_allowed({.bytes = BYTES,
                        .monitor_bytes = retained_bytes,
                        .total_bytes = retained_bytes + other_bytes,
                        .wide_color = wide_color}) ||
                    !allocation_succeeds) {
                    return false;
                }
                retained_bytes += BYTES;
                target = {
                    .present = true, .width = crop.width, .height = crop.height, .bytes = BYTES};
            }
            return true;
        }
        bool execute_window_plan(const CaptureWindowPlan &plan, std::size_t &used) {
            if (!plan.crop.has_value()) {
                return true;
            }
            if (targets.size() <= used) {
                targets.emplace_back();
            }
            auto &pair = targets[used++];
            const bool BACKDROP = prepare_target(pair[0], *plan.crop);
            const bool ACCUMULATED = prepare_target(pair[1], *plan.crop);
            if (!BACKDROP || !ACCUMULATED) {
                return false;
            }
            crops.push_back(*plan.crop);
            return true;
        }

        bool run() {
            CaptureSession session{features};
            bool failed = false;
            boundaries.clear();
            crops.clear();
            native_window_draws = 0;
            native_layer_draws = 0;
            if (!session.accepts_fadeout(selected_window_fadeout)) {
                return false;
            }
            std::size_t used = 0;
            for (const auto &event : events) {
                if (const auto *boundary = std::get_if<CaptureBoundary>(&event);
                    boundary != nullptr) {
                    const auto PLAN = session.boundary(*boundary);
                    for (std::size_t index = 0; index < PLAN.count; ++index) {
                        boundaries.push_back(PLAN.planes[index]);
                    }
                } else if (const auto *window = std::get_if<CaptureWindowFacts>(&event);
                    window != nullptr) {
                    const auto PLAN = failed ? CaptureWindowPlan{} : session.window(*window);
                    failed = !execute_window_plan(PLAN, used) || failed;
                    ++native_window_draws;
                } else if (const auto *layer = std::get_if<CaptureLayerFacts>(&event);
                    layer != nullptr) {
                    if (session.include_layer(*layer)) {
                        ++native_layer_draws;
                    }
                } else {
                    failed = true;
                }
            }
            for (std::size_t index = used; index < targets.size(); ++index) {
                for (const auto &target : targets[index]) {
                    retained_bytes -= target.bytes;
                }
            }
            targets.resize(used);
            return session.complete({.native_captured = native_succeeds && !failed,
                .wallpaper_captured = wallpaper_succeeds});
        }
    };

}  // namespace hyprimate::workspace::test
