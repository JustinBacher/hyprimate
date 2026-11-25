#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <numbers>
#include <span>

#include "window/bubble/Bubble.hpp"
#include "window/bubble/BubbleCurl.hpp"
#include "window/bubble/BubbleHoleGeometry.hpp"
#include "window/bubble/BubbleRuptureEvents.hpp"
#include "window/bubble/BubbleSheets.hpp"

namespace hyprimate {
    using detail::basis;
    using detail::BurstEvent;
    using detail::collapse;
    using detail::cross;
    using detail::dot;
    using detail::normalize;
    using detail::PI_VALUE;
    using detail::radius_at;
    namespace {
        BubbleHole sheet_hole(
            const BubbleSurface &surface, const BubbleSheet &sheet, std::size_t index) {
            BubbleHole hole = surface.holes[index];
            hole.radius = sheet.radii[index];
            for (std::size_t mode = 0; mode < hole.amplitude.size(); ++mode) {
                hole.amplitude.at(mode) =
                    sheet.onsets[index] * (.014 - (.002 * static_cast<double>(mode)));
            }
            return hole;
        }
        Vec3 sheet_material(
            const BubbleSurface &surface, const BubbleHole &hole, double phi, double fraction) {
            const double RADIUS = std::min(surface.half_size.x, surface.half_size.y);
            const double BOUNDARY = bubble_hole_boundary(hole, phi);
            const double WIDTH =
                bubble_curl_section(surface, hole, BOUNDARY * (surface.spherical ? RADIUS : 1), phi)
                    .width;
            const double DISTANCE =
                BOUNDARY + ((1 - fraction) * WIDTH / (surface.spherical ? RADIUS : 1));
            if (!surface.spherical) {
                return {
                    .x = hole.centre.x + (DISTANCE * std::cos(phi)),
                    .y = hole.centre.y + (DISTANCE * std::sin(phi)),
                    .z = 0,
                };
            }
            Vec3 basis_first;
            Vec3 basis_second;
            basis(hole.centre, basis_first, basis_second);
            const Vec3 DIR{
                .x = (basis_first.x * std::cos(phi)) + (basis_second.x * std::sin(phi)),
                .y = (basis_first.y * std::cos(phi)) + (basis_second.y * std::sin(phi)),
                .z = (basis_first.z * std::cos(phi)) + (basis_second.z * std::sin(phi)),
            };
            return {
                .x = (hole.centre.x * std::cos(DISTANCE)) + (DIR.x * std::sin(DISTANCE)),
                .y = (hole.centre.y * std::cos(DISTANCE)) + (DIR.y * std::sin(DISTANCE)),
                .z = (hole.centre.z * std::cos(DISTANCE)) + (DIR.z * std::sin(DISTANCE)),
            };
        }
        Vec3 rotate(Vec3 value, Vec3 axis, double angle) {
            const double COSINE = std::cos(angle);
            const double SINE = std::sin(angle);
            const double DOT_PRODUCT = dot(axis, value);
            const Vec3 CROSS_PRODUCT = cross(axis, value);
            return {
                .x = (value.x * COSINE) + (CROSS_PRODUCT.x * SINE) +
                     (axis.x * DOT_PRODUCT * (1 - COSINE)),
                .y = (value.y * COSINE) + (CROSS_PRODUCT.y * SINE) +
                     (axis.y * DOT_PRODUCT * (1 - COSINE)),
                .z = (value.z * COSINE) + (CROSS_PRODUCT.z * SINE) +
                     (axis.z * DOT_PRODUCT * (1 - COSINE)),
            };
        }
    }  // namespace

