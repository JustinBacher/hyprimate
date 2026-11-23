#include "ConstructionCloseSite.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>

#include "ConstructionArt.hpp"

namespace hyprimate::construction {
namespace {
using Regional = studies::RegionalClose;
constexpr double MAX_TRIM = 48; // Matches the opening and motion distinction between trim and scene outsets.
constexpr double AA = 2;
constexpr double RETREAT_FRACTION = .24;  // Clears before the local cue.

Box at_capture(Box b, Box capture) noexcept {
    return {b.x - capture.x, b.y - capture.y, b.w, b.h};
}

double right(Box b) noexcept { return b.x + b.w; }
double bottom(Box b) noexcept { return b.y + b.h; }

bool valid_box(Box b) noexcept {
    return is_usable(b) && right(b) > b.x && bottom(b) > b.y &&
        std::isfinite(right(b)) && std::isfinite(bottom(b));
}

bool same_box(Box a, Box b) noexcept {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

Box intersection(Box a, Box b) noexcept {
    const double x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::min(right(a), right(b)) - x, std::min(bottom(a), bottom(b)) - y};
}

Box united(Box a, Box b) noexcept {
    if (!is_usable(a)) return b;
    const double x = std::min(a.x, b.x), y = std::min(a.y, b.y);
    return {x, y, std::max(right(a), right(b)) - x, std::max(bottom(a), bottom(b)) - y};
}

bool identity(const ConstructionQuad &q) noexcept {
    return q.origin == std::array{q.uv[0], q.uv[1], 0.F} &&
        q.u == std::array{q.uv[2], 0.F, 0.F} && q.v == std::array{0.F, q.uv[3], 0.F};
}

Vec3 unpack(std::array<float, 3> value) noexcept { return {value[0], value[1], value[2]}; }

struct SourceEdges {
    double left, top, right, bottom;
};

double seam_epsilon(double span) noexcept {
    return 4 * std::numeric_limits<float>::epsilon() * std::max(1.0, span);
}

// Adjacent partitions packed as separate float origins and widths can have a
// roundoff-sized gap/overlap. Snap shared cuts once, then adjust pose by the same fractions.
void align_source_axis(std::span<SourceEdges> faces, double SourceEdges::*first,
    double SourceEdges::*last, double limit) noexcept {
    std::array<double *, 2 * Regional::MAX_VISIBLE_FACES> edges;
    std::size_t count = 0;
    for (auto &face : faces) {
        edges[count++] = &(face.*first);
        edges[count++] = &(face.*last);
    }
    const auto ordered = std::span{edges}.first(count);
    std::ranges::sort(ordered, {}, [](const double *edge) { return *edge; });
    for (std::size_t i = 0; i < ordered.size();) {
        const double edge = *ordered[i];
        std::size_t j = i + 1;
        while (j < ordered.size() && *ordered[j] - edge <= seam_epsilon(edge)) ++j;
        const double aligned = edge <= seam_epsilon(0) ? 0 :
            limit - *ordered[j - 1] <= seam_epsilon(limit) ? limit : edge;
        while (i < j) *ordered[i++] = aligned;
    }
}

struct Hull {
    Vec3 lo{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), 0};
    Vec3 hi{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), 0};

