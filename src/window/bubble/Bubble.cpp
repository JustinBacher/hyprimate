#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "shared/Core.hpp"
#include "window/bubble/Bubble.hpp"
#include "window/bubble/BubbleMotion.hpp"
#include "window/bubble/BubbleFramePrivate.hpp"

namespace hyprimate {
    namespace {

        // Reject unrealistically large dimensions as well as non-finite values so all
        // derived distances and bounds remain representable.
        bool usable(Vec2 size) {
            constexpr double LIMIT = 1.0e9;
            return std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0 &&
                   size.x <= LIMIT && size.y <= LIMIT;
        }

    }  // namespace

    namespace bubble_frame_detail {
        void start_rupture(BubbleSurface &surface,
            Vec2 spot,
            std::uint64_t seed,
            RuptureDuration duration,
            bool sphere) {
            surface.closing = true;
            surface.spherical = sphere;
            surface.clock = 0;
            surface.rate = 1 / duration.seconds;
            surface.debris = seed;
            surface.aureole = 1;
            surface.rupture_visibility = surface.visibility;
            surface.hole_count = 3;
            if (!sphere && surface.thickness <= 0) {
                surface.thickness = 380 + (270 * unit_draw(seed));
            }
            const double SPOT_X = spot.x;
            const double SPOT_Y = spot.y;
            if (sphere) {
                std::get<0>(surface.holes).centre = {
                    .x = SPOT_X,
                    .y = -.55,
                    .z = std::sqrt(std::max(0.0, 1 - SPOT_X * SPOT_X - .55 * .55)),
                };
                const double TURN = (unit_draw(seed) - .5) * .5;
                std::get<1>(surface.holes).centre = {
                    .x = (-.5 * std::cos(TURN)) - (.45 * std::sin(TURN)),
                    .y = (-.5 * std::sin(TURN)) + (.45 * std::cos(TURN)),
                    .z = -.74,
                };
                std::get<2>(surface.holes).centre = {
                    .x = (.55 * std::cos(TURN)) - (.35 * std::sin(TURN)),
                    .y = (.55 * std::sin(TURN)) + (.35 * std::cos(TURN)),
                    .z = -.76,
                };
                for (auto &hole : surface.holes) {
                    const double NORM = std::sqrt((hole.centre.x * hole.centre.x) +
                                                  (hole.centre.y * hole.centre.y) +
                                                  (hole.centre.z * hole.centre.z));
                    if (NORM > 0) {
                        hole.centre = {
                            .x = hole.centre.x / NORM,
                            .y = hole.centre.y / NORM,
                            .z = hole.centre.z / NORM,
                        };
                    }
                }
                surface.speed = 12.5;
            } else {
                std::get<0>(surface.holes).centre = {
                    .x = SPOT_X * surface.half_size.x,
                    .y = -std::abs(SPOT_Y) * surface.half_size.y,
                    .z = 0,
                };
                const double FIRST = .72 + (.04 * unit_draw(seed));
                const double SECOND = .72 + (.04 * unit_draw(seed));
                const double CROSS1 = .5 + (.1 * unit_draw(seed));
                const double CROSS2 = .5 + (.1 * unit_draw(seed));
                if (surface.half_size.x >= surface.half_size.y) {
                    std::get<1>(surface.holes).centre = {
                        .x = -surface.half_size.x * FIRST,
                        .y = surface.half_size.y * CROSS1,
                        .z = 0,
                    };
                    std::get<2>(surface.holes).centre = {
                        .x = surface.half_size.x * SECOND,
                        .y = surface.half_size.y * CROSS2,
                        .z = 0,
                    };
                } else {
                    std::get<1>(surface.holes).centre = {
                        .x = surface.half_size.x * CROSS1,
                        .y = surface.half_size.y * FIRST,
                        .z = 0,
                    };
                    std::get<2>(surface.holes).centre = {
                        .x = -surface.half_size.x * CROSS2,
                        .y = -surface.half_size.y * SECOND,
                        .z = 0,
                    };
                }
                const double FARTHEST =
                    std::hypot(surface.half_size.x + std::abs(std::get<0>(surface.holes).centre.x),
                        surface.half_size.y + std::abs(std::get<0>(surface.holes).centre.y));
                surface.speed = FARTHEST * 2.5;
            }
            std::get<0>(surface.holes).start = sphere ? .31 : .15;
            std::get<1>(surface.holes).start = sphere ? .38 : .25;
            std::get<2>(surface.holes).start = sphere ? .42 : .31;
            for (auto &hole : std::span(surface.holes).first(surface.hole_count)) {
                for (double &phase : hole.phase) {
                    phase = 6.283185307179586 * unit_draw(seed);
                }
            }
            update_rupture(surface);
        }
        void update_rupture(BubbleSurface &surface) {
            for (std::size_t hole_index = 0; hole_index < surface.hole_count; ++hole_index) {
                auto &hole = surface.holes[hole_index];
                const double AGE = std::max(0.0, surface.clock - hole.start);
                const double TRANSIENT = surface.spherical ? .05 : .075;
                hole.radius =
                    surface.speed * (AGE - (TRANSIENT * (1 - std::exp(-AGE / TRANSIENT))));
                hole.precursor = 1 - smoothstep(hole.start, hole.start + .018, surface.clock);
                double precursor_offset = .06;
                if (hole_index == 0) {
                    precursor_offset = surface.spherical ? .10 : .15;
                }
                hole.precursor *=
                    smoothstep(hole.start - precursor_offset, hole.start, surface.clock);
                hole.rim = std::clamp(
                    1.2 + (.12 * std::sqrt(
                                     hole.radius * (surface.spherical ? surface.half_size.x : 1))),
                    1.2,
                    4.5);
                const double FINGERS = smoothstep(.012, .045, AGE);
                for (std::size_t mode = 0; mode < hole.amplitude.size(); ++mode) {
                    hole.amplitude.at(mode) = FINGERS * (.014 - (.002 * static_cast<double>(mode)));
                }
            }
        }
    }  // namespace bubble_frame_detail

