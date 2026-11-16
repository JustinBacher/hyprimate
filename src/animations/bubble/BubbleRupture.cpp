#include <algorithm>
#include <cmath>

#include "animations/bubble/BubbleCurl.hpp"
#include "animations/bubble/BubbleRupture.hpp"

namespace hyprimate {
    namespace {
        constexpr double pi = 3.14159265358979323846;

        Vec3 cross(Vec3 a, Vec3 b) {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }
        double dot(Vec3 a, Vec3 b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
        Vec3 normalize(Vec3 a) {
            const double n = std::sqrt(dot(a, a));
            return n > 0 ? Vec3{a.x / n, a.y / n, a.z / n} : Vec3{1, 0, 0};
        }
        void basis(Vec3 c, Vec3 &e1, Vec3 &e2) {
            e1 = normalize(cross(std::abs(c.z) < .9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0}, c));
            e2 = cross(c, e1);
        }
        double radiusAt(const BubbleSurface &s, const BubbleHole &h, double clock) {
            const double age = std::max(0.0, clock - h.start);
            const double transient = s.spherical ? .05 : .075;
            return s.speed * (age - transient * (1 - std::exp(-age / transient)));
        }
    }  // namespace

    double bubbleHoleAzimuth(const BubbleHole &hole, Vec3 p, bool spherical) {
        if (!spherical) return std::atan2(p.y - hole.centre.y, p.x - hole.centre.x);
        Vec3 e1, e2;
        basis(hole.centre, e1, e2);
        return std::atan2(dot(p, e2), dot(p, e1));
    }

