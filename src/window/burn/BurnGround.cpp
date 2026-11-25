#include <algorithm>
#include <chrono>
#include <cmath>

#include "BurnGroundPrivate.hpp"

using namespace hyprimate;
using namespace burn_ground_detail;
namespace {
    bool overlaps(Box left, Box right) noexcept {
        return left.x < right.x + right.w && left.x + left.w > right.x &&
               left.y < right.y + right.h && left.y + left.h > right.y;
    }
    Box sweep_bounds(const animation::Motion &motion) noexcept {
        const auto &from = motion.from_capture;
        const auto &to = motion.capture;
        const double X = std::min(from.x, to.x), Y = std::min(from.y, to.y);
        return {X,
            Y,
            std::max(from.x + from.w, to.x + to.w) - X,
            std::max(from.y + from.h, to.y + to.h) - Y};
    }
    bool same(Box left, Box right) noexcept {
        return left.x == right.x && left.y == right.y && left.w == right.w && left.h == right.h;
    }
}  // namespace

float CBurnGround::Cell::damage() const noexcept {
    return scorch_damage(severity, age, lifetime, healing);
}
float CBurnGround::Cell::heat() const noexcept {
    return severity > 0 ? severity * static_cast<float>(std::exp(-age * 5.5)) : 0;
}
Vec2 CBurnGround::Area::position(std::size_t x, std::size_t y) const noexcept {
    return {screen.x + (static_cast<double>(x) + .5) * screen.w / WIDTH,
        screen.y + (static_cast<double>(y) + .5) * screen.h / HEIGHT};
}
bool CBurnGround::Area::active() const noexcept {
    return erase_pending || particle_count != 0 || opening_count != 0 ||
           std::ranges::any_of(
               cells, [](const Cell &cell) { return cell.severity > 0 || cell.m_coverage != 0; });
}
CBurnGround::Area *CBurnGround::area_for(const animation::Step &step) {
    const auto WORKSPACE = step.pinned ? 0 : step.workspace;
    for (auto &area : m_areas) {
        if (area && area->monitor == step.monitor && area->workspace == WORKSPACE) {
            return area.get();
        }
    }
    auto slot =
        std::find_if(m_areas.begin(), m_areas.end(), [](const auto &area) { return !area; });
    if (slot == m_areas.end()) {
        // Never evict live damage: its old monitor must receive the final erase frame.
        slot = std::find_if(
            m_areas.begin(), m_areas.end(), [](const auto &area) { return !area->active(); });
    }
    if (slot == m_areas.end()) {
        return nullptr;
    }
    if (!*slot) {
        *slot = std::make_unique<Area>();
    }
    // Reusable slots are already inactive: no cells, particles or openings remain.
    (*slot)->clock_started = false;
    (*slot)->monitor = step.monitor;
    (*slot)->workspace = WORKSPACE;
    (*slot)->screen = step.screen;
    for (const auto &screen : m_screens) {
        if (screen.monitor == step.monitor) {
            (*slot)->last = screen.now;
            (*slot)->clock_started = true;
            break;
        }
    }
    return slot->get();
}
void CBurnGround::stamp(
    const BurnFront &front, const BurnParams &params, const animation::Step &step, bool opening) {
    if (step.monitor == 0 || !is_usable(step.screen) || !is_usable(front.capture) ||
        !std::isfinite(front.progress) || !std::isfinite(step.elapsed)) {
        return;
    }
    if (!step.workspace_visible || step.workspace_moving || step.stalled) {
        cancel_owner(front.seed, step.window);
        return;
    }
    const auto SOURCE = std::find_if(m_screens.begin(),
        m_screens.end(),
        [&](const ScreenFacts &item) { return item.monitor == step.monitor; });
    if (!step.pinned && SOURCE != m_screens.end() && SOURCE->workspace != step.workspace &&
        SOURCE->special_workspace != step.workspace) {
        cancel_owner(front.seed, step.window);
        return;
    }
    // Only the closing front consumes progress intervals. Movement-only
    // acquisitions must not recycle the closing owners' history slots.
    auto owner = m_owners.end();
    if (front.closing) {
        owner = std::find_if(m_owners.begin(), m_owners.end(), [&](const Owner &item) {
            return item.seed == front.seed;
        });
        if (owner == m_owners.end() || front.seed == 0) {
            owner =
                m_owners.begin() + static_cast<std::ptrdiff_t>(m_next_owner++ % m_owners.size());
            *owner = {front.seed, 1};
        }
    }
    const double PREVIOUS = front.closing ? owner->progress : 0;
    stamp_area(front, params, step, opening, PREVIOUS);
    for (const auto &screen : m_screens) {
        if (!screen.enabled || screen.monitor == 0 || screen.monitor == step.monitor) {
            continue;
        }
        const bool SPECIAL = SOURCE != m_screens.end() && SOURCE->special_workspace != 0 &&
                             SOURCE->special_workspace == step.workspace;
        if (SPECIAL && screen.special_workspace != step.workspace) {
            continue;
        }
        auto target = step;
        target.monitor = screen.monitor;
        target.screen = screen.screen;
        target.workspace = SPECIAL ? screen.special_workspace : screen.workspace;
        stamp_area(front, params, target, opening, PREVIOUS);
    }
    if (front.closing) owner->progress = std::clamp(front.progress, 0.0, 1.0);
}
void CBurnGround::stamp_area(const BurnFront &front,
    const BurnParams &params,
    const animation::Step &step,
    bool opening,
    double previous) {
    const bool MOVED =
        !step.dying && step.movement_enabled && step.motion && !step.motion->workspace_shifted &&
        (step.motion->translated || step.motion->resized) && is_usable(step.motion->from_capture) &&
        is_usable(step.motion->capture) && overlaps(sweep_bounds(*step.motion), step.screen);
    const bool FRONT_HERE = (opening || front.closing) && overlaps(front.capture, step.screen);
    if (!is_usable(step.screen) || (!FRONT_HERE && !MOVED)) return;
    auto *target = area_for(step);
    if (!target) return;
    auto &area = *target;
    if (!same(area.screen, step.screen)) {
        area.cells.fill({});
        area.particle_count = 0;
        area.erase_pending = true;
        area.screen = step.screen;
    }
    const double PROGRESS = std::clamp(front.progress, 0.0, 1.0);
    const double PREVIOUS = previous;
    const auto OPENINGS_END =
        area.openings.begin() + static_cast<std::ptrdiff_t>(area.opening_count);
    auto opening_it = std::find_if(area.openings.begin(), OPENINGS_END, [&](const Opening &item) {
        return item.front.seed == front.seed;
    });
    if (FRONT_HERE && opening && !front.closing && PROGRESS > 0 && PROGRESS < 1) {
        if (opening_it == OPENINGS_END && area.opening_count < area.openings.size()) {
            opening_it = area.openings.begin() + static_cast<std::ptrdiff_t>(area.opening_count++);
        }
        if (opening_it != area.openings.end()) {
            *opening_it = {.front = front,
                .width = params.front_width,
                .variation = params.variation,
                .untouched = 0};
        }
    } else if (opening_it != OPENINGS_END) {
        *opening_it = area.openings[--area.opening_count];
    }
    if (FRONT_HERE && front.closing && PROGRESS < PREVIOUS) {
        const GridAxis X{area.screen.x, area.screen.w / WIDTH, WIDTH};
        const GridAxis Y{area.screen.y, area.screen.h / HEIGHT, HEIGHT};
        const auto [LEFT, RIGHT] = X.crossed(front.capture.x, front.capture.x + front.capture.w);
        const auto [TOP, BOTTOM] = Y.crossed(front.capture.y, front.capture.y + front.capture.h);
        const double FRONT_WIDTH = std::clamp(params.front_width, .001, .2);
        for (std::size_t row = TOP; row < BOTTOM; ++row) {
            for (std::size_t column = LEFT; column < RIGHT; ++column) {
                const auto POINT = area.position(column, row);
                if (!inside(POINT, front.capture)) continue;
                const double ARRIVAL =
                    burn_arrival({(POINT.x - front.capture.x) / front.capture.w,
                                     (POINT.y - front.capture.y) / front.capture.h},
                        front.seed,
                        params.variation);
                if (!(ARRIVAL >= PROGRESS - FRONT_WIDTH && ARRIVAL <= PREVIOUS + FRONT_WIDTH))
                    continue;
                const auto INDEX = row * WIDTH + column;
                auto &cell = area.cells[INDEX];
                std::uint64_t hash = front.seed ^ INDEX * 0x9E3779B97F4A7C15ULL;
                const float SEVERITY = static_cast<float>(.89 * (.84 + .16 * unit_draw(hash)));
                cell.severity = std::max(cell.damage(), SEVERITY);
                cell.age = 0;
                cell.lifetime = std::max(0.0, params.scorch_lifetime);
                cell.healing = std::max(.001, params.healing_duration);
            }
        }
    }
    if (MOVED) stamp_movement(area, front.seed, step.window, params, *step.motion);
}
void CBurnGround::tick(const ScreenFacts &screen) {
    auto known = std::find_if(m_screens.begin(), m_screens.end(), [&](const ScreenFacts &item) {
        return item.monitor == screen.monitor;
    });
    if (known == m_screens.end()) {
        known = std::find_if(m_screens.begin(), m_screens.end(), [](const ScreenFacts &item) {
            return item.monitor == 0;
        });
    }
    if (known != m_screens.end()) {
        *known = screen;
    }
    for (auto &area : m_areas) {
        if (!area || area->monitor != screen.monitor) {
            continue;
        }
        if (!screen.enabled || !same(area->screen, screen.screen)) {
            area->cells.fill({});
            area->particle_count = 0;
            area->opening_count = 0;
            area->erase_pending = false;
            area->clock_started = false;
            continue;
        }
        area->erase_pending = false;
        const double ELAPSED =
            area->clock_started
                ? std::max(0.0, std::chrono::duration<double>(screen.now - area->last).count())
                : 0;
        area->last = screen.now;
        area->clock_started = true;
        for (std::size_t index = area->opening_count; index > 0; --index) {
            auto &opening = area->openings[index - 1];
            opening.untouched += ELAPSED;
            if (opening.untouched > STALL_SECONDS) {
                opening = area->openings[--area->opening_count];
            }
        }
        for (auto &cell : area->cells) {
            if (cell.severity > 0) {
                cell.age += ELAPSED;
                if (cell.age >= cell.lifetime + cell.healing) {
                    cell.age = cell.lifetime = cell.healing = 0;
                    cell.severity = 0;
                }
            }
            if (cell.m_coverage != 0) {
                auto &movement = cell.m_movement;
                movement.age += static_cast<float>(ELAPSED);
                if (movement.age >= movement.lifetime + movement.healing) {
                    cell.m_coverage = cell.m_regrown = 0;
                    movement = {};
                }
            }
        }
        for (std::size_t index = area->particle_count; index > 0; --index) {
            auto &particle = area->particles[index - 1];
            const double OLD_AGE = particle.age;
            particle.age += ELAPSED;
            if (particle.age >= particle.lifetime) {
                particle = area->particles[--area->particle_count];
                continue;
            }
            const double FREE_START = std::max(OLD_AGE, particle.held_until);
            const double AIRTIME = particle.age - FREE_START;
            if (AIRTIME <= 0) continue;
            const double PHASE = particle.shape * 2 * std::numbers::pi;
            const double FREQUENCY = 4 + 2.0 / particle.lifetime;
            const double START = FREE_START * FREQUENCY + PHASE;
            const double END = particle.age * FREQUENCY + PHASE;
            const double SWAY = particle.flutter / FREQUENCY;
            // Exact linear drag toward the wind/settling velocity. Integrate
            // only free flight, including ticks that straddle detachment.
            const double RELAXATION = -std::expm1(-particle.drag * AIRTIME);
            const double DISTANCE_FACTOR = RELAXATION / particle.drag;
            const double VX = particle.velocity.x - particle.terminal.x;
            const double VY = particle.velocity.y - particle.terminal.y;
            particle.position.x += particle.terminal.x * AIRTIME + VX * DISTANCE_FACTOR +
                                   SWAY * (std::sin(END) - std::sin(START));
            particle.position.y += particle.terminal.y * AIRTIME + VY * DISTANCE_FACTOR +
                                   .28 * SWAY * (std::cos(END) - std::cos(START));
            particle.velocity.x -= VX * RELAXATION;
            particle.velocity.y -= VY * RELAXATION;
            particle.angle += particle.spin * AIRTIME;
            if (particle.kind == Particle::Kind::Paper) {
                const double FLIGHT = particle.age - particle.held_until;
                particle.fold = std::sin(
                    1.24 + FLIGHT * (7 + 8 * particle.shape) + .24 * std::sin(FLIGHT * 12));
            }
        }
    }
}
void CBurnGround::cancel(std::uint64_t owner) noexcept {
    cancel_owner(owner, 0);
}
void CBurnGround::cancel_owner(std::uint64_t seed, animation::WindowId window) noexcept {
    const auto OWNER = window ? window : seed;
    for (auto &area : m_areas) {
        if (!area) {
            continue;
        }
        for (std::size_t index = area->opening_count; index > 0; --index) {
            if (area->openings[index - 1].front.seed == seed) {
                area->openings[index - 1] = area->openings[--area->opening_count];
                area->erase_pending = true;
            }
        }
        for (std::size_t index = 0; index < area->particle_count; ++index) {
            auto &particle = area->particles[index];
            if (particle.owner == OWNER && particle.window_owned == (window != 0) &&
                particle.age < particle.held_until)
                particle.held_until = particle.age;
        }
    }
}
void CBurnGround::clear(std::optional<MonitorId> monitor) noexcept {
    for (auto &screen : m_screens) {
        if (!monitor || screen.monitor == *monitor) {
            screen = {};
        }
    }
    for (auto &area : m_areas) {
        if (!area || (monitor && area->monitor != *monitor)) {
            continue;
        }
        area.reset();
    }
    if (!monitor) {
        m_owners.fill({});
    }
}
bool CBurnGround::active(MonitorId monitor) const noexcept {
    return std::ranges::any_of(m_areas,
        [monitor](const auto &area) { return area && area->monitor == monitor && area->active(); });
}
