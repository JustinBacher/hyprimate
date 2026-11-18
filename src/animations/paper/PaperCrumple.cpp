#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mdspan>
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

        double clamp_unit(double value) noexcept {
            return std::clamp(value, 0.0, 1.0);
        }

        // Validates and clamps params in place. False when any input is nonfinite or the duration
        // is not positive.
        bool sanitize(PaperParams &params) noexcept {
            if (!std::isfinite(params.duration) || !std::isfinite(params.crumpliness) ||
                !std::isfinite(params.shading) || !std::isfinite(params.variation)) {
                return false;
            }
            if (!(params.duration > 0.0)) {
                return false;
            }
            params.duration = std::clamp(params.duration, MIN_DURATION, MAX_DURATION);
            params.crumpliness = clamp_unit(params.crumpliness);
            params.shading = clamp_unit(params.shading);
            params.variation = clamp_unit(params.variation);
            return true;
        }

        std::size_t grid_index(int col, int row) noexcept {
            return (static_cast<std::size_t>(row) * GRID) + static_cast<std::size_t>(col);
        }

    }  // namespace

    void PaperCrumple::begin(
        Transition kind, const PaperParams &params, std::uint64_t seed) noexcept {
        PaperParams paper_params = params;
        if (!sanitize(paper_params)) {
            reset();
            return;
        }
        if (kind == Transition::Open) {
            m_params = paper_params;
            m_rate = 1.0 / paper_params.duration;
            m_phase = Phase::Opening;
            m_s = 1.0;
            roll_shape(seed);
            return;
        }
        if (m_phase == Phase::Opening) {
            // Keep the geometry continuous: same s, take and mirroring; only the rate follows the
            // new duration.
            m_params.duration = paper_params.duration;
            m_rate = 1.0 / paper_params.duration;
            m_phase = Phase::Closing;
            return;
        }
        m_params = paper_params;
        m_rate = 1.0 / paper_params.duration;
        m_phase = Phase::Closing;
        m_s = 0.0;
        roll_shape(seed);
    }

    bool PaperCrumple::advance(double elapsed_seconds) noexcept {
        if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0 ||
            elapsed_seconds > STALL_SECONDS) {
            if (m_phase == Phase::Opening) {
                reset();
            } else if (m_phase == Phase::Closing) {
                m_phase = Phase::Closed;
                m_s = 1.0;
            }
            return active();
        }
        const double STEP = std::min(elapsed_seconds, MAX_PLAYED_STEP);
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

    void PaperCrumple::roll_shape(std::uint64_t seed) noexcept {
        // Base stream is fixed; random stream follows the seed. variation blends between them, so
        // variation 0 ignores the seed and every window plays the same take the same way.
        std::uint64_t base = BASE_SEED;
        std::uint64_t random = seed;
        const double VARIATION = m_params.variation;
        const auto DRAW = [&] {
            const double BASE_DRAW = unit_draw(base);
            const double RANDOM_DRAW = unit_draw(random);
            return BASE_DRAW + (VARIATION * (RANDOM_DRAW - BASE_DRAW));
        };
        m_take_pick = std::min(DRAW(), 0x1.fffffffffffffp-1);
        m_mirror_x = DRAW() < 0.5;
        m_mirror_y = DRAW() < 0.5;
        m_take = -1;
    }

    void PaperCrumple::choose_take(double aspect) const noexcept {
        if (m_take >= 0) {
            return;
        }
        // Takes are landscape; a portrait window plays one with its axes swapped.
        m_transpose = aspect < 1.0;
        const double LANDSCAPE = m_transpose ? 1.0 / aspect : aspect;
        const int TAKES = paper_take_count();
        // Nearest baked aspect on a log scale, then the seeded pick among the takes baked at that
        // aspect.
        double nearest = 0.0;
        double best_distance = INFINITY;
        for (int candidate = 0; candidate < TAKES; ++candidate) {
            const double DISTANCE = std::abs(std::log(paper_take_aspect(candidate) / LANDSCAPE));
            if (DISTANCE < best_distance) {
                best_distance = DISTANCE;
                nearest = paper_take_aspect(candidate);
            }
        }
        int family = 0;
        for (int family_take = 0; family_take < TAKES; ++family_take) {
            family += static_cast<int>(paper_take_aspect(family_take) == nearest);
        }
        int wanted = static_cast<int>(m_take_pick * family);
        for (int take = 0; take < TAKES; ++take) {
            if (paper_take_aspect(take) != nearest) {
                continue;
            }
            if (wanted-- == 0) {
                m_take = take;
                return;
            }
        }
    }

    void PaperCrumple::frame(Vec2 size, SheetFrame &out) const noexcept {
        if (m_phase == Phase::Idle) {
            flat_sheet(out);
            return;
        }

        const double WIDTH = std::isfinite(size.x) && size.x > 0.0 ? size.x : 1.0;
        const double HEIGHT = std::isfinite(size.y) && size.y > 0.0 ? size.y : 1.0;
        const double LENGTH_SCALE = std::sqrt(WIDTH * HEIGHT);
        choose_take(WIDTH / HEIGHT);

        // Playback position in the take: crumpliness sets how far toward the tight ball the crumple
        // goes. Frames are joined by a Catmull-Rom spline, so vertex velocities stay continuous
        // across baked frames; linear blending would kink every vertex's path at every frame, which
        // reads as judder.
        const int FRAMES = paper_take_frames();
        const double END = LOOSEST_END + ((1.0 - LOOSEST_END) * m_params.crumpliness);
        const double FRAME_POS = std::clamp(m_s, 0.0, 1.0) * END * (FRAMES - 1);
        const int FRAME_START = std::min(static_cast<int>(FRAME_POS), FRAMES - 2);
        const int F_BACK = std::max(FRAME_START - 1, 0);
        const int F_NEXT = std::min(FRAME_START + 2, FRAMES - 1);
        const double FRACTION = FRAME_POS - FRAME_START;
        const double FRACTION_SQUARED = FRACTION * FRACTION;
        const double FRACTION_CUBED = FRACTION_SQUARED * FRACTION;
        // Catmull-Rom weights for the frames before, at, after and two after the playback position.
        const double WEIGHT_BACK = 0.5 * (-FRACTION + (2.0 * FRACTION_SQUARED) - FRACTION_CUBED);
        const double WEIGHT_START = 0.5 * (2.0 - (5.0 * FRACTION_SQUARED) + (3.0 * FRACTION_CUBED));
        const double WEIGHT_END =
            0.5 * (FRACTION + (4.0 * FRACTION_SQUARED) - (3.0 * FRACTION_CUBED));
        const double WEIGHT_NEXT = 0.5 * (-FRACTION_SQUARED + FRACTION_CUBED);

        // The take's sheet has its own aspect; stretch it to the window's, keeping area, so rest
        // maps to rest exactly.
        const double WINDOW_LANDSCAPE = m_transpose ? HEIGHT / WIDTH : WIDTH / HEIGHT;
        const double TAKE_ASPECT = paper_take_aspect(m_take);
        const double STRETCH_X = std::sqrt(WINDOW_LANDSCAPE / TAKE_ASPECT);
        const double STRETCH_Y = 1.0 / STRETCH_X;

        // The sheet stays facing the viewer: no rotation beyond what the crumple itself does. It
        // only recedes at the end, as the ball drops away.
        const double SHRINK = 1.0 - (0.35 * smoothstep(0.86, 1.0, m_s));

        SheetGrid world;  // every vertex is written below
        const std::mdspan<Vec3, std::extents<std::size_t, GRID, GRID>, std::layout_left> WORLD_GRID{
            world.data()};
        for (int j = 0; j < GRID; ++j) {
            for (int i = 0; i < GRID; ++i) {
                // Window vertex (i, j) reads take vertex (si, sj): swapped for portrait windows,
                // then mirrored.
                int source_col = m_transpose ? j : i;
                int source_row = m_transpose ? i : j;
                if (m_mirror_x) {
                    source_col = PAPER_CELLS - source_col;
                }
                if (m_mirror_y) {
                    source_row = PAPER_CELLS - source_row;
                }
                const std::size_t TAKE = grid_index(source_col, source_row);
                std::array<double, 3> pos_back{};
                std::array<double, 3> pos_start{};
                std::array<double, 3> pos_end{};
                std::array<double, 3> pos_next{};
                paper_take_vertex(m_take, F_BACK, TAKE, pos_back);
                paper_take_vertex(m_take, FRAME_START, TAKE, pos_start);
                paper_take_vertex(m_take, FRAME_START + 1, TAKE, pos_end);
                paper_take_vertex(m_take, F_NEXT, TAKE, pos_next);
                std::array<double, 3> blended_pos{};
                const auto *back = pos_back.cbegin();
                const auto *start = pos_start.cbegin();
                const auto *end = pos_end.cbegin();
                const auto *next = pos_next.cbegin();
                for (double &component : blended_pos) {
                    component = (WEIGHT_BACK * *back) + (WEIGHT_START * *start) +
                                (WEIGHT_END * *end) + (WEIGHT_NEXT * *next);
                    ++back;
                    ++start;
                    ++end;
                    ++next;
                }
                double pos_x = std::get<0>(blended_pos) * STRETCH_X * (m_mirror_x ? -1.0 : 1.0);
                double pos_y = std::get<1>(blended_pos) * STRETCH_Y * (m_mirror_y ? -1.0 : 1.0);
                if (m_transpose) {
                    std::swap(pos_x, pos_y);
                }
                WORLD_GRID[i, j] = {
                    .x = LENGTH_SCALE * SHRINK * pos_x,
                    .y = LENGTH_SCALE * SHRINK * pos_y,
                    .z = LENGTH_SCALE * SHRINK * std::get<2>(blended_pos),
                };
            }
        }
        project_sheet(
            world, {.x = WIDTH, .y = HEIGHT}, m_params.shading, SheetLighting::PaperCreases, out);
    }

}  // namespace hyprimate