    void add(Vec3 p) noexcept {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
};

Box close_envelope(Hull hull, Box local_body, Box cap, double focal, double worker_size) noexcept {
    // The study moves print only down to its toe and inward toward a point on a
    // saved face. Cover that path and the two workers' walks, local flash and dust.
    // An event envelope avoids evaluating the indexed mesh a second time for damage.
    const double span = std::min(cap.w, cap.h);
    const double worker = .075 * std::min(local_body.w, local_body.h) * worker_size * (45.0 / 42);
    // The flagger travels under five heights including the starting setback.
    // A sixth covers the painted edges; a two-height vertical apron also covers the soft accents.
    const double apron = std::max(6 * worker, .12 * span);
    const double vertical_apron = std::max(2 * worker, .10 * span);
    const double floor = std::max(hull.hi.y, cap.h) + .02 * span;
    const double min_y = hull.lo.y - vertical_apron;
    const double max_y = floor + vertical_apron;
    const double z_guard = worker + .5 * (hull.hi.z - hull.lo.z);
    const double min_z = hull.lo.z - std::max(.027 * span, z_guard);
    const double max_z = hull.hi.z + z_guard;
    const double px = local_body.x + local_body.w / 2;
    const double py = local_body.y + local_body.h / 2;
    Hull projected;
    for (const double z : {min_z, max_z}) {
        const double w = 1 - z / focal;
        if (w <= .5) return {};
        for (const double x : {hull.lo.x - apron, hull.hi.x + apron}) {
            for (const double y : {min_y, max_y})
                projected.add({px + (x - px) / w, py + (y - py) / w, 0});
        }
    }
    return {projected.lo.x - AA, projected.lo.y - AA,
        projected.hi.x - projected.lo.x + 2 * AA, projected.hi.y - projected.lo.y + 2 * AA};
}

// Use the same texel anchors as opening; keep a stable cutout width as limbs
// bend between the planted soles, pelvis, shoulder, and evaluated hand contacts.
void link(ConstructionBatch &effects, Part part, Vec3 a, Vec3 b, double facing, double pixel,
    double opacity) noexcept {
    const double dx = part.tip.x - part.pivot.x, dy = part.tip.y - part.pivot.y;
    const double length_squared = dx * dx + dy * dy;
    const Vec3 delta = b - a;
    const Vec3 side = unit({delta.y, -delta.x, 0}, {1, 0, 0}) *
        (std::sqrt(length_squared) * pixel * facing);
    stamp(effects, part, a, (delta * dx + side * dy) * (1 / length_squared),
        (delta * dy - side * dx) * (1 / length_squared), opacity);
}

void worker_art(ConstructionBatch &effects, const Regional::Worker &worker) noexcept {
    constexpr std::array thigh{NEAR_THIGH, FAR_THIGH}, shin{NEAR_SHIN, FAR_SHIN};
    constexpr std::array boot{NEAR_BOOT, FAR_BOOT}, sleeve{NEAR_SLEEVE, FAR_SLEEVE};
    constexpr std::array forearm{NEAR_FOREARM, FAR_FOREARM};
    const double h = worker.height, facing = worker.facing, opacity = worker.opacity;
    const auto joint = [&](Part part, Vec3 a, Vec3 b, double pixel) {
        link(effects, part, a, b, facing, h * pixel, opacity);
    };
    const auto leg = [&](std::size_t i) {
        const double side = i == 0 ? 1 : -1;
        const Vec3 hip = worker.hip + Vec3{side * facing * .045 * h, 0, side * .026 * h};
        const Vec3 ankle = worker.soles[i] - Vec3{0, (boot[i].tip.y - boot[i].pivot.y) * .0043 * h, 0};
        const Vec3 knee = mix(hip, ankle, .51) + Vec3{facing * .085 * h, 0, 0};
        joint(thigh[i], hip, knee, .0049);
        joint(shin[i], knee, ankle, .0044);
        stamp(effects, boot[i], ankle, {facing * .0043 * h, 0, 0}, {0, .0043 * h, 0}, opacity);
    };
    const auto arm = [&](std::size_t i, Vec3 hand) {
        const double side = i == 0 ? 1 : -1;
        const Vec3 shoulder = worker.shoulder + Vec3{side * facing * .044 * h, 0, side * .027 * h};
        const Vec3 elbow = mix(shoulder, hand, .46) + Vec3{-facing * .070 * h, .060 * h, 0};
        const Vec3 wrist = hand - unit(hand - elbow, {facing, 0, 0}) * std::min(.079 * h, length(hand - elbow) / 2);
        joint(sleeve[i], shoulder, elbow, .0045);
        joint(forearm[i], elbow, wrist, .0041);
        joint(GLOVE, wrist, hand, .0038);
    };
    leg(1);
    arm(1, worker.far_hand);
    leg(0);
    joint(TORSO, worker.hip, mix(worker.shoulder, worker.head, .46), .0072);
    const Vec3 chin = worker.head + Vec3{0, .092 * h, 0};
    stamp(effects, HEAD, chin, {facing * .0046 * h, 0, 0}, {0, .0046 * h, 0}, opacity);
    arm(0, worker.near_hand);
}

bool worker_cutout(const ConstructionQuad &q) noexcept {
    if (q.surface != ConstructionSurface::Atlas || q.tint[3] == 0 ||
        q.uv[2] != 56.F / ATLAS_WIDTH || q.uv[3] != 56.F / ATLAS_HEIGHT) return false;
    constexpr std::array parts{HEAD, TORSO, NEAR_SLEEVE, NEAR_FOREARM, FAR_SLEEVE, FAR_FOREARM,
        NEAR_THIGH, NEAR_SHIN, FAR_THIGH, FAR_SHIN, NEAR_BOOT, FAR_BOOT, GLOVE, HAMMER};
    return std::ranges::any_of(parts, [&](Part part) {
        return q.uv[0] == static_cast<float>(part.col * 64 + 4) / ATLAS_WIDTH &&
               q.uv[1] == static_cast<float>(part.row * 64 + 4) / ATLAS_HEIGHT;
    });
}

void glow(ConstructionBatch &effects, Vec3 center, Vec2 radius, Tint opaque, double opacity) noexcept {
    // The shared Shadow surface is an untethered soft radial mask; the tint can
    // be colored and does not sample the snapshot or atlas.
    quad(effects, {center.x - radius.x, center.y - radius.y, center.z},
        {2 * radius.x, 0, 0}, {0, 2 * radius.y, 0}, tint(opaque, opacity), ConstructionSurface::Shadow);
}
}  // namespace

