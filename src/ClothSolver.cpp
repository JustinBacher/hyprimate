#include "ClothSolver.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace liquid_glass {
namespace {
constexpr int    SUBSTEPS = 4;
constexpr double PIN_RADIUS = 0.07; // the pinch that holds the sheet
constexpr double FACE_DRAG = 4;      // air resists fabric across its face far more than along it
constexpr double MAX_STRETCH = 1.02;
// Grid offsets of the three constraint families, twice each: weave, shear and bending (skipping one particle).
// Compliance is XPBD's inverse stiffness. Fabric barely stretches, shears a little and bends freely; what resists
// compression along the weave is what makes a slack sheet buckle into folds instead of shrinking.
enum Kind { WEAVE_X, WEAVE_Y, SHEAR_A, SHEAR_B, BEND_X, BEND_Y, KINDS };
constexpr std::array<std::array<int, 2>, KINDS> OFFSET{{{1, 0}, {0, 1}, {1, 1}, {-1, 1}, {2, 0}, {0, 2}}};
constexpr std::array<double, KINDS>             COMPLIANCE{2e-8, 2e-8, 4e-6, 4e-6, 1.5e-5, 1.5e-5};

Vec3   add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3   sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3   mul(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(Vec3 a) { return std::sqrt(dot(a, a)); }

// A smooth, divergence-careless stand-in for turbulence: three octaves per component, unit-order amplitude.
Vec3 gust(Vec3 q, double phase) {
    const auto wave = [&](double kx, double ky, double kz, double rate, double shift) {
        return std::sin(kx * q.x + ky * q.y + kz * q.z + rate * phase + shift);
    };
    return {.5 * wave(3.1, 4.3, 2.0, 1.0, 0.0) + .3 * wave(-7.9, 5.3, 3.0, 1.7, 1.3) + .2 * wave(12.7, -14.9, 5.0, 2.9, 4.1),
            .5 * wave(4.7, -2.9, 2.0, 1.3, 2.2) + .3 * wave(6.1, 8.3, -3.0, 2.1, 5.0) + .2 * wave(-15.1, 11.3, 5.0, 3.1, .7),
            .5 * wave(3.7, 3.3, 2.0, 1.1, 3.9) + .3 * wave(8.9, -6.7, 3.0, 1.9, .4) + .2 * wave(13.3, 16.1, -5.0, 2.7, 2.8)};
}
} // namespace

ClothSolver::ClothSolver(int cells, Vec2 size, Vec2 grabUV)
    : m_cells(cells), m_size(size), m_particles((cells + 1) * (cells + 1)), m_previous(m_particles.size()),
      m_normals(m_particles.size()), m_air(m_particles.size()), m_tether(m_particles.size()) {
    const int  n = cells + 1;
    const auto distance = [&](int x, int y) {
        return std::hypot((double(x) / cells - grabUV.x) * size.x, (double(y) / cells - grabUV.y) * size.y);
    };
    const int grabX = std::clamp(int(std::lround(grabUV.x * cells)), 0, cells);
    const int grabY = std::clamp(int(std::lround(grabUV.y * cells)), 0, cells);
    for (int y = 0; y <= cells; ++y)
        for (int x = 0; x <= cells; ++x) {
            auto &p = particle(x, y);
            p.position = p.rest = {(double(x) / cells - .5) * size.x, -double(y) / cells * size.y, 0};
            p.pinned = (x == grabX && y == grabY) || distance(x, y) <= PIN_RADIUS;
        }
    for (int y = 0; y <= cells; ++y)
        for (int x = 0; x <= cells; ++x)
            for (int kind = 0; kind < KINDS; ++kind) {
                const int bx = x + OFFSET[kind][0], by = y + OFFSET[kind][1];
                if (bx < 0 || bx > cells || by > cells || (particle(x, y).pinned && particle(bx, by).pinned))
                    continue;
                m_edges.push_back({y * n + x, by * n + bx, kind});
                if (kind <= WEAVE_Y)
                    m_weave.push_back(m_edges.back());
            }
    // The whole sheet hangs off the pinch, so the stretch limit is swept outwards from it and back, and every
    // particle is tethered to the held one it lies nearest.
    const auto fromGrab = [&](const Edge &e) {
        return std::min(distance(e.a % n, e.a / n), distance(e.b % n, e.b / n));
    };
    std::stable_sort(m_weave.begin(), m_weave.end(),
                     [&](const Edge &a, const Edge &b) { return fromGrab(a) < fromGrab(b); });
    std::vector<int> held;
    for (int k = 0; k < n * n; ++k)
        if (m_particles[k].pinned)
            held.push_back(k);
    for (int k = 0; k < n * n; ++k)
        m_tether[k] = *std::min_element(held.begin(), held.end(), [&](int a, int b) {
            return length(sub(m_particles[a].rest, m_particles[k].rest)) <
                   length(sub(m_particles[b].rest, m_particles[k].rest));
        });
}

void ClothSolver::resize(Vec2 size) {
    const double sx = size.x / m_size.x, sy = size.y / m_size.y;
    m_size = size;
    for (auto &p : m_particles) {
        p.position = {p.position.x * sx, p.position.y * sy, p.position.z};
        p.velocity = {p.velocity.x * sx, p.velocity.y * sy, p.velocity.z};
        p.rest = {p.rest.x * sx, p.rest.y * sy, 0};
    }
}

void ClothSolver::step(double dt, const Forces &f) {
    const int    n = m_cells + 1;
    const double h = dt / SUBSTEPS;
    const double windSpeed = length(f.wind);
    const double along = 1 - std::exp(-f.drag * h), across = 1 - std::exp(-f.drag * FACE_DRAG * h);
    const double keep = std::exp(-f.damping * h), ease = std::exp(-f.hold * h);
    std::array<double, KINDS> rest{}, alpha{};
    for (int kind = 0; kind < KINDS; ++kind) {
        rest[kind] = std::hypot(OFFSET[kind][0] * m_size.x, OFFSET[kind][1] * m_size.y) / m_cells;
        alpha[kind] = COMPLIANCE[kind] / (h * h);
    }
    // The air a particle meets and the way it faces change little within one step.
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const Vec3 dx = sub(particle(std::min(m_cells, x + 1), y).position, particle(std::max(0, x - 1), y).position);
            const Vec3 dy = sub(particle(x, std::max(0, y - 1)).position, particle(x, std::min(m_cells, y + 1)).position);
            const Vec3 normal{dx.y * dy.z - dx.z * dy.y, dx.z * dy.x - dx.x * dy.z, dx.x * dy.y - dx.y * dy.x};
            const double size = length(normal);
            const int    k = y * n + x;
            m_normals[k] = size > 1e-12 ? mul(normal, 1 / size) : Vec3{0, 0, 1};
            m_air[k] = f.wind;
            if (f.gust > 0 && windSpeed > 0)
                m_air[k] = add(f.wind, mul(gust(add(m_particles[k].position, f.airOffset), f.phase), f.gust * windSpeed));
        }
    for (int s = 0; s < SUBSTEPS; ++s) {
        for (std::size_t k = 0; k < m_particles.size(); ++k) {
            auto &p = m_particles[k];
            m_previous[k] = p.position;
            if (p.pinned) {
                p.velocity = {};
                continue;
            }
            const Vec3 relative = sub(m_air[k], p.velocity);
            const Vec3 facing = mul(m_normals[k], dot(relative, m_normals[k]));
            Vec3       v = add(p.velocity, add(mul(sub(relative, facing), along), mul(facing, across)));
            v = add(v, mul(f.acceleration, h));
            p.velocity = mul(v, keep);
            p.position = add(p.position, mul(p.velocity, h));
        }
        const auto project = [&](const Edge &e, double restLength, double softness, bool stretchOnly) {
            auto        &a = m_particles[e.a];
            auto        &b = m_particles[e.b];
            const Vec3   delta = sub(b.position, a.position);
            const double size = length(delta);
            const double wa = a.pinned ? 0 : 1, wb = b.pinned ? 0 : 1;
            if (size < 1e-12 || (stretchOnly && size <= restLength))
                return;
            const Vec3 d = mul(delta, (size - restLength) / (size * (wa + wb + softness)));
            a.position = add(a.position, mul(d, wa));
            b.position = sub(b.position, mul(d, wb));
        };
        for (const auto &e : m_edges)
            project(e, rest[e.kind], alpha[e.kind], false);
        // Whatever the load, the weave does not stretch. Relaxing edge against edge converges far too slowly where
        // the whole sheet's load meets the pinch, so first nothing may sit further from the held cloth than the cloth
        // between them is long.
        for (std::size_t k = 0; k < m_particles.size(); ++k) {
            auto &p = m_particles[k];
            if (p.pinned)
                continue;
            const auto  &holder = m_particles[m_tether[k]];
            const double reach = length(sub(p.rest, holder.rest)) * MAX_STRETCH;
            const Vec3   delta = sub(p.position, holder.position);
            const double size = length(delta);
            if (size > reach)
                p.position = add(holder.position, mul(delta, reach / size));
        }
        for (int sweep = 0; sweep < 2; ++sweep) {
            for (auto e = m_weave.rbegin(); e != m_weave.rend(); ++e)
                project(*e, rest[e->kind] * MAX_STRETCH, 0, true);
            for (const auto &e : m_weave)
                project(e, rest[e.kind] * MAX_STRETCH, 0, true);
        }
        // Drawn flat by hand rather than by a spring: it closes a share of the remaining distance and cannot overshoot,
        // and it has the last word over folds that have crossed through themselves.
        for (std::size_t k = 0; k < m_particles.size(); ++k)
            if (auto &p = m_particles[k]; !p.pinned) {
                p.velocity = mul(sub(p.position, m_previous[k]), 1 / h); // the easing itself lends no momentum
                p.position = add(p.rest, mul(sub(p.position, p.rest), ease));
            }
    }
}
} // namespace liquid_glass
