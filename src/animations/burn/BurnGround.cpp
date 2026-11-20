#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

#include "BurnGround.hpp"

using namespace hyprimate;
namespace {
    bool inside(Vec2 point, Box box) noexcept {
        return point.x >= box.x && point.y >= box.y && point.x <= box.x + box.w &&
               point.y <= box.y + box.h;
    }
    // Intersect the four linear half-spaces in time: exact for translation and linear resize.
    bool swept(Vec2 point, const animation::Motion &motion) noexcept {
        double first = 0, last = 1;
        const auto CONSTRAIN = [&](double value, double delta) {
            if (delta > 0) {
                last = std::min(last, -value / delta);
            } else if (delta < 0) {
                first = std::max(first, -value / delta);
            } else if (value > 0) {
                return false;
            }
            return first <= last;
        };
        const auto &from = motion.from_capture;
        const auto &to = motion.capture;
        return CONSTRAIN(from.x - point.x, to.x - from.x) &&
               CONSTRAIN(point.x - from.x - from.w, from.x + from.w - to.x - to.w) &&
               CONSTRAIN(from.y - point.y, to.y - from.y) &&
               CONSTRAIN(point.y - from.y - from.h, from.y + from.h - to.y - to.h);
    }
    bool same(Box left, Box right) noexcept {
        return left.x == right.x && left.y == right.y && left.w == right.w && left.h == right.h;
    }
}  // namespace

