#include "ConstructionSite.hpp"

#include "ConstructionCrew.hpp"

#include <numbers>

#include "WindowAnimationFacts.hpp"

namespace hyprimate {
namespace {
using namespace construction;

constexpr double TENSION_START = .270;
constexpr double LIFT_START = .315;
constexpr double LIFT_PEAK = .425;
constexpr double RETIRE = .930;
// Authoring controls for the receiving fastener (same socket paints the bolt and aims the hammer).
constexpr double FASTENER_FROM_EDGE = -7.0;
constexpr double FASTENER_ABOVE_FLOOR = 37.0;
constexpr Tint NAVY{29, 46, 58, 255}, STEEL{65, 100, 111, 255}, MINT{152, 188, 175, 255};
constexpr Tint DARK_GOLD{148, 86, 47, 255}, GOLD{225, 160, 79, 255}, PALE_GOLD{247, 206, 121, 255};
constexpr Tint WHITE{255, 255, 255, 255};

struct Cues {
    double time, lower, seat, strike, unlatch, release;
};

struct Panel {
    Vec3 heel, across, down, normal;
    double width, height, lift;

    Vec3 at(double x, double y, double depth = 0) const noexcept {
        return heel + across * x + down * y + normal * depth;
    }
};

struct Rig {
    Vec3 pivot, elbow, sheave, hook, join, bar_l, bar_r, eye_l, eye_r;
    double slack, opacity;
};

bool usable(Box box) noexcept {
    return is_usable(box) && std::isfinite(box.x + box.w) && std::isfinite(box.y + box.h);
}

Layout derive_layout(Box body, Box cap, Box area) noexcept {
    const double bottom = body.y + body.h;
    const double right = body.x + body.w;
    const double seam = body.x + body.w * .58, lintel = body.y + body.h * .60;
    const Box opening{seam, lintel, right - seam, bottom - lintel};
    const double scale = std::clamp(std::min(body.w / 300.0, body.h / 245.0), .18, 1.0);
    const double right_room = area.x + area.w - right;
    const double floor = bottom + std::clamp(area.y + area.h - bottom - scale * 16, -10 * scale, 14 * scale);
    const double throw_x = right_room >= 72 * scale ? std::min(56 * scale, body.w * .15) : -std::min(60 * scale, body.w * .14);
    // Do not turn oversized capture outsets (e.g. a dimmed output) into a wall.
    const bool small_outsets = body.x <= 48 && body.y <= 48 && cap.w - right <= 48 && cap.h - bottom <= 48;
    const Box moving{seam, lintel, small_outsets ? std::max(opening.w, cap.w - seam) : opening.w,
        small_outsets ? std::max(opening.h, cap.h - lintel) : opening.h};
    return {body, cap, opening, moving, {seam, floor, 7 * scale}, scale, throw_x,
        seam - 67 * scale, right + std::clamp(right_room - 12 * scale, 3 * scale, 73 * scale),
        std::clamp(floor - bottom - 39 * scale, -opening.h * .84, -opening.h * .07),
        std::clamp(floor - bottom - FASTENER_ABOVE_FLOOR * scale, -opening.h * .78, -opening.h * .06)};
}

Panel pose_panel(const Layout &l, const Cues &c) noexcept {
    const double t = c.time, s = l.scale;
    const double lift = smoothstep(LIFT_START, LIFT_PEAK, t);
    const double slew = smoothstep(.358, GUIDE_LATCH, t);
    const double lowering = smoothstep(c.lower, c.seat, t);
    const double fly = 1 - lowering;
    const double initial_y = l.floor.y - (l.opening.y + l.opening.h);
    const double height = std::min(78 * s, l.opening.h * .42);
    const double dy = std::lerp(initial_y, -height, lift) * (1 - slew) + (-8 * s) * slew;
    const double swing = std::sin(std::numbers::pi * slew);
    const double yaw = fly * (.17 * (1 - slew) + .11 * swing + .032 * slew);
    const double pitch = fly * (.045 + .045 * swing);
    const double roll = fly * (-.10 * swing - .009 * slew);
    const double cr = std::cos(roll), sr = std::sin(roll);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const auto turn = [cr, sr](Vec3 p) -> Vec3 { return {p.x * cr - p.y * sr, p.x * sr + p.y * cr, p.z}; };
    return {Vec3{l.opening.x, l.opening.y + l.opening.h, 0} +
            Vec3{l.travel * (1 - slew), dy, s * (7 + 24 * lift * (1 - slew))} * fly,
        turn({cy, 0, -sy}), turn({sy * sp, cp, cy * sp}), turn({sy * cp, -sp, cy * cp}),
        l.opening.w, l.opening.h, fly * std::max(0.0, -dy)};
}

void flat(ConstructionBatch &batch, Box box, double depth, Tint color) noexcept {
    if (box.w > 0 && box.h > 0) quad(batch, {box.x, box.y, depth}, {box.w, 0, 0}, {0, box.h, 0}, color);
}

void client(ConstructionBatch &batch, Box source, Vec3 origin, Vec3 u, Vec3 v) noexcept {
    if (source.w <= 0 || source.h <= 0) return;
    quad(batch, origin, u * source.w, v * source.h, WHITE, ConstructionSurface::Client,
        {static_cast<float>(source.x), static_cast<float>(source.y), static_cast<float>(source.w), static_cast<float>(source.h)});
}

void identity(ConstructionBatch &batch, Box source) noexcept {
    client(batch, source, {source.x, source.y, 0}, {1, 0, 0}, {0, 1, 0});
}

void standing_bay(ConstructionBatch &batch, Box cap, Box vacancy) noexcept {
    const double right = std::clamp(vacancy.x + vacancy.w, 0.0, cap.w);
    const double bottom = std::clamp(vacancy.y + vacancy.h, 0.0, cap.h);
    const double left = std::clamp(vacancy.x, 0.0, right);
    const double top = std::clamp(vacancy.y, 0.0, bottom);
    identity(batch, {0, 0, cap.w, top});
    identity(batch, {0, top, left, bottom - top});
    identity(batch, {right, top, cap.w - right, bottom - top});
    identity(batch, {0, bottom, cap.w, cap.h - bottom});
}

void line(ConstructionBatch &batch, Vec3 a, Vec3 b, double width, Tint color) noexcept {
    const Vec3 along = b - a;
    const Vec3 side = unit({along.y, -along.x, 0}, {1, 0, 0}) * width;
    quad(batch, a - side * .5, along, side, color);
}

void rope(ConstructionBatch &batch, Vec3 a, Vec3 b, double slack, double scale, double opacity) noexcept {
    const auto dark = tint(NAVY, opacity);
    const auto stripe = tint(PALE_GOLD, opacity);
    if (slack > .05) {
        const Vec3 mid = mix(a, b, .5) + Vec3{slack * .65, slack, 0};
        line(batch, a, mid, 2.4 * scale, dark);
        line(batch, mid, b, 2.4 * scale, dark);
        line(batch, a, mid, .78 * scale, stripe);
        line(batch, mid, b, .78 * scale, stripe);
    } else {
        line(batch, a, b, 2.4 * scale, dark);
        line(batch, a, b, .78 * scale, stripe);
    }
}

void lattice(ConstructionBatch &batch, Vec3 a, Vec3 b, double width, double s, double opacity) noexcept {
    const Vec3 along = b - a;
    const Vec3 side = unit({-along.y, along.x, 0}, {1, 0, 0}) * (.5 * width);
    const int count = static_cast<int>(std::clamp(std::ceil(length(along) / (22 * s)), 1.0, 14.0));
    const Vec3 stride = along * (1.0 / count);
    const auto steel = tint(STEEL, opacity);
    const auto navy = tint(NAVY, opacity);
    line(batch, a - side, b - side, 4.4 * s, navy);
    line(batch, a + side, b + side, 4.4 * s, navy);
    for (int i = 0; i < count; ++i) {
        const Vec3 low = a + stride * i;
        if (i % 2 == 0) line(batch, low - side, low + stride + side, 2.2 * s, steel);
        else line(batch, low + side, low + stride - side, 2.2 * s, steel);
    }
    line(batch, a - side, b - side, 2.0 * s, tint(PALE_GOLD, opacity));
    line(batch, a + side, b + side, 2.0 * s, tint(GOLD, opacity));
}

Rig pose_rig(const Layout &l, const Panel &p, const Cues &c) noexcept {
    const double t = c.time, s = l.scale;
    const double linked = 1 - smoothstep(c.unlatch, c.release + .022, t);
    const double withdraw = smoothstep(c.release, RETIRE, t);
    const double payout = (1 - smoothstep(TENSION_START, LIFT_START, t)) + smoothstep(c.strike + .009, c.unlatch, t);
    // Both lifting points are derived from the exact panel pose, including brake and seat.
    const Vec3 left = p.at(p.width * .18, -p.height - 2.5 * s);
    const Vec3 right = p.at(p.width * .82, -p.height - 2.5 * s);
    const Vec3 anchor = mix(left, right, .5);
    const Vec3 join = anchor + Vec3{withdraw * (l.opening.w * .30 + 31 * s), (-17 + 4 * payout - 22 * withdraw) * s, 1.0 * s};
    const Vec3 bar = (right - left) * (.41 - .12 * withdraw);
    const Vec3 bar_l = join - bar, bar_r = join + bar;
    const Vec3 hook = join + Vec3{-2.8 * s, -13.8 * s, 0};
    const Vec3 pivot{l.opening.x + l.opening.w * .83 + (8 + 41 * withdraw) * s, l.floor.y - 16 * s, -8 * s};
    const Vec3 sheave = hook + Vec3{(20 - 13 * smoothstep(LIFT_START, LIFT_PEAK, t) + 12 * withdraw) * s, -35 * s, -7 * s};
    const Vec3 elbow = mix(pivot, sheave, .54) + Vec3{(24 + 9 * smoothstep(.40, .52, t)) * s, -7 * s, -2 * s};
    return {pivot, elbow, sheave, hook, join, bar_l, bar_r,
        mix(bar_l + Vec3{3 * s, 16 * s, 0}, left, linked), mix(bar_r + Vec3{-2 * s, 14.5 * s, 0}, right, linked),
        7 * s * payout, 1 - smoothstep(c.release + .032, RETIRE, t)};
}

void crane(ConstructionBatch &batch, const Layout &l, const Rig &r) noexcept {
    const double s = l.scale;
    const double a = r.opacity;
    lattice(batch, r.pivot, r.elbow, 17 * s, s, a);
    lattice(batch, r.elbow, r.sheave, 12 * s, s, a);
    const Vec3 piston = mix(r.pivot, r.elbow, .31) + Vec3{-9 * s, 2 * s, 0};
    line(batch, r.pivot + Vec3{-10 * s, 8 * s, 0}, piston, 5.0 * s, tint(NAVY, a));
    line(batch, piston, mix(r.pivot, r.elbow, .67), 2.3 * s, tint(MINT, a));
    stamp(batch, SHEAVE, r.pivot, .27 * s, a);
    stamp(batch, SHEAVE, r.elbow, .22 * s, a);
    stamp(batch, SHEAVE, r.sheave, .22 * s, a);
    const Vec3 track{r.pivot.x + 9 * s, l.floor.y + .2 * s, l.floor.z - 3 * s};
    stamp(batch, CHASSIS, track, {.91 * s, 0, 0}, {0, .59 * s, 0}, a);
    stamp(batch, COUNTERWEIGHT, track + Vec3{13 * s, -15 * s, -s}, .49 * s, a);
    stamp(batch, CAB, track + Vec3{-14 * s, -16 * s, s}, .59 * s, a);
}

void rigging(ConstructionBatch &batch, const Layout &l, const Rig &r) noexcept {
    const double s = l.scale, a = r.opacity;
    rope(batch, r.sheave, r.hook, r.slack * .41, s, a);
    line(batch, r.bar_l, r.bar_r, 5.5 * s, tint(NAVY, a));
    line(batch, r.bar_l, r.bar_r, 2.4 * s, tint(PALE_GOLD, a));
    stamp_link(batch, HOOK, r.hook, r.join, a);
    rope(batch, r.bar_l, r.eye_l, r.slack, s, a);
    rope(batch, r.bar_r, r.eye_r, r.slack, s, a);
    stamp(batch, SLING_EYE, r.eye_l, .155 * s, a);
    stamp(batch, SLING_EYE, r.eye_r, .155 * s, a);
}

void travelling_face(ConstructionBatch &batch, const Layout &l, const Panel &p, const Cues &c) noexcept {
    const double w = p.width, h = p.height, s = l.scale;
    const double trim = 1 - smoothstep(c.seat - .043, c.seat, c.time);
    const double back = -7 * s, lip = 3.4 * s;
    // Rear flange has its own depth and silhouette. The rabbet/returns occupy the
    // space between it and the face; none of these lie under the client's glazing.
    quad(batch, p.at(-lip, -h - lip, back), p.across * lip, p.down * (h + 2 * lip), tint(STEEL, trim));
    quad(batch, p.at(w, -h - lip, back), p.across * lip, p.down * (h + 2 * lip), tint(NAVY, trim));
    quad(batch, p.at(0, -h - lip, back), p.across * w, p.down * lip, tint(MINT, trim));
    quad(batch, p.at(0, 0, back), p.across * w, p.down * lip, tint(STEEL, trim));
    quad(batch, p.at(0, -h, back), p.normal * -back, p.down * h, tint(STEEL, trim));
    quad(batch, p.at(w, -h, back), p.normal * -back, p.down * h, tint(NAVY, trim));
    quad(batch, p.at(0, -h, back), p.across * w, p.normal * -back, tint(PALE_GOLD, trim));
    quad(batch, p.at(0, 0, back), p.across * w, p.normal * -back, tint(DARK_GOLD, trim));
    client(batch, l.opening, p.at(0, -h), p.across, p.down);
    // Normal decoration outsets ride alongside the panel, but never become its wall or flange.
    client(batch, {l.opening.x + w, l.opening.y, l.moving.w - w, h}, p.at(w, -h), p.across, p.down);
    client(batch, {l.opening.x, l.opening.y + h, l.moving.w, l.moving.h - h}, p.at(0, 0), p.across, p.down);
}

void deck(ConstructionBatch &batch, const Layout &l, const Panel &p, double opacity) noexcept {
    const double s = l.scale;
    const double x = l.deck_left, w = l.deck_right - x, y = l.floor.y;
    const Vec3 top{x, y - 1.6 * s, -15 * s};
    const Vec3 edge{0, 3.0 * s, 35 * s};
    quad(batch, top, {w, 0, 0}, edge, tint({139, 113, 82, 255}, opacity));
    quad(batch, top + edge, {w, 0, 0}, {0, 4.5 * s, 0}, tint({86, 57, 45, 255}, opacity));
    line(batch, top + edge, top + edge + Vec3{w, 0, 0}, 1.3 * s, tint(PALE_GOLD, opacity));
    // Project a broad light onto the deck, then blend to a narrow umbra at contact.
    // Both lie on the staging plane; a dropped load does not stamp shadow onto raw client pixels.
    const double altitude = smoothstep(0, 53 * s, p.lift);
    const double spread = .93 + .22 * altitude;
    const double diameter = std::min(p.width * spread, w - 2 * s);
    const double shadow_x = std::clamp(p.heel.x + p.width * .5 + 16 * s * altitude,
        x + diameter * .5, l.deck_right - diameter * .5);
    const double shadow_depth = std::lerp(5.0, -6.0, altitude);
    const Vec3 shadow{shadow_x, top.y + (shadow_depth + 15) * s * (3.0 / 35), shadow_depth * s};
    const Vec3 major{diameter / 53.0, 0, 0};
    const Vec3 minor = edge * ((.42 + .34 * altitude) / 53.0);
    stamp(batch, SOFT_SHADOW, shadow, major, minor, opacity * (.32 + .68 * altitude));
    stamp(batch, CONTACT_SHADOW, shadow, major * .88, minor * .59, opacity * (.73 - .68 * altitude));
    const double stand = l.floor.x - 14 * s;
    for (double side : {-9.0, 9.0}) {
        const double leg = stand + side * s;
        line(batch, {leg, y - 12 * s, l.floor.z}, {leg + s, y, l.floor.z}, 2.2 * s, tint(NAVY, opacity));
    }
    stamp(batch, TIMBER, {stand, y - 15.5 * s, l.floor.z}, {.47 * s, 0, 0}, {0, .27 * s, 0}, opacity);
    line(batch, {l.floor.x - 53 * s, y + 1.1 * s, l.floor.z}, {l.floor.x - 4 * s, y + 1.1 * s, l.floor.z},
        1.1 * s, tint(NAVY, .78 * opacity));
}

void receiver(ConstructionBatch &batch, const Layout &l, const Panel &p, double retire, double opacity) noexcept {
    const double s = l.scale;
    const double heel = l.opening.y + l.opening.h;
    const double travel = retire * 30 * s;
    const Vec3 sill{l.opening.x - 5 * s, heel + travel, 0};
    quad(batch, sill, {l.opening.w + 7 * s, 0, 0}, {0, 3 * s, 8 * s}, tint(MINT, opacity));
    quad(batch, sill + Vec3{0, 3 * s, 8 * s}, {l.opening.w + 7 * s, 0, 0}, {0, 6 * s, 0}, tint(STEEL, opacity));
    const double contact = 1 - smoothstep(0, 9 * s, length(p.heel - Vec3{l.opening.x, heel, 0}));
    line(batch, sill + Vec3{5 * s, .9 * s, 2.4 * s},
        sill + Vec3{l.opening.w + 5 * s, .9 * s, 2.4 * s}, 1.8 * s, tint(NAVY, .74 * opacity * contact));
    flat(batch, {l.opening.x - 6.0 * s, l.opening.y + travel, 5.7 * s, l.opening.h}, -2 * s, tint(NAVY, opacity));
    flat(batch, {l.opening.x - 5.0 * s, l.opening.y + travel, 2.1 * s, l.opening.h}, -2 * s, tint(GOLD, opacity));
}

void foreground_post(ConstructionBatch &batch, const Layout &l, double withdraw, double opacity) noexcept {
    const double s = l.scale, x = l.floor.x - (36 + 9 * withdraw) * s, y = l.floor.y + 27 * s * withdraw;
    const double top = y - 31 * s, z = l.floor.z + 10 * s;
    quad(batch, {x - 4 * s, y, z}, {13 * s, 0, 0}, {-2 * s, 3.5 * s, 3 * s}, tint(NAVY, opacity));
    quad(batch, {x + 4.0 * s, top, z}, {4.2 * s, 1.4 * s, -5 * s}, {0, 32 * s, 0}, tint(DARK_GOLD, opacity));
    flat(batch, {x, top, 4.0 * s, 32 * s}, z, tint(GOLD, opacity));
    line(batch, {x + s, top + s, z}, {x + s, y, z}, .9 * s, tint(PALE_GOLD, opacity));
    for (int stripe = 0; stripe < 3; ++stripe) {
        const double by = top + (5 + stripe * 5) * s;
        quad(batch, {x, by, z}, {4 * s, -2 * s, 0}, {0, 2.1 * s, 0}, tint(NAVY, opacity));
    }
}

void contact_response(ConstructionBatch &batch, Vec3 bolt, double scale, const Cues &c, double opacity) noexcept {
    const double hit = (c.time - c.strike) / .030;
    stamp(batch, FASTENER, bolt, (c.time < c.strike ? .155 : .119) * scale, opacity);
    if (hit < 0 || hit >= 1) return;
    // One short physical impact, not a spark loop. The shrinking washer is the changed part.
    constexpr std::array<Vec2, 4> RAYS{{{1, -.52}, {.65, -.92}, {.98, .52}, {.50, .91}}};
    for (const auto ray : RAYS) {
        const Vec3 direction{ray.x, ray.y, 0};
        const Vec3 from = bolt + direction * ((2.8 + 8 * hit) * scale);
        line(batch, from, from + direction * (4.2 * (1 - hit) * scale), 1.35 * scale,
            tint(PALE_GOLD, opacity * (1 - hit)));
    }
}

Box projected_bounds(const ConstructionBatch &batch, Box body, Box capture) noexcept {
    const double focal = 4 * std::max({body.w, body.h, 64.0});
    const Vec2 center{body.x + body.w * .5, body.y + body.h * .5};
    double min_x = 0, min_y = 0, max_x = capture.w, max_y = capture.h;
    for (std::size_t i = 0; i < batch.count; ++i) {
        const auto &q = batch.quads[i];
        for (unsigned corner = 0; corner != 4; ++corner) {
            const double u = (corner & 1U) != 0, v = (corner & 2U) != 0;
            const double w = 1.0 - (q.origin[2] + u * q.u[2] + v * q.v[2]) / focal;
            const double x = center.x + (q.origin[0] + u * q.u[0] + v * q.v[0] - center.x) / w;
            const double y = center.y + (q.origin[1] + u * q.u[1] + v * q.v[1] - center.y) / w;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    return {min_x - 2, min_y - 2, max_x - min_x + 4, max_y - min_y + 4};
}
}  // namespace

void ConstructionSite::build(Box body, Box capture, Box work_area, std::uint64_t seed, double duration) {
    m_body = body;
    m_capture = capture;
    m_duration = duration;
    m_elapsed = 0;
    m_delayed = (seed & 1U) != 0;
    m_active = usable(body) && usable(capture) && usable(work_area) && std::isfinite(duration) && duration > 0;
    m_batch.count = 0;
    m_batch.overflow = false;
    m_bounds = {};
    if (!m_active) return;
    m_layout = derive_layout({body.x - capture.x, body.y - capture.y, body.w, body.h},
        {0, 0, capture.w, capture.h}, {work_area.x - capture.x, work_area.y - capture.y, work_area.w, work_area.h});
    publish(0);
}

void ConstructionSite::step(const animation::Step &step) noexcept {
    if (!m_active) return;
    if (step.stalled || step.dying || !std::isfinite(step.elapsed) || step.elapsed < 0 || step.elapsed > STALL_SECONDS) {
        m_active = false;
    } else if (step.elapsed > 0) {
        const bool before_identity = m_elapsed < RETIRE * m_duration;
        m_elapsed += step.elapsed;
        // If ordinary frame cadence skips the tail, still publish identity once.
        m_active = m_elapsed < m_duration || before_identity;
        if (m_active) publish(m_elapsed / m_duration);
    }
    if (!m_active) {
        m_batch.count = 0;
        m_batch.overflow = false;
        m_bounds = {0, 0, m_capture.w, m_capture.h};
    }
}

void ConstructionSite::publish(double time) noexcept {
    auto &batch = m_batch;
    batch.count = 0;
    batch.overflow = false;
    const Layout &l = m_layout;
    const Box cap = l.capture;
    // Keep rendering through a deliberate exact identity interval before expiring.
    if (time >= RETIRE) {
        identity(batch, cap);
        m_bounds = cap;
        return;
    }

    const double delay = m_delayed ? .08 : 0;
    const double seat = .66 + delay;
    const Cues c{time, .55 + delay, seat, seat + .055, seat + .082, seat + .096};
    const Panel panel = pose_panel(l, c);
    const double s = l.scale;
    const double retract = smoothstep(c.release, RETIRE, time);
    const double stage_opacity = 1 - smoothstep(c.release + .014, RETIRE, time);
    const Vec3 guide = panel.at(0, l.guide_y);
    const Vec3 bolt = panel.at(FASTENER_FROM_EDGE * s, l.bolt_y, 7 * s);
    const Rig machine = pose_rig(l, panel, c);
    const CrewPose crew = evaluate_crew({l.floor, s, time, c.seat, c.strike, c.release, RETIRE, guide, bolt});

    if (time >= c.seat) identity(batch, cap);
    else standing_bay(batch, cap, l.moving);
    deck(batch, l, panel, stage_opacity);
    receiver(batch, l, panel, retract, stage_opacity);
    crane(batch, l, machine);
    if (time < c.seat) travelling_face(batch, l, panel, c);
    rigging(batch, l, machine);
    draw_crew_far(batch, crew);
    foreground_post(batch, l, retract, stage_opacity);
    contact_response(batch, bolt, s, c, stage_opacity);
    draw_crew_near(batch, crew);
    m_bounds = projected_bounds(batch, l.body, cap);
}

}  // namespace hyprimate