    double bubbleHoleBoundary(const BubbleHole &hole, double phi) {
        double r = 1;
        for (std::size_t i = 0; i < kBubbleRimModes.size(); ++i)
            r += hole.amplitude[i] * std::cos(kBubbleRimModes[i] * phi + hole.phase[i]);
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
                const auto &a = entry.key.holes[i], &b = s.holes[i];
                same = a.start == b.start && a.centre.x == b.centre.x && a.centre.y == b.centre.y &&
                       a.centre.z == b.centre.z && a.phase == b.phase;
            }
            if (same) return entry;
            entry.key = s;
            entry.position = {};
            entry.time = 0;
            entry.mergeCount = 0;
            entry.valid = true;
            const auto candidate = [&](Vec3 p) {
                double first = 1;
                for (std::size_t i = 0; i < n; ++i) {
                    const auto &h = s.holes[i];
                    const double d = s.spherical
                                         ? std::acos(std::clamp(dot(p, h.centre), -1.0, 1.0))
                                         : std::hypot(p.x - h.centre.x, p.y - h.centre.y);
                    first = std::min(first, h.start + d / s.speed + (s.spherical ? .05 : .075));
                }
                if (first > entry.time) {
                    entry.time = first;
                    entry.position = p;
                }
            };
            if (s.spherical) {
                for (Vec3 axis : {Vec3{1, 0, 0},
                         Vec3{-1, 0, 0},
                         Vec3{0, 1, 0},
                         Vec3{0, -1, 0},
                         Vec3{0, 0, 1},
                         Vec3{0, 0, -1}})
                    candidate(axis);
                for (std::size_t i = 0; i < n; ++i)
                    candidate({-s.holes[i].centre.x, -s.holes[i].centre.y, -s.holes[i].centre.z});
                entry.position = {entry.position.x * s.halfSize.x,
                    entry.position.y * s.halfSize.x,
                    entry.position.z * s.halfSize.x};
            } else {
                for (int x = -1; x <= 1; ++x)
                    for (int y = -1; y <= 1; ++y)
                        candidate({x * s.halfSize.x, y * s.halfSize.y, 0});
            }
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t j = i + 1; j < n; ++j) {
                    const auto &a = s.holes[i], &b = s.holes[j];
                    const double distance =
                        s.spherical ? std::acos(std::clamp(dot(a.centre, b.centre), -1.0, 1.0))
                                    : std::hypot(b.centre.x - a.centre.x, b.centre.y - a.centre.y);
                    double lo = std::max(a.start, b.start), hi = 1;
                    if (radiusAt(s, a, lo) >= distance ||
                        radiusAt(s, a, hi) + radiusAt(s, b, hi) < distance)
                        continue;
                    for (int k = 0; k < 9; ++k) {
                        const double mid = (lo + hi) * .5;
                        if (radiusAt(s, a, mid) + radiusAt(s, b, mid) < distance)
                            lo = mid;
                        else
                            hi = mid;
                    }
                    const double weight = distance > 0 ? radiusAt(s, a, hi) / distance : 0;
                    Vec3 origin{a.centre.x * (1 - weight) + b.centre.x * weight,
                        a.centre.y * (1 - weight) + b.centre.y * weight,
                        a.centre.z * (1 - weight) + b.centre.z * weight};
                    if (s.spherical) {
                        const double denom = std::sin(distance);
                        if (std::abs(denom) > 1e-8) {
                            const double wa = std::sin((1 - weight) * distance) / denom;
                            const double wb = std::sin(weight * distance) / denom;
                            origin = {a.centre.x * wa + b.centre.x * wb,
                                a.centre.y * wa + b.centre.y * wb,
                                a.centre.z * wa + b.centre.z * wb};
                        } else {
                            origin = normalize(origin);
                        }
                    } else if (std::abs(origin.x) > s.halfSize.x ||
                               std::abs(origin.y) > s.halfSize.y) {
                        continue;
                    }
                    bool consumed = false;
                    for (std::size_t k = 0; k < n; ++k) {
                        if (k == i || k == j || hi <= s.holes[k].start) continue;
                        BubbleHole other = s.holes[k];
                        other.radius = radiusAt(s, other, hi);
                        const double onset = smoothstep(.012, .045, hi - other.start);
                        for (std::size_t m = 0; m < other.amplitude.size(); ++m)
                            other.amplitude[m] = onset * (.014 - .002 * m);
                        const double d =
                            s.spherical
                                ? std::acos(std::clamp(dot(origin, other.centre), -1.0, 1.0))
                                : std::hypot(origin.x - other.centre.x, origin.y - other.centre.y);
                        if (d < bubbleHoleBoundary(
                                    other, bubbleHoleAzimuth(other, origin, s.spherical)))
                            consumed = true;
                    }
                    if (consumed) continue;
                    if (s.spherical)
                        origin = {origin.x * s.halfSize.x,
                            origin.y * s.halfSize.x,
                            origin.z * s.halfSize.x};
                    entry.merges[entry.mergeCount++] = {
                        origin, hi, static_cast<std::uint32_t>(1000 + 20 * i + 5 * j)};
                }
            }
            return entry;
        }
    }  // namespace
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
        if (!s.closing || s.speed <= 0 || s.rate <= 0 || s.halfSize.x <= 0 || s.halfSize.y <= 0)
            return 0;
        if (!impactMode) {
            bool started = false;
            for (std::size_t i = 0; i < std::min(s.holeCount, kBubbleHoles); ++i)
                started |= s.clock > s.holes[i].start;
            if (!started) return 0;
        }
        const ImpactCache *selected = impactMode ? nullptr : &impactCache(s);
        std::size_t count = 0;
        for (std::size_t hi = 0; hi < std::min(s.holeCount, kBubbleHoles); ++hi) {
            const auto &h = s.holes[hi];
            const double minHalf = std::min(s.halfSize.x, s.halfSize.y);
            const double threshold =
                std::max(24.0, .08 * minHalf) / (s.spherical ? s.halfSize.x : 1);
            if (!impactMode && h.radius < threshold) continue;
            const double transient = s.spherical ? .05 : .075;
            const double unitsPerPixel = s.spherical ? 1 / s.halfSize.x : 1;
            std::uint64_t holeRng = s.debris ^ (hi * 0x9e3779b97f4a7c15ULL) ^ 0x94d049bb133111ebULL;
            const int fingerCount = 14 + static_cast<int>(13 * unitDraw(holeRng));
            Vec3 e1{}, e2{};
            if (s.spherical) basis(h.centre, e1, e2);
            for (int finger = 0; finger < fingerCount; ++finger) {
                if (impactMode && count >= (s.spherical ? 3u : 2u)) break;
                std::uint64_t rng =
                    s.debris ^ (hi * 0x9e3779b97f4a7c15ULL) ^ (finger * 0xbf58476d1ce4e5b9ULL);
                if (unitDraw(rng) < .27) continue;
                const double phi = 2 * pi * (finger + .9 * (unitDraw(rng) - .5)) / fingerCount;
                const int order = static_cast<int>(fingerCount * unitDraw(rng));
                const double siteRadius =
                    std::max(threshold, (order + 1) * 50 / (2 * pi) * unitsPerPixel);
                if (!impactMode && h.radius < siteRadius) continue;
                const double target = siteRadius / s.speed;
                double onsetAge = target + transient;
                for (int i = 0; i < 4; ++i) {
                    const double expAge = std::exp(-onsetAge / transient);
                    onsetAge -= (onsetAge - transient * (1 - expAge) - target) / (1 - expAge);
                }
                const double fingerStart = h.start + onsetAge;
                const double delay = .08 * s.rate * unitDraw(rng);
                const double period = (.05 + .04 * unitDraw(rng)) * s.rate;
                const double length = 6 + 16 * unitDraw(rng);
                const double threadRadius = .4 + .5 * unitDraw(rng);
                const double tilt = .7 * (unitDraw(rng) - .5);
                const double cp = std::cos(phi), sp = std::sin(phi);
                const Vec3 direction =
                    s.spherical
                        ? Vec3{e1.x * cp + e2.x * sp, e1.y * cp + e2.y * sp, e1.z * cp + e2.z * sp}
                        : Vec3{cp, sp, 0};
                double corrugation = 0, currentCorrugation = 0;
                for (std::size_t m = 0; m < kBubbleRimModes.size(); ++m) {
                    const double wave = std::cos(kBubbleRimModes[m] * phi + h.phase[m]);
                    corrugation += (.014 - .002 * m) * wave;
                    currentCorrugation += h.amplitude[m] * wave;
                }
                const auto at = [&](double radius) {
                    if (!s.spherical)
                        return Vec3{h.centre.x + direction.x * radius,
                            h.centre.y + direction.y * radius,
                            0};
                    const double c = std::cos(radius), sn = std::sin(radius), scale = s.halfSize.x;
                    return Vec3{scale * (h.centre.x * c + direction.x * sn),
                        scale * (h.centre.y * c + direction.y * sn),
                        scale * (h.centre.z * c + direction.z * sn)};
                };
                const auto edge = [&](double boundary) {
                    const Vec3 base = at(boundary);
                    const double c = s.spherical ? std::cos(boundary) : 1;
                    const double sn = s.spherical ? std::sin(boundary) : 0;
                    const Vec3 outward{direction.x * c - h.centre.x * sn,
                        direction.y * c - h.centre.y * sn,
                        direction.z * c - h.centre.z * sn};
                    const Vec3 normal = s.spherical ? Vec3{base.x / s.halfSize.x,
                                                          base.y / s.halfSize.x,
                                                          base.z / s.halfSize.x}
                                                    : Vec3{0, 0, 1};
                    return bubbleCurlSample(base,
                        outward,
                        normal,
                        bubbleCurlSection(s, h, boundary * (s.spherical ? s.halfSize.x : 1), phi),
                        1)
                        .position;
                };
                const auto tipAt = [&](double boundary, Vec3 root) {
                    const Vec3 base = at(boundary);
                    const Vec3 tip = at(std::max(0.0, boundary - length * unitsPerPixel));
                    const double dx = tip.x - base.x, dy = tip.y - base.y;
                    return Vec3{root.x + dx * std::cos(tilt) - dy * std::sin(tilt),
                        root.y + dx * std::sin(tilt) + dy * std::cos(tilt),
                        root.z + tip.z - base.z};
                };
                const double factor = .55 + .4 * unitDraw(rng);
                const bool bead = unitDraw(rng) < .13;
                const double size = bead ? 2.5 + 1.5 * unitDraw(rng) : .8 + 1.7 * unitDraw(rng);
                const double maxLifetime =
                    (bead ? .18 + .12 * unitDraw(rng) : .08 + .07 * unitDraw(rng)) * s.rate;
                const int satellites = 1 + (unitDraw(rng) < .85);
                const double satelliteScale = .3 + .2 * unitDraw(rng);
                const double scatter = .04 + .08 * unitDraw(rng);
                for (int drop = 0; drop < 4; ++drop) {
                    const double firstDelay = std::max(delay, period * .35);
                    const double forming = std::min(period * .65, firstDelay + drop * period);
                    const double birth = fingerStart + firstDelay + drop * period;
                    const double lifetime = std::min(maxLifetime, .995 - birth);
                    if (lifetime <= 0) continue;
                    const double age = s.clock - birth;
                    if (!impactMode && age >= -forming && age < 0 && h.radius > 0) {
                        const double currentRadius = h.radius * (1 + currentCorrugation);
                        const Vec3 root = at(currentRadius);
                        bool onFilm = s.spherical ? currentRadius < pi
                                                  : std::abs(root.x) <= s.halfSize.x &&
                                                        std::abs(root.y) <= s.halfSize.y;
                        const Vec3 position = s.spherical ? Vec3{root.x / s.halfSize.x,
                                                                root.y / s.halfSize.x,
                                                                root.z / s.halfSize.x}
                                                          : root;
                        for (std::size_t other = 0;
                            onFilm && other < std::min(s.holeCount, kBubbleHoles);
                            ++other) {
                            if (other == hi || s.holes[other].radius <= 0) continue;
                            const auto &prior = s.holes[other];
                            if (s.spherical) {
                                const double alignment = dot(position, prior.centre);
                                if (prior.radius * .94 >= pi ||
                                    (prior.radius * .94 < pi &&
                                        alignment > std::cos(prior.radius * .94))) {
                                    onFilm = false;
                                } else if ((prior.radius * 1.06 >= pi ||
                                               alignment > std::cos(prior.radius * 1.06)) &&
                                           alignment > std::cos(std::min(pi,
                                                           bubbleHoleBoundary(prior,
                                                               bubbleHoleAzimuth(
                                                                   prior, position, true))))) {
                                    onFilm = false;
                                }
                            } else {
                                const double dx = root.x - prior.centre.x,
                                             dy = root.y - prior.centre.y;
                                const double distance = std::hypot(dx, dy);
                                if (distance < prior.radius * 1.06 &&
                                    distance < bubbleHoleBoundary(prior,
                                                   bubbleHoleAzimuth(prior, position, false)))
                                    onFilm = false;
                            }
                        }
                        if (onFilm) {
                            const double progress = 1 + age / forming;
                            const double behind =
                                s.spherical ? length * progress / s.halfSize.x : length * progress;
                            const Vec3 baseTip = at(std::max(0.0, currentRadius - behind));
                            const Vec3 curled = edge(currentRadius);
                            const double dx = baseTip.x - root.x, dy = baseTip.y - root.y;
                            BubbleParticle ligament;
                            ligament.centre = {curled.x + dx * std::cos(tilt) - dy * std::sin(tilt),
                                curled.y + dx * std::sin(tilt) + dy * std::cos(tilt)};
                            ligament.tail = {curled.x, curled.y};
                            ligament.radius =
                                std::max(threadRadius * 1.25, size * (.6 + .4 * progress));
                            ligament.tailRadius = threadRadius;
                            ligament.depth = (curled.z + baseTip.z - root.z) / minHalf;
                            if (count < out.size())
                                out[count++] = ligament;
                            else if (count < identified.size())
                                identified[count++] = {ligament,
                                    static_cast<std::uint32_t>(
                                        2000 + hi * 104 + finger * 4 + drop)};
                        }
                    }
                    if (impactMode ? drop != 0 : (age < 0 || age >= lifetime)) continue;
                    const double r = radiusAt(s, h, birth);
                    if (s.spherical && r >= pi) continue;
                    const double onset = smoothstep(.012, .045, birth - h.start);
                    const double boundary = r * (1 + onset * corrugation);
                    if (s.spherical && boundary >= pi) continue;
                    const Vec3 base = at(boundary);
                    if (!s.spherical &&
                        (std::abs(base.x) > s.halfSize.x || std::abs(base.y) > s.halfSize.y))
                        continue;
                    bool consumed = false;
                    for (std::size_t other = 0; other < std::min(s.holeCount, kBubbleHoles);
                        ++other) {
                        if (other == hi || birth <= s.holes[other].start) continue;
                        const auto &source = s.holes[other];
                        const Vec3 position = s.spherical ? Vec3{base.x / s.halfSize.x,
                                                                base.y / s.halfSize.x,
                                                                base.z / s.halfSize.x}
                                                          : base;
                        const double upper = s.speed * (birth - source.start) * 1.06;
                        const double dx = base.x - source.centre.x, dy = base.y - source.centre.y;
                        if (!s.spherical && dx * dx + dy * dy >= upper * upper) continue;
                        if (s.spherical && upper < pi &&
                            dot(position, source.centre) <= std::cos(upper))
                            continue;
                        BubbleHole prior = source;
                        prior.radius = radiusAt(s, prior, birth);
                        const double priorOnset = smoothstep(.012, .045, birth - prior.start);
                        for (std::size_t m = 0; m < prior.amplitude.size(); ++m)
                            prior.amplitude[m] = priorOnset * (.014 - .002 * m);
                        const double distance = s.spherical ? 0 : std::sqrt(dx * dx + dy * dy);
                        const double alignment = s.spherical ? dot(position, prior.centre) : 0;
                        const bool inside =
                            s.spherical
                                ? (prior.radius * .94 >= pi ||
                                      alignment > std::cos(std::min(pi, prior.radius * .94)) ||
                                      ((prior.radius * 1.06 >= pi ||
                                           alignment > std::cos(prior.radius * 1.06)) &&
                                          alignment >
                                              std::cos(std::min(pi,
                                                  bubbleHoleBoundary(prior,
                                                      bubbleHoleAzimuth(prior, position, true))))))
                                : (distance < prior.radius * 1.06 &&
                                      distance < bubbleHoleBoundary(prior,
                                                     bubbleHoleAzimuth(prior, position, false)));
                        if (inside) {
                            consumed = true;
                            break;
                        }
                    }
                    if (consumed) continue;
                    const Vec3 p = tipAt(boundary, edge(boundary));
                    const double transient = s.spherical ? .05 : .075;
                    const double rimVelocity = s.speed *
                                               ((birth - h.start - r / s.speed) / transient) *
                                               (1 + onset * corrugation);
                    const double velocity = rimVelocity * factor;
                    const double c = s.spherical ? std::cos(boundary) : 1;
                    const double sn = s.spherical ? std::sin(boundary) : 0;
                    const double scale = s.spherical ? s.halfSize.x : 1;
                    const Vec3 tangent{scale * (direction.x * c - h.centre.x * sn),
                        scale * (direction.y * c - h.centre.y * sn),
                        scale * (direction.z * c - h.centre.z * sn)};
                    const double vx = tangent.x * velocity;
                    const double vy = tangent.y * velocity;
                    const double minHalf = std::min(s.halfSize.x, s.halfSize.y);
                    const double magnitude = s.spherical ? std::hypot(vx, vy) : velocity;
                    const double tau =
                        magnitude > 0 ? std::min(.13, .35 * minHalf / magnitude) : .13;
                    const double maxWidth =
                        std::min(48.0, .18 * minHalf) * std::clamp(s.aureole, 0.0, 1.0);
                    const double plane =
                        (s.spherical ? minHalf * 1.01 : minHalf * .10) + 2 * maxWidth / 1.7;
                    const double distance = plane - p.z;
                    const std::uint32_t sourceId =
                        static_cast<std::uint32_t>(hi * 104 + finger * 4 + drop);
                    const bool candidate =
                        drop == 0 &&
                        (impactMode || std::find(selected->ids.begin(),
                                           selected->ids.begin() + selected->count,
                                           sourceId) != selected->ids.begin() + selected->count);
                    const double vz = tangent.z * velocity +
                                      magnitude * (candidate ? (s.spherical ? 2.8 : 1.4) : .3);
                    const double fraction = candidate && vz > 0 ? distance / (vz * tau) : 2;
                    const double hitAge =
                        fraction > 0 && fraction < 1 ? -tau * std::log1p(-fraction) : lifetime + 1;
                    const double hitRadius = size * (1 - hitAge / lifetime) * 1.5;
                    const bool hits = hitAge < lifetime && hitRadius >= 1.5 && hitRadius <= 3.5;
                    if (impactMode) {
                        if (hits && count < impacts.size() && count < kBubbleMaxImpacts) {
                            const double travel = tau * (1 - std::exp(-hitAge / tau));
                            const double sagLimit = .05 * minHalf;
                            const double sag =
                                sagLimit * (1 - std::exp(-120 * hitAge * hitAge / sagLimit));
                            if (count < impactIds.size()) impactIds[count] = sourceId;
                            impacts[count++] = {{p.x + vx * travel, p.y + vy * travel + sag},
                                hitRadius,
                                (birth + hitAge - s.clock) / s.rate,
                                {0, size * 2}};
                        }
                        continue;
                    }
                    const double drag = tau * (1 - std::exp(-age / tau));
                    const double sagLimit = .05 * minHalf;
                    const double sag =
                        sagLimit > 0 ? sagLimit * (1 - std::exp(-120 * age * age / sagLimit)) : 0;
                    BubbleParticle particle;
                    particle.centre = {p.x + vx * drag, p.y + vy * drag + sag};
                    particle.radius = size * (1 - age / lifetime);
                    particle.depth = (p.z + vz * drag) / minHalf;
                    if (hits) {
                        const double approach = std::clamp(age / hitAge, 0.0, 1.0);
                        particle.radius *= 1 + .5 * approach * approach;
                        particle.blur = approach * approach;
                    }
                    const double detach = .04 * s.rate;
                    const double currentBoundary = h.radius * (1 + currentCorrugation);
                    const Vec3 tailBase = at(currentBoundary);
                    bool tailOnFilm = s.spherical ? currentBoundary < pi
                                                  : std::abs(tailBase.x) <= s.halfSize.x &&
                                                        std::abs(tailBase.y) <= s.halfSize.y;
                    const Vec3 tailPosition = s.spherical ? Vec3{tailBase.x / s.halfSize.x,
                                                                tailBase.y / s.halfSize.x,
                                                                tailBase.z / s.halfSize.x}
                                                          : tailBase;
                    for (std::size_t other = 0;
                        tailOnFilm && other < std::min(s.holeCount, kBubbleHoles);
                        ++other) {
                        if (other == hi || s.holes[other].radius <= 0) continue;
                        const auto &prior = s.holes[other];
                        const double distance =
                            s.spherical
                                ? std::acos(std::clamp(dot(tailPosition, prior.centre), -1.0, 1.0))
                                : std::hypot(
                                      tailBase.x - prior.centre.x, tailBase.y - prior.centre.y);
                        if (distance < bubbleHoleBoundary(prior,
                                           bubbleHoleAzimuth(prior, tailPosition, s.spherical)))
                            tailOnFilm = false;
                    }
                    const Vec3 tail = age < detach && tailOnFilm ? edge(currentBoundary) : p;
                    particle.tail =
                        age < detach && tailOnFilm ? Vec2{tail.x, tail.y} : particle.centre;
                    particle.tailRadius = age < detach && tailOnFilm
                                              ? size * .3 * std::max(0.0, 1 - age / detach)
                                              : 0;
                    if (!hits || age < hitAge) {
                        if (count < out.size())
                            out[count++] = particle;
                        else if (count < identified.size())
                            identified[count++] = {
                                particle, static_cast<std::uint32_t>(hi * 104 + finger * 4 + drop)};
                    }
                    for (int satellite = 0; satellite < satellites; ++satellite) {
                        const double satelliteLife = lifetime * (.7 + .1 * satellite);
                        if (age >= satelliteLife) continue;
                        const double lateral = satellite == 0 ? scatter : -scatter;
                        const double svx = .8 * vx - lateral * vy;
                        const double svy = .8 * vy + lateral * vx;
                        const double speed = std::hypot(svx, svy);
                        const double stau = speed > 0 ? std::min(.13, .35 * minHalf / speed) : .13;
                        const double travel = stau * (1 - std::exp(-age / stau));
                        BubbleParticle small;
                        small.centre = {p.x + svx * travel - tangent.x / scale * .5,
                            p.y + svy * travel - tangent.y / scale * .5 + sag};
                        small.tail = small.centre;
                        small.radius = size * satelliteScale * (1 - age / satelliteLife);
                        small.depth = (p.z + .8 * vz * travel) / minHalf;
                        if (count < out.size())
                            out[count++] = small;
                        else if (count < identified.size())
                            identified[count++] = {small,
                                static_cast<std::uint32_t>(
                                    10000 + (hi * 104 + finger * 4 + drop) * 2 + satellite)};
                    }
                }
            }
        }
        if (!impactMode && s.holeCount) {
            const auto burst = [&](Vec3 origin, double birth, std::uint32_t id) {
                const double age = s.clock - birth;
                const double life = std::min(.22 * s.rate, .995 - birth);
                if (age < 0 || age >= life) return;
                const double reach = .12 * std::min(s.halfSize.x, s.halfSize.y);
                for (std::uint32_t i = 0; i < 5; ++i) {
                    const double phi = 2 * pi * (i + .13 * (id % 7)) / 4;
                    const double motion = reach * (1 - std::exp(-age / .09));
                    BubbleParticle p;
                    p.centre = {origin.x + motion * std::cos(phi),
                        origin.y + motion * std::sin(phi) + reach * .1 * age};
                    p.tail = p.centre;
                    p.radius = (i == 4 ? std::min(6.0 + ((id / 5) % 5) * 2.0,
                                             .08 * std::min(s.halfSize.x, s.halfSize.y))
                                       : 2.5 + .35 * i) *
                               (1 - age / life);
                    p.depth = s.spherical ? origin.z / s.halfSize.x : 0;
                    if (i == 4) {
                        p.thickness = std::max(250.0, s.thickness) * (1 - age / life);
                        p.spin = phi + age * 28;
                    }
                    if (count < out.size())
                        out[count++] = p;
                    else if (count < identified.size())
                        identified[count++] = {p, id + i};
                }
            };
            const auto &events = collapse(s);
            for (std::size_t i = 0; i < events.mergeCount; ++i)
                burst(events.merges[i].position, events.merges[i].time, events.merges[i].id);
            burst(events.position, events.time, 1200);
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
                const auto &a = entry.key.holes[i], &b = s.holes[i];
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
            surface.halfSize.x <= 0 || surface.halfSize.y <= 0)
            return 0;
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
