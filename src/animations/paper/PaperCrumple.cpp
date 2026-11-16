#include <algorithm>
#include <cmath>
#include <utility>

#include "animations/paper/PaperCrumple.hpp"
#include "animations/paper/PaperTakes.hpp"

namespace hyprimate {
    namespace {

        constexpr double MIN_DURATION = 0.1;
        constexpr double MAX_DURATION = 3.0;
        // Longest step actually played. The compositor stalls for a frame or two when a window maps
        // or unmaps (about 0.2 s when a window closes, with or without this plugin); played in
        // full, that skipped a third of a 0.5 s crumple in one frame. Capped, a stall delays the
        // crumple instead of jumping it.
        constexpr double MAX_PLAYED_STEP = 1.0 / 30.0;
        constexpr std::uint64_t BASE_SEED = 0x50A9E5C0FFEEULL;
        constexpr int GRID = PAPER_CELLS + 1;

        // Share of the baked take a crumple plays through at crumpliness 0 and 1: lower stops at a
        // looser, larger ball.
        constexpr double LOOSEST_END = 0.7;

        double clampUnit(double v) noexcept {
            return std::clamp(v, 0.0, 1.0);
        }

        // Validates and clamps params in place. False when any input is nonfinite or the duration
        // is not positive.
        bool sanitize(PaperParams &p) noexcept {
            if (!std::isfinite(p.duration) || !std::isfinite(p.crumpliness) ||
                !std::isfinite(p.shading) || !std::isfinite(p.variation))
                return false;
            if (!(p.duration > 0.0)) return false;
            p.duration = std::clamp(p.duration, MIN_DURATION, MAX_DURATION);
            p.crumpliness = clampUnit(p.crumpliness);
            p.shading = clampUnit(p.shading);
            p.variation = clampUnit(p.variation);
            return true;
        }

        std::size_t gridIndex(int i, int j) noexcept {
            return std::size_t(j) * GRID + std::size_t(i);
        }

    }  // namespace

    void PaperCrumple::begin(
        Transition kind, const PaperParams &params, std::uint64_t seed) noexcept {
        PaperParams p = params;
        if (!sanitize(p)) {
            reset();
            return;
        }
        if (kind == Transition::Open) {
            m_params = p;
            m_rate = 1.0 / p.duration;
            m_phase = Phase::Opening;
            m_s = 1.0;
            rollShape(seed);
            return;
        }
        if (m_phase == Phase::Opening) {
            // Keep the geometry continuous: same s, take and mirroring; only the rate follows the
            // new duration.
            m_params.duration = p.duration;
            m_rate = 1.0 / p.duration;
            m_phase = Phase::Closing;
            return;
        }
        m_params = p;
        m_rate = 1.0 / p.duration;
        m_phase = Phase::Closing;
        m_s = 0.0;
        rollShape(seed);
    }

