#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>

#include "Check.hpp"
#include "shared/Core.hpp"
#include "window/bubble/Bubble.hpp"
#include "window/bubble/BubbleFormation.hpp"

namespace {
    using namespace hyprimate;
    constexpr Vec2 SIZE{.x = 1000, .y = 700};
    void formation_during_opening() {
        for (const Vec2 WINDOW_SIZE :
            {Vec2{.x = 320, .y = 900}, Vec2{.x = 1100, .y = 320}, Vec2{.x = 640, .y = 640}, SIZE}) {
            Bubble bubble;
            bubble.begin(Transition::Open, BubbleDuration{1}, 42, WINDOW_SIZE);
            auto previous = bubble.frame(WINDOW_SIZE);
            const double SHORT_HALF = std::min(WINDOW_SIZE.x, WINDOW_SIZE.y) / 2;
            bool contact = false;
            bool deformed = false;
            bool lingered = false;
            bool late_birth = false;
            bool short_first = WINDOW_SIZE.x == WINDOW_SIZE.y;
            bool early_morph = false;
            bool grew_from_companion = false;
            double emerged = -1;
            std::size_t peak_early = 0;
            std::size_t births = 0;
            double smallest = std::numeric_limits<double>::infinity();
            double largest = 0;
            for (int i = 1; i <= 1000; ++i) {
                bubble.advance(.001);
                const auto FRAME = bubble.frame(WINDOW_SIZE);
                CHECK(FRAME.formation_count <= FRAME.formation.size());
                CHECK(FRAME.shell.visibility == 0 && FRAME.window.hole_count == 0);
                // Visibility, size and position of the window bubble change continuously.
                CHECK(std::abs(FRAME.window.visibility - previous.window.visibility) < .05);
                CHECK(std::abs(FRAME.window.content - previous.window.content) < .05);
                if (previous.window.visibility > 0) {
                    CHECK(std::abs(FRAME.window.half_size.x - previous.window.half_size.x) <
                          WINDOW_SIZE.x * .03);
                    CHECK(std::abs(FRAME.window.half_size.y - previous.window.half_size.y) <
                          WINDOW_SIZE.y * .03);
                    CHECK(std::abs(FRAME.window.corner - previous.window.corner) <
                          std::max(WINDOW_SIZE.x, WINDOW_SIZE.y) * .03);
                }
                if (emerged < 0 && FRAME.window.visibility > 0) {
                    emerged = i / 1000.0;
                    // It is the film it grew from, drawn at the same place and size while its
                    // membrane takes over, and that film was already there a moment before.
                    const auto &origin = std::get<BUBBLE_FOAM_CAPACITY>(FRAME.formation);
                    CHECK(FRAME.formation_count == kBubbleFormationCapacity &&
                          origin.visibility > .9);
                    CHECK(
                        std::abs(origin.radius - FRAME.window.half_size.x) < 1e-6 * WINDOW_SIZE.x);
                    CHECK(origin.centre.x == FRAME.window.motion.carry.x &&
                          origin.centre.y == FRAME.window.motion.carry.y);
                    for (const auto &film :
                        std::span(previous.formation).first(previous.formation_count)) {
                        if (film.visibility <= 0) {
                            continue;
                        }
                        const double GAP = std::hypot(
                            film.centre.x - origin.centre.x, film.centre.y - origin.centre.y);
                        grew_from_companion |=
                            GAP < origin.radius * .2 && film.radius > origin.radius * .8;
                    }
                }
                if (FRAME.window.visibility > 0) {
                    const double X_EDGE =
                        FRAME.window.half_size.x + std::abs(FRAME.window.motion.carry.x);
                    const double Y_EDGE =
                        FRAME.window.half_size.y + std::abs(FRAME.window.motion.carry.y);
                    const bool X_CONTACT = X_EDGE >= (WINDOW_SIZE.x / 2) - (SHORT_HALF * .02);
                    const bool Y_CONTACT = Y_EDGE >= (WINDOW_SIZE.y / 2) - (SHORT_HALF * .02);
                    if (!contact && (X_CONTACT || Y_CONTACT)) {
                        short_first = WINDOW_SIZE.x == WINDOW_SIZE.y ||
                                      (WINDOW_SIZE.x < WINDOW_SIZE.y ? X_CONTACT : Y_CONTACT);
                    }
                    contact |= X_CONTACT || Y_CONTACT;
                    if (std::abs(FRAME.window.half_size.x - FRAME.window.half_size.y) >
                        SHORT_HALF * .1) {
                        CHECK(contact);
                        deformed = true;
                    }
                    if (contact && !(X_CONTACT && Y_CONTACT)) {
                        const double SMALL =
                            std::min(FRAME.window.half_size.x, FRAME.window.half_size.y);
                        constexpr double PI_VALUE = std::numbers::pi;
                        const double AREA =
                            (4 * FRAME.window.half_size.x * FRAME.window.half_size.y) -
                            ((4 - PI_VALUE) * FRAME.window.corner * FRAME.window.corner);
                        // A modest amount of inflation after first contact must
                        // already flatten the ends, before the other walls arrive.
                        if (AREA > 1.08 * PI_VALUE * SMALL * SMALL) {
                            CHECK(FRAME.window.corner < .97 * SMALL);
                            early_morph = true;
                        }
                    }
                }
                std::size_t live = 0;
                std::size_t index = 0;
                const auto *previous_film = previous.formation.begin();
                for (const auto &film : std::span(FRAME.formation).first(FRAME.formation_count)) {
                    if (film.visibility > 0) {
                        ++live;
                        CHECK(film.radius > 0 && std::isfinite(film.centre.x) &&
                              std::isfinite(film.centre.y));
                        CHECK(film.thickness > 250 && film.thickness < 900);
                        if (index < previous.formation_count && previous_film->visibility > 0 &&
                            previous_film->seed == film.seed) {
                            CHECK(std::abs(film.radius - previous_film->radius) <
                                  std::max(WINDOW_SIZE.x, WINDOW_SIZE.y) * .02);
                            CHECK(std::hypot(film.centre.x - previous_film->centre.x,
                                      film.centre.y - previous_film->centre.y) <
                                  std::max(WINDOW_SIZE.x, WINDOW_SIZE.y) * .05);
                        } else {
                            ++births;
                            late_birth |= i > 300;
                        }
                        if (film.growth >= 1 && film.collapse == 0) {
                            smallest = std::min(smallest, film.radius);
                            largest = std::max(largest, film.radius);
                        }
                    }
                    ++index;
                    ++previous_film;
                }
                if (i < 500) {
                    peak_early = std::max(peak_early, live);
                }
                lingered |= bubble.landed() && live > 0;
                previous = FRAME;
            }
            // Small bubbles hold the stage for the first half; the window bubble forms late.
            CHECK(emerged >= .5 && emerged <= .72);
            CHECK(grew_from_companion);
            CHECK(peak_early >= 15 && births > peak_early && late_birth);
            CHECK(largest > smallest * 3);
            CHECK(contact && short_first && lingered);
            if (WINDOW_SIZE.x != WINDOW_SIZE.y) {
                CHECK(deformed && early_morph);
            }
            CHECK(!bubble.active() && !bubble.closed());
            CHECK(previous.window.half_size.x == WINDOW_SIZE.x / 2 &&
                  previous.window.half_size.y == WINDOW_SIZE.y / 2);
            CHECK(previous.window.corner == 0 && previous.window.depth == 0);
            CHECK(previous.window.content == 1 && previous.window.visibility == 1);
            CHECK(previous.window.core.x == 0 && previous.window.core.y == 0);
            for (const auto &film : std::span(previous.formation).first(previous.formation_count)) {
                CHECK(film.visibility == 0);
            }
        }
    }
    // Where the window bubble forms comes from the foam's own history, not a fixed point, and
    // its companions keep pushing it while it grows.
    void formation_movement_depends_on_seed() {
        std::array<Vec2, 2> origins{};
        auto *origin = origins.begin();
        for (int seed = 0; seed < 2; ++seed, ++origin) {
            Bubble bubble;
            bubble.begin(Transition::Open, BubbleDuration{1}, 40 + seed, SIZE);
            bool emerged = false;
            bool pushed = false;
            Vec2 previous{};
            for (int i = 0; i < 900; ++i) {
                bubble.advance(.001);
                const auto FRAME = bubble.frame(SIZE);
                const auto &surface = FRAME.window;
                if (surface.visibility <= 0) {
                    continue;
                }
                if (!emerged) {
                    emerged = true;
                    *origin = surface.motion.carry;
                } else if (!bubble.landed()) {
                    bool touching = false;
                    for (const auto &other : std::span(FRAME.formation)
                             .first(std::min(BUBBLE_FOAM_CAPACITY, FRAME.formation_count))) {
                        if (other.visibility <= 0) {
                            continue;
                        }
                        const double DISTANCE = std::hypot(surface.motion.carry.x - other.centre.x,
                            surface.motion.carry.y - other.centre.y);
                        touching |= DISTANCE <= std::max(surface.half_size.x, surface.half_size.y) +
                                                    other.radius;
                    }
                    const double STEP = std::hypot(
                        surface.motion.carry.x - previous.x, surface.motion.carry.y - previous.y);
                    pushed |= touching && STEP > 1e-3;
                }
                previous = surface.motion.carry;
            }
            CHECK(emerged && pushed);
        }
        CHECK(
            std::hypot(std::get<0>(origins).x - std::get<1>(origins).x,
                std::get<0>(origins).y - std::get<1>(origins).y) > .02 * std::min(SIZE.x, SIZE.y));
    }
    void pressure_changes_inflation_and_settles() {
        constexpr double PI_VALUE = std::numbers::pi;
        for (const Vec2 WINDOW_SIZE :
            {Vec2{.x = 320, .y = 900}, Vec2{.x = 1100, .y = 320}, Vec2{.x = 640, .y = 640}, SIZE}) {
            Bubble bubble;
            bubble.begin(Transition::Open, BubbleDuration{1}, 42, WINDOW_SIZE);
            const auto AREA = [](const BubbleSurface &surface) {
                return (4 * surface.half_size.x * surface.half_size.y) -
                       ((4 - PI_VALUE) * surface.corner * surface.corner);
            };
            double previous_area = AREA(bubble.frame(WINDOW_SIZE).window);
            double min_rate = std::numeric_limits<double>::infinity();
            double max_rate = 0;
            double peak_core = 0;
            double late_core = 0;
            bool positive = false;
            bool negative = false;
            double free_rate = 0;
            double constrained_rate = 0;
            int contact_step = 0;
            for (int i = 1; i <= 100; ++i) {
                bubble.advance(.01);
                const auto FRAME = bubble.frame(WINDOW_SIZE);
                // Equal integration intervals avoid mistaking substep quantisation (or
                // sqrt(area)'s changing radius rate) for variable air flow.
                if (i % 5 == 0) {
                    const double CURRENT_AREA = AREA(FRAME.window);
                    const double RATE = CURRENT_AREA - previous_area;
                    if (i >= 15 && !bubble.landed() &&
                        RATE > WINDOW_SIZE.x * WINDOW_SIZE.y * .001) {
                        min_rate = std::min(min_rate, RATE);
                        max_rate = std::max(max_rate, RATE);
                    }
                    if (std::max(WINDOW_SIZE.x, WINDOW_SIZE.y) >
                        2 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y)) {
                        const double RADIUS =
                            std::min((WINDOW_SIZE.x / 2) - std::abs(FRAME.window.motion.carry.x),
                                (WINDOW_SIZE.y / 2) - std::abs(FRAME.window.motion.carry.y));
                        const double CONTACT_RATIO = CURRENT_AREA / (PI_VALUE * RADIUS * RADIUS);
                        if (contact_step == 0) {
                            if (CONTACT_RATIO < 1) {
                                free_rate = RATE;
                            } else {
                                contact_step = i;
                            }
                        }
                        // Compare complete intervals on either side of contact.
                        if (contact_step > 0 && i == contact_step + 10) {
                            constrained_rate = RATE;
                        }
                    }
                    previous_area = CURRENT_AREA;
                }
                if (bubble.landed()) {
                    peak_core =
                        std::max(peak_core, std::hypot(FRAME.window.core.x, FRAME.window.core.y));
                    positive |=
                        FRAME.window.core.x > .0001 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y);
                    negative |=
                        FRAME.window.core.x < -.0001 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y);
                }
                if (i == 99) {
                    late_core = std::hypot(FRAME.window.core.x, FRAME.window.core.y);
                }
            }
            CHECK(max_rate > min_rate * 1.15);
            if (std::max(WINDOW_SIZE.x, WINDOW_SIZE.y) >
                2 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y)) {
                CHECK(contact_step > 0 && free_rate > 0 && constrained_rate > 0);
                CHECK(constrained_rate > 1.3 * free_rate);
            }
            CHECK(peak_core > .002 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y));
            CHECK(positive && negative);
            CHECK(late_core < peak_core * .1);
            CHECK(bubble.frame(WINDOW_SIZE).window.core.x == 0 &&
                  bubble.frame(WINDOW_SIZE).window.core.y == 0);
        }
    }
    void formation_controls_keep_the_endpoint() {
        std::array<BubbleFormationParams, 20> cases{};
        std::get<0>(cases).count = 0;
        std::get<1>(cases).count = 1;
        std::get<2>(cases).birthRate = .25;
        std::get<3>(cases).birthRate = 2;
        std::get<4>(cases).sizeVariation = 0;
        std::get<5>(cases).sizeVariation = 1;
        std::get<6>(cases).growthVariation = 0;
        std::get<7>(cases).growthVariation = 1;
        std::get<8>(cases).interaction = .25;
        std::get<9>(cases).interaction = 2;
        std::get<10>(cases).damping = .5;
        std::get<11>(cases).damping = 2;
        std::get<12>(cases).buoyancy = 0;
        std::get<13>(cases).buoyancy = 2;
        std::get<14>(cases).linger = 0;
        std::get<15>(cases).linger = 1;
        std::get<16>(cases) = {.count = 4,
            .birthRate = .25,
            .sizeVariation = 0,
            .growthVariation = 0,
            .interaction = .25,
            .damping = .5,
            .buoyancy = 0,
            .linger = 0};
        std::get<17>(cases) = {.count = 32,
            .birthRate = 2,
            .sizeVariation = 1,
            .growthVariation = 1,
            .interaction = 2,
            .damping = 2,
            .buoyancy = 2,
            .linger = 1};
        std::get<18>(cases) = {.count = 32,
            .birthRate = 2,
            .sizeVariation = 1,
            .growthVariation = 1,
            .interaction = 2,
            .damping = .5,
            .buoyancy = 2,
            .linger = 1};
        std::get<19>(cases) = {.count = 32,
            .birthRate = .25,
            .sizeVariation = 0,
            .growthVariation = 0,
            .interaction = .25,
            .damping = 2,
            .buoyancy = 0,
            .linger = 1};
        for (const Vec2 WINDOW_SIZE :
            {Vec2{.x = 320, .y = 900}, Vec2{.x = 1100, .y = 320}, Vec2{.x = 640, .y = 640}}) {
            for (const auto &params : cases) {
                Bubble bubble;
                bubble.begin(Transition::Open, BubbleDuration{1}, 42, WINDOW_SIZE, params);
                for (int i = 0; i < 99; ++i) {
                    bubble.advance(.01);
                    const auto FRAME = bubble.frame(WINDOW_SIZE);
                    std::size_t visible = 0;
                    for (const auto &film :
                        std::span(FRAME.formation).first(FRAME.formation_count)) {
                        if (film.visibility > 0) {
                            ++visible;
                        }
                        CHECK(std::isfinite(film.centre.x) && std::isfinite(film.centre.y));
                        CHECK(std::isfinite(film.radius));
                    }
                    // The film the window bubble grew from is drawn once more while it takes over.
                    CHECK(visible <= static_cast<std::size_t>(params.count) + 1);
                    CHECK(std::isfinite(FRAME.window.core.x) && std::isfinite(FRAME.window.core.y));
                }
                const auto BEFORE_END = bubble.frame(WINDOW_SIZE);
                // The exact native endpoint must not hide a failed inflation solver.
                CHECK(bubble.landed());
                CHECK(std::abs(BEFORE_END.window.half_size.x - (WINDOW_SIZE.x / 2)) <
                      .001 * WINDOW_SIZE.x);
                CHECK(std::abs(BEFORE_END.window.half_size.y - (WINDOW_SIZE.y / 2)) <
                      .001 * WINDOW_SIZE.y);
                CHECK(BEFORE_END.window.corner < .001 * std::min(WINDOW_SIZE.x, WINDOW_SIZE.y));
                bubble.advance(.01);
                CHECK(!bubble.active() && bubble.frame(WINDOW_SIZE).formation_count == 0);
            }
        }
    }

    // The final foam slot must not prevent the window bubble from emerging.
    void single_companion_cannot_block_emergence() {
        constexpr int DEADLINE_TICK = 245;  // first simulation tick at or after .68
        const BubbleFormationParams PARAMS{.count = 1, .birthRate = 2, .linger = 1};
        BubbleFormation formation;
        // Before reserving a slot, seed 255 stranded the only film away from the inlet.
        formation.begin(SIZE, 255, PARAMS);
        formation.advance(DEADLINE_TICK / 360.0, SIZE);
        BubbleFrame deadline{.size = SIZE};
        formation.writeFrame(deadline);
        // At emergence the window membrane fades in from zero; geometry, rather than
        // incidental companion films, identifies the primary at the deadline.
        CHECK(deadline.window.half_size.x > 0 && deadline.window.half_size.y > 0);

        formation.advance(.78, SIZE);
        BubbleFrame visible{.size = SIZE};
        formation.writeFrame(visible);
        CHECK(visible.window.visibility > 0 && visible.window.content > 0);
    }

    void deferred_size_preserves_seed() {
        const BubbleFormationParams PARAMS{.count = 40};
        BubbleFormation direct;
        BubbleFormation deferred;
        BubbleFormation zero_seed;
        direct.begin(SIZE, 255, PARAMS);
        deferred.begin({}, 255, PARAMS);
        zero_seed.begin(SIZE, 0, PARAMS);
        direct.advance(.2, SIZE);
        deferred.advance(.2, SIZE);
        zero_seed.advance(.2, SIZE);
        BubbleFrame expected{.size = SIZE};
        BubbleFrame actual{.size = SIZE};
        BubbleFrame other{.size = SIZE};
        direct.writeFrame(expected);
        deferred.writeFrame(actual);
        zero_seed.writeFrame(other);
        // Before emergence the inlet position is exposed as the window's carry.
        // Also show that this observation distinguishes the requested seed from zero.
        CHECK(expected.window.motion.carry.x != other.window.motion.carry.x ||
              expected.window.motion.carry.y != other.window.motion.carry.y);
        CHECK(actual.window.motion.carry.x == expected.window.motion.carry.x);
        CHECK(actual.window.motion.carry.y == expected.window.motion.carry.y);
    }

}  // namespace

int main() {
    formation_during_opening();
    formation_movement_depends_on_seed();
    pressure_changes_inflation_and_settles();
    formation_controls_keep_the_endpoint();
    single_companion_cannot_block_emergence();
    deferred_size_preserves_seed();
    return g_failures == 0 ? 0 : 1;
}