bool CloseSite::start(Box body, Box capture, std::optional<std::span<const ConstructionQuad>> currently_visible,
    std::uint64_t seed, double seconds, double worker_size) noexcept {
    m_close.cancel();
    m_blank = false;
    m_hide_workers = worker_size == 0;
    m_body = m_capture = m_envelope = m_required_snapshot = {};
    m_departing.reset();
    m_departing_count = 0;
    m_departure_seconds = m_departure_elapsed = m_departure_distance = 0;
    const Box local = at_capture(body, capture), cap{0, 0, capture.w, capture.h};
    if (!valid_box(body) || !valid_box(capture) || !valid_box(local) || !std::isfinite(seconds) || seconds <= 0 ||
        local.x < 0 || local.y < 0 || right(local) > cap.w || bottom(local) > cap.h)
        return false;

    if (currently_visible && currently_visible->empty()) {
        m_body = body;
        m_capture = capture;
        m_departure_seconds = RETREAT_FRACTION * seconds;
        m_blank = true;
        return true;
    }
    if (std::max({local.x, local.y, cap.w - right(local), cap.h - bottom(local)}) > MAX_TRIM) return false;
    std::array<Regional::VisibleFace, Regional::MAX_VISIBLE_FACES> faces;
    Box required_snapshot{};
    std::size_t count = 1;
    if (currently_visible) {
        count = currently_visible->size();
        if (count > faces.size()) return false;
        std::array<SourceEdges, Regional::MAX_VISIBLE_FACES> edges;
        const double eps_x = seam_epsilon(cap.w), eps_y = seam_epsilon(cap.h);
        const auto cuts = std::span{edges}.first(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto &q = (*currently_visible)[i];
            const Box source{q.uv[0], q.uv[1], q.uv[2], q.uv[3]};
            if (q.surface != ConstructionSurface::Client || !valid_box(source) ||
                source.x < -seam_epsilon(0) || source.y < -seam_epsilon(0) ||
                right(source) > cap.w + eps_x || bottom(source) > cap.h + eps_y)
                return false;
            cuts[i] = {source.x, source.y, right(source), bottom(source)};
        }
        align_source_axis(cuts, &SourceEdges::left, &SourceEdges::right, cap.w);
        align_source_axis(cuts, &SourceEdges::top, &SourceEdges::bottom, cap.h);
        for (std::size_t i = 0; i < count; ++i) {
            const auto &q = (*currently_visible)[i];
            const auto &cut = cuts[i];
            const Box source{cut.left, cut.top, cut.right - cut.left, cut.bottom - cut.top};
            if (!valid_box(source)) return false;
            const Vec3 u = unpack(q.u), v = unpack(q.v);
            faces[i] = {source,
                unpack(q.origin) + u * ((source.x - q.uv[0]) / q.uv[2]) + v * ((source.y - q.uv[1]) / q.uv[3]),
                u * (source.w / q.uv[2]), v * (source.h / q.uv[3])};
            if (!identity(q)) required_snapshot = united(required_snapshot, source);
        }
    } else {
        // The idle source is the genuine retained capture, not a synthetic body crop.
        faces[0] = Regional::VisibleFace::flat(cap);
    }

    const double focal = 4 * std::max({local.w, local.h, 64.0});
    Hull hull;
    const auto visible = std::span{faces}.first(count);
    for (const auto &face : visible) {
        // The worker reaches an inset along a real panel edge. Unrelated or stretched
        // data isn't a supported previous pose and would defeat its local travel bound.
        if (!(length(face.u) <= 2 * face.source.w && length(face.v) <= 2 * face.source.h)) return false;
        for (const auto p : {face.origin, face.origin + face.u, face.origin + face.v,
                 face.origin + face.u + face.v}) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !(std::abs(p.z) < focal / 8)) return false;
            hull.add(p);
        }
    }
    const auto envelope = close_envelope(hull, local, cap, focal, worker_size);
    // The study's lattice rectangle is also its source domain. Use the real capture
    // domain so normal decoration in a face survives; the projection still uses body.
    if (!valid_box(envelope) || !m_close.start(cap, visible, seed, seconds, worker_size, local)) return false;
    m_body = body;
    m_capture = capture;
    m_envelope = envelope;
    m_required_snapshot = required_snapshot;
    m_departure_seconds = RETREAT_FRACTION * seconds;
    return true;
}