    void Bubble::begin(Transition direction,
        BubbleDuration duration,
        std::uint64_t seed,
        Vec2 size,
        const BubbleFormationParams &formation) {
        if ((direction != Transition::Open && direction != Transition::Close) ||
            !std::isfinite(duration.seconds) || duration.seconds <= 0 || !usable(size)) {
            reset();
            return;
        }
        if (m_close_motion_frozen) {
            m_motion.reset();
            m_close_motion_frozen = false;
        }
        m_close_from_opening = false;
        const bool FROM_OPENING = direction == Transition::Close && m_active && m_opening;
        const bool FREEZE_MOTION = direction == Transition::Close && m_motion.stirred() &&
                                   (!FROM_OPENING || m_formation.landed());
        const bool INHERIT = FROM_OPENING || FREEZE_MOTION;
        BubbleSurface START_WINDOW{};
        double START_SEED = 0;
        if (INHERIT) {
            const auto &START = frame(size);
            START_WINDOW = START.window;
            START_SEED = START.seed;
        } else {
            START_WINDOW.half_size = {.x = size.x * .5, .y = size.y * .5};
            START_WINDOW.visibility = 1;
        }
        const double MATERIAL_SEED = FROM_OPENING ? START_SEED : unit_draw(seed);
        const double SPOT_X = (.16 + (.12 * unit_draw(seed))) * (unit_draw(seed) < .5 ? -1 : 1);
        const double SPOT_Y = (.12 + (.12 * unit_draw(seed))) * (unit_draw(seed) < .5 ? -1 : 1);
        m_close_window = direction == Transition::Close ? START_WINDOW : BubbleSurface{};
        m_close_from_opening = direction == Transition::Close && FROM_OPENING;
        m_close_motion_frozen = direction == Transition::Close && FREEZE_MOTION;
        m_size = size;
        m_duration = duration.seconds;
        m_elapsed = 0;
        m_seed = MATERIAL_SEED;
        m_rupture = {.x = SPOT_X, .y = SPOT_Y};
        m_flow = 0;
        // A closing window no longer moves; retain only movement that contributed to its snapshot.
        if (direction == Transition::Close && !m_close_motion_frozen) {
            m_motion.reset();
        }
        if (direction == Transition::Close && m_close_window.visibility > 0 &&
            !m_close_window.closing) {
            bubble_frame_detail::start_rupture(m_close_window,
                m_rupture,
                static_cast<std::uint64_t>(m_seed * 0x1p53),
                bubble_frame_detail::RuptureDuration{m_duration},
                false);
        }
        m_active = true;
        m_opening = direction == Transition::Open;
        if (m_opening) {
            m_formation.begin(size, seed, formation);
        }
        m_closed = false;
        invalidate_geometry();
    }

