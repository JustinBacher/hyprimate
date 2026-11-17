#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleCurl.hpp"
#include "animations/bubble/BubbleRupture.hpp"
#include "animations/bubble/BubbleSheets.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {
    namespace {
        constexpr double PI_VALUE = 3.14159265358979323846;

        Vec3 cross(Vec3 a, Vec3 b) {
            return {.x = (a.y * b.z) - (a.z * b.y),
                .y = (a.z * b.x) - (a.x * b.z),
                .z = (a.x * b.y) - (a.y * b.x)};
        }
        double dot(Vec3 a, Vec3 b) {
            return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
        }
        Vec3 normalize(Vec3 a) {
            const double LENGTH = std::sqrt(dot(a, a));
            return LENGTH > 0 ? Vec3{.x = a.x / LENGTH, .y = a.y / LENGTH, .z = a.z / LENGTH}
                              : Vec3{.x = 1, .y = 0, .z = 0};
        }
        void basis(Vec3 c, Vec3 &e1, Vec3 &e2) {
            e1 = normalize(cross(
                std::abs(c.z) < .9 ? Vec3{.x = 0, .y = 0, .z = 1} : Vec3{.x = 1, .y = 0, .z = 0},
                c));
            e2 = cross(c, e1);
        }
        double radiusAt(const BubbleSurface &s, const BubbleHole &h, double clock) {
            const double AGE = std::max(0.0, clock - h.start);
            const double TRANSIENT = s.spherical ? .05 : .075;
            return s.speed * (AGE - (TRANSIENT * (1 - std::exp(-AGE / TRANSIENT))));
        }
    }  // namespace

    double bubbleHoleAzimuth(const BubbleHole &hole, Vec3 p, bool spherical) {
        if (!spherical) {
            return std::atan2(p.y - hole.centre.y, p.x - hole.centre.x);
        }
        Vec3 basis_first;
        Vec3 basis_second;
        basis(hole.centre, basis_first, basis_second);
        return std::atan2(dot(p, basis_second), dot(p, basis_first));
    }

    double bubbleHoleBoundary(const BubbleHole &hole, double phi) {
        double factor = 1;
        for (std::size_t i = 0; i < kBubbleRimModes.size(); ++i) {
            factor += hole.amplitude[i] * std::cos((kBubbleRimModes[i] * phi) + hole.phase[i]);
        }
        return hole.radius * factor;
    }

    namespace {
        struct BurstEvent {
            Vec3 position{};
            double time = 0;
            std::uint32_t id = 0;
        };
        struct CollapseCache {
            BubbleSurface key{};
            Vec3 position{};
            double time = 0;
            std::array<BurstEvent, 6> merges{};
            std::size_t mergeCount = 0;
            bool valid = false;
        };

        const CollapseCache &collapse(const BubbleSurface &s) {
            static thread_local std::array<CollapseCache, 2> cache;
            auto &entry = cache[s.spherical ? 1 : 0];
            const auto HOLE_COUNT = std::min(s.holeCount, kBubbleHoles);
            bool same =
                entry.valid && entry.key.speed == s.speed && entry.key.holeCount == s.holeCount &&
                entry.key.halfSize.x == s.halfSize.x && entry.key.halfSize.y == s.halfSize.y;
            for (std::size_t i = 0; same && i < HOLE_COUNT; ++i) {
                const auto &cachedHole = entry.key.holes[i];
                const auto &currentHole = s.holes[i];
                same = cachedHole.start == currentHole.start &&
                       cachedHole.centre.x == currentHole.centre.x &&
                       cachedHole.centre.y == currentHole.centre.y &&
                       cachedHole.centre.z == currentHole.centre.z &&
                       cachedHole.phase == currentHole.phase;
            }
            if (same) {
                return entry;
            }
            entry.key = s;
            entry.position = {};
            entry.time = 0;
            entry.mergeCount = 0;
            entry.valid = true;
            const auto CANDIDATE = [&](Vec3 p) {
                double first = 1;
                for (std::size_t i = 0; i < HOLE_COUNT; ++i) {
                    const auto &hole = s.holes[i];
                    const double CANDIDATE_DISTANCE =
                        s.spherical ? std::acos(std::clamp(dot(p, hole.centre), -1.0, 1.0))
                                    : std::hypot(p.x - hole.centre.x, p.y - hole.centre.y);
                    first = std::min(first,
                        hole.start + (CANDIDATE_DISTANCE / s.speed) + (s.spherical ? .05 : .075));
                }
                if (first > entry.time) {
                    entry.time = first;
                    entry.position = p;
                }
            };
            if (s.spherical) {
                for (const Vec3 AXIS : {Vec3{.x = 1, .y = 0, .z = 0},
                         Vec3{.x = -1, .y = 0, .z = 0},
                         Vec3{.x = 0, .y = 1, .z = 0},
                         Vec3{.x = 0, .y = -1, .z = 0},
                         Vec3{.x = 0, .y = 0, .z = 1},
                         Vec3{.x = 0, .y = 0, .z = -1}}) {
                    CANDIDATE(AXIS);
                }
                for (std::size_t i = 0; i < HOLE_COUNT; ++i) {
                    CANDIDATE({.x = -s.holes[i].centre.x,
                        .y = -s.holes[i].centre.y,
                        .z = -s.holes[i].centre.z});
                }
                entry.position = {.x = entry.position.x * s.halfSize.x,
                    .y = entry.position.y * s.halfSize.x,
                    .z = entry.position.z * s.halfSize.x};
            } else {
                for (int col = -1; col <= 1; ++col) {
                    for (int row = -1; row <= 1; ++row) {
                        CANDIDATE({.x = col * s.halfSize.x, .y = row * s.halfSize.y, .z = 0});
                    }
                }
            }
            for (std::size_t i = 0; i < HOLE_COUNT; ++i) {
                for (std::size_t j = i + 1; j < HOLE_COUNT; ++j) {
                    const auto &firstHole = s.holes[i];
                    const auto &secondHole = s.holes[j];
                    const double DISTANCE =
                        s.spherical ? std::acos(std::clamp(
                                          dot(firstHole.centre, secondHole.centre), -1.0, 1.0))
                                    : std::hypot(secondHole.centre.x - firstHole.centre.x,
                                          secondHole.centre.y - firstHole.centre.y);
                    double startTime = std::max(firstHole.start, secondHole.start);
                    double endTime = 1;
                    if (radiusAt(s, firstHole, startTime) >= DISTANCE ||
                        radiusAt(s, firstHole, endTime) + radiusAt(s, secondHole, endTime) <
                            DISTANCE) {
                        continue;
                    }
                    for (int k = 0; k < 9; ++k) {
                        const double MID = (startTime + endTime) * .5;
                        if (radiusAt(s, firstHole, MID) + radiusAt(s, secondHole, MID) < DISTANCE) {
                            startTime = MID;
                        } else {
                            endTime = MID;
                        }
                    }
                    const double WEIGHT =
                        DISTANCE > 0 ? radiusAt(s, firstHole, endTime) / DISTANCE : 0;
                    Vec3 origin{
                        .x = (firstHole.centre.x * (1 - WEIGHT)) + (secondHole.centre.x * WEIGHT),
                        .y = (firstHole.centre.y * (1 - WEIGHT)) + (secondHole.centre.y * WEIGHT),
                        .z = (firstHole.centre.z * (1 - WEIGHT)) + (secondHole.centre.z * WEIGHT)};
                    if (s.spherical) {
                        const double DENOM = std::sin(DISTANCE);
                        if (std::abs(DENOM) > 1e-8) {
                            const double WEIGHT_A = std::sin((1 - WEIGHT) * DISTANCE) / DENOM;
                            const double WEIGHT_B = std::sin(WEIGHT * DISTANCE) / DENOM;
                            origin = {.x = (firstHole.centre.x * WEIGHT_A) +
                                           (secondHole.centre.x * WEIGHT_B),
                                .y = (firstHole.centre.y * WEIGHT_A) +
                                     (secondHole.centre.y * WEIGHT_B),
                                .z = (firstHole.centre.z * WEIGHT_A) +
                                     (secondHole.centre.z * WEIGHT_B)};
                        } else {
                            origin = normalize(origin);
                        }
                    } else if (std::abs(origin.x) > s.halfSize.x ||
                               std::abs(origin.y) > s.halfSize.y) {
                        continue;
                    }
                    bool consumed = false;
                    for (std::size_t k = 0; k < HOLE_COUNT; ++k) {
                        if (k == i || k == j || endTime <= s.holes[k].start) {
                            continue;
                        }
                        BubbleHole other = s.holes[k];
                        other.radius = radiusAt(s, other, endTime);
                        const double ONSET = smoothstep(.012, .045, endTime - other.start);
                        for (std::size_t mode = 0; mode < other.amplitude.size(); ++mode) {
                            other.amplitude[mode] = ONSET * (.014 - (.002 * mode));
                        }
                        const double OTHER_DISTANCE =
                            s.spherical
                                ? std::acos(std::clamp(dot(origin, other.centre), -1.0, 1.0))
                                : std::hypot(origin.x - other.centre.x, origin.y - other.centre.y);
                        if (OTHER_DISTANCE < bubbleHoleBoundary(other,
                                                 bubbleHoleAzimuth(other, origin, s.spherical))) {
                            consumed = true;
                        }
                    }
                    if (consumed) {
                        continue;
                    }
                    if (s.spherical) {
                        origin = {.x = origin.x * s.halfSize.x,
                            .y = origin.y * s.halfSize.x,
                            .z = origin.z * s.halfSize.x};
                    }
                    entry.merges[entry.mergeCount++] = {.position = origin,
                        .time = endTime,
                        .id = static_cast<std::uint32_t>(1000 + (20 * i) + (5 * j))};
                }
            }
            return entry;
        }
    }  // namespace

    namespace {
        BubbleHole sheetHole(const BubbleSurface &s, const BubbleSheet &sheet, std::size_t i) {
            BubbleHole hole = s.holes[i];
            hole.radius = sheet.radii[i];
            for (std::size_t mode = 0; mode < hole.amplitude.size(); ++mode) {
                hole.amplitude[mode] = sheet.onsets[i] * (.014 - (.002 * mode));
            }
            return hole;
        }
        Vec3 sheetMaterial(const BubbleSurface &s, const BubbleHole &h, double phi, double u) {
            const double RADIUS = std::min(s.halfSize.x, s.halfSize.y);
            const double BOUNDARY = bubbleHoleBoundary(h, phi);
            const double WIDTH =
                bubbleCurlSection(s, h, BOUNDARY * (s.spherical ? RADIUS : 1), phi).width;
            const double DISTANCE = BOUNDARY + ((1 - u) * WIDTH / (s.spherical ? RADIUS : 1));
            if (!s.spherical) {
                return {.x = h.centre.x + (DISTANCE * std::cos(phi)),
                    .y = h.centre.y + (DISTANCE * std::sin(phi)),
                    .z = 0};
            }
            Vec3 basis_first;
            Vec3 basis_second;
            basis(h.centre, basis_first, basis_second);
            const Vec3 DIR{.x = (basis_first.x * std::cos(phi)) + (basis_second.x * std::sin(phi)),
                .y = (basis_first.y * std::cos(phi)) + (basis_second.y * std::sin(phi)),
                .z = (basis_first.z * std::cos(phi)) + (basis_second.z * std::sin(phi))};
            return {.x = (h.centre.x * std::cos(DISTANCE)) + (DIR.x * std::sin(DISTANCE)),
                .y = (h.centre.y * std::cos(DISTANCE)) + (DIR.y * std::sin(DISTANCE)),
                .z = (h.centre.z * std::cos(DISTANCE)) + (DIR.z * std::sin(DISTANCE))};
        }
        Vec3 rotate(Vec3 v, Vec3 axis, double angle) {
            const double COSINE = std::cos(angle);
            const double SINE = std::sin(angle);
            const double DOT_PRODUCT = dot(axis, v);
            const Vec3 CROSS_PRODUCT = cross(axis, v);
            return {.x = (v.x * COSINE) + (CROSS_PRODUCT.x * SINE) +
                         (axis.x * DOT_PRODUCT * (1 - COSINE)),
                .y = (v.y * COSINE) + (CROSS_PRODUCT.y * SINE) +
                     (axis.y * DOT_PRODUCT * (1 - COSINE)),
                .z = (v.z * COSINE) + (CROSS_PRODUCT.z * SINE) +
                     (axis.z * DOT_PRODUCT * (1 - COSINE))};
        }
    }  // namespace

    bool bubbleSheetContains(const BubbleSurface &s, const BubbleSheet &sheet, Vec3 material) {
        if (sheet.hole >= std::min(s.holeCount, kBubbleHoles)) {
            return false;
        }
        const auto HOLE = sheetHole(s, sheet, sheet.hole);
        const double RADIUS = std::min(s.halfSize.x, s.halfSize.y);
        if (RADIUS <= 0) {
            return false;
        }
        const double PHI = bubbleHoleAzimuth(HOLE, material, s.spherical);
        const double HOLE_BOUNDARY = bubbleHoleBoundary(HOLE, PHI);
        const double HOLE_DISTANCE =
            s.spherical ? std::acos(std::clamp(dot(material, HOLE.centre), -1.0, 1.0))
                        : std::hypot(material.x - HOLE.centre.x, material.y - HOLE.centre.y);
        const double CURL_WIDTH =
            bubbleCurlSection(s, HOLE, HOLE_BOUNDARY * (s.spherical ? RADIUS : 1), PHI).width;
        if (CURL_WIDTH <= 0) {
            return false;
        }
        const double FRACTION =
            1 - ((HOLE_DISTANCE - HOLE_BOUNDARY) * (s.spherical ? RADIUS : 1) / CURL_WIDTH);
        if (FRACTION < 0 || FRACTION > 1) {
            return false;
        }
        const double LIMIT =
            sheet.halfAngle * (.82 + (.14 * std::sin(PI_VALUE * FRACTION)) +
                                  (.04 * std::sin((3 * PI_VALUE * FRACTION) + HOLE.phase[0])));
        if (std::abs(std::remainder(PHI - sheet.phi, 2 * PI_VALUE)) > LIMIT) {
            return false;
        }
        const double OWN_RANK = 1 - FRACTION;
        for (std::size_t i = 0; i < std::min(s.holeCount, kBubbleHoles); ++i) {
            if (i == sheet.hole || sheet.radii[i] <= 0) {
                continue;
            }
            const auto OTHER = sheetHole(s, sheet, i);
            const double OTHER_PHI = bubbleHoleAzimuth(OTHER, material, s.spherical);
            const double BOUNDARY = bubbleHoleBoundary(OTHER, OTHER_PHI);
            const double DISTANCE =
                s.spherical ? std::acos(std::clamp(dot(material, OTHER.centre), -1.0, 1.0))
                            : std::hypot(material.x - OTHER.centre.x, material.y - OTHER.centre.y);
            const double WIDTH =
                bubbleCurlSection(s, OTHER, BOUNDARY * (s.spherical ? RADIUS : 1), OTHER_PHI).width;
            if (WIDTH <= 0) {
                continue;
            }
            const double RANK = (DISTANCE - BOUNDARY) * (s.spherical ? RADIUS : 1) / WIDTH;
            if (RANK >= 0 && RANK <= 1 &&
                (RANK < OWN_RANK || (RANK == OWN_RANK && i < sheet.hole))) {
                return false;
            }
        }
        return true;
    }

    BubbleCurlSample bubbleSheetSample(
        const BubbleSurface &s, const BubbleSheet &sheet, double phi, double u) {
        auto sample = bubblePeelSample(s, sheetHole(s, sheet, sheet.hole), phi, u);
        Vec3 relative{.x = sample.position.x - sheet.pivot.x,
            .y = sample.position.y - sheet.pivot.y,
            .z = sample.position.z - sheet.pivot.z};
        relative = rotate(relative, sheet.axis, sheet.rotation);
        sample.position = {.x = sheet.pivot.x + sheet.translation.x + (relative.x * sheet.scale),
            .y = sheet.pivot.y + sheet.translation.y + (relative.y * sheet.scale),
            .z = sheet.pivot.z + sheet.translation.z + (relative.z * sheet.scale)};
        sample.normal = rotate(sample.normal, sheet.axis, sheet.rotation);
        return sample;
    }

    std::size_t bubbleSheets(const BubbleSurface &s, std::span<BubbleSheet> out) {
        const std::size_t HOLE_COUNT = std::min(s.holeCount, kBubbleHoles);
        const double RADIUS = std::min(s.halfSize.x, s.halfSize.y);
        if (!s.closing || HOLE_COUNT == 0 || s.speed <= 0 || s.rate <= 0 || RADIUS <= 0 ||
            out.empty()) {
            return 0;
        }
        const auto &events = collapse(s);
        struct Candidate {
            const BurstEvent *event;
            double birth;
            std::size_t source, other;
        };
        const auto CANDIDATE_FOR = [&](const BurstEvent &event) {
            const Vec3 POINT = s.spherical ? normalize(event.position) : event.position;
            std::size_t source = 0;
            std::size_t other = HOLE_COUNT;
            if (event.id != 1200) {
                source = (event.id - 1000) / 20;
                other = ((event.id - 1000) % 20) / 5;
            } else {
                double first = 1e100;
                for (std::size_t i = 0; i < HOLE_COUNT; ++i) {
                    const double ARRIVAL_DISTANCE =
                        s.spherical
                            ? std::acos(std::clamp(dot(POINT, s.holes[i].centre), -1.0, 1.0))
                            : std::hypot(
                                  POINT.x - s.holes[i].centre.x, POINT.y - s.holes[i].centre.y);
                    const double ARRIVAL = s.holes[i].start + (ARRIVAL_DISTANCE / s.speed);
                    if (ARRIVAL < first) {
                        first = ARRIVAL;
                        source = i;
                    }
                }
            }
            const auto WIDTH_AT = [&](std::size_t i) {
                BubbleHole currentHole = s.holes[i];
                currentHole.radius = radiusAt(s, currentHole, event.time);
                const double ONSET = smoothstep(.012, .045, event.time - currentHole.start);
                for (std::size_t mode = 0; mode < currentHole.amplitude.size(); ++mode) {
                    currentHole.amplitude[mode] = ONSET * (.014 - (.002 * mode));
                }
                const double PHI = bubbleHoleAzimuth(currentHole, POINT, s.spherical);
                return bubbleCurlSection(s,
                    currentHole,
                    bubbleHoleBoundary(currentHole, PHI) * (s.spherical ? RADIUS : 1),
                    PHI)
                    .width;
            };
            double width = WIDTH_AT(source);
            if (other < HOLE_COUNT) {
                width = .5 * (width + WIDTH_AT(other));
            }
            const double BIRTH = std::max(s.holes[source].start,
                event.time - (.55 * width / (s.speed * (s.spherical ? RADIUS : 1))));
            return Candidate{.event = &event, .birth = BIRTH, .source = source, .other = other};
        };
        std::array<Candidate, kBubbleMaxSheets> candidates{};
        std::size_t total = 0;
        const auto INSERT = [&](const BurstEvent &event) {
            const auto CANDIDATE = CANDIDATE_FOR(event);
            std::size_t index = total++;
            while (index > 0 && (candidates[index - 1].birth > CANDIDATE.birth ||
                                    (candidates[index - 1].birth == CANDIDATE.birth &&
                                        candidates[index - 1].event->id > CANDIDATE.event->id))) {
                candidates[index] = candidates[index - 1];
                --index;
            }
            candidates[index] = CANDIDATE;
        };
        for (std::size_t i = 0; i < std::min(events.mergeCount, events.merges.size()); ++i) {
            INSERT(events.merges[i]);
        }
        const BurstEvent FINAL{.position = events.position, .time = events.time, .id = 1200};
        INSERT(FINAL);
        const auto SHEETS = out.first(std::min(out.size(), kBubbleMaxSheets));
        std::size_t count = 0;
        // Try the other side of a neck only if the first owns no visible material.
        for (std::size_t attempt = 0; attempt < total * 2 && count < SHEETS.size(); ++attempt) {
            const auto &candidate = candidates[attempt / 2];
            const auto &event = *candidate.event;
            const bool ALTERNATE = attempt % 2 != 0;
            if (ALTERNATE &&
                (event.id == 1200 || (count > 0 && SHEETS[count - 1].id == event.id))) {
                continue;
            }
            const Vec3 POINT = s.spherical ? normalize(event.position) : event.position;
            const std::size_t SOURCE = ALTERNATE ? candidate.other : candidate.source;
            double phi = bubbleHoleAzimuth(s.holes[SOURCE], POINT, s.spherical);
            BubbleSheet sheet;
            sheet.id = event.id;
            sheet.hole = SOURCE;
            sheet.birth = candidate.birth;
            if (sheet.birth > s.clock || sheet.birth >= 1) {
                continue;
            }
            sheet.phi = phi;
            for (std::size_t i = 0; i < HOLE_COUNT; ++i) {
                sheet.radii[i] = radiusAt(s, s.holes[i], sheet.birth);
                sheet.onsets[i] = smoothstep(.012, .045, sheet.birth - s.holes[i].start);
            }
            const auto HOLE = sheetHole(s, sheet, SOURCE);
            const double BOUNDARY = bubbleHoleBoundary(HOLE, phi);
            const double WIDTH =
                bubbleCurlSection(s, HOLE, BOUNDARY * (s.spherical ? RADIUS : 1), phi).width;
            if (WIDTH <= 0 || (s.spherical && BOUNDARY >= PI_VALUE)) {
                continue;
            }
            sheet.halfAngle = std::min(.35,
                std::clamp(.11 * RADIUS, 16.0, 40.0) /
                    std::max((BOUNDARY * (s.spherical ? RADIUS : 1)) + (.5 * WIDTH), 1.0));
            bool available = false;
            // Centre the piece on surviving material, not on an occluded sliver's empty pivot.
            for (const double OFFSET : {0.0, -1.0 / 3, 1.0 / 3, -2.0 / 3, 2.0 / 3}) {
                const double TRIAL_PHI = phi + (sheet.halfAngle * OFFSET);
                const Vec3 MATERIAL_POINT = sheetMaterial(s, HOLE, TRIAL_PHI, .5);
                if (!s.spherical) {
                    const double CORNER = std::clamp(s.corner, 0.0, RADIUS);
                    const double CORNER_X =
                        std::max(std::abs(MATERIAL_POINT.x) - (s.halfSize.x - CORNER), 0.0);
                    const double CORNER_Y =
                        std::max(std::abs(MATERIAL_POINT.y) - (s.halfSize.y - CORNER), 0.0);
                    if (std::abs(MATERIAL_POINT.x) > s.halfSize.x ||
                        std::abs(MATERIAL_POINT.y) > s.halfSize.y ||
                        (CORNER_X * CORNER_X) + (CORNER_Y * CORNER_Y) > CORNER * CORNER) {
                        continue;
                    }
                }
                bool valid = bubbleSheetContains(s, sheet, MATERIAL_POINT);
                for (std::size_t i = 0; i < HOLE_COUNT && valid; ++i) {
                    if (i == SOURCE || sheet.radii[i] <= 0) {
                        continue;
                    }
                    const auto PRIOR = sheetHole(s, sheet, i);
                    const double DISTANCE =
                        s.spherical
                            ? std::acos(std::clamp(dot(MATERIAL_POINT, PRIOR.centre), -1.0, 1.0))
                            : std::hypot(MATERIAL_POINT.x - PRIOR.centre.x,
                                  MATERIAL_POINT.y - PRIOR.centre.y);
                    const double PRIOR_PHI = bubbleHoleAzimuth(PRIOR, MATERIAL_POINT, s.spherical);
                    if (DISTANCE < bubbleHoleBoundary(PRIOR, PRIOR_PHI)) {
                        valid = false;
                    }
                }
                for (std::size_t i = 0; i < count && valid; ++i) {
                    if (SHEETS[i].birth <= sheet.birth &&
                        bubbleSheetContains(s, SHEETS[i], MATERIAL_POINT)) {
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
            sheet.pivot = bubblePeelSample(s, HOLE, phi, .5).position;
            const Vec3 BEFORE = bubblePeelSample(s, HOLE, phi - .001, .5).position;
            const Vec3 AFTER = bubblePeelSample(s, HOLE, phi + .001, .5).position;
            sheet.axis = normalize(
                {.x = AFTER.x - BEFORE.x, .y = AFTER.y - BEFORE.y, .z = AFTER.z - BEFORE.z});
            sheet.age = std::max(0.0, (s.clock - sheet.birth) / s.rate);
            sheet.life = std::min(.16, (1 - sheet.birth) / s.rate);
            const double LIFE_FRACTION =
                sheet.life > 0 ? std::clamp(sheet.age / sheet.life, 0.0, 1.0) : 1;
            sheet.scale = 1 - smoothstep(0, 1, LIFE_FRACTION);
            sheet.rotation = 2.2 * LIFE_FRACTION;
            const auto ROOT = bubblePeelSample(s, HOLE, phi, 0).position;
            const Vec3 OUTWARD =
                normalize({.x = ROOT.x - (s.spherical ? HOLE.centre.x * RADIUS : HOLE.centre.x),
                    .y = ROOT.y - (s.spherical ? HOLE.centre.y * RADIUS : HOLE.centre.y),
                    .z = s.spherical ? ROOT.z - (HOLE.centre.z * RADIUS) : 0});
            const double TRAVEL = std::min(.16 * RADIUS, 80.0) * (1 - std::exp(-sheet.age / .08));
            sheet.translation = {.x = OUTWARD.x * TRAVEL,
                .y = (OUTWARD.y * TRAVEL) + (.04 * RADIUS * LIFE_FRACTION * LIFE_FRACTION),
                .z = (OUTWARD.z * TRAVEL) + (.05 * RADIUS * LIFE_FRACTION)};
            SHEETS[count++] = sheet;
        }
        return count;
    }
    namespace {
        struct ImpactCache {
            BubbleSurface key{};
            std::array<BubbleImpact, kBubbleMaxImpacts> impacts{};
            std::array<std::uint32_t, kBubbleMaxImpacts> ids{};
            std::size_t count = 0;
            bool valid = false;
        };
        const ImpactCache &impactCache(const BubbleSurface &surface);
    }  // namespace

    static std::size_t debris(const BubbleSurface &s,
        std::span<BubbleParticle> out,
        std::span<BubbleImpact> impacts,
        std::span<detail::IdentifiedParticle> identified,
        bool impactMode,
        std::span<std::uint32_t> impactIds = {}) {
        if (!s.closing || s.speed <= 0 || s.rate <= 0 || s.halfSize.x <= 0 || s.halfSize.y <= 0) {
            return 0;
        }
        if (!impactMode) {
            bool started = false;
            for (std::size_t i = 0; i < std::min(s.holeCount, kBubbleHoles); ++i) {
                started |= s.clock > s.holes[i].start;
            }
            if (!started) {
                return 0;
            }
        }
        const ImpactCache *selected = impactMode ? nullptr : &impactCache(s);
        std::size_t count = 0;
        for (std::size_t hi = 0; hi < std::min(s.holeCount, kBubbleHoles); ++hi) {
            const auto &hole = s.holes[hi];
            const double MIN_HALF = std::min(s.halfSize.x, s.halfSize.y);
            const double THRESHOLD =
                std::max(24.0, .08 * MIN_HALF) / (s.spherical ? s.halfSize.x : 1);
            if (!impactMode && hole.radius < THRESHOLD) {
                continue;
            }
            const double TRANSIENT = s.spherical ? .05 : .075;
            const double UNITS_PER_PIXEL = s.spherical ? 1 / s.halfSize.x : 1;
            std::uint64_t holeRng = s.debris ^ (hi * 0x9e3779b97f4a7c15ULL) ^ 0x94d049bb133111ebULL;
            const int FINGER_COUNT = 14 + static_cast<int>(13 * unit_draw(holeRng));
            Vec3 basis_first{};
            Vec3 basis_second{};
            if (s.spherical) {
                basis(hole.centre, basis_first, basis_second);
            }
            for (int finger = 0; finger < FINGER_COUNT; ++finger) {
                if (impactMode && count >= (s.spherical ? 3U : 2U)) {
                    break;
                }
                std::uint64_t rng =
                    s.debris ^ (hi * 0x9e3779b97f4a7c15ULL) ^ (finger * 0xbf58476d1ce4e5b9ULL);
                if (unit_draw(rng) < .27) {
                    continue;
                }
                const double PHI =
                    2 * PI_VALUE * (finger + (.9 * (unit_draw(rng) - .5))) / FINGER_COUNT;
                const int ORDER = static_cast<int>(FINGER_COUNT * unit_draw(rng));
                const double SITE_RADIUS =
                    std::max(THRESHOLD, (ORDER + 1) * 50 / (2 * PI_VALUE) * UNITS_PER_PIXEL);
                if (!impactMode && hole.radius < SITE_RADIUS) {
                    continue;
                }
                const double TARGET = SITE_RADIUS / s.speed;
                double onsetAge = TARGET + TRANSIENT;
                for (int i = 0; i < 4; ++i) {
                    const double EXP_AGE = std::exp(-onsetAge / TRANSIENT);
                    onsetAge -= (onsetAge - (TRANSIENT * (1 - EXP_AGE)) - TARGET) / (1 - EXP_AGE);
                }
                const double FINGER_START = hole.start + onsetAge;
                const double DELAY = .08 * s.rate * unit_draw(rng);
                const double PERIOD = (.05 + (.04 * unit_draw(rng))) * s.rate;
                const double LENGTH = 6 + (16 * unit_draw(rng));
                const double THREAD_RADIUS = .4 + (.5 * unit_draw(rng));
                const double TILT = .7 * (unit_draw(rng) - .5);
                const double COS_PHI = std::cos(PHI);
                const double SIN_PHI = std::sin(PHI);
                const Vec3 DIRECTION =
                    s.spherical ? Vec3{.x = (basis_first.x * COS_PHI) + (basis_second.x * SIN_PHI),
                                      .y = (basis_first.y * COS_PHI) + (basis_second.y * SIN_PHI),
                                      .z = (basis_first.z * COS_PHI) + (basis_second.z * SIN_PHI)}
                                : Vec3{.x = COS_PHI, .y = SIN_PHI, .z = 0};
                double corrugation = 0;
                double currentCorrugation = 0;
                for (std::size_t rimMode = 0; rimMode < kBubbleRimModes.size(); ++rimMode) {
                    const double WAVE =
                        std::cos((kBubbleRimModes[rimMode] * PHI) + hole.phase[rimMode]);
                    corrugation += (.014 - (.002 * rimMode)) * WAVE;
                    currentCorrugation += hole.amplitude[rimMode] * WAVE;
                }
                const auto POINT_AT = [&](double radius) {
                    if (!s.spherical) {
                        return Vec3{.x = hole.centre.x + (DIRECTION.x * radius),
                            .y = hole.centre.y + (DIRECTION.y * radius),
                            .z = 0};
                    }
                    const double RADIUS_COSINE = std::cos(radius);
                    const double RADIUS_SINE = std::sin(radius);
                    const double SCALE = s.halfSize.x;
                    return Vec3{.x = SCALE * ((hole.centre.x * RADIUS_COSINE) +
                                                 (DIRECTION.x * RADIUS_SINE)),
                        .y =
                            SCALE * ((hole.centre.y * RADIUS_COSINE) + (DIRECTION.y * RADIUS_SINE)),
                        .z = SCALE *
                             ((hole.centre.z * RADIUS_COSINE) + (DIRECTION.z * RADIUS_SINE))};
                };
                const auto EDGE = [&](double boundary) {
                    BubbleHole frozen = hole;
                    frozen.radius = boundary;
                    frozen.amplitude = {};
                    return bubblePeelSample(s, frozen, PHI, 1).position;
                };
                const auto TIP_AT = [&](double boundary, Vec3 root) {
                    const Vec3 BASE = POINT_AT(boundary);
                    const Vec3 TIP = POINT_AT(std::max(0.0, boundary - (LENGTH * UNITS_PER_PIXEL)));
                    const double BEAD_DELTA_X = TIP.x - BASE.x;
                    const double BEAD_DELTA_Y = TIP.y - BASE.y;
                    return Vec3{.x = root.x + (BEAD_DELTA_X * std::cos(TILT)) -
                                     (BEAD_DELTA_Y * std::sin(TILT)),
                        .y = root.y + (BEAD_DELTA_X * std::sin(TILT)) +
                             (BEAD_DELTA_Y * std::cos(TILT)),
                        .z = root.z + TIP.z - BASE.z};
                };
                const double FACTOR = .55 + (.4 * unit_draw(rng));
                const bool BEAD = unit_draw(rng) < .13;
                const double SIZE =
                    BEAD ? 2.5 + (1.5 * unit_draw(rng)) : .8 + (1.7 * unit_draw(rng));
                const double MAX_LIFETIME =
                    (BEAD ? .18 + (.12 * unit_draw(rng)) : .08 + (.07 * unit_draw(rng))) * s.rate;
                const int SATELLITES = 1 + static_cast<int>(unit_draw(rng) < .85);
                const double SATELLITE_SCALE = .3 + (.2 * unit_draw(rng));
                const double SCATTER = .04 + (.08 * unit_draw(rng));
                for (int drop = 0; drop < 4; ++drop) {
                    const double FIRST_DELAY = std::max(DELAY, PERIOD * .35);
                    const double FORMING = std::min(PERIOD * .65, FIRST_DELAY + (drop * PERIOD));
                    const double BIRTH = FINGER_START + FIRST_DELAY + (drop * PERIOD);
                    const double LIFETIME = std::min(MAX_LIFETIME, .995 - BIRTH);
                    if (LIFETIME <= 0) {
                        continue;
                    }
                    const double AGE = s.clock - BIRTH;
                    if (!impactMode && AGE >= -FORMING && AGE < 0 && hole.radius > 0) {
                        const double CURRENT_RADIUS = hole.radius * (1 + currentCorrugation);
                        const Vec3 ROOT = POINT_AT(CURRENT_RADIUS);
                        bool onFilm = s.spherical ? CURRENT_RADIUS < PI_VALUE
                                                  : std::abs(ROOT.x) <= s.halfSize.x &&
                                                        std::abs(ROOT.y) <= s.halfSize.y;
                        const Vec3 POSITION = s.spherical ? Vec3{.x = ROOT.x / s.halfSize.x,
                                                                .y = ROOT.y / s.halfSize.x,
                                                                .z = ROOT.z / s.halfSize.x}
                                                          : ROOT;
                        for (std::size_t other = 0;
                            onFilm && other < std::min(s.holeCount, kBubbleHoles);
                            ++other) {
                            if (other == hi || s.holes[other].radius <= 0) {
                                continue;
                            }
                            const auto &prior = s.holes[other];
                            if (s.spherical) {
                                const double ALIGNMENT = dot(POSITION, prior.centre);
                                if (prior.radius * .94 >= PI_VALUE ||
                                    (prior.radius * .94 < PI_VALUE &&
                                        ALIGNMENT > std::cos(prior.radius * .94))) {
                                    onFilm = false;
                                } else if ((prior.radius * 1.06 >= PI_VALUE ||
                                               ALIGNMENT > std::cos(prior.radius * 1.06)) &&
                                           ALIGNMENT > std::cos(std::min(PI_VALUE,
                                                           bubbleHoleBoundary(prior,
                                                               bubbleHoleAzimuth(
                                                                   prior, POSITION, true))))) {
                                    onFilm = false;
                                }
                            } else {
                                const double ROOT_DELTA_X = ROOT.x - prior.centre.x;
                                const double ROOT_DELTA_Y = ROOT.y - prior.centre.y;
                                const double DISTANCE = std::hypot(ROOT_DELTA_X, ROOT_DELTA_Y);
                                if (DISTANCE < prior.radius * 1.06 &&
                                    DISTANCE < bubbleHoleBoundary(prior,
                                                   bubbleHoleAzimuth(prior, POSITION, false))) {
                                    onFilm = false;
                                }
                            }
                        }
                        if (onFilm) {
                            const double PROGRESS = 1 + (AGE / FORMING);
                            const double BEHIND =
                                s.spherical ? LENGTH * PROGRESS / s.halfSize.x : LENGTH * PROGRESS;
                            const Vec3 BASE_TIP = POINT_AT(std::max(0.0, CURRENT_RADIUS - BEHIND));
                            const Vec3 CURLED = EDGE(CURRENT_RADIUS);
                            const double TIP_DELTA_X = BASE_TIP.x - ROOT.x;
                            const double TIP_DELTA_Y = BASE_TIP.y - ROOT.y;
                            BubbleParticle ligament;
                            ligament.centre = {.x = CURLED.x + (TIP_DELTA_X * std::cos(TILT)) -
                                                    (TIP_DELTA_Y * std::sin(TILT)),
                                .y = CURLED.y + (TIP_DELTA_X * std::sin(TILT)) +
                                     (TIP_DELTA_Y * std::cos(TILT))};
                            ligament.tail = {.x = CURLED.x, .y = CURLED.y};
                            ligament.radius =
                                std::max(THREAD_RADIUS * 1.25, SIZE * (.6 + (.4 * PROGRESS)));
                            ligament.tailRadius = THREAD_RADIUS;
                            ligament.depth = (CURLED.z + BASE_TIP.z - ROOT.z) / MIN_HALF;
                            if (count < out.size()) {
                                out[count++] = ligament;
                            } else if (count < identified.size()) {
                                identified[count++] = {.particle = ligament,
                                    .id = static_cast<std::uint32_t>(
                                        2000 + (hi * 104) + (finger * 4) + drop)};
                            }
                        }
                    }
                    if (impactMode ? drop != 0 : (AGE < 0 || AGE >= LIFETIME)) {
                        continue;
                    }
                    const double RADIUS = radiusAt(s, hole, BIRTH);
                    if (s.spherical && RADIUS >= PI_VALUE) {
                        continue;
                    }
                    const double ONSET = smoothstep(.012, .045, BIRTH - hole.start);
                    const double BOUNDARY = RADIUS * (1 + (ONSET * corrugation));
                    if (s.spherical && BOUNDARY >= PI_VALUE) {
                        continue;
                    }
                    const Vec3 BASE = POINT_AT(BOUNDARY);
                    if (!s.spherical &&
                        (std::abs(BASE.x) > s.halfSize.x || std::abs(BASE.y) > s.halfSize.y)) {
                        continue;
                    }
                    bool consumed = false;
                    for (std::size_t other = 0; other < std::min(s.holeCount, kBubbleHoles);
                        ++other) {
                        if (other == hi || BIRTH <= s.holes[other].start) {
                            continue;
                        }
                        const auto &source = s.holes[other];
                        const Vec3 POSITION = s.spherical ? Vec3{.x = BASE.x / s.halfSize.x,
                                                                .y = BASE.y / s.halfSize.x,
                                                                .z = BASE.z / s.halfSize.x}
                                                          : BASE;
                        const double UPPER = s.speed * (BIRTH - source.start) * 1.06;
                        const double DELTA_X = BASE.x - source.centre.x;
                        const double DELTA_Y = BASE.y - source.centre.y;
                        if (!s.spherical &&
                            (DELTA_X * DELTA_X) + (DELTA_Y * DELTA_Y) >= UPPER * UPPER) {
                            continue;
                        }
                        if (s.spherical && UPPER < PI_VALUE &&
                            dot(POSITION, source.centre) <= std::cos(UPPER)) {
                            continue;
                        }
                        BubbleHole prior = source;
                        prior.radius = radiusAt(s, prior, BIRTH);
                        const double PRIOR_ONSET = smoothstep(.012, .045, BIRTH - prior.start);
                        for (std::size_t mode = 0; mode < prior.amplitude.size(); ++mode) {
                            prior.amplitude[mode] = PRIOR_ONSET * (.014 - (.002 * mode));
                        }
                        const double DISTANCE =
                            s.spherical ? 0 : std::sqrt((DELTA_X * DELTA_X) + (DELTA_Y * DELTA_Y));
                        const double ALIGNMENT = s.spherical ? dot(POSITION, prior.centre) : 0;
                        const bool INSIDE =
                            s.spherical
                                ? (prior.radius * .94 >= PI_VALUE ||
                                      ALIGNMENT >
                                          std::cos(std::min(PI_VALUE, prior.radius * .94)) ||
                                      ((prior.radius * 1.06 >= PI_VALUE ||
                                           ALIGNMENT > std::cos(prior.radius * 1.06)) &&
                                          ALIGNMENT >
                                              std::cos(std::min(PI_VALUE,
                                                  bubbleHoleBoundary(prior,
                                                      bubbleHoleAzimuth(prior, POSITION, true))))))
                                : (DISTANCE < prior.radius * 1.06 &&
                                      DISTANCE < bubbleHoleBoundary(prior,
                                                     bubbleHoleAzimuth(prior, POSITION, false)));
                        if (INSIDE) {
                            consumed = true;
                            break;
                        }
                    }
                    if (consumed) {
                        continue;
                    }
                    const Vec3 POINT = TIP_AT(BOUNDARY, EDGE(BOUNDARY));
                    const double TRANSIENT = s.spherical ? .05 : .075;
                    const double RIM_VELOCITY =
                        s.speed * ((BIRTH - hole.start - (RADIUS / s.speed)) / TRANSIENT) *
                        (1 + (ONSET * corrugation));
                    const double VELOCITY = RIM_VELOCITY * FACTOR;
                    const double COSINE = s.spherical ? std::cos(BOUNDARY) : 1;
                    const double SINE = s.spherical ? std::sin(BOUNDARY) : 0;
                    const double SCALE = s.spherical ? s.halfSize.x : 1;
                    const Vec3 TANGENT{
                        .x = SCALE * ((DIRECTION.x * COSINE) - (hole.centre.x * SINE)),
                        .y = SCALE * ((DIRECTION.y * COSINE) - (hole.centre.y * SINE)),
                        .z = SCALE * ((DIRECTION.z * COSINE) - (hole.centre.z * SINE))};
                    const double VELOCITY_X = TANGENT.x * VELOCITY;
                    const double VELOCITY_Y = TANGENT.y * VELOCITY;
                    const double MIN_HALF = std::min(s.halfSize.x, s.halfSize.y);
                    const double MAGNITUDE =
                        s.spherical ? std::hypot(VELOCITY_X, VELOCITY_Y) : VELOCITY;
                    const double TAU =
                        MAGNITUDE > 0 ? std::min(.13, .35 * MIN_HALF / MAGNITUDE) : .13;
                    const double MAX_WIDTH =
                        std::min(48.0, .18 * MIN_HALF) * std::clamp(s.aureole, 0.0, 1.0);
                    const double PLANE =
                        (s.spherical ? MIN_HALF * 1.01 : MIN_HALF * .10) + (2 * MAX_WIDTH / 1.7);
                    const double DISTANCE = PLANE - POINT.z;
                    const auto SOURCE_ID =
                        static_cast<std::uint32_t>((hi * 104) + (finger * 4) + drop);
                    const bool CANDIDATE =
                        drop == 0 &&
                        (impactMode || std::find(selected->ids.begin(),
                                           selected->ids.begin() + selected->count,
                                           SOURCE_ID) != selected->ids.begin() + selected->count);
                    const double VELOCITY_Z =
                        (TANGENT.z * VELOCITY) +
                        (MAGNITUDE * (CANDIDATE ? (s.spherical ? 2.8 : 1.4) : .3));
                    const double FRACTION =
                        CANDIDATE && VELOCITY_Z > 0 ? DISTANCE / (VELOCITY_Z * TAU) : 2;
                    const double HIT_AGE =
                        FRACTION > 0 && FRACTION < 1 ? -TAU * std::log1p(-FRACTION) : LIFETIME + 1;
                    const double HIT_RADIUS = SIZE * (1 - (HIT_AGE / LIFETIME)) * 1.5;
                    const bool HITS = HIT_AGE < LIFETIME && HIT_RADIUS >= 1.5 && HIT_RADIUS <= 3.5;
                    if (impactMode) {
                        if (HITS && count < impacts.size() && count < kBubbleMaxImpacts) {
                            const double TRAVEL = TAU * (1 - std::exp(-HIT_AGE / TAU));
                            const double SAG_LIMIT = .05 * MIN_HALF;
                            const double SAG =
                                SAG_LIMIT * (1 - std::exp(-120 * HIT_AGE * HIT_AGE / SAG_LIMIT));
                            if (count < impactIds.size()) {
                                impactIds[count] = SOURCE_ID;
                            }
                            impacts[count++] = {.position = {.x = POINT.x + (VELOCITY_X * TRAVEL),
                                                    .y = POINT.y + (VELOCITY_Y * TRAVEL) + SAG},
                                .radius = HIT_RADIUS,
                                .delay = (BIRTH + HIT_AGE - s.clock) / s.rate,
                                .drift = {.x = 0, .y = SIZE * 2}};
                        }
                        continue;
                    }
                    const double DRAG = TAU * (1 - std::exp(-AGE / TAU));
                    const double SAG_LIMIT = .05 * MIN_HALF;
                    const double SAG =
                        SAG_LIMIT > 0 ? SAG_LIMIT * (1 - std::exp(-120 * AGE * AGE / SAG_LIMIT))
                                      : 0;
                    BubbleParticle particle;
                    particle.centre = {.x = POINT.x + (VELOCITY_X * DRAG),
                        .y = POINT.y + (VELOCITY_Y * DRAG) + SAG};
                    particle.radius = SIZE * (1 - (AGE / LIFETIME));
                    particle.depth = (POINT.z + (VELOCITY_Z * DRAG)) / MIN_HALF;
                    if (HITS) {
                        const double APPROACH = std::clamp(AGE / HIT_AGE, 0.0, 1.0);
                        particle.radius *= 1 + (.5 * APPROACH * APPROACH);
                        particle.blur = APPROACH * APPROACH;
                    }
                    const double DETACH = .04 * s.rate;
                    const double CURRENT_BOUNDARY = hole.radius * (1 + currentCorrugation);
                    const Vec3 TAIL_BASE = POINT_AT(CURRENT_BOUNDARY);
                    bool tailOnFilm = s.spherical ? CURRENT_BOUNDARY < PI_VALUE
                                                  : std::abs(TAIL_BASE.x) <= s.halfSize.x &&
                                                        std::abs(TAIL_BASE.y) <= s.halfSize.y;
                    const Vec3 TAIL_POSITION = s.spherical ? Vec3{.x = TAIL_BASE.x / s.halfSize.x,
                                                                 .y = TAIL_BASE.y / s.halfSize.x,
                                                                 .z = TAIL_BASE.z / s.halfSize.x}
                                                           : TAIL_BASE;
                    for (std::size_t other = 0;
                        tailOnFilm && other < std::min(s.holeCount, kBubbleHoles);
                        ++other) {
                        if (other == hi || s.holes[other].radius <= 0) {
                            continue;
                        }
                        const auto &prior = s.holes[other];
                        const double DISTANCE =
                            s.spherical
                                ? std::acos(std::clamp(dot(TAIL_POSITION, prior.centre), -1.0, 1.0))
                                : std::hypot(
                                      TAIL_BASE.x - prior.centre.x, TAIL_BASE.y - prior.centre.y);
                        if (DISTANCE < bubbleHoleBoundary(prior,
                                           bubbleHoleAzimuth(prior, TAIL_POSITION, s.spherical))) {
                            tailOnFilm = false;
                        }
                    }
                    const Vec3 TAIL = AGE < DETACH && tailOnFilm ? EDGE(CURRENT_BOUNDARY) : POINT;
                    particle.tail = AGE < DETACH && tailOnFilm ? Vec2{.x = TAIL.x, .y = TAIL.y}
                                                               : particle.centre;
                    particle.tailRadius = AGE < DETACH && tailOnFilm
                                              ? SIZE * .3 * std::max(0.0, 1 - (AGE / DETACH))
                                              : 0;
                    if (!HITS || AGE < HIT_AGE) {
                        if (count < out.size()) {
                            out[count++] = particle;
                        } else if (count < identified.size()) {
                            identified[count++] = {.particle = particle,
                                .id = static_cast<std::uint32_t>((hi * 104) + (finger * 4) + drop)};
                        }
                    }
                    for (int satellite = 0; satellite < SATELLITES; ++satellite) {
                        const double SATELLITE_LIFE = LIFETIME * (.7 + (.1 * satellite));
                        if (AGE >= SATELLITE_LIFE) {
                            continue;
                        }
                        const double LATERAL = satellite == 0 ? SCATTER : -SCATTER;
                        const double SVX = (.8 * VELOCITY_X) - (LATERAL * VELOCITY_Y);
                        const double SVY = (.8 * VELOCITY_Y) + (LATERAL * VELOCITY_X);
                        const double SPEED = std::hypot(SVX, SVY);
                        const double STAU = SPEED > 0 ? std::min(.13, .35 * MIN_HALF / SPEED) : .13;
                        const double TRAVEL = STAU * (1 - std::exp(-AGE / STAU));
                        BubbleParticle small;
                        small.centre = {.x = POINT.x + (SVX * TRAVEL) - (TANGENT.x / SCALE * .5),
                            .y = POINT.y + (SVY * TRAVEL) - (TANGENT.y / SCALE * .5) + SAG};
                        small.tail = small.centre;
                        small.radius = SIZE * SATELLITE_SCALE * (1 - (AGE / SATELLITE_LIFE));
                        small.depth = (POINT.z + (.8 * VELOCITY_Z * TRAVEL)) / MIN_HALF;
                        if (count < out.size()) {
                            out[count++] = small;
                        } else if (count < identified.size()) {
                            identified[count++] = {.particle = small,
                                .id = static_cast<std::uint32_t>(
                                    10000 + (((hi * 104) + (finger * 4) + drop) * 2) + satellite)};
                        }
                    }
                }
            }
        }
        if (!impactMode && (s.holeCount != 0u)) {
            const auto BURST = [&](Vec3 origin, double birth, std::uint32_t id) {
                const double AGE = s.clock - birth;
                const double LIFE = std::min(.22 * s.rate, .995 - birth);
                if (AGE < 0 || AGE >= LIFE) {
                    return;
                }
                const double REACH = .12 * std::min(s.halfSize.x, s.halfSize.y);
                for (std::uint32_t i = 0; i < 4; ++i) {
                    const double PHI = 2 * PI_VALUE * (i + (.13 * (id % 7))) / 4;
                    const double MOTION = REACH * (1 - std::exp(-AGE / .09));
                    BubbleParticle newParticle;
                    newParticle.centre = {.x = origin.x + (MOTION * std::cos(PHI)),
                        .y = origin.y + (MOTION * std::sin(PHI)) + (REACH * .1 * AGE)};
                    newParticle.tail = newParticle.centre;
                    newParticle.radius = (2.5 + (.35 * i)) * (1 - (AGE / LIFE));
                    newParticle.depth = s.spherical ? origin.z / s.halfSize.x : 0;
                    if (count < out.size()) {
                        out[count++] = newParticle;
                    } else if (count < identified.size()) {
                        identified[count++] = {.particle = newParticle, .id = id + i};
                    }
                }
            };
            const auto &events = collapse(s);
            for (std::size_t i = 0; i < events.mergeCount; ++i) {
                BURST(events.merges[i].position, events.merges[i].time, events.merges[i].id);
            }
            BURST(events.position, events.time, 1200);
        }
        return count;
    }

    namespace {
        const ImpactCache &impactCache(const BubbleSurface &s) {
            static thread_local std::array<ImpactCache, 2> cache;
            auto &entry = cache[s.spherical ? 1 : 0];
            const auto HOLE_COUNT = std::min(s.holeCount, kBubbleHoles);
            bool same = entry.valid && entry.key.speed == s.speed && entry.key.rate == s.rate &&
                        entry.key.debris == s.debris && entry.key.aureole == s.aureole &&
                        entry.key.holeCount == s.holeCount &&
                        entry.key.halfSize.x == s.halfSize.x &&
                        entry.key.halfSize.y == s.halfSize.y;
            for (std::size_t i = 0; same && i < HOLE_COUNT; ++i) {
                const auto &cachedHole = entry.key.holes[i];
                const auto &currentHole = s.holes[i];
                same = cachedHole.start == currentHole.start &&
                       cachedHole.centre.x == currentHole.centre.x &&
                       cachedHole.centre.y == currentHole.centre.y &&
                       cachedHole.centre.z == currentHole.centre.z &&
                       cachedHole.phase == currentHole.phase;
            }
            if (!same) {
                entry.key = s;
                entry.valid = true;
                BubbleSurface atZero = s;
                atZero.clock = 0;
                entry.count = debris(atZero, {}, entry.impacts, {}, true, entry.ids);
            }
            return entry;
        }
    }  // namespace

    std::size_t bubbleParticles(const BubbleSurface &surface, std::span<BubbleParticle> out) {
        return debris(surface, out, {}, {}, false);
    }

    std::size_t bubbleImpacts(const BubbleSurface &surface, std::span<BubbleImpact> out) {
        if (!surface.closing || surface.speed <= 0 || surface.rate <= 0 ||
            surface.halfSize.x <= 0 || surface.halfSize.y <= 0) {
            return 0;
        }
        const auto &entry = impactCache(surface);
        const auto OUTPUT_COUNT = std::min(out.size(), entry.count);
        for (std::size_t i = 0; i < OUTPUT_COUNT; ++i) {
            out[i] = entry.impacts[i];
            out[i].delay -= surface.clock / surface.rate;
        }
        return OUTPUT_COUNT;
    }

    std::size_t detail::bubbleParticlesWithIds(
        const BubbleSurface &surface, std::span<IdentifiedParticle> out) {
        return debris(surface, {}, {}, out, false);
    }

}  // namespace hyprimate