bool CloseSite::can_capture(Box local_snapshot_crop, double scale) const noexcept {
    if ((!m_close.active() && !m_blank) || !valid_box(local_snapshot_crop) ||
        !std::isfinite(scale) || scale <= 0) return false;
    const Box crop = intersection(local_snapshot_crop, {0, 0, m_capture.w, m_capture.h});
    if (!valid_box(crop)) return false;
    // For a flat face, missing source is also off-output. For a posed face, refuse
    // at handoff rather than risk cutting a hole in a previously visible patch.
    const Box need = m_required_snapshot;
    // Reserve the half-texel footprint of a bilinear tap at a monitor cut,
    // not at a true client boundary shared by both render paths.
    const double half = .5 / scale;
    return !is_usable(need) || (need.x >= crop.x + (crop.x > 0 ? half : 0) &&
        need.y >= crop.y + (crop.y > 0 ? half : 0) &&
        right(need) <= right(crop) - (right(crop) < m_capture.w ? half : 0) &&
        bottom(need) <= bottom(crop) - (bottom(crop) < m_capture.h ? half : 0));
}

bool CloseSite::depart(const ConstructionBatch &last_presented) noexcept {
    if (m_hide_workers) return m_blank || m_close.active();
    if ((!m_blank && !m_close.active()) || m_departing || last_presented.overflow ||
        last_presented.count > last_presented.quads.size()) return false;

    const Box local = at_capture(m_body, m_capture);
    const double distance = std::min(18.0, .08 * std::min(local.w, local.h));
    const double focal = 4 * std::max({local.w, local.h, 64.0});
    const double px = local.x + local.w / 2, py = local.y + local.h / 2;
    const auto quads = std::span{last_presented.quads}.first(last_presented.count);
    std::size_t count = 0;
    Hull projected;
    for (const auto &q : quads) {
        if (!worker_cutout(q)) continue;
        ++count;
        const Vec3 origin = unpack(q.origin), u = unpack(q.u), v = unpack(q.v);
        for (const auto p : {origin, origin + u, origin + v, origin + u + v}) {
            if (!(std::abs(p.x) <= 0x1p24 && std::abs(p.y) <= 0x1p24 &&
                    std::abs(p.z) < focal / 8)) return false;
            const double w = 1 - p.z / focal;
            const double x = px + (p.x - px) / w;
            projected.add({x, py + (p.y - py) / w, 0});
            projected.add({x, py + (p.y + distance - py) / w, 0});
        }
    }
    if (count == 0) return true;
    // Leave room for the bounded flash/dust art even if their cue timing changes.
    if (count > last_presented.quads.size() - 12 || m_departure_seconds <= 0) return false;
    const Box retreat{projected.lo.x - AA, projected.lo.y - AA,
        projected.hi.x - projected.lo.x + 2 * AA, projected.hi.y - projected.lo.y + 2 * AA};
    std::unique_ptr<ConstructionQuad[]> saved{new (std::nothrow) ConstructionQuad[count]};
    if (!saved) return false;
    for (const auto &q : quads) {
        if (worker_cutout(q)) saved[m_departing_count++] = q;
    }
    m_departing.swap(saved);
    m_departure_distance = distance;
    m_envelope = m_blank ? retreat : united(m_envelope, retreat);
    return true;
}

