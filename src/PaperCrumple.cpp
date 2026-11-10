#include "PaperCrumple.hpp"

#include "PaperTakes.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace liquid_glass {
namespace {

constexpr double         MIN_DURATION  = 0.1;
constexpr double         MAX_DURATION  = 3.0;
constexpr double         MAX_STEP      = 0.25; // seconds; larger or invalid steps abort the animation
// Longest step actually played. The compositor stalls for a frame or two when a window maps or unmaps (about 0.2 s
// when a window closes, with or without this plugin); played in full, that skipped a third of a 0.5 s crumple in
// one frame. Capped, a stall delays the crumple instead of jumping it.
constexpr double         MAX_PLAYED_STEP = 1.0 / 30.0;
constexpr std::uint64_t  BASE_SEED     = 0x50A9E5C0FFEEULL;
constexpr int            GRID          = PAPER_CELLS + 1;
constexpr std::size_t    GRID_VERTICES = PAPER_GRID_VERTICES;

// Share of the baked take a crumple plays through at crumpliness 0 and 1: lower stops at a looser, larger ball.
constexpr double LOOSEST_END = 0.7;

// Light direction (towards the light), normalised below.
constexpr double LIGHT_X = -0.35, LIGHT_Y = -0.55, LIGHT_Z = 0.76;

// Neighbouring triangles whose normals differ by more than about 35 degrees are across a crease: they do not share
// a smoothed normal, so the crease stays a sharp line of light and shadow.
constexpr double CREASE_COS = 0.82;

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, double s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double clampUnit(double v) noexcept { return std::clamp(v, 0.0, 1.0); }

double smoothstep(double a, double b, double x) noexcept {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Uniform in [0, 1) from a splitmix64 stream.
double unit(std::uint64_t& state) noexcept {
    return static_cast<double>(splitmix64(state) >> 11) * 0x1.0p-53;
}

// Validates and clamps params in place. False when any input is nonfinite or the duration is not positive.
bool sanitize(PaperParams& p) noexcept {
    if (!std::isfinite(p.duration) || !std::isfinite(p.crumpliness) || !std::isfinite(p.shading) ||
        !std::isfinite(p.variation))
        return false;
    if (!(p.duration > 0.0))
        return false;
    p.duration    = std::clamp(p.duration, MIN_DURATION, MAX_DURATION);
    p.crumpliness = clampUnit(p.crumpliness);
    p.shading     = clampUnit(p.shading);
    p.variation   = clampUnit(p.variation);
    return true;
}

// Position of grid vertex `index` in the (PAPER_CELLS+1)^2 lattice, as (u, v) in [0, 1].
Vec2 gridUv(std::size_t index) noexcept {
    const int i = static_cast<int>(index % GRID);
    const int j = static_cast<int>(index / GRID);
    return {static_cast<double>(i) / PAPER_CELLS, static_cast<double>(j) / PAPER_CELLS};
}

std::size_t gridIndex(int i, int j) noexcept { return std::size_t(j) * GRID + std::size_t(i); }

// Grid corners of triangle t: two per cell, same winding as the idle sheet and the baked takes.
std::array<std::size_t, 3> triangleCorners(std::size_t t) noexcept {
    const std::size_t cell   = t / 2;
    const int         i      = int(cell % PAPER_CELLS);
    const int         j      = int(cell / PAPER_CELLS);
    const bool        second = (t & 1U) != 0;
    return second ? std::array<std::size_t, 3>{gridIndex(i + 1, j), gridIndex(i, j + 1), gridIndex(i + 1, j + 1)}
                  : std::array<std::size_t, 3>{gridIndex(i, j), gridIndex(i, j + 1), gridIndex(i + 1, j)};
}

// Triangles touching grid vertex (i, j), at most six; returns how many were written.
int vertexTriangles(int i, int j, std::array<std::size_t, 6>& out) noexcept {
    int        count = 0;
    const auto add   = [&](int ci, int cj, unsigned half) {
        if (ci >= 0 && cj >= 0 && ci < PAPER_CELLS && cj < PAPER_CELLS)
            out[count++] = (std::size_t(cj) * PAPER_CELLS + std::size_t(ci)) * 2 + half;
    };
    add(i, j, 0);         // as the first triangle's (i, j) corner
    add(i - 1, j, 0);     // as (i + 1, j) of the cell to the left, in both of its triangles
    add(i - 1, j, 1);
    add(i, j - 1, 0);     // as (i, j + 1) of the cell above, in both of its triangles
    add(i, j - 1, 1);
    add(i - 1, j - 1, 1); // as (i + 1, j + 1) of the cell up and to the left
    return count;
}

} // namespace

void PaperCrumple::begin(Transition kind, const PaperParams& params, std::uint64_t seed) noexcept {
    PaperParams p = params;
    if (!sanitize(p)) {
        reset();
        return;
    }
    if (kind == Transition::Open) {
        m_params = p;
        m_rate   = 1.0 / p.duration;
        m_phase  = Phase::Opening;
        m_s      = 1.0;
        rollShape(seed);
        return;
    }
    if (m_phase == Phase::Opening) {
        // Keep the geometry continuous: same s, take and mirroring; only the rate follows the new duration.
        m_params.duration = p.duration;
        m_rate            = 1.0 / p.duration;
        m_phase           = Phase::Closing;
        return;
    }
    m_params = p;
    m_rate   = 1.0 / p.duration;
    m_phase  = Phase::Closing;
    m_s      = 0.0;
    rollShape(seed);
}

bool PaperCrumple::advance(double elapsedSeconds) noexcept {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 || elapsedSeconds > MAX_STEP) {
        if (m_phase == Phase::Opening)
            reset();
        else if (m_phase == Phase::Closing) {
            m_phase = Phase::Closed;
            m_s     = 1.0;
        }
        return active();
    }
    const double step = std::min(elapsedSeconds, MAX_PLAYED_STEP);
    if (m_phase == Phase::Opening) {
        m_s -= m_rate * step;
        if (m_s <= 0.0)
            reset();
    } else if (m_phase == Phase::Closing) {
        m_s += m_rate * step;
        if (m_s >= 1.0) {
            m_phase = Phase::Closed;
            m_s     = 1.0;
        }
    }
    return active();
}