    bool bubble_sheet_contains(
        const BubbleSurface &surface, const BubbleSheet &sheet, Vec3 material) {
        if (sheet.hole >= std::min(surface.hole_count, K_BUBBLE_HOLES)) {
            return false;
        }
        const auto HOLE = sheet_hole(surface, sheet, sheet.hole);
        const double RADIUS = std::min(surface.half_size.x, surface.half_size.y);
        if (RADIUS <= 0) {
            return false;
        }
        const double PHI = bubble_hole_azimuth(HOLE, material, surface.spherical);
        const double HOLE_BOUNDARY = bubble_hole_boundary(HOLE, PHI);
        const double HOLE_DISTANCE =
            surface.spherical ? std::acos(std::clamp(dot(material, HOLE.centre), -1.0, 1.0))
                              : std::hypot(material.x - HOLE.centre.x, material.y - HOLE.centre.y);
        const double CURL_WIDTH = bubble_curl_section(
            surface, HOLE, HOLE_BOUNDARY * (surface.spherical ? RADIUS : 1), PHI)
                                      .width;
        if (CURL_WIDTH <= 0) {
            return false;
        }
        const double FRACTION =
            1 - ((HOLE_DISTANCE - HOLE_BOUNDARY) * (surface.spherical ? RADIUS : 1) / CURL_WIDTH);
        if (FRACTION < 0 || FRACTION > 1) {
            return false;
        }
        const double LIMIT = sheet.half_angle *
                             (.82 + (.14 * std::sin(PI_VALUE * FRACTION)) +
                                 (.04 * std::sin((3 * PI_VALUE * FRACTION) + HOLE.phase.front())));
        if (std::abs(std::remainder(PHI - sheet.phi, 2 * PI_VALUE)) > LIMIT) {
            return false;
        }
        const double OWN_RANK = 1 - FRACTION;
        const auto *radius = sheet.radii.begin();
        for (std::size_t i = 0; i < std::min(surface.hole_count, K_BUBBLE_HOLES); ++i, ++radius) {
            if (i == sheet.hole || *radius <= 0) {
                continue;
            }
            const auto OTHER = sheet_hole(surface, sheet, i);
            const double OTHER_PHI = bubble_hole_azimuth(OTHER, material, surface.spherical);
            const double BOUNDARY = bubble_hole_boundary(OTHER, OTHER_PHI);
            const double DISTANCE =
                surface.spherical
                    ? std::acos(std::clamp(dot(material, OTHER.centre), -1.0, 1.0))
                    : std::hypot(material.x - OTHER.centre.x, material.y - OTHER.centre.y);
            const double WIDTH = bubble_curl_section(
                surface, OTHER, BOUNDARY * (surface.spherical ? RADIUS : 1), OTHER_PHI)
                                     .width;
            if (WIDTH <= 0) {
                continue;
            }
            const double RANK = (DISTANCE - BOUNDARY) * (surface.spherical ? RADIUS : 1) / WIDTH;
            if (RANK >= 0 && RANK <= 1 &&
                (RANK < OWN_RANK || (RANK == OWN_RANK && i < sheet.hole))) {
                return false;
            }
        }
        return true;
    }

