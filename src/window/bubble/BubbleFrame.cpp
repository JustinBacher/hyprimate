#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include "window/bubble/Bubble.hpp"
#include "window/bubble/BubbleFramePrivate.hpp"
#include "window/bubble/BubbleMotion.hpp"

namespace hyprimate {
    namespace {
        void initialize_frame(BubbleFrame &frame, Vec2 size) {
            frame.size = size;
            frame.window = {};
            frame.window.half_size = {.x = size.x * .5, .y = size.y * .5};
            frame.window.visibility = 1;
            frame.shell = {};
            frame.seed = 0;
            frame.formation_count = 0;
            frame.micro_count = 0;
        }

        void apply_motion_to_formation(
            BubbleFrame &frame, Vec2 half_size, const BubbleDeformation &deformation) {
            for (auto &satellite : std::span(frame.formation).first(frame.formation_count)) {
                if (satellite.visibility > 0) {
                    satellite.centre = satellite.anchor =
                        bubble_motion_point(half_size, deformation, satellite.centre);
                }
            }
        }

    }  // namespace

    void Bubble::apply_motion(BubbleFrame &frame) const {
        if (!m_motion.stirred()) {
            return;
        }
        auto &window = frame.window;
        const auto MOVING = m_motion.deformation();
        auto &shape = window.motion;
        shape.drag = MOVING.drag;
        shape.sheen = std::max(shape.sheen, MOVING.sheen);
        shape.round = MOVING.round;
        shape.extent = MOVING.extent;
        shape.carry.x += MOVING.carry.x;
        shape.carry.y += MOVING.carry.y;
        for (std::size_t index = 0; index < shape.wave_cos.size(); ++index) {
            shape.wave_cos[index] += MOVING.wave_cos[index];
            shape.wave_sin[index] += MOVING.wave_sin[index];
        }
        shape.buds = MOVING.buds;
        apply_motion_to_formation(frame, window.half_size, MOVING);
        window.transparency = m_transparency;
        const Vec2 CORE = m_motion.core();
        window.core = {.x = window.core.x + CORE.x, .y = window.core.y + CORE.y};
        if (window.thickness <= 0) {
            window.thickness = 450;
        }
        window.flow = m_flow;
        frame.micro_count = m_motion.microbubbles(frame.micro);
    }

