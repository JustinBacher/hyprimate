#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

#include "window/bubble/Bubble.hpp"
#include "window/bubble/BubbleHoleGeometry.hpp"
#include "window/bubble/BubbleRuptureEvents.hpp"

namespace hyprimate::detail {
        // NOLINTNEXTLINE(readability-function-cognitive-complexity): Preserve cache and merge-event
        // evaluation order.
        const CollapseCache &collapse(const BubbleSurface &surface) {
            static thread_local std::array<CollapseCache, 2> cache;
            auto &entry = surface.spherical ? cache.back() : cache.front();
            const auto HOLE_COUNT = std::min(surface.hole_count, K_BUBBLE_HOLES);
            bool same = entry.valid && entry.key.speed == surface.speed &&
                        entry.key.hole_count == surface.hole_count &&
                        entry.key.half_size.x == surface.half_size.x &&
                        entry.key.half_size.y == surface.half_size.y;
            const auto *cached = entry.key.holes.begin();
            const auto *current = surface.holes.begin();
            for (std::size_t i = 0; same && i < HOLE_COUNT; ++i) {
                const auto &cached_hole = *cached++;
                const auto &current_hole = *current++;
                same = cached_hole.start == current_hole.start &&
                       cached_hole.centre.x == current_hole.centre.x &&
                       cached_hole.centre.y == current_hole.centre.y &&
                       cached_hole.centre.z == current_hole.centre.z &&
                       cached_hole.phase == current_hole.phase;
            }
            if (same) {
                return entry;
            }
            entry.key = surface;
            entry.position = {};
            entry.time = 0;
            entry.merge_count = 0;
            entry.valid = true;
            const auto CANDIDATE = [&](Vec3 point) {
                double first = 1;
                for (const auto &hole : std::span(surface.holes).first(HOLE_COUNT)) {
                    const double CANDIDATE_DISTANCE =
                        surface.spherical
                            ? std::acos(std::clamp(dot(point, hole.centre), -1.0, 1.0))
                            : std::hypot(point.x - hole.centre.x, point.y - hole.centre.y);
                    first = std::min(first,
                        hole.start + (CANDIDATE_DISTANCE / surface.speed) +
                            (surface.spherical ? .05 : .075));
                }
                if (first > entry.time) {
                    entry.time = first;
                    entry.position = point;
                }
            };
            if (surface.spherical) {
                for (const Vec3 AXIS : {
                         Vec3{.x = 1, .y = 0, .z = 0},
                         Vec3{.x = -1, .y = 0, .z = 0},
                         Vec3{.x = 0, .y = 1, .z = 0},
                         Vec3{.x = 0, .y = -1, .z = 0},
                         Vec3{.x = 0, .y = 0, .z = 1},
                         Vec3{.x = 0, .y = 0, .z = -1},
                     }) {
                    CANDIDATE(AXIS);
                }
                for (const auto &hole : std::span(surface.holes).first(HOLE_COUNT)) {
                    CANDIDATE({
                        .x = -hole.centre.x,
                        .y = -hole.centre.y,
                        .z = -hole.centre.z,
                    });
                }
                entry.position = {
                    .x = entry.position.x * surface.half_size.x,
                    .y = entry.position.y * surface.half_size.x,
                    .z = entry.position.z * surface.half_size.x,
                };
            } else {
                for (int col = -1; col <= 1; ++col) {
                    for (int row = -1; row <= 1; ++row) {
                        CANDIDATE({
                            .x = col * surface.half_size.x,
                            .y = row * surface.half_size.y,
                            .z = 0,
                        });
                    }
                }
            }
            const auto *first_hole_it = surface.holes.begin();
            auto *merge = entry.merges.begin();
            for (std::size_t i = 0; i < HOLE_COUNT; ++i, ++first_hole_it) {
                const auto *second_hole_it = first_hole_it;
                ++second_hole_it;
                for (std::size_t j = i + 1; j < HOLE_COUNT; ++j, ++second_hole_it) {
                    const auto &first_hole = *first_hole_it;
                    const auto &second_hole = *second_hole_it;
                    const double DISTANCE =
                        surface.spherical
                            ? std::acos(
                                  std::clamp(dot(first_hole.centre, second_hole.centre), -1.0, 1.0))
                            : std::hypot(second_hole.centre.x - first_hole.centre.x,
                                  second_hole.centre.y - first_hole.centre.y);
                    double start_time = std::max(first_hole.start, second_hole.start);
                    double end_time = 1;
                    if (radius_at(surface, first_hole, start_time) >= DISTANCE ||
                        radius_at(surface, first_hole, end_time) +
                                radius_at(surface, second_hole, end_time) <
                            DISTANCE) {
                        continue;
                    }
                    for (int k = 0; k < 9; ++k) {
                        const double MID = (start_time + end_time) * .5;
                        if (radius_at(surface, first_hole, MID) +
                                radius_at(surface, second_hole, MID) <
                            DISTANCE) {
                            start_time = MID;
                        } else {
                            end_time = MID;
                        }
                    }
                    const double WEIGHT =
                        DISTANCE > 0 ? radius_at(surface, first_hole, end_time) / DISTANCE : 0;
                    Vec3 origin{
                        .x = (first_hole.centre.x * (1 - WEIGHT)) + (second_hole.centre.x * WEIGHT),
                        .y = (first_hole.centre.y * (1 - WEIGHT)) + (second_hole.centre.y * WEIGHT),
                        .z = (first_hole.centre.z * (1 - WEIGHT)) + (second_hole.centre.z * WEIGHT),
                    };
                    if (surface.spherical) {
                        const double DENOM = std::sin(DISTANCE);
                        if (std::abs(DENOM) > 1e-8) {
                            const double WEIGHT_A = std::sin((1 - WEIGHT) * DISTANCE) / DENOM;
                            const double WEIGHT_B = std::sin(WEIGHT * DISTANCE) / DENOM;
                            origin = {
                                .x = (first_hole.centre.x * WEIGHT_A) +
                                     (second_hole.centre.x * WEIGHT_B),
                                .y = (first_hole.centre.y * WEIGHT_A) +
                                     (second_hole.centre.y * WEIGHT_B),
                                .z = (first_hole.centre.z * WEIGHT_A) +
                                     (second_hole.centre.z * WEIGHT_B),
                            };
                        } else {
                            origin = normalize(origin);
                        }
                    } else if (std::abs(origin.x) > surface.half_size.x ||
                               std::abs(origin.y) > surface.half_size.y) {
                        continue;
                    }
                    bool consumed = false;
                    const auto *hole_it = surface.holes.begin();
                    for (std::size_t k = 0; k < HOLE_COUNT; ++k, ++hole_it) {
                        if (k == i || k == j || end_time <= hole_it->start) {
                            continue;
                        }
                        BubbleHole other = *hole_it;
                        other.radius = radius_at(surface, other, end_time);
                        const double ONSET = smoothstep(.012, .045, end_time - other.start);
                        std::size_t mode = 0;
                        for (auto &amplitude : other.amplitude) {
                            amplitude = ONSET * (.014 - (.002 * static_cast<double>(mode++)));
                        }
                        const double OTHER_DISTANCE =
                            surface.spherical
                                ? std::acos(std::clamp(dot(origin, other.centre), -1.0, 1.0))
                                : std::hypot(origin.x - other.centre.x, origin.y - other.centre.y);
                        if (OTHER_DISTANCE <
                            bubble_hole_boundary(
                                other, bubble_hole_azimuth(other, origin, surface.spherical))) {
                            consumed = true;
                        }
                    }
                    if (consumed) {
                        continue;
                    }
                    if (surface.spherical) {
                        origin = {
                            .x = origin.x * surface.half_size.x,
                            .y = origin.y * surface.half_size.x,
                            .z = origin.z * surface.half_size.x,
                        };
                    }
                    *merge = {
                        .position = origin,
                        .time = end_time,
                        .id = static_cast<std::uint32_t>(1000 + (20 * i) + (5 * j)),
                    };
                    ++merge;
                    ++entry.merge_count;
                }
            }
            return entry;
        }

}  // namespace hyprimate::detail