animation::Frame CloseSite::advance(const animation::Step &step) noexcept {
    if (!step.dying || (!m_blank && !m_close.active())) return {};
    if (step.stalled || !std::isfinite(step.elapsed) || step.elapsed < 0 || step.elapsed > STALL_SECONDS ||
        !same_box(step.body, m_body) || !same_box(step.capture, m_capture) || !valid_box(step.screen)) {
        m_close.cancel();
        m_blank = false;
        return {.interrupted = true};
    }
    if (m_departing && m_departure_elapsed < m_departure_seconds)
        m_departure_elapsed = std::min(m_departure_elapsed + step.elapsed, m_departure_seconds);
    if (m_blank && (!m_departing || m_departure_elapsed >= m_departure_seconds)) {
        m_blank = false;
        return {};  // Normal completion keeps native suppressed for the already-blank site.
    }
    if (!m_blank) {
        m_close.advance(step.elapsed);
        if (!m_close.active()) return {};
    }
    const auto bounds = intersection(m_envelope, at_capture(step.screen, m_capture));
    if (!valid_box(bounds)) {
        m_close.cancel();
        return {.interrupted = true};
    }
    return {.running = true, .bounds = bounds};
}

bool CloseSite::emit(Box authorized_local_crop, Regional::Frame &frame, ConstructionBatch &effects) const noexcept {
    effects.count = 0;
    effects.overflow = false;
    const Box crop = valid_box(authorized_local_crop) ?
        intersection(authorized_local_crop, {0, 0, m_capture.w, m_capture.h}) : Box{};
    const bool allowed = valid_box(crop) && (m_close.active() || m_blank);
    m_close.emit({.crop = crop, .authorized = allowed}, frame);
    if (!allowed) return false;

    if (!m_departing) {
        for (const auto &worker : frame.crew()) worker_art(effects, worker);
    } else if (m_departure_elapsed < m_departure_seconds && (m_blank || !frame.crew().empty())) {
        const double progress = m_departure_elapsed / m_departure_seconds;
        const auto saved = std::span{m_departing.get(), m_departing_count};
        if (progress == 0) {
            for (const auto &q : saved) effects.add(q);
        } else {
            const double displacement = m_departure_distance * smoothstep(0, 1, progress);
            const double opacity = 1 - smoothstep(.16, 1, progress);
            for (auto q : saved) {
                q.origin[1] = static_cast<float>(q.origin[1] + displacement);
                q.tint = tint(q.tint, opacity);
                if (q.tint[3] != 0) effects.add(q);
            }
        }
    }
    for (const auto &dust : frame.dust()) {
        const Vec3 center{dust.center.x, dust.center.y, 0};
        glow(effects, center, dust.radii, {183, 163, 142, 255}, dust.opacity);
        glow(effects, center + Vec3{dust.radii.x * .45, -dust.radii.y * .32, 0},
            {dust.radii.x * .64, dust.radii.y * .82}, {222, 205, 180, 255}, dust.opacity * .62);
    }
    if (const auto &flash = frame.flash) {
        const auto [center, radius, intensity] = *flash;
        glow(effects, center, {radius, radius}, {255, 161, 60, 255}, intensity * .72);
        glow(effects, center, {radius * .37, radius * .37}, {255, 247, 214, 255}, intensity);
    }
    return true;
}

}  // namespace hyprimate::construction
