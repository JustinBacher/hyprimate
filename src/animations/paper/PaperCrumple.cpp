#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "animations/paper/PaperCrumple.hpp"
#include "animations/paper/PaperTakes.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/SheetFrame.hpp"

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
                !std::isfinite(p.shading) || !std::isfinite(p.variation)) {
                return false;
            }
            if (!(p.duration > 0.0)) {
                return false;
            }
            p.duration = std::clamp(p.duration, MIN_DURATION, MAX_DURATION);
            p.crumpliness = clampUnit(p.crumpliness);
            p.shading = clampUnit(p.shading);
            p.variation = clampUnit(p.variation);
            return true;
        }

        std::size_t gridIndex(int i, int j) noexcept {
            return (static_cast<std::size_t>(j) * GRID) + static_cast<std::size_t>(i);
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
            elapsedSeconds > STALL_SECONDS) {
            if (m_phase == Phase::Opening) {
                reset();
            } else if (m_phase == Phase::Closing) {
                m_phase = Phase::Closed;
                m_s = 1.0;
            }
            return active();
        }
        const double STEP = std::min(elapsedSeconds, MAX_PLAYED_STEP);
        if (m_phase == Phase::Opening) {
            m_s -= m_rate * STEP;
            if (m_s <= 0.0) {
                reset();
            }
        } else if (m_phase == Phase::Closing) {
            m_s += m_rate * STEP;
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
        if (m_phase == Phase::Idle) {
            return 1.0;
        }
        if (m_phase == Phase::Closed) {
            return 0.0;
        }
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
        const double VARIATION = m_params.variation;
        const auto DRAW = [&] {
            const double b = unit_draw(base);
            const double r = unit_draw(random);
            return b + (VARIATION * (r - b));
        };
        m_takePick = std::min(DRAW(), 0x1.fffffffffffffp-1);
        m_mirrorX = DRAW() < 0.5;
        m_mirrorY = DRAW() < 0.5;
        m_take = -1;
    }

    void PaperCrumple::chooseTake(double aspect) const noexcept {
        if (m_take >= 0) {
            return;
        }
        // Takes are landscape; a portrait window plays one with its axes swapped.
        m_transpose = aspect < 1.0;
        const double LANDSCAPE = m_transpose ? 1.0 / aspect : aspect;
        const int TAKES = paperTakeCount();
        // Nearest baked aspect on a log scale, then the seeded pick among the takes baked at that
        // aspect.
        double nearest = 0.0;
        double bestDistance = INFINITY;
        for (int t = 0; t < TAKES; ++t) {
            const double DISTANCE = std::abs(std::log(paperTakeAspect(t) / LANDSCAPE));
            if (DISTANCE < bestDistance) {
                bestDistance = DISTANCE;
                nearest = paperTakeAspect(t);
            }
        }
        int family = 0;
        for (int t = 0; t < TAKES; ++t) {
            family += static_cast<int>(paperTakeAspect(t) == nearest);
        }
        int wanted = static_cast<int>(m_takePick * family);
        for (int t = 0; t < TAKES; ++t) {
            if (paperTakeAspect(t) != nearest) {
                continue;
            }
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
        const int FRAMES = paperTakeFrames();
        const double END = LOOSEST_END + ((1.0 - LOOSEST_END) * m_params.crumpliness);
        const double at = std::clamp(m_s, 0.0, 1.0) * END * (FRAMES - 1);
        const int f0 = std::min(static_cast<int>(at), FRAMES - 2);
        const int F_BACK = std::max(f0 - 1, 0);
        const int F_NEXT = std::min(f0 + 2, FRAMES - 1);
        const double u = at - f0;
        const double u2 = u * u;
        const double u3 = u2 * u;
        // Catmull-Rom weights for the frames before, at, after and two after the playback position.
        const double w0 = 0.5 * (-u + (2.0 * u2) - u3);
        const double w1 = 0.5 * (2.0 - (5.0 * u2) + (3.0 * u3));
        const double w2 = 0.5 * (u + (4.0 * u2) - (3.0 * u3));
        const double w3 = 0.5 * (-u2 + u3);

        // The take's sheet has its own aspect; stretch it to the window's, keeping area, so rest
        // maps to rest exactly.
        const double WINDOW_LANDSCAPE = m_transpose ? H / W : W / H;
        const double TAKE_ASPECT = paperTakeAspect(m_take);
        const double STRETCH_X = std::sqrt(WINDOW_LANDSCAPE / TAKE_ASPECT);
        const double STRETCH_Y = 1.0 / STRETCH_X;

        // The sheet stays facing the viewer: no rotation beyond what the crumple itself does. It
        // only recedes at the end, as the ball drops away.
        const double SHRINK = 1.0 - (0.35 * smoothstep(0.86, 1.0, m_s));

        SheetGrid world;  // every vertex is written below
        for (int j = 0; j < GRID; ++j) {
            for (int i = 0; i < GRID; ++i) {
                // Window vertex (i, j) reads take vertex (si, sj): swapped for portrait windows,
                // then mirrored.
                int si = m_transpose ? j : i;
                int sj = m_transpose ? i : j;
                if (m_mirrorX) {
                    si = PAPER_CELLS - si;
                }
                if (m_mirrorY) {
                    sj = PAPER_CELLS - sj;
                }
                const std::size_t TAKE = gridIndex(si, sj);
                double p0[3];
                double p1[3];
                double p2[3];
                double p3[3];
                paperTakeVertex(m_take, F_BACK, TAKE, p0);
                paperTakeVertex(m_take, f0, TAKE, p1);
                paperTakeVertex(m_take, f0 + 1, TAKE, p2);
                paperTakeVertex(m_take, F_NEXT, TAKE, p3);
                double p[3];
                for (int k = 0; k < 3; ++k) {
                    p[k] = (w0 * p0[k]) + (w1 * p1[k]) + (w2 * p2[k]) + (w3 * p3[k]);
                }
                double x = p[0] * STRETCH_X * (m_mirrorX ? -1.0 : 1.0);
                double y = p[1] * STRETCH_Y * (m_mirrorY ? -1.0 : 1.0);
                if (m_transpose) {
                    std::swap(x, y);
                }
                world[gridIndex(i, j)] = {
                    .x = L * SHRINK * x, .y = L * SHRINK * y, .z = L * SHRINK * p[2]};
            }
        }
        projectSheet(world, {.x = W, .y = H}, m_params.shading, SheetLighting::PaperCreases, out);
    }

}  // namespace hyprimate