    void Bubble::advance(double elapsed) {
        if (!m_active || !std::isfinite(elapsed) || elapsed <= 0) {
            return;
        }
        // A stalled compositor should not skip the animation in a single frame.
        m_elapsed = std::min(m_duration, m_elapsed + std::min(elapsed, STALL_SECONDS));
        if (m_opening) {
            m_formation.advance(m_elapsed / m_duration, m_size);
            m_flow = m_elapsed * 2;
        }
        if (m_elapsed >= m_duration - (m_duration * 1e-12)) {
            m_elapsed = m_duration;
            m_active = false;
            m_closed = !m_opening;
            if (m_close_motion_frozen) {
                m_motion.reset();
                m_close_motion_frozen = false;
                m_close_from_opening = false;
            }
        }
        invalidate_geometry();
    }

    void Bubble::advance(double elapsed, Vec2 size, Box body, const BubbleMotionParams &params) {
        if (!std::isfinite(elapsed) || elapsed <= 0) {
            return;
        }
        if (m_active && m_opening && usable(size)) {
            m_size = size;
        }
        const bool SETTLING = opening();
        advance(elapsed);
        if (m_closed || (m_active && !m_opening)) {
            return;
        }
        m_transparency = std::clamp(params.transparency, 0.0, 1.0);
        m_motion.advance(elapsed, size, body, params);
        // Once landed, the film keeps flowing at the pace the movement stirs it.
        if (!SETTLING) {
            m_flow +=
                std::min(elapsed, STALL_SECONDS) * (.6 + std::min(m_motion.speed() / 800, 3.0));
        }
        invalidate_geometry();
    }

    void Bubble::reset() {
        if (m_close_motion_frozen) {
            m_motion.reset();
        }
        m_close_motion_frozen = false;
        m_close_from_opening = false;
        m_formation = {};
        m_size = {};
        m_duration = m_elapsed = m_seed = 0;
        m_rupture = {};
        m_active = m_opening = m_closed = false;
        invalidate_geometry();
    }

    void Bubble::drive(Vec2 displacement, bool carried) {
        if (m_close_motion_frozen || !std::isfinite(displacement.x) ||
            !std::isfinite(displacement.y) || (displacement.x == 0 && displacement.y == 0)) {
            return;
        }
        m_motion.drive(displacement, carried);
        invalidate_geometry();
    }

    void Bubble::seed_motion(std::uint64_t seed) {
        m_motion.seed(seed);
    }

    void Bubble::stop_motion() {
        if (m_close_motion_frozen) {
            return;
        }
        const bool WAS_STIRRED = m_motion.stirred();
        m_motion.reset();
        if (WAS_STIRRED) {
            invalidate_geometry();
        }
    }

    bool Bubble::landed() const {
        return m_active && m_opening && m_formation.landed();
    }

    bool Bubble::stirred() const {
        return !m_close_motion_frozen && m_motion.stirred();
    }

    const BubbleMotion &Bubble::motion() const {
        if (m_close_motion_frozen) {
            static const BubbleMotion EMPTY{};
            return EMPTY;
        }
        return m_motion;
    }

    void Bubble::clear_emissions() {
        m_motion.clear_emissions();
    }


    bool Bubble::active() const {
        return m_active;
    }
    bool Bubble::opening() const {
        return m_active && m_opening;
    }
    bool Bubble::closed() const {
        return m_closed;
    }

    void Bubble::invalidate_geometry() noexcept {
        m_geometry_dirty = true;
    }


    const BubbleFrame &Bubble::frame(Vec2 size) const {
        if (!usable(size)) {
            static const BubbleFrame INVALID{};
            return INVALID;
        }
        if (!m_geometry_dirty && m_prepared_size.x == size.x && m_prepared_size.y == size.y) {
            return m_prepared;
        }
        project(size, m_prepared);
        m_prepared_size = size;
        m_geometry_dirty = false;
        return m_prepared;
    }

    Box Bubble::bounds(Vec2 size) const {
        if (!usable(size) || m_closed) {
            return {};
        }
        const auto &FRAME = frame(size);
        return bubble_frame_detail::bubble_frame_bounds(FRAME);
    }

}  // namespace hyprimate