    void Bubble::project(Vec2 size, BubbleFrame &frame) const {
        initialize_frame(frame, size);
        if (!m_active) {
            if (m_closed) {
                frame.window.visibility = 0;
            } else {
                apply_motion(frame);
            }
            return;
        }
        frame.seed = m_seed;
        if (m_opening) {
            m_formation.writeFrame(frame);
            if (frame.formation_count == BUBBLE_FOAM_CAPACITY + 1) {
                for (std::size_t index = 0; index < BUBBLE_FOAM_CAPACITY; ++index) {
                    auto &satellite = frame.formation[index];
                    if (satellite.visibility <= 0) {
                        satellite = {};
                        satellite.visibility = 0;
                    }
                }
            }
            frame.window.transparency = m_transparency;
            // Movement joins a physically landed film without replacing its residual modes.
            if (m_formation.landed()) {
                apply_motion(frame);
            }
            return;
        }
        const double PROGRESS = std::clamp(m_elapsed / m_duration, 0.0, 1.0);
        if (m_close_from_opening) {
            frame.size = m_size;
            m_formation.writeFrame(frame);
            if (m_close_motion_frozen) {
                apply_motion_to_formation(frame, frame.window.half_size, m_motion.deformation());
            }
            if (frame.formation_count == BUBBLE_FOAM_CAPACITY + 1) {
                for (std::size_t index = 0; index < BUBBLE_FOAM_CAPACITY; ++index) {
                    auto &satellite = frame.formation[index];
                    if (satellite.visibility <= 0) {
                        satellite = {};
                        satellite.visibility = 0;
                    }
                }
            }
        }
        if (m_close_motion_frozen) {
            frame.micro_count = m_motion.microbubbles(frame.micro);
        }
        frame.size = size;
        frame.window = m_close_window;
        const double SCALE_X = size.x / m_size.x;
        const double SCALE_Y = size.y / m_size.y;
        const double SCALE = std::min(SCALE_X, SCALE_Y);
        const auto CLOSE = [&](BubbleSurface &surface, bool spherical) {
            const double X_SCALE = spherical ? SCALE : SCALE_X;
            const double Y_SCALE = spherical ? SCALE : SCALE_Y;
            surface.half_size = {
                .x = surface.half_size.x * X_SCALE,
                .y = surface.half_size.y * Y_SCALE,
            };
            surface.corner *= SCALE;
            surface.depth *= SCALE;
            const double DAMPING = SCALE * (1 - smoothstep(0, .5, PROGRESS));
            surface.core = {.x = surface.core.x * DAMPING, .y = surface.core.y * DAMPING};
            surface.ripple *= SCALE * (1 - smoothstep(0, .35, PROGRESS));
            // The film relaxes from the movement's deformation as it starts to tear.
            auto &motion = surface.motion;
            motion.drag = {.x = motion.drag.x * DAMPING, .y = motion.drag.y * DAMPING};
            std::get<0>(motion.wave_cos) *= DAMPING;
            std::get<0>(motion.wave_sin) *= DAMPING;
            std::get<1>(motion.wave_cos) *= DAMPING;
            std::get<1>(motion.wave_sin) *= DAMPING;
            std::get<2>(motion.wave_cos) *= DAMPING;
            std::get<2>(motion.wave_sin) *= DAMPING;
            std::get<3>(motion.wave_cos) *= DAMPING;
            std::get<3>(motion.wave_sin) *= DAMPING;
            motion.sheen *= 1 - smoothstep(0, .5, PROGRESS);
            // A bubble that closes while contracted bursts that way: its shape is the film being
            // torn.
            motion.extent = {.x = motion.extent.x * X_SCALE, .y = motion.extent.y * Y_SCALE};
            if (!surface.closing) {
                if (surface.visibility <= 0) {
                    return;
                }
                bubble_frame_detail::start_rupture(surface,
                    m_rupture,
                    static_cast<std::uint64_t>(m_seed * 0x1p53),
                    bubble_frame_detail::RuptureDuration{m_duration},
                    spherical);
            } else {
                for (auto &hole : std::span(surface.holes).first(surface.hole_count)) {
                    if (!spherical) {
                        hole.centre.x *= X_SCALE;
                        hole.centre.y *= Y_SCALE;
                    }
                }
                // Flat radii use the larger scale to preserve coverage after an anisotropic resize.
                if (!spherical) {
                    surface.speed *= std::max(SCALE_X, SCALE_Y);
                }
            }
            surface.clock += m_elapsed * surface.rate;
            surface.flow += m_elapsed;
            bubble_frame_detail::update_rupture(surface);
        };
        CLOSE(frame.window, false);
        CLOSE(frame.shell, true);
        for (auto &satellite : std::span(frame.formation).first(frame.formation_count)) {
            satellite.centre = {
                .x = satellite.centre.x * SCALE_X, .y = satellite.centre.y * SCALE_Y};
            satellite.anchor = {
                .x = satellite.anchor.x * SCALE_X, .y = satellite.anchor.y * SCALE_Y};
            satellite.radius *= SCALE;
            // A close cancels the air supply: companions rupture without depositing screen debris.
            satellite.collapse = std::max(satellite.collapse, smoothstep(0, .25, PROGRESS));
            satellite.visibility *= 1 - smoothstep(.15, .30, PROGRESS);
        }
        // Microbubbles on the window's film burst with it.
        for (auto &microbubble : std::span(frame.micro).first(frame.micro_count)) {
            microbubble.centre = {
                .x = microbubble.centre.x * SCALE_X,
                .y = microbubble.centre.y * SCALE_Y,
            };
            microbubble.anchor = {
                .x = microbubble.anchor.x * SCALE_X,
                .y = microbubble.anchor.y * SCALE_Y,
            };
            microbubble.collapse = std::max(microbubble.collapse, smoothstep(0, .25, PROGRESS));
        }
    }