float CBurnGround::Cell::damage() const noexcept {
    if (severity <= 0) {
        return 0;
    }
    return severity *
           static_cast<float>(1.0 - smoothstep(lifetime, lifetime + std::max(.001, healing), age));
}
float CBurnGround::Cell::heat() const noexcept {
    return severity * static_cast<float>(std::exp(-age * 5.5));
}
Vec2 CBurnGround::Area::position(std::size_t x, std::size_t y) const noexcept {
    return {screen.x + (static_cast<double>(x) + .5) * screen.w / WIDTH,
        screen.y + (static_cast<double>(y) + .5) * screen.h / HEIGHT};
}
bool CBurnGround::Area::active() const noexcept {
    return erase_pending || particle_count != 0 || opening_count != 0 ||
           std::ranges::any_of(cells, [](const Cell &cell) { return cell.severity > 0; });
}
CBurnGround::Area &CBurnGround::area_for(const animation::Step &step) {
    const auto WORKSPACE = step.pinned ? 0 : step.workspace;
    for (auto &area : m_areas) {
        if (area && area->monitor == step.monitor && area->workspace == WORKSPACE) {
            return *area;
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
        // Saturation is handled by stamp before reaching here.
        return *m_areas.front();
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
    return **slot;
}
void CBurnGround::stamp(
    const BurnFront &front, const BurnParams &params, const animation::Step &step, bool opening) {
    if (step.monitor == 0 || !is_usable(step.screen) || !is_usable(front.capture) ||
        !std::isfinite(front.progress) || !std::isfinite(step.elapsed)) {
        return;
    }
    if (!step.workspace_visible || step.workspace_moving || step.stalled) {
        cancel(front.seed);
        return;
    }
    const auto SOURCE = std::find_if(m_screens.begin(),
        m_screens.end(),
        [&](const ScreenFacts &item) { return item.monitor == step.monitor; });
    if (!step.pinned && SOURCE != m_screens.end() && SOURCE->workspace != step.workspace &&
        SOURCE->special_workspace != step.workspace) {
        cancel(front.seed);
        return;
    }
    auto owner = std::find_if(m_owners.begin(), m_owners.end(), [&](const Owner &item) {
        return item.seed == front.seed && item.closing == front.closing;
    });
    if (owner == m_owners.end() || front.seed == 0) {
        owner = m_owners.begin() + static_cast<std::ptrdiff_t>(m_next_owner++ % m_owners.size());
        *owner = {front.seed, front.closing ? 1.0 : 0.0, front.closing};
    }
    const double PREVIOUS = owner->progress;
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
    owner->progress = std::clamp(front.progress, 0.0, 1.0);
}
void CBurnGround::stamp_area(const BurnFront &front,
    const BurnParams &params,
    const animation::Step &step,
    bool opening,
    double previous) {
    Box affected = front.capture;
    if (step.motion && is_usable(step.motion->from_capture) && is_usable(step.motion->capture)) {
        affected.x = std::min(step.motion->from_capture.x, step.motion->capture.x);
        affected.y = std::min(step.motion->from_capture.y, step.motion->capture.y);
        affected.w = std::max(step.motion->from_capture.x + step.motion->from_capture.w,
                         step.motion->capture.x + step.motion->capture.w) -
                     affected.x;
        affected.h = std::max(step.motion->from_capture.y + step.motion->from_capture.h,
                         step.motion->capture.y + step.motion->capture.h) -
                     affected.y;
    }
    if (affected.x >= step.screen.x + step.screen.w ||
        affected.y >= step.screen.y + step.screen.h || affected.x + affected.w <= step.screen.x ||
        affected.y + affected.h <= step.screen.y) {
        return;
    }
    const auto WORKSPACE = step.pinned ? 0 : step.workspace;
    const bool AVAILABLE = std::ranges::any_of(m_areas, [&](const auto &area) {
        return !area || !area->active() ||
               (area->monitor == step.monitor && area->workspace == WORKSPACE);
    });
    if (!AVAILABLE) {
        return;
    }
    auto &area = area_for(step);
    if (!same(area.screen, step.screen)) {
        area.cells.fill({});
        area.particle_count = 0;
        area.erase_pending = true;
        area.screen = step.screen;
    }
    const double PROGRESS = std::clamp(front.progress, 0.0, 1.0);
    const double PREVIOUS = previous;
    const bool ADVANCED =
        (front.closing || opening) && (front.closing ? PROGRESS < PREVIOUS : PROGRESS > PREVIOUS);
    const bool MOVED = step.movement_enabled && step.motion && !step.motion->workspace_shifted &&
                       (step.motion->translated || step.motion->resized) &&
                       is_usable(step.motion->from_capture) && is_usable(step.motion->capture);
    auto opening_it = std::find_if(area.openings.begin(),
        area.openings.begin() + static_cast<std::ptrdiff_t>(area.opening_count),
        [&](const Opening &item) { return item.front.seed == front.seed; });
    if (opening && !front.closing && PROGRESS > 0 && PROGRESS < 1) {
        if (opening_it == area.openings.begin() + static_cast<std::ptrdiff_t>(area.opening_count) &&
            area.opening_count < area.openings.size()) {
            opening_it = area.openings.begin() + static_cast<std::ptrdiff_t>(area.opening_count++);
        }
        if (opening_it != area.openings.end()) {
            *opening_it = {.front = front,
                .width = params.front_width,
                .variation = params.variation,
                .untouched = 0};
        }
    } else if (opening_it !=
               area.openings.begin() + static_cast<std::ptrdiff_t>(area.opening_count)) {
        *opening_it = area.openings[--area.opening_count];
    }
    if (!MOVED && !(ADVANCED && front.closing)) {
        return;
    }
    const double WIDTH = std::clamp(params.front_width, .001, .2);
    for (std::size_t row = 0; row < HEIGHT; ++row) {
        for (std::size_t column = 0; column < CBurnGround::WIDTH; ++column) {
            const auto POINT = area.position(column, row);
            bool heated = false;
            if (ADVANCED && front.closing && inside(POINT, front.capture)) {
                const auto ARRIVAL =
                    burn_arrival({(POINT.x - front.capture.x) / front.capture.w,
                                     (POINT.y - front.capture.y) / front.capture.h},
                        front.seed,
                        params.variation);
                heated = ARRIVAL >= PROGRESS - WIDTH && ARRIVAL <= PREVIOUS + WIDTH;
            }
            const bool TRAIL = MOVED && swept(POINT, *step.motion);
            if (!heated && !TRAIL) {
                continue;
            }
            auto &cell = area.cells[row * CBurnGround::WIDTH + column];
            // Fixed spatial variation, independent of frame subdivision and refresh frequency.
            std::uint64_t hash =
                front.seed ^ (row * CBurnGround::WIDTH + column) * 0x9E3779B97F4A7C15ULL;
            const float SEVERITY =
                static_cast<float>((TRAIL ? .94 : .89) * (.84 + .16 * unit_draw(hash)));
            cell.severity = std::max(cell.damage(), SEVERITY);
            cell.age = 0;
            cell.lifetime = std::max(0.0, params.scorch_lifetime);
            cell.healing = std::max(.001, params.healing_duration);
        }
    }
    if (MOVED && params.ember_density > 0 && area.particle_count < MAX_PARTICLES) {
        const auto &motion = *step.motion;
        const double DISTANCE = std::hypot(
            motion.capture.x - motion.from_capture.x, motion.capture.y - motion.from_capture.y);
        m_rng ^= front.seed + 0x9E3779B97F4A7C15ULL;
        const double EMISSION = std::clamp(DISTANCE * params.ember_density * .16, 0.0, 12.0);
        const auto WHOLE = static_cast<std::size_t>(EMISSION);
        const auto COUNT = WHOLE + static_cast<std::size_t>(
                                       unit_draw(m_rng) < EMISSION - static_cast<double>(WHOLE));
        const double ANGLE = params.wind_direction * std::numbers::pi / 180.0;
        for (std::size_t index = 0; index < COUNT && area.particle_count < MAX_PARTICLES; ++index) {
            const double ALONG = unit_draw(m_rng);
            const bool EMBER = unit_draw(m_rng) > .35;
            const Vec2 POSITION{std::lerp(motion.from_capture.x, motion.capture.x, ALONG) +
                                    unit_draw(m_rng) * motion.capture.w,
                std::lerp(motion.from_capture.y, motion.capture.y, ALONG) +
                    unit_draw(m_rng) * motion.capture.h};
            const double SPEED =
                std::max(0.0, params.wind_strength) * (.45 + unit_draw(m_rng) * .85);
            const double DRIFT = (unit_draw(m_rng) - .5) * 42;
            const double BUOYANCY = EMBER ? 16 + unit_draw(m_rng) * 45 : 2 + unit_draw(m_rng) * 9;
            area.particles[area.particle_count++] = {.owner = front.seed,
                .position = POSITION,
                .velocity = {std::cos(ANGLE) * SPEED + DRIFT, std::sin(ANGLE) * SPEED - BUOYANCY},
                .age = 0,
                .lifetime = .35 + unit_draw(m_rng) * 1.1,
                .size = EMBER ? .75 + unit_draw(m_rng) * 1.85 : 1.25 + unit_draw(m_rng) * 3.0,
                .wind = (3 + params.wind_strength * .075) * (.5 + unit_draw(m_rng)),
                .gravity = std::max(0.0, params.gravity_strength) * (EMBER ? -.035 : .18) *
                           (.5 + unit_draw(m_rng)),
                .ember = EMBER};
        }
    }
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
            if (cell.severity <= 0) {
                continue;
            }
            cell.age += ELAPSED;
            if (cell.age >= cell.lifetime + cell.healing) {
                cell = {};
            }
        }
        for (std::size_t index = area->particle_count; index > 0; --index) {
            auto &particle = area->particles[index - 1];
            particle.age += ELAPSED;
            if (particle.age >= particle.lifetime) {
                particle = area->particles[--area->particle_count];
                continue;
            }
            const double PHASE =
                static_cast<double>(particle.owner % 4096) * .013 + particle.size * 2.7;
            const double OLD = particle.age - ELAPSED;
            const double FREQUENCY = 4 + 2.0 / particle.lifetime;
            particle.position.x +=
                particle.wind *
                (std::sin(particle.age * FREQUENCY + PHASE) - std::sin(OLD * FREQUENCY + PHASE)) /
                FREQUENCY;
            particle.position.y +=
                particle.wind * .28 *
                (std::cos(particle.age * FREQUENCY + PHASE) - std::cos(OLD * FREQUENCY + PHASE)) /
                FREQUENCY;
            particle.position.x += particle.velocity.x * ELAPSED;
            particle.position.y +=
                particle.velocity.y * ELAPSED + .5 * particle.gravity * ELAPSED * ELAPSED;
            particle.velocity.y += particle.gravity * ELAPSED;
        }
    }
}
void CBurnGround::cancel(std::uint64_t owner) noexcept {
    for (auto &area : m_areas) {
        if (!area) {
            continue;
        }
        for (std::size_t index = area->opening_count; index > 0; --index) {
            if (area->openings[index - 1].front.seed == owner) {
                area->openings[index - 1] = area->openings[--area->opening_count];
                area->erase_pending = true;
            }
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
