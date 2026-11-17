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
        constexpr double pi = 3.14159265358979323846;

        Vec3 cross(Vec3 a, Vec3 b) {
            return {.x = (a.y * b.z) - (a.z * b.y),
                .y = (a.z * b.x) - (a.x * b.z),
                .z = (a.x * b.y) - (a.y * b.x)};
        }
        double dot(Vec3 a, Vec3 b) {
            return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
        }
        Vec3 normalize(Vec3 a) {
            const double n = std::sqrt(dot(a, a));
            return n > 0 ? Vec3{.x = a.x / n, .y = a.y / n, .z = a.z / n}
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
        Vec3 e1;
        Vec3 e2;
        basis(hole.centre, e1, e2);
        return std::atan2(dot(p, e2), dot(p, e1));
    }

    double bubbleHoleBoundary(const BubbleHole &hole, double phi) {
        double r = 1;
        for (std::size_t i = 0; i < kBubbleRimModes.size(); ++i) {
            r += hole.amplitude[i] * std::cos((kBubbleRimModes[i] * phi) + hole.phase[i]);
        }
        return hole.radius * r;
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
            const auto n = std::min(s.holeCount, kBubbleHoles);
            bool same =
                entry.valid && entry.key.speed == s.speed && entry.key.holeCount == s.holeCount &&
                entry.key.halfSize.x == s.halfSize.x && entry.key.halfSize.y == s.halfSize.y;
            for (std::size_t i = 0; same && i < n; ++i) {
                const auto &a = entry.key.holes[i];
                const auto &b = s.holes[i];
                same = a.start == b.start && a.centre.x == b.centre.x && a.centre.y == b.centre.y &&
                       a.centre.z == b.centre.z && a.phase == b.phase;
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
                for (std::size_t i = 0; i < n; ++i) {
                    const auto &h = s.holes[i];
                    const double d = s.spherical
                                         ? std::acos(std::clamp(dot(p, h.centre), -1.0, 1.0))
                                         : std::hypot(p.x - h.centre.x, p.y - h.centre.y);
                    first = std::min(first, h.start + (d / s.speed) + (s.spherical ? .05 : .075));
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
                for (std::size_t i = 0; i < n; ++i) {
                    CANDIDATE({.x = -s.holes[i].centre.x,
                        .y = -s.holes[i].centre.y,
                        .z = -s.holes[i].centre.z});
                }
                entry.position = {.x = entry.position.x * s.halfSize.x,
                    .y = entry.position.y * s.halfSize.x,
                    .z = entry.position.z * s.halfSize.x};
            } else {
                for (int x = -1; x <= 1; ++x) {
                    for (int y = -1; y <= 1; ++y) {
                        CANDIDATE({.x = x * s.halfSize.x, .y = y * s.halfSize.y, .z = 0});
                    }
                }
            }
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = i + 1; j < n; ++j) {
                    const auto &a = s.holes[i];
                    const auto &b = s.holes[j];
                    const double DISTANCE =
                        s.spherical ? std::acos(std::clamp(dot(a.centre, b.centre), -1.0, 1.0))
                                    : std::hypot(b.centre.x - a.centre.x, b.centre.y - a.centre.y);
                    double lo = std::max(a.start, b.start);
                    double hi = 1;
                    if (radiusAt(s, a, lo) >= DISTANCE ||
                        radiusAt(s, a, hi) + radiusAt(s, b, hi) < DISTANCE) {
                        continue;
                    }
                    for (int k = 0; k < 9; ++k) {
                        const double MID = (lo + hi) * .5;
                        if (radiusAt(s, a, MID) + radiusAt(s, b, MID) < DISTANCE) {
                            lo = MID;
                        } else {
                            hi = MID;
                        }
                    }
                    const double WEIGHT = DISTANCE > 0 ? radiusAt(s, a, hi) / DISTANCE : 0;
                    Vec3 origin{.x = (a.centre.x * (1 - WEIGHT)) + (b.centre.x * WEIGHT),
                        .y = (a.centre.y * (1 - WEIGHT)) + (b.centre.y * WEIGHT),
                        .z = (a.centre.z * (1 - WEIGHT)) + (b.centre.z * WEIGHT)};
                    if (s.spherical) {
                        const double DENOM = std::sin(DISTANCE);
                        if (std::abs(DENOM) > 1e-8) {
                            const double wa = std::sin((1 - WEIGHT) * DISTANCE) / DENOM;
                            const double wb = std::sin(WEIGHT * DISTANCE) / DENOM;
                            origin = {.x = (a.centre.x * wa) + (b.centre.x * wb),
                                .y = (a.centre.y * wa) + (b.centre.y * wb),
                                .z = (a.centre.z * wa) + (b.centre.z * wb)};
                        } else {
                            origin = normalize(origin);
                        }
                    } else if (std::abs(origin.x) > s.halfSize.x ||
                               std::abs(origin.y) > s.halfSize.y) {
                        continue;
                    }
                    bool consumed = false;
                    for (std::size_t k = 0; k < n; ++k) {
                        if (k == i || k == j || hi <= s.holes[k].start) {
                            continue;
                        }
                        BubbleHole other = s.holes[k];
                        other.radius = radiusAt(s, other, hi);
                        const double ONSET = smoothstep(.012, .045, hi - other.start);
                        for (std::size_t m = 0; m < other.amplitude.size(); ++m) {
                            other.amplitude[m] = ONSET * (.014 - (.002 * m));
                        }
                        const double d =
                            s.spherical
                                ? std::acos(std::clamp(dot(origin, other.centre), -1.0, 1.0))
                                : std::hypot(origin.x - other.centre.x, origin.y - other.centre.y);
                        if (d < bubbleHoleBoundary(
                                    other, bubbleHoleAzimuth(other, origin, s.spherical))) {
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
                        .time = hi,
                        .id = static_cast<std::uint32_t>(1000 + (20 * i) + (5 * j))};
                }
            }
            return entry;
        }
    }  // namespace

    namespace {
        BubbleHole sheetHole(const BubbleSurface &s, const BubbleSheet &sheet, std::size_t i) {
            BubbleHole h = s.holes[i];
            h.radius = sheet.radii[i];
            for (std::size_t m = 0; m < h.amplitude.size(); ++m) {
                h.amplitude[m] = sheet.onsets[i] * (.014 - (.002 * m));
            }
            return h;
        }
        Vec3 sheetMaterial(const BubbleSurface &s, const BubbleHole &h, double phi, double u) {
            const double r = std::min(s.halfSize.x, s.halfSize.y);
            const double b = bubbleHoleBoundary(h, phi);
            const double w = bubbleCurlSection(s, h, b * (s.spherical ? r : 1), phi).width;
            const double d = b + ((1 - u) * w / (s.spherical ? r : 1));
            if (!s.spherical) {
                return {.x = h.centre.x + (d * std::cos(phi)),
                    .y = h.centre.y + (d * std::sin(phi)),
                    .z = 0};
            }
            Vec3 e1;
            Vec3 e2;
            basis(h.centre, e1, e2);
            const Vec3 DIR{.x = (e1.x * std::cos(phi)) + (e2.x * std::sin(phi)),
                .y = (e1.y * std::cos(phi)) + (e2.y * std::sin(phi)),
                .z = (e1.z * std::cos(phi)) + (e2.z * std::sin(phi))};
            return {.x = (h.centre.x * std::cos(d)) + (DIR.x * std::sin(d)),
                .y = (h.centre.y * std::cos(d)) + (DIR.y * std::sin(d)),
                .z = (h.centre.z * std::cos(d)) + (DIR.z * std::sin(d))};
        }
        Vec3 rotate(Vec3 v, Vec3 axis, double angle) {
            const double c = std::cos(angle);
            const double sn = std::sin(angle);
            const double d = dot(axis, v);
            const Vec3 x = cross(axis, v);
            return {.x = (v.x * c) + (x.x * sn) + (axis.x * d * (1 - c)),
                .y = (v.y * c) + (x.y * sn) + (axis.y * d * (1 - c)),
                .z = (v.z * c) + (x.z * sn) + (axis.z * d * (1 - c))};
        }
    }  // namespace

    bool bubbleSheetContains(const BubbleSurface &s, const BubbleSheet &sheet, Vec3 material) {
        if (sheet.hole >= std::min(s.holeCount, kBubbleHoles)) {
            return false;
        }
        const auto h = sheetHole(s, sheet, sheet.hole);
        const double r = std::min(s.halfSize.x, s.halfSize.y);
        if (r <= 0) {
            return false;
        }
        const double PHI = bubbleHoleAzimuth(h, material, s.spherical);
        const double b = bubbleHoleBoundary(h, PHI);
        const double d = s.spherical ? std::acos(std::clamp(dot(material, h.centre), -1.0, 1.0))
                                     : std::hypot(material.x - h.centre.x, material.y - h.centre.y);
        const double w = bubbleCurlSection(s, h, b * (s.spherical ? r : 1), PHI).width;
        if (w <= 0) {
            return false;
        }
        const double u = 1 - ((d - b) * (s.spherical ? r : 1) / w);
        if (u < 0 || u > 1) {
            return false;
        }
        const double LIMIT = sheet.halfAngle * (.82 + (.14 * std::sin(pi * u)) +
                                                   (.04 * std::sin((3 * pi * u) + h.phase[0])));
        if (std::abs(std::remainder(PHI - sheet.phi, 2 * pi)) > LIMIT) {
            return false;
        }
        const double OWN_RANK = 1 - u;
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
                bubbleCurlSection(s, OTHER, BOUNDARY * (s.spherical ? r : 1), OTHER_PHI).width;
            if (WIDTH <= 0) {
                continue;
            }
            const double RANK = (DISTANCE - BOUNDARY) * (s.spherical ? r : 1) / WIDTH;
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
        const std::size_t n = std::min(s.holeCount, kBubbleHoles);
        const double r = std::min(s.halfSize.x, s.halfSize.y);
        if (!s.closing || n == 0 || s.speed <= 0 || s.rate <= 0 || r <= 0 || out.empty()) {
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
            std::size_t other = n;
            if (event.id != 1200) {
                source = (event.id - 1000) / 20;
                other = ((event.id - 1000) % 20) / 5;
            } else {
                double first = 1e100;
                for (std::size_t i = 0; i < n; ++i) {
                    const double d =
                        s.spherical
                            ? std::acos(std::clamp(dot(POINT, s.holes[i].centre), -1.0, 1.0))
                            : std::hypot(
                                  POINT.x - s.holes[i].centre.x, POINT.y - s.holes[i].centre.y);
                    const double ARRIVAL = s.holes[i].start + (d / s.speed);
                    if (ARRIVAL < first) {
                        first = ARRIVAL;
                        source = i;
                    }
                }
            }
            const auto WIDTH_AT = [&](std::size_t i) {
                BubbleHole h = s.holes[i];
                h.radius = radiusAt(s, h, event.time);
                const double ONSET = smoothstep(.012, .045, event.time - h.start);
                for (std::size_t m = 0; m < h.amplitude.size(); ++m) {
                    h.amplitude[m] = ONSET * (.014 - (.002 * m));
                }
                const double PHI = bubbleHoleAzimuth(h, POINT, s.spherical);
                return bubbleCurlSection(
                    s, h, bubbleHoleBoundary(h, PHI) * (s.spherical ? r : 1), PHI)
                    .width;
            };
            double width = WIDTH_AT(source);
            if (other < n) {
                width = .5 * (width + WIDTH_AT(other));
            }
            const double BIRTH = std::max(s.holes[source].start,
                event.time - (.55 * width / (s.speed * (s.spherical ? r : 1))));
            return Candidate{.event = &event, .birth = BIRTH, .source = source, .other = other};
        };
        std::array<Candidate, kBubbleMaxSheets> candidates{};
        std::size_t total = 0;
        const auto INSERT = [&](const BurstEvent &event) {
            const auto CANDIDATE = CANDIDATE_FOR(event);
            std::size_t i = total++;
            while (i > 0 && (candidates[i - 1].birth > CANDIDATE.birth ||
                                (candidates[i - 1].birth == CANDIDATE.birth &&
                                    candidates[i - 1].event->id > CANDIDATE.event->id))) {
                candidates[i] = candidates[i - 1];
                --i;
            }
            candidates[i] = CANDIDATE;
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
            for (std::size_t i = 0; i < n; ++i) {
                sheet.radii[i] = radiusAt(s, s.holes[i], sheet.birth);
                sheet.onsets[i] = smoothstep(.012, .045, sheet.birth - s.holes[i].start);
            }
            const auto h = sheetHole(s, sheet, SOURCE);
            const double b = bubbleHoleBoundary(h, phi);
            const double w = bubbleCurlSection(s, h, b * (s.spherical ? r : 1), phi).width;
            if (w <= 0 || (s.spherical && b >= pi)) {
                continue;
            }
            sheet.halfAngle = std::min(.35,
                std::clamp(.11 * r, 16.0, 40.0) /
                    std::max((b * (s.spherical ? r : 1)) + (.5 * w), 1.0));
            bool available = false;
            // Centre the piece on surviving material, not on an occluded sliver's empty pivot.
            for (const double OFFSET : {0.0, -1.0 / 3, 1.0 / 3, -2.0 / 3, 2.0 / 3}) {
                const double TRIAL_PHI = phi + (sheet.halfAngle * OFFSET);
                const Vec3 p = sheetMaterial(s, h, TRIAL_PHI, .5);
                if (!s.spherical) {
                    const double CORNER = std::clamp(s.corner, 0.0, r);
                    const double x = std::max(std::abs(p.x) - (s.halfSize.x - CORNER), 0.0);
                    const double y = std::max(std::abs(p.y) - (s.halfSize.y - CORNER), 0.0);
                    if (std::abs(p.x) > s.halfSize.x || std::abs(p.y) > s.halfSize.y ||
                        (x * x) + (y * y) > CORNER * CORNER) {
                        continue;
                    }
                }
                bool valid = bubbleSheetContains(s, sheet, p);
                for (std::size_t i = 0; i < n && valid; ++i) {
                    if (i == SOURCE || sheet.radii[i] <= 0) {
                        continue;
                    }
                    const auto PRIOR = sheetHole(s, sheet, i);
                    const double DISTANCE =
                        s.spherical ? std::acos(std::clamp(dot(p, PRIOR.centre), -1.0, 1.0))
                                    : std::hypot(p.x - PRIOR.centre.x, p.y - PRIOR.centre.y);
                    const double PRIOR_PHI = bubbleHoleAzimuth(PRIOR, p, s.spherical);
                    if (DISTANCE < bubbleHoleBoundary(PRIOR, PRIOR_PHI)) {
                        valid = false;
                    }
                }
                for (std::size_t i = 0; i < count && valid; ++i) {
                    if (SHEETS[i].birth <= sheet.birth && bubbleSheetContains(s, SHEETS[i], p)) {
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
            sheet.pivot = bubblePeelSample(s, h, phi, .5).position;
            const Vec3 BEFORE = bubblePeelSample(s, h, phi - .001, .5).position;
            const Vec3 AFTER = bubblePeelSample(s, h, phi + .001, .5).position;
            sheet.axis = normalize(
                {.x = AFTER.x - BEFORE.x, .y = AFTER.y - BEFORE.y, .z = AFTER.z - BEFORE.z});
            sheet.age = std::max(0.0, (s.clock - sheet.birth) / s.rate);
            sheet.life = std::min(.16, (1 - sheet.birth) / s.rate);
            const double t = sheet.life > 0 ? std::clamp(sheet.age / sheet.life, 0.0, 1.0) : 1;
            sheet.scale = 1 - smoothstep(0, 1, t);
            sheet.rotation = 2.2 * t;
            const auto ROOT = bubblePeelSample(s, h, phi, 0).position;
            const Vec3 OUTWARD =
                normalize({.x = ROOT.x - (s.spherical ? h.centre.x * r : h.centre.x),
                    .y = ROOT.y - (s.spherical ? h.centre.y * r : h.centre.y),
                    .z = s.spherical ? ROOT.z - (h.centre.z * r) : 0});
            const double TRAVEL = std::min(.16 * r, 80.0) * (1 - std::exp(-sheet.age / .08));
            sheet.translation = {.x = OUTWARD.x * TRAVEL,
                .y = (OUTWARD.y * TRAVEL) + (.04 * r * t * t),
                .z = (OUTWARD.z * TRAVEL) + (.05 * r * t)};
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
            const auto &h = s.holes[hi];
            const double MIN_HALF = std::min(s.halfSize.x, s.halfSize.y);
            const double THRESHOLD =
                std::max(24.0, .08 * MIN_HALF) / (s.spherical ? s.halfSize.x : 1);
            if (!impactMode && h.radius < THRESHOLD) {
                continue;
            }
            const double TRANSIENT = s.spherical ? .05 : .075;
            const double UNITS_PER_PIXEL = s.spherical ? 1 / s.halfSize.x : 1;
            std::uint64_t holeRng = s.debris ^ (hi * 0x9e3779b97f4a7c15ULL) ^ 0x94d049bb133111ebULL;
            const int FINGER_COUNT = 14 + static_cast<int>(13 * unit_draw(holeRng));
            Vec3 e1{};
            Vec3 e2{};
            if (s.spherical) {
                basis(h.centre, e1, e2);
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
                const double PHI = 2 * pi * (finger + (.9 * (unit_draw(rng) - .5))) / FINGER_COUNT;
                const int ORDER = static_cast<int>(FINGER_COUNT * unit_draw(rng));
                const double SITE_RADIUS =
                    std::max(THRESHOLD, (ORDER + 1) * 50 / (2 * pi) * UNITS_PER_PIXEL);
                if (!impactMode && h.radius < SITE_RADIUS) {
                    continue;
                }
                const double TARGET = SITE_RADIUS / s.speed;
                double onsetAge = TARGET + TRANSIENT;
                for (int i = 0; i < 4; ++i) {
                    const double EXP_AGE = std::exp(-onsetAge / TRANSIENT);
                    onsetAge -= (onsetAge - (TRANSIENT * (1 - EXP_AGE)) - TARGET) / (1 - EXP_AGE);
                }
                const double FINGER_START = h.start + onsetAge;
                const double DELAY = .08 * s.rate * unit_draw(rng);
                const double PERIOD = (.05 + (.04 * unit_draw(rng))) * s.rate;
                const double LENGTH = 6 + (16 * unit_draw(rng));
                const double THREAD_RADIUS = .4 + (.5 * unit_draw(rng));
                const double TILT = .7 * (unit_draw(rng) - .5);
                const double cp = std::cos(PHI);
                const double sp = std::sin(PHI);
                const Vec3 DIRECTION = s.spherical ? Vec3{.x = (e1.x * cp) + (e2.x * sp),
                                                         .y = (e1.y * cp) + (e2.y * sp),
                                                         .z = (e1.z * cp) + (e2.z * sp)}
                                                   : Vec3{.x = cp, .y = sp, .z = 0};
                double corrugation = 0;
                double currentCorrugation = 0;
                for (std::size_t m = 0; m < kBubbleRimModes.size(); ++m) {
                    const double WAVE = std::cos((kBubbleRimModes[m] * PHI) + h.phase[m]);
                    corrugation += (.014 - (.002 * m)) * WAVE;
                    currentCorrugation += h.amplitude[m] * WAVE;
                }
                const auto at = [&](double radius) {
                    if (!s.spherical) {
                        return Vec3{.x = h.centre.x + (DIRECTION.x * radius),
                            .y = h.centre.y + (DIRECTION.y * radius),
                            .z = 0};
                    }
                    const double c = std::cos(radius);
                    const double sn = std::sin(radius);
                    const double SCALE = s.halfSize.x;
                    return Vec3{.x = SCALE * ((h.centre.x * c) + (DIRECTION.x * sn)),
                        .y = SCALE * ((h.centre.y * c) + (DIRECTION.y * sn)),
                        .z = SCALE * ((h.centre.z * c) + (DIRECTION.z * sn))};
                };
                const auto EDGE = [&](double boundary) {
                    BubbleHole frozen = h;
                    frozen.radius = boundary;
                    frozen.amplitude = {};
                    return bubblePeelSample(s, frozen, PHI, 1).position;
                };
                const auto TIP_AT = [&](double boundary, Vec3 root) {
                    const Vec3 BASE = at(boundary);
                    const Vec3 TIP = at(std::max(0.0, boundary - (LENGTH * UNITS_PER_PIXEL)));
                    const double dx = TIP.x - BASE.x;
                    const double dy = TIP.y - BASE.y;
                    return Vec3{.x = root.x + (dx * std::cos(TILT)) - (dy * std::sin(TILT)),
                        .y = root.y + (dx * std::sin(TILT)) + (dy * std::cos(TILT)),
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
                    if (!impactMode && AGE >= -FORMING && AGE < 0 && h.radius > 0) {
                        const double CURRENT_RADIUS = h.radius * (1 + currentCorrugation);
                        const Vec3 ROOT = at(CURRENT_RADIUS);
                        bool onFilm = s.spherical ? CURRENT_RADIUS < pi
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
                                if (prior.radius * .94 >= pi ||
                                    (prior.radius * .94 < pi &&
                                        ALIGNMENT > std::cos(prior.radius * .94))) {
                                    onFilm = false;
                                } else if ((prior.radius * 1.06 >= pi ||
                                               ALIGNMENT > std::cos(prior.radius * 1.06)) &&
                                           ALIGNMENT > std::cos(std::min(pi,
                                                           bubbleHoleBoundary(prior,
                                                               bubbleHoleAzimuth(
                                                                   prior, POSITION, true))))) {
                                    onFilm = false;
                                }
                            } else {
                                const double dx = ROOT.x - prior.centre.x;
                                const double dy = ROOT.y - prior.centre.y;
                                const double DISTANCE = std::hypot(dx, dy);
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
                            const Vec3 BASE_TIP = at(std::max(0.0, CURRENT_RADIUS - BEHIND));
                            const Vec3 CURLED = EDGE(CURRENT_RADIUS);
                            const double dx = BASE_TIP.x - ROOT.x;
                            const double dy = BASE_TIP.y - ROOT.y;
                            BubbleParticle ligament;
                            ligament.centre = {
                                .x = CURLED.x + (dx * std::cos(TILT)) - (dy * std::sin(TILT)),
                                .y = CURLED.y + (dx * std::sin(TILT)) + (dy * std::cos(TILT))};
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
                    const double r = radiusAt(s, h, BIRTH);
                    if (s.spherical && r >= pi) {
                        continue;
                    }
                    const double ONSET = smoothstep(.012, .045, BIRTH - h.start);
                    const double BOUNDARY = r * (1 + (ONSET * corrugation));
                    if (s.spherical && BOUNDARY >= pi) {
                        continue;
                    }
                    const Vec3 BASE = at(BOUNDARY);
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
                        const double dx = BASE.x - source.centre.x;
                        const double dy = BASE.y - source.centre.y;
                        if (!s.spherical && (dx * dx) + (dy * dy) >= UPPER * UPPER) {
                            continue;
                        }
                        if (s.spherical && UPPER < pi &&
                            dot(POSITION, source.centre) <= std::cos(UPPER)) {
                            continue;
                        }
                        BubbleHole prior = source;
                        prior.radius = radiusAt(s, prior, BIRTH);
                        const double PRIOR_ONSET = smoothstep(.012, .045, BIRTH - prior.start);
                        for (std::size_t m = 0; m < prior.amplitude.size(); ++m) {
                            prior.amplitude[m] = PRIOR_ONSET * (.014 - (.002 * m));
                        }
                        const double DISTANCE = s.spherical ? 0 : std::sqrt((dx * dx) + (dy * dy));
                        const double ALIGNMENT = s.spherical ? dot(POSITION, prior.centre) : 0;
                        const bool INSIDE =
                            s.spherical
                                ? (prior.radius * .94 >= pi ||
                                      ALIGNMENT > std::cos(std::min(pi, prior.radius * .94)) ||
                                      ((prior.radius * 1.06 >= pi ||
                                           ALIGNMENT > std::cos(prior.radius * 1.06)) &&
                                          ALIGNMENT >
                                              std::cos(std::min(pi,
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
                    const Vec3 p = TIP_AT(BOUNDARY, EDGE(BOUNDARY));
                    const double TRANSIENT = s.spherical ? .05 : .075;
                    const double RIM_VELOCITY = s.speed *
                                                ((BIRTH - h.start - (r / s.speed)) / TRANSIENT) *
                                                (1 + (ONSET * corrugation));
                    const double VELOCITY = RIM_VELOCITY * FACTOR;
                    const double c = s.spherical ? std::cos(BOUNDARY) : 1;
                    const double sn = s.spherical ? std::sin(BOUNDARY) : 0;
                    const double SCALE = s.spherical ? s.halfSize.x : 1;
                    const Vec3 TANGENT{.x = SCALE * ((DIRECTION.x * c) - (h.centre.x * sn)),
                        .y = SCALE * ((DIRECTION.y * c) - (h.centre.y * sn)),
                        .z = SCALE * ((DIRECTION.z * c) - (h.centre.z * sn))};
                    const double vx = TANGENT.x * VELOCITY;
                    const double vy = TANGENT.y * VELOCITY;
                    const double MIN_HALF = std::min(s.halfSize.x, s.halfSize.y);
                    const double MAGNITUDE = s.spherical ? std::hypot(vx, vy) : VELOCITY;
                    const double TAU =
                        MAGNITUDE > 0 ? std::min(.13, .35 * MIN_HALF / MAGNITUDE) : .13;
                    const double MAX_WIDTH =
                        std::min(48.0, .18 * MIN_HALF) * std::clamp(s.aureole, 0.0, 1.0);
                    const double PLANE =
                        (s.spherical ? MIN_HALF * 1.01 : MIN_HALF * .10) + (2 * MAX_WIDTH / 1.7);
                    const double DISTANCE = PLANE - p.z;
                    const auto SOURCE_ID =
                        static_cast<std::uint32_t>((hi * 104) + (finger * 4) + drop);
                    const bool CANDIDATE =
                        drop == 0 &&
                        (impactMode || std::find(selected->ids.begin(),
                                           selected->ids.begin() + selected->count,
                                           SOURCE_ID) != selected->ids.begin() + selected->count);
                    const double vz = (TANGENT.z * VELOCITY) +
                                      (MAGNITUDE * (CANDIDATE ? (s.spherical ? 2.8 : 1.4) : .3));
                    const double FRACTION = CANDIDATE && vz > 0 ? DISTANCE / (vz * TAU) : 2;
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
                            impacts[count++] = {.position = {.x = p.x + (vx * TRAVEL),
                                                    .y = p.y + (vy * TRAVEL) + SAG},
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
                    particle.centre = {.x = p.x + (vx * DRAG), .y = p.y + (vy * DRAG) + SAG};
                    particle.radius = SIZE * (1 - (AGE / LIFETIME));
                    particle.depth = (p.z + (vz * DRAG)) / MIN_HALF;
                    if (HITS) {
                        const double APPROACH = std::clamp(AGE / HIT_AGE, 0.0, 1.0);
                        particle.radius *= 1 + (.5 * APPROACH * APPROACH);
                        particle.blur = APPROACH * APPROACH;
                    }
                    const double DETACH = .04 * s.rate;
                    const double CURRENT_BOUNDARY = h.radius * (1 + currentCorrugation);
                    const Vec3 TAIL_BASE = at(CURRENT_BOUNDARY);
                    bool tailOnFilm = s.spherical ? CURRENT_BOUNDARY < pi
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
                    const Vec3 TAIL = AGE < DETACH && tailOnFilm ? EDGE(CURRENT_BOUNDARY) : p;
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
                        const double SVX = (.8 * vx) - (LATERAL * vy);
                        const double SVY = (.8 * vy) + (LATERAL * vx);
                        const double SPEED = std::hypot(SVX, SVY);
                        const double STAU = SPEED > 0 ? std::min(.13, .35 * MIN_HALF / SPEED) : .13;
                        const double TRAVEL = STAU * (1 - std::exp(-AGE / STAU));
                        BubbleParticle small;
                        small.centre = {.x = p.x + (SVX * TRAVEL) - (TANGENT.x / SCALE * .5),
                            .y = p.y + (SVY * TRAVEL) - (TANGENT.y / SCALE * .5) + SAG};
                        small.tail = small.centre;
                        small.radius = SIZE * SATELLITE_SCALE * (1 - (AGE / SATELLITE_LIFE));
                        small.depth = (p.z + (.8 * vz * TRAVEL)) / MIN_HALF;
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
                    const double PHI = 2 * pi * (i + (.13 * (id % 7))) / 4;
                    const double MOTION = REACH * (1 - std::exp(-AGE / .09));
                    BubbleParticle p;
                    p.centre = {.x = origin.x + (MOTION * std::cos(PHI)),
                        .y = origin.y + (MOTION * std::sin(PHI)) + (REACH * .1 * AGE)};
                    p.tail = p.centre;
                    p.radius = (2.5 + (.35 * i)) * (1 - (AGE / LIFE));
                    p.depth = s.spherical ? origin.z / s.halfSize.x : 0;
                    if (count < out.size()) {
                        out[count++] = p;
                    } else if (count < identified.size()) {
                        identified[count++] = {.particle = p, .id = id + i};
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
            const auto n = std::min(s.holeCount, kBubbleHoles);
            bool same = entry.valid && entry.key.speed == s.speed && entry.key.rate == s.rate &&
                        entry.key.debris == s.debris && entry.key.aureole == s.aureole &&
                        entry.key.holeCount == s.holeCount &&
                        entry.key.halfSize.x == s.halfSize.x &&
                        entry.key.halfSize.y == s.halfSize.y;
            for (std::size_t i = 0; same && i < n; ++i) {
                const auto &a = entry.key.holes[i];
                const auto &b = s.holes[i];
                same = a.start == b.start && a.centre.x == b.centre.x && a.centre.y == b.centre.y &&
                       a.centre.z == b.centre.z && a.phase == b.phase;
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
        const auto n = std::min(out.size(), entry.count);
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = entry.impacts[i];
            out[i].delay -= surface.clock / surface.rate;
        }
        return n;
    }

    std::size_t detail::bubbleParticlesWithIds(
        const BubbleSurface &surface, std::span<IdentifiedParticle> out) {
        return debris(surface, {}, {}, out, false);
    }

}  // namespace hyprimate
