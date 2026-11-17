#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

#include "animations/bubble/Bubble.hpp"
#include "animations/bubble/BubbleMotion.hpp"
#include "animations/shared/Core.hpp"

namespace hyprimate {
    namespace {

        // Reject unrealistically large dimensions as well as non-finite values so all
        // derived distances and bounds remain representable.
        bool usable(Vec2 size) {
            constexpr double LIMIT = 1.0e9;
            return std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0 &&
                   size.x <= LIMIT && size.y <= LIMIT;
        }

        double mix(double lhs, double rhs, double fraction) {
            return lhs + ((rhs - lhs) * fraction);
        }

        BubbleFrame neutral(Vec2 size) {
            BubbleFrame frame{};
            frame.size = size;
            frame.window.half_size = {.x = size.x * .5, .y = size.y * .5};
            frame.window.visibility = 1;
            return frame;
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
                    hole.amplitude[mode] = FINGERS * (.014 - (.002 * static_cast<double>(mode)));
                }
            }
        }

        void start_rupture(
            BubbleSurface &surface, Vec2 spot, std::uint64_t seed, double duration, bool sphere) {
            surface.closing = true;
            surface.spherical = sphere;
            surface.clock = 0;
            surface.rate = 1 / duration;
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

    }  // namespace

    void Bubble::begin(Transition direction, double duration, std::uint64_t seed, Vec2 size) {
        if ((direction != Transition::Open && direction != Transition::Close) ||
            !std::isfinite(duration) || duration <= 0 || !usable(size)) {
            reset();
            return;
        }
        const bool FROM_OPENING = direction == Transition::Close && m_active && m_opening;
        // A close takes over the film as it is: mid-opening, or still deformed by movement.
        const bool INHERIT = FROM_OPENING || (direction == Transition::Close && m_motion.stirred());
        const auto START = INHERIT ? frame(size) : neutral(size);
        const double MATERIAL_SEED = FROM_OPENING ? START.seed : unit_draw(seed);
        const double SPOT_X = (.16 + (.12 * unit_draw(seed))) * (unit_draw(seed) < .5 ? -1 : 1);
        const double SPOT_Y = (.12 + (.12 * unit_draw(seed))) * (unit_draw(seed) < .5 ? -1 : 1);
        m_initial = START;
        m_size = size;
        m_duration = std::clamp(duration, .01, 60.0);
        m_elapsed = 0;
        m_seed = MATERIAL_SEED;
        m_rupture = {.x = SPOT_X, .y = SPOT_Y};
        m_flow = 0;
        // A closing window no longer moves; its deformation now lives in the close's start frame.
        if (direction == Transition::Close) {
            m_motion.reset();
        }
        if (direction == Transition::Close && m_initial.window.visibility > 0 &&
            !m_initial.window.closing) {
            start_rupture(m_initial.window,
                m_rupture,
                static_cast<std::uint64_t>(m_seed * 0x1p53),
                m_duration,
                false);
        }
        m_active = true;
        m_opening = direction == Transition::Open;
        m_closed = false;
    }

    void Bubble::advance(double elapsed) {
        if (!m_active || !std::isfinite(elapsed) || elapsed <= 0) {
            return;
        }
        // A stalled compositor should not skip the animation in a single frame.
        m_elapsed = std::min(m_duration, m_elapsed + std::min(elapsed, STALL_SECONDS));
        if (m_opening) {
            m_flow = m_elapsed * 2;
        }
        if (m_elapsed >= m_duration - (m_duration * 1e-12)) {
            m_elapsed = m_duration;
            m_active = false;
            m_closed = !m_opening;
        }
    }

    void Bubble::advance(double elapsed, Vec2 size, Box body, const BubbleMotionParams &params) {
        if (!std::isfinite(elapsed) || elapsed <= 0) {
            return;
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
    }

    void Bubble::reset() {
        m_initial = {};
        m_size = {};
        m_duration = m_elapsed = m_seed = 0;
        m_rupture = {};
        m_active = m_opening = m_closed = false;
    }

    void Bubble::drive(Vec2 displacement, bool carried) {
        m_motion.drive(displacement, carried);
    }

    void Bubble::seed_motion(std::uint64_t seed) {
        m_motion.seed(seed);
    }

    void Bubble::stop_motion() {
        m_motion.reset();
    }

    bool Bubble::landed() const {
        return m_active && m_opening && m_elapsed >= .68 * m_duration;
    }

    bool Bubble::stirred() const {
        return m_motion.stirred();
    }

    const BubbleMotion &Bubble::motion() const {
        return m_motion;
    }

    void Bubble::clear_emissions() {
        m_motion.clear_emissions();
    }

    void Bubble::apply_motion(BubbleFrame &frame) const {
        if (!m_motion.stirred()) {
            return;
        }
        auto &window = frame.window;
        window.motion = m_motion.deformation();
        window.transparency = m_transparency;
        const Vec2 CORE = m_motion.core();
        window.core = {.x = window.core.x + CORE.x, .y = window.core.y + CORE.y};
        if (window.thickness <= 0) {
            window.thickness = 450;
        }
        window.flow = m_flow;
        frame.micro_count = m_motion.microbubbles(frame.micro);
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

    BubbleFrame Bubble::frame(Vec2 size) const {
        if (!usable(size)) {
            return {};
        }
        BubbleFrame result_frame = neutral(size);
        if (!m_active) {
            if (m_closed) {
                result_frame.window.visibility = 0;
            } else {
                apply_motion(result_frame);
            }
            return result_frame;
        }
        const double PROGRESS = std::clamp(m_elapsed / m_duration, 0.0, 1.0);
        result_frame.seed = m_seed;
        if (m_opening) {
            const double SHORT_SIDE = std::min(size.x, size.y);
            auto &shell = result_frame.shell;
            const double RADIUS = SHORT_SIDE * mix(.08, .46, smoothstep(0, .30, PROGRESS));
            shell.half_size = {.x = RADIUS, .y = RADIUS};
            shell.corner = shell.depth = RADIUS;
            shell.content = .55 * smoothstep(.04, .26, PROGRESS);
            shell.film = 1;
            shell.visibility = smoothstep(0, .13, PROGRESS);
            shell.spherical = true;
            shell.thickness = std::min(1500.0, 450 * std::pow(SHORT_SIDE * .46 / RADIUS, 2));
            shell.flow = m_elapsed * 2;
            start_rupture(
                shell, m_rupture, static_cast<std::uint64_t>(m_seed * 0x1p53), m_duration, true);
            shell.clock = PROGRESS;
            update_rupture(shell);
            if (PROGRESS >= .55) {
                shell.visibility = 0;
            }

            // The window is a second membrane behind the rupturing, still round shell.
            auto &window = result_frame.window;
            const double WINDOW_RADIUS = SHORT_SIDE * mix(.08, .40, smoothstep(.36, .52, PROGRESS));
            const double FLATTEN = smoothstep(.52, .68, PROGRESS);
            const double SPLAT =
                .045 * smoothstep(.55, .68, PROGRESS) * (1 - smoothstep(.68, .76, PROGRESS));
            window.half_size = {
                .x = mix(WINDOW_RADIUS, size.x * .5, FLATTEN) + (size.x * .5 * SPLAT),
                .y = mix(WINDOW_RADIUS, size.y * .5, FLATTEN) + (size.y * .5 * SPLAT),
            };
            window.corner = mix(WINDOW_RADIUS, 0, FLATTEN);
            window.depth = WINDOW_RADIUS * (1 - smoothstep(.52, .68, PROGRESS));
            const double SETTLE = std::clamp((PROGRESS - .64) / .36, 0.0, 1.0);
            const double ENVELOPE = SHORT_SIDE * .20 * smoothstep(.48, .64, PROGRESS) *
                                    std::exp(-2.0 * SETTLE) * (1 - smoothstep(.92, 1.0, PROGRESS));
            constexpr double PI_VALUE = std::numbers::pi;
            window.core = {
                .x = ENVELOPE * (std::cos(3 * PI_VALUE * SETTLE) +
                                    (2 * std::sin(3 * PI_VALUE * SETTLE) / (3 * PI_VALUE))),
                .y = ENVELOPE * .35 * std::sin(5 * PI_VALUE * SETTLE),
            };
            window.film = 1 - smoothstep(.56, 1.0, PROGRESS);
            // The inner film stays clear while the outer bubble bursts, so no image appears with
            // the pop; the window's imagery develops as the film flattens and lands.
            window.content = smoothstep(.50, .78, PROGRESS);
            window.thickness = 450;
            window.flow = m_elapsed * 2;
            window.visibility = smoothstep(.36, .42, PROGRESS);
            window.ripple = SHORT_SIDE * .012 * smoothstep(.56, .68, PROGRESS) *
                            (1 - smoothstep(.68, .76, PROGRESS));
            window.time = PROGRESS;
            apply_motion(result_frame);
            return result_frame;
        }
        result_frame = m_initial;
        result_frame.size = size;
        result_frame.seed = m_seed;
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
                start_rupture(surface,
                    m_rupture,
                    static_cast<std::uint64_t>(m_seed * 0x1p53),
                    m_duration,
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
            update_rupture(surface);
        };
        CLOSE(result_frame.window, false);
        CLOSE(result_frame.shell, true);
        // Microbubbles on the window's film burst with it.
        for (auto &microbubble : std::span(result_frame.micro).first(result_frame.micro_count)) {
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
        return result_frame;
    }

    Box Bubble::bounds(Vec2 size) const {
        if (!usable(size) || m_closed) {
            return {};
        }
        const auto FRAME = frame(size);
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
        for (const auto &microbubble : std::span(FRAME.micro).first(FRAME.micro_count)) {
            const double RADIUS = (microbubble.radius * 1.4) + 3;
            box = {
                std::min(std::get<0>(box), microbubble.centre.x - RADIUS),
                std::min(std::get<1>(box), microbubble.centre.y - RADIUS),
                std::max(std::get<2>(box), microbubble.centre.x + RADIUS),
                std::max(std::get<3>(box), microbubble.centre.y + RADIUS),
            };
        }
        return {
            .x = (size.x * .5) + std::get<0>(box),
            .y = (size.y * .5) + std::get<1>(box),
            .w = std::get<2>(box) - std::get<0>(box),
            .h = std::get<3>(box) - std::get<1>(box),
        };
    }

}  // namespace hyprimate
