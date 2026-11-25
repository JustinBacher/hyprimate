#include "ConstructionCloseSite.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>

#include "ConstructionClosePrivate.hpp"

namespace hyprimate::construction {
namespace {
using namespace close_detail;
constexpr double MAX_TRIM = 48; // Matches the opening and motion distinction between trim and scene outsets.
constexpr double AA = 2;
constexpr double RETREAT_FRACTION = .24;  // Clears before the local cue.

Box at_capture(Box b, Box capture) noexcept {
    return {b.x - capture.x, b.y - capture.y, b.w, b.h};
}

bool same_box(Box a, Box b) noexcept {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

Box intersection(Box a, Box b) noexcept {
    const double x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::min(right(a), right(b)) - x, std::min(bottom(a), bottom(b)) - y};
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
    if (!import_close_faces(currently_visible, cap, faces, count, required_snapshot)) return false;

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

    emit_close_effects(frame, effects, bool(m_departing),
        {m_departing.get(), m_departing_count}, m_departure_elapsed, m_departure_seconds,
        m_departure_distance, m_blank);
    return true;
}

}  // namespace hyprimate::construction