void PaperCrumple::reset() noexcept {
    m_phase = Phase::Idle;
    m_s     = 0.0;
}

bool PaperCrumple::active() const noexcept { return m_phase == Phase::Opening || m_phase == Phase::Closing; }

bool PaperCrumple::closed() const noexcept { return m_phase == Phase::Closed; }

double PaperCrumple::opacity() const noexcept {
    if (m_phase == Phase::Idle)
        return 1.0;
    if (m_phase == Phase::Closed)
        return 0.0;
    return 1.0 - smoothstep(0.86, 1.0, m_s);
}

bool PaperCrumple::shaded() const noexcept { return m_params.shading > 0.0; }

void PaperCrumple::rollShape(std::uint64_t seed) noexcept {
    // Base stream is fixed; random stream follows the seed. variation blends between them, so variation 0
    // ignores the seed and every window plays the same take the same way.
    std::uint64_t base      = BASE_SEED;
    std::uint64_t random    = seed;
    const double  variation = m_params.variation;
    auto          draw      = [&]() {
        const double b = unit(base);
        const double r = unit(random);
        return b + variation * (r - b);
    };
    m_takePick = std::min(draw(), 0x1.fffffffffffffp-1);
    m_mirrorX  = draw() < 0.5;
    m_mirrorY  = draw() < 0.5;
    m_take     = -1;
}

void PaperCrumple::chooseTake(double aspect) const noexcept {
    if (m_take >= 0)
        return;
    // Takes are landscape; a portrait window plays one with its axes swapped.
    m_transpose            = aspect < 1.0;
    const double landscape = m_transpose ? 1.0 / aspect : aspect;
    const int    takes     = paperTakeCount();
    // Nearest baked aspect on a log scale, then the seeded pick among the takes baked at that aspect.
    double nearest = 0.0, bestDistance = INFINITY;
    for (int t = 0; t < takes; ++t) {
        const double distance = std::abs(std::log(paperTakeAspect(t) / landscape));
        if (distance < bestDistance) {
            bestDistance = distance;
            nearest      = paperTakeAspect(t);
        }
    }
    int family = 0;
    for (int t = 0; t < takes; ++t)
        family += paperTakeAspect(t) == nearest;
    int wanted = int(m_takePick * family);
    for (int t = 0; t < takes; ++t) {
        if (paperTakeAspect(t) != nearest)
            continue;
        if (wanted-- == 0) {
            m_take = t;
            return;
        }
    }
}