    Box bubble_frame_detail::bubble_frame_bounds(const BubbleFrame &FRAME) {
        const Vec2 size = FRAME.size;
        // Each membrane's box around where it is drawn; a carried film is offset from the window.
        const auto EXTENT = [](const BubbleSurface &surface) {
            double reach = std::hypot(surface.motion.drag.x, surface.motion.drag.y);
            reach += std::hypot(
                std::get<0>(surface.motion.wave_cos), std::get<0>(surface.motion.wave_sin));
            reach += std::hypot(
                std::get<1>(surface.motion.wave_cos), std::get<1>(surface.motion.wave_sin));
            reach += std::hypot(
                std::get<2>(surface.motion.wave_cos), std::get<2>(surface.motion.wave_sin));
            reach += std::hypot(
                std::get<3>(surface.motion.wave_cos), std::get<3>(surface.motion.wave_sin));
            double swell = 0;
            for (const auto &bud : surface.motion.buds) {
                swell = std::max(swell, bud.height);
            }
            reach += swell;
            // A contracted membrane may reach past its rectangle where its shape bulges; one
            // overshooting back swells slightly past it everywhere.
            const Vec2 ROUND = bubble_round_half(surface.half_size, surface.motion);
            reach += std::max(0.0, -surface.motion.round) *
                     std::max(surface.half_size.x, surface.half_size.y);
            const Vec2 BASE{
                .x = std::max(surface.half_size.x, ROUND.x),
                .y = std::max(surface.half_size.y, ROUND.y),
            };
            const double MARGIN = 8 + std::abs(surface.ripple) + reach +
                                  (surface.closing ? 1.2 * std::min(BASE.x, BASE.y) : 0);
            const Vec2 HALF{.x = BASE.x + MARGIN, .y = BASE.y + MARGIN};
            const Vec2 CARRY = surface.motion.carry;
            return std::array<double, 4>{
                CARRY.x - HALF.x,
                CARRY.y - HALF.y,
                CARRY.x + HALF.x,
                CARRY.y + HALF.y,
            };
        };
        auto box = EXTENT(FRAME.window);
        const auto SHELL = EXTENT(FRAME.shell);
        std::get<0>(box) = std::min(std::get<0>(box), std::get<0>(SHELL));
        std::get<2>(box) = std::max(std::get<2>(box), std::get<2>(SHELL));
        std::get<1>(box) = std::min(std::get<1>(box), std::get<1>(SHELL));
        std::get<3>(box) = std::max(std::get<3>(box), std::get<3>(SHELL));
        const auto SMALL = [&](const BubbleMicro &microbubble) {
            if (microbubble.visibility <= 0) {
                return;
            }
            const double RADIUS = (microbubble.radius * (1.4 + std::abs(microbubble.squash.x) +
                                                            std::abs(microbubble.squash.y))) +
                                  3;
            box = {
                std::min(std::get<0>(box), microbubble.centre.x - RADIUS),
                std::min(std::get<1>(box), microbubble.centre.y - RADIUS),
                std::max(std::get<2>(box), microbubble.centre.x + RADIUS),
                std::max(std::get<3>(box), microbubble.centre.y + RADIUS),
            };
        };
        for (const auto &satellite : std::span(FRAME.formation).first(FRAME.formation_count)) {
            SMALL(satellite);
        }
        for (const auto &microbubble : std::span(FRAME.micro).first(FRAME.micro_count)) {
            SMALL(microbubble);
        }
        return {
            .x = (size.x * .5) + std::get<0>(box),
            .y = (size.y * .5) + std::get<1>(box),
            .w = std::get<2>(box) - std::get<0>(box),
            .h = std::get<3>(box) - std::get<1>(box),
        };
    }

}  // namespace hyprimate