    BubbleCurlSample bubble_sheet_sample(
        const BubbleSurface &surface, const BubbleSheet &sheet, double phi, double fraction) {
        auto sample =
            bubble_peel_sample(surface, sheet_hole(surface, sheet, sheet.hole), phi, fraction);
        Vec3 relative{
            .x = sample.position.x - sheet.pivot.x,
            .y = sample.position.y - sheet.pivot.y,
            .z = sample.position.z - sheet.pivot.z,
        };
        relative = rotate(relative, sheet.axis, sheet.rotation);
        sample.position = {
            .x = sheet.pivot.x + sheet.translation.x + (relative.x * sheet.scale),
            .y = sheet.pivot.y + sheet.translation.y + (relative.y * sheet.scale),
            .z = sheet.pivot.z + sheet.translation.z + (relative.z * sheet.scale),
        };
        sample.normal = rotate(sample.normal, sheet.axis, sheet.rotation);
        return sample;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): Preserve candidate order and sheet
    // geometry FP evaluation.
    std::size_t bubble_sheets(const BubbleSurface &surface, std::span<BubbleSheet> out) {
        const std::size_t HOLE_COUNT = std::min(surface.hole_count, K_BUBBLE_HOLES);
        const double RADIUS = std::min(surface.half_size.x, surface.half_size.y);
        if (!surface.closing || HOLE_COUNT == 0 || surface.speed <= 0 || surface.rate <= 0 ||
            RADIUS <= 0 || out.empty()) {
            return 0;
        }
        const auto &events = collapse(surface);
        struct Candidate {
            const BurstEvent *event;
            double birth;
            std::size_t source, other;
        };
        const auto CANDIDATE_FOR = [&](const BurstEvent &event) {
            const Vec3 POINT = surface.spherical ? normalize(event.position) : event.position;
            std::size_t source = 0;
            std::size_t other = HOLE_COUNT;
            if (event.id != 1200) {
                source = (event.id - 1000) / 20;
                other = ((event.id - 1000) % 20) / 5;
            } else {
                double first = 1e100;
                std::size_t index = 0;
                for (const auto &hole : std::span(surface.holes).first(HOLE_COUNT)) {
                    const std::size_t HOLE_INDEX = index++;
                    const double ARRIVAL_DISTANCE =
                        surface.spherical
                            ? std::acos(std::clamp(dot(POINT, hole.centre), -1.0, 1.0))
                            : std::hypot(POINT.x - hole.centre.x, POINT.y - hole.centre.y);
                    const double ARRIVAL = hole.start + (ARRIVAL_DISTANCE / surface.speed);
                    if (ARRIVAL < first) {
                        first = ARRIVAL;
                        source = HOLE_INDEX;
                    }
                }
            }
            const auto WIDTH_AT = [&](std::size_t hole_index) {
                BubbleHole current_hole = surface.holes[hole_index];
                current_hole.radius = radius_at(surface, current_hole, event.time);
                const double ONSET = smoothstep(.012, .045, event.time - current_hole.start);
                for (std::size_t mode = 0; mode < current_hole.amplitude.size(); ++mode) {
                    current_hole.amplitude.at(mode) =
                        ONSET * (.014 - (.002 * static_cast<double>(mode)));
                }
                const double PHI = bubble_hole_azimuth(current_hole, POINT, surface.spherical);
                return bubble_curl_section(surface,
                    current_hole,
                    bubble_hole_boundary(current_hole, PHI) * (surface.spherical ? RADIUS : 1),
                    PHI)
                    .width;
            };
            double width = WIDTH_AT(source);
            if (other < HOLE_COUNT) {
                width = .5 * (width + WIDTH_AT(other));
            }
            const double BIRTH = std::max(surface.holes[source].start,
                event.time - (.55 * width / (surface.speed * (surface.spherical ? RADIUS : 1))));
            return Candidate{.event = &event, .birth = BIRTH, .source = source, .other = other};
        };
        std::array<Candidate, K_BUBBLE_MAX_SHEETS> candidates{};
        std::size_t total = 0;
        const auto INSERT = [&](const BurstEvent &event) {
            const auto CANDIDATE = CANDIDATE_FOR(event);
            auto *cursor = std::next(candidates.begin(), static_cast<std::ptrdiff_t>(total++));
            while (cursor != candidates.begin()) {
                auto *const PREVIOUS = std::prev(cursor);
                if (PREVIOUS->birth > CANDIDATE.birth ||
                    (PREVIOUS->birth == CANDIDATE.birth &&
                        PREVIOUS->event->id > CANDIDATE.event->id)) {
                    *cursor = *PREVIOUS;
                    cursor = PREVIOUS;
                } else {
                    break;
                }
            }
            *cursor = CANDIDATE;
        };
        for (const auto &merge :
            std::span(events.merges).first(std::min(events.merge_count, events.merges.size()))) {
            INSERT(merge);
        }
        const BurstEvent FINAL{.position = events.position, .time = events.time, .id = 1200};
        INSERT(FINAL);
        const auto SHEETS = out.first(std::min(out.size(), K_BUBBLE_MAX_SHEETS));
        std::size_t count = 0;
        auto sheet_out = SHEETS.begin();
        // Try the other side of a neck only if the first owns no visible material.
        for (std::size_t attempt = 0; attempt < total * 2 && count < SHEETS.size(); ++attempt) {
            const auto &candidate = candidates[attempt / 2];
            const auto &event = *candidate.event;
            const bool ALTERNATE = attempt % 2 != 0;
            if (ALTERNATE &&
                (event.id == 1200 || (count > 0 && std::prev(sheet_out)->id == event.id))) {
                continue;
            }
            const Vec3 POINT = surface.spherical ? normalize(event.position) : event.position;
            const std::size_t SOURCE = ALTERNATE ? candidate.other : candidate.source;
            double phi = bubble_hole_azimuth(surface.holes[SOURCE], POINT, surface.spherical);
            BubbleSheet sheet;
            sheet.id = event.id;
            sheet.hole = SOURCE;
            sheet.birth = candidate.birth;
            if (sheet.birth > surface.clock || sheet.birth >= 1) {
                continue;
            }
            sheet.phi = phi;
            auto *radius = sheet.radii.begin();
            auto *onset = sheet.onsets.begin();
            for (const auto &hole : std::span(surface.holes).first(HOLE_COUNT)) {
                *radius = radius_at(surface, hole, sheet.birth);
                ++radius;
                *onset = smoothstep(.012, .045, sheet.birth - hole.start);
                ++onset;
            }
            const auto HOLE = sheet_hole(surface, sheet, SOURCE);
            const double BOUNDARY = bubble_hole_boundary(HOLE, phi);
            const double WIDTH =
                bubble_curl_section(surface, HOLE, BOUNDARY * (surface.spherical ? RADIUS : 1), phi)
                    .width;
            if (WIDTH <= 0 || (surface.spherical && BOUNDARY >= PI_VALUE)) {
                continue;
            }
            sheet.half_angle = std::min(.35,
                std::clamp(.11 * RADIUS, 16.0, 40.0) /
                    std::max((BOUNDARY * (surface.spherical ? RADIUS : 1)) + (.5 * WIDTH), 1.0));
            bool available = false;
            // Centre the piece on surviving material, not on an occluded sliver's empty pivot.
            for (const double OFFSET : {0.0, -1.0 / 3, 1.0 / 3, -2.0 / 3, 2.0 / 3}) {
                const double TRIAL_PHI = phi + (sheet.half_angle * OFFSET);
                const Vec3 MATERIAL_POINT = sheet_material(surface, HOLE, TRIAL_PHI, .5);
                if (!surface.spherical) {
                    const double CORNER = std::clamp(surface.corner, 0.0, RADIUS);
                    const double CORNER_X =
                        std::max(std::abs(MATERIAL_POINT.x) - (surface.half_size.x - CORNER), 0.0);
                    const double CORNER_Y =
                        std::max(std::abs(MATERIAL_POINT.y) - (surface.half_size.y - CORNER), 0.0);
                    if (std::abs(MATERIAL_POINT.x) > surface.half_size.x ||
                        std::abs(MATERIAL_POINT.y) > surface.half_size.y ||
                        (CORNER_X * CORNER_X) + (CORNER_Y * CORNER_Y) > CORNER * CORNER) {
                        continue;
                    }
                }
                bool valid = bubble_sheet_contains(surface, sheet, MATERIAL_POINT);
                const auto *radius = sheet.radii.begin();
                for (std::size_t i = 0; i < HOLE_COUNT && valid; ++i, ++radius) {
                    if (i == SOURCE || *radius <= 0) {
                        continue;
                    }
                    const auto PRIOR = sheet_hole(surface, sheet, i);
                    const double DISTANCE =
                        surface.spherical
                            ? std::acos(std::clamp(dot(MATERIAL_POINT, PRIOR.centre), -1.0, 1.0))
                            : std::hypot(MATERIAL_POINT.x - PRIOR.centre.x,
                                  MATERIAL_POINT.y - PRIOR.centre.y);
                    const double PRIOR_PHI =
                        bubble_hole_azimuth(PRIOR, MATERIAL_POINT, surface.spherical);
                    if (DISTANCE < bubble_hole_boundary(PRIOR, PRIOR_PHI)) {
                        valid = false;
                    }
                }
                auto prior = SHEETS.begin();
                for (std::size_t i = 0; i < count && valid; ++i, ++prior) {
                    if (prior->birth <= sheet.birth &&
                        bubble_sheet_contains(surface, *prior, MATERIAL_POINT)) {
                        valid = false;
                    }
                }
                if (valid) {
                    phi = sheet.phi = TRIAL_PHI;
                    available = true;
                    break;
                }
            }
            if (!available) {
                continue;
            }
            sheet.pivot = bubble_peel_sample(surface, HOLE, phi, .5).position;
            const Vec3 BEFORE = bubble_peel_sample(surface, HOLE, phi - .001, .5).position;
            const Vec3 AFTER = bubble_peel_sample(surface, HOLE, phi + .001, .5).position;
            sheet.axis = normalize(
                {.x = AFTER.x - BEFORE.x, .y = AFTER.y - BEFORE.y, .z = AFTER.z - BEFORE.z});
            sheet.age = std::max(0.0, (surface.clock - sheet.birth) / surface.rate);
            sheet.life = std::min(.16, (1 - sheet.birth) / surface.rate);
            const double LIFE_FRACTION =
                sheet.life > 0 ? std::clamp(sheet.age / sheet.life, 0.0, 1.0) : 1;
            sheet.scale = 1 - smoothstep(0, 1, LIFE_FRACTION);
            sheet.rotation = 2.2 * LIFE_FRACTION;
            const auto ROOT = bubble_peel_sample(surface, HOLE, phi, 0).position;
            const Vec3 OUTWARD = normalize({
                .x = ROOT.x - (surface.spherical ? HOLE.centre.x * RADIUS : HOLE.centre.x),
                .y = ROOT.y - (surface.spherical ? HOLE.centre.y * RADIUS : HOLE.centre.y),
                .z = surface.spherical ? ROOT.z - (HOLE.centre.z * RADIUS) : 0,
            });
            const double TRAVEL = std::min(.16 * RADIUS, 80.0) * (1 - std::exp(-sheet.age / .08));
            sheet.translation = {
                .x = OUTWARD.x * TRAVEL,
                .y = (OUTWARD.y * TRAVEL) + (.04 * RADIUS * LIFE_FRACTION * LIFE_FRACTION),
                .z = (OUTWARD.z * TRAVEL) + (.05 * RADIUS * LIFE_FRACTION),
            };
            *sheet_out++ = sheet;
            ++count;
        }
        return count;
    }

}  // namespace hyprimate