void PaperCrumple::facets(Vec2 size, PaperFacets& out) const noexcept {
    if (m_phase == Phase::Idle) {
        for (std::size_t t = 0; t < PAPER_TRIANGLES; ++t) {
            const auto  corners = triangleCorners(t);
            PaperFacet& facet   = out[t];
            for (std::size_t c = 0; c < 3; ++c) {
                const Vec2 uv     = gridUv(corners[c]);
                facet.uv[c]       = uv;
                facet.position[c] = {uv.x - 0.5, uv.y - 0.5};
            }
            facet.shade = {0.0, 0.0, 0.0};
        }
        return;
    }

    const double W = std::isfinite(size.x) && size.x > 0.0 ? size.x : 1.0;
    const double H = std::isfinite(size.y) && size.y > 0.0 ? size.y : 1.0;
    const double L = std::sqrt(W * H);
    chooseTake(W / H);

    // Playback position in the take: crumpliness sets how far toward the tight ball the crumple goes. Frames are
    // joined by a Catmull-Rom spline, so vertex velocities stay continuous across baked frames; linear blending would
    // kink every vertex's path at every frame, which reads as judder.
    const int    frames = paperTakeFrames();
    const double end    = LOOSEST_END + (1.0 - LOOSEST_END) * m_params.crumpliness;
    const double at     = std::clamp(m_s, 0.0, 1.0) * end * (frames - 1);
    const int    f0     = std::min(int(at), frames - 2);
    const int    fBack  = std::max(f0 - 1, 0);
    const int    fNext  = std::min(f0 + 2, frames - 1);
    const double u      = at - f0;
    const double u2 = u * u, u3 = u2 * u;
    // Catmull-Rom weights for the frames before, at, after and two after the playback position.
    const double w0 = 0.5 * (-u + 2.0 * u2 - u3);
    const double w1 = 0.5 * (2.0 - 5.0 * u2 + 3.0 * u3);
    const double w2 = 0.5 * (u + 4.0 * u2 - 3.0 * u3);
    const double w3 = 0.5 * (-u2 + u3);

    // The take's sheet has its own aspect; stretch it to the window's, keeping area, so rest maps to rest exactly.
    const double windowLandscape = m_transpose ? H / W : W / H;
    const double takeAspect      = paperTakeAspect(m_take);
    const double stretchX        = std::sqrt(windowLandscape / takeAspect);
    const double stretchY        = 1.0 / stretchX;

    // The sheet stays facing the viewer: no rotation beyond what the crumple itself does. It only recedes at the
    // end, as the ball drops away.
    const double shrink = 1.0 - 0.35 * smoothstep(0.86, 1.0, m_s);
    const double f      = 3.0 * L;

    std::array<Vec3, GRID_VERTICES> world{};
    std::array<Vec2, GRID_VERTICES> screen{};
    for (int j = 0; j < GRID; ++j) {
        for (int i = 0; i < GRID; ++i) {
            // Window vertex (i, j) reads take vertex (si, sj): swapped for portrait windows, then mirrored.
            int si = m_transpose ? j : i;
            int sj = m_transpose ? i : j;
            if (m_mirrorX)
                si = PAPER_CELLS - si;
            if (m_mirrorY)
                sj = PAPER_CELLS - sj;
            const std::size_t take = gridIndex(si, sj);
            double            p0[3], p1[3], p2[3], p3[3];
            paperTakeVertex(m_take, fBack, take, p0);
            paperTakeVertex(m_take, f0, take, p1);
            paperTakeVertex(m_take, f0 + 1, take, p2);
            paperTakeVertex(m_take, fNext, take, p3);
            double p[3];
            for (int k = 0; k < 3; ++k)
                p[k] = w0 * p0[k] + w1 * p1[k] + w2 * p2[k] + w3 * p3[k];
            double x = p[0] * stretchX * (m_mirrorX ? -1.0 : 1.0);
            double y = p[1] * stretchY * (m_mirrorY ? -1.0 : 1.0);
            if (m_transpose)
                std::swap(x, y);
            const double z = p[2];
            const Vec3   X{L * shrink * x, L * shrink * y, L * shrink * z};

            const std::size_t vi = gridIndex(i, j);
            world[vi]            = X;
            const double denom   = std::max(0.25 * f, f - X.z);
            screen[vi]           = {X.x * f / denom / W, X.y * f / denom / H};
        }
    }

    // Back-to-front order by mean world z; the triangle index breaks ties. Scratch is fixed-size.
    std::array<std::pair<double, std::uint16_t>, PAPER_TRIANGLES> order{};
    for (std::size_t t = 0; t < PAPER_TRIANGLES; ++t) {
        const auto   corners = triangleCorners(t);
        const double meanZ   = (world[corners[0]].z + world[corners[1]].z + world[corners[2]].z) / 3.0;
        order[t]             = {meanZ, static_cast<std::uint16_t>(t)};
    }
    std::sort(order.begin(), order.end());

    // Lighting: two-sided, fixed light direction, shaded per corner. Each corner averages the normals of the triangles
    // around its vertex that bend less than CREASE_COS away from its own triangle (area-weighted), so light runs
    // smoothly over the paper between folds and still breaks sharply along a real crease. Per-triangle shading
    // glittered: on the crumpled ball the triangles are a pixel or two wide and each flipped between light and dark
    // from frame to frame; fully smooth normals blurred the creases into pillows.
    const double lx = LIGHT_X, ly = LIGHT_Y, lz = LIGHT_Z;
    const double lLen = std::sqrt(lx * lx + ly * ly + lz * lz);
    const Vec3   Ld{lx / lLen, ly / lLen, lz / lLen};
    const bool   lit = shaded();

    // Area-weighted (unnormalised) and unit normal of every triangle.
    std::array<Vec3, PAPER_TRIANGLES> faceNormal{};
    std::array<Vec3, PAPER_TRIANGLES> faceUnit{};
    if (lit) {
        for (std::size_t tri = 0; tri < PAPER_TRIANGLES; ++tri) {
            const auto   c   = triangleCorners(tri);
            const Vec3   n   = cross(world[c[1]] - world[c[0]], world[c[2]] - world[c[0]]);
            const double len = std::sqrt(dot(n, n));
            faceNormal[tri]  = n;
            faceUnit[tri]    = len >= 1e-12 ? n * (1.0 / len) : Vec3{};
        }
    }
    // Normal at `vertex` as seen by triangle `tri`.
    const auto cornerNormal = [&](std::size_t tri, std::size_t vertex) {
        std::array<std::size_t, 6> around{};
        const int count = vertexTriangles(int(vertex % GRID), int(vertex / GRID), around);
        Vec3      sum{};
        for (int k = 0; k < count; ++k) {
            if (dot(faceUnit[around[k]], faceUnit[tri]) < CREASE_COS)
                continue;
            const Vec3& n = faceNormal[around[k]];
            sum           = {sum.x + n.x, sum.y + n.y, sum.z + n.z};
        }
        return sum;
    };
    // Shade for a normal seen from the side `facing` (+1 front, -1 back) of the sheet.
    const auto shadeOf = [&](Vec3 n, double facing) {
        const double len = std::sqrt(dot(n, n));
        if (len < 1e-12)
            return 0.0;
        n                 = n * (facing / len);
        const double diff = dot(n, Ld) - Ld.z;
        const double s    = diff >= 0.0 ? diff / (1.0 - Ld.z) : diff / (1.0 + Ld.z);
        return std::clamp(s * 1.8 * m_params.shading, -1.0, 1.0);
    };

    for (std::size_t slot = 0; slot < PAPER_TRIANGLES; ++slot) {
        const auto  corners = triangleCorners(order[slot].second);
        PaperFacet& facet   = out[slot];
        for (std::size_t n = 0; n < 3; ++n) {
            facet.position[n] = screen[corners[n]];
            facet.uv[n]       = gridUv(corners[n]);
        }
        facet.shade = {0.0, 0.0, 0.0};
        if (!lit)
            continue;
        // The side of the sheet this triangle shows decides which way its vertex normals point.
        const double facing = faceNormal[order[slot].second].z < 0.0 ? -1.0 : 1.0;
        for (std::size_t n = 0; n < 3; ++n)
            facet.shade[n] = shadeOf(cornerNormal(order[slot].second, corners[n]), facing);
    }
}

} // namespace liquid_glass