    bool PaperCrumple::advance(double elapsedSeconds) noexcept {
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 ||
            elapsedSeconds > kStallSeconds) {
            if (m_phase == Phase::Opening)
                reset();
            else if (m_phase == Phase::Closing) {
                m_phase = Phase::Closed;
                m_s = 1.0;
            }
            return active();
        }
        const double step = std::min(elapsedSeconds, MAX_PLAYED_STEP);
        if (m_phase == Phase::Opening) {
            m_s -= m_rate * step;
            if (m_s <= 0.0) reset();
        } else if (m_phase == Phase::Closing) {
            m_s += m_rate * step;
            if (m_s >= 1.0) {
                m_phase = Phase::Closed;
                m_s = 1.0;
            }
        }
        return active();
    }

    void PaperCrumple::reset() noexcept {
        m_phase = Phase::Idle;
        m_s = 0.0;
    }

    bool PaperCrumple::active() const noexcept {
        return m_phase == Phase::Opening || m_phase == Phase::Closing;
    }

    bool PaperCrumple::closed() const noexcept {
        return m_phase == Phase::Closed;
    }

    double PaperCrumple::opacity() const noexcept {
        if (m_phase == Phase::Idle) return 1.0;
        if (m_phase == Phase::Closed) return 0.0;
        return 1.0 - smoothstep(0.86, 1.0, m_s);
    }

    bool PaperCrumple::shaded() const noexcept {
        return m_params.shading > 0.0;
    }

    void PaperCrumple::rollShape(std::uint64_t seed) noexcept {
        // Base stream is fixed; random stream follows the seed. variation blends between them, so
        // variation 0 ignores the seed and every window plays the same take the same way.
        std::uint64_t base = BASE_SEED;
        std::uint64_t random = seed;
        const double variation = m_params.variation;
        auto draw = [&]() {
            const double b = unitDraw(base);
            const double r = unitDraw(random);
            return b + variation * (r - b);
        };
        m_takePick = std::min(draw(), 0x1.fffffffffffffp-1);
        m_mirrorX = draw() < 0.5;
        m_mirrorY = draw() < 0.5;
        m_take = -1;
    }

    void PaperCrumple::chooseTake(double aspect) const noexcept {
        if (m_take >= 0) return;
        // Takes are landscape; a portrait window plays one with its axes swapped.
        m_transpose = aspect < 1.0;
        const double landscape = m_transpose ? 1.0 / aspect : aspect;
        const int takes = paperTakeCount();
        // Nearest baked aspect on a log scale, then the seeded pick among the takes baked at that
        // aspect.
        double nearest = 0.0, bestDistance = INFINITY;
        for (int t = 0; t < takes; ++t) {
            const double distance = std::abs(std::log(paperTakeAspect(t) / landscape));
            if (distance < bestDistance) {
                bestDistance = distance;
                nearest = paperTakeAspect(t);
            }
        }
        int family = 0;
        for (int t = 0; t < takes; ++t) family += paperTakeAspect(t) == nearest;
        int wanted = int(m_takePick * family);
        for (int t = 0; t < takes; ++t) {
            if (paperTakeAspect(t) != nearest) continue;
            if (wanted-- == 0) {
                m_take = t;
                return;
            }
        }
    }

    void PaperCrumple::frame(Vec2 size, SheetFrame &out) const noexcept {
        if (m_phase == Phase::Idle) {
            flatSheet(out);
            return;
        }

        const double W = std::isfinite(size.x) && size.x > 0.0 ? size.x : 1.0;
        const double H = std::isfinite(size.y) && size.y > 0.0 ? size.y : 1.0;
        const double L = std::sqrt(W * H);
        chooseTake(W / H);

        // Playback position in the take: crumpliness sets how far toward the tight ball the crumple
        // goes. Frames are joined by a Catmull-Rom spline, so vertex velocities stay continuous
        // across baked frames; linear blending would kink every vertex's path at every frame, which
        // reads as judder.
        const int frames = paperTakeFrames();
        const double end = LOOSEST_END + (1.0 - LOOSEST_END) * m_params.crumpliness;
        const double at = std::clamp(m_s, 0.0, 1.0) * end * (frames - 1);
        const int f0 = std::min(int(at), frames - 2);
        const int fBack = std::max(f0 - 1, 0);
        const int fNext = std::min(f0 + 2, frames - 1);
        const double u = at - f0;
        const double u2 = u * u, u3 = u2 * u;
        // Catmull-Rom weights for the frames before, at, after and two after the playback position.
        const double w0 = 0.5 * (-u + 2.0 * u2 - u3);
        const double w1 = 0.5 * (2.0 - 5.0 * u2 + 3.0 * u3);
        const double w2 = 0.5 * (u + 4.0 * u2 - 3.0 * u3);
        const double w3 = 0.5 * (-u2 + u3);

        // The take's sheet has its own aspect; stretch it to the window's, keeping area, so rest
        // maps to rest exactly.
        const double windowLandscape = m_transpose ? H / W : W / H;
        const double takeAspect = paperTakeAspect(m_take);
        const double stretchX = std::sqrt(windowLandscape / takeAspect);
        const double stretchY = 1.0 / stretchX;

        // The sheet stays facing the viewer: no rotation beyond what the crumple itself does. It
        // only recedes at the end, as the ball drops away.
        const double shrink = 1.0 - 0.35 * smoothstep(0.86, 1.0, m_s);

        SheetGrid world;  // every vertex is written below
        for (int j = 0; j < GRID; ++j) {
            for (int i = 0; i < GRID; ++i) {
                // Window vertex (i, j) reads take vertex (si, sj): swapped for portrait windows,
                // then mirrored.
                int si = m_transpose ? j : i;
                int sj = m_transpose ? i : j;
                if (m_mirrorX) si = PAPER_CELLS - si;
                if (m_mirrorY) sj = PAPER_CELLS - sj;
                const std::size_t take = gridIndex(si, sj);
                double p0[3], p1[3], p2[3], p3[3];
                paperTakeVertex(m_take, fBack, take, p0);
                paperTakeVertex(m_take, f0, take, p1);
                paperTakeVertex(m_take, f0 + 1, take, p2);
                paperTakeVertex(m_take, fNext, take, p3);
                double p[3];
                for (int k = 0; k < 3; ++k)
                    p[k] = w0 * p0[k] + w1 * p1[k] + w2 * p2[k] + w3 * p3[k];
                double x = p[0] * stretchX * (m_mirrorX ? -1.0 : 1.0);
                double y = p[1] * stretchY * (m_mirrorY ? -1.0 : 1.0);
                if (m_transpose) std::swap(x, y);
                world[gridIndex(i, j)] = {L * shrink * x, L * shrink * y, L * shrink * p[2]};
            }
        }
        projectSheet(world, {W, H}, m_params.shading, SheetLighting::PaperCreases, out);
    }

}  // namespace hyprimate
