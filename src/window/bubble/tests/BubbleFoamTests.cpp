#include <cmath>
#include <cstddef>
#include <stdexcept>

#include "Check.hpp"
#include "window/bubble/BubbleFoam.hpp"

namespace {
    using namespace hyprimate;
    void foam_rejects_invalid_slots() {
        BubbleFoam foam;
        foam.reset(3, {}, 2);
        const int MISSING = foam.nearest({}, 1);
        CHECK(MISSING == -1);
        const int FIRST = foam.spawn({}, {}, .05, 600, 1, 10);
        CHECK(FIRST == 0);
        const int SECOND = foam.spawn({.x = 1, .y = 0}, {}, .03, 600, 1, 10);
        CHECK(SECOND == 1);
        const int FULL = foam.spawn({}, {}, .05, 600, 1, 10);
        CHECK(FULL == -1);

        const BubbleFoam &view = foam;
        for (const int slot : {MISSING, FULL, -3, 2, static_cast<int>(BUBBLE_FOAM_CAPACITY)}) {
            bool mutable_threw = false;
            bool const_threw = false;
            try {
                (void)foam.film(slot);
            } catch (const std::out_of_range &) {
                mutable_threw = true;
            }
            try {
                (void)view.film(slot);
            } catch (const std::out_of_range &) {
                const_threw = true;
            }
            CHECK(mutable_threw && const_threw);
        }
        foam.film(FIRST).fed = true;
        CHECK(view.film(FIRST).live && view.film(FIRST).fed);
        CHECK(view.film(SECOND).live);

        // The mutable film API also permits an invalid recipient; it must not index outside the
        // budget.
        for (const int recipient : {-3, 2, static_cast<int>(BUBBLE_FOAM_CAPACITY)}) {
            foam.film(FIRST).into = recipient;
            foam.step(.001, {}, nullptr);
            CHECK(view.film(FIRST).live && view.film(FIRST).into == -1);
            CHECK(view.film(SECOND).live);
        }
        foam.take(SECOND);
        CHECK(!view.film(SECOND).live);
        foam.reset(4, {}, 1);
        bool stale_threw = false;
        try {
            (void)view.film(SECOND);
        } catch (const std::out_of_range &) {
            stale_threw = true;
        }
        CHECK(stale_threw);
    }

    // Two pressed films coalesce through a neck: the smaller empties into the larger and their
    // air, a volume, is conserved; nothing is merged that never touched.
    void foam_coalescence_conserves_volume() {
        BubbleFoam foam;
        foam.reset(3, {}, 4);
        const int FIRST = foam.spawn({.x = 0, .y = 0}, {}, .05, 600, 1, 10);
        const int SECOND = foam.spawn({.x = .075, .y = 0}, {}, .03, 600, 1, 10);
        const int FAR = foam.spawn({.x = .6, .y = 0}, {}, .03, 600, 1, 10);
        const double VOLUME = std::pow(.05, 3) + std::pow(.03, 3);
        const BubbleFoamFlow STILL;
        for (int i = 0; i < 360 && foam.film(SECOND).live; ++i) {
            foam.step(1.0 / 360, STILL, nullptr);
        }
        CHECK(foam.film(FIRST).live && !foam.film(SECOND).live);
        CHECK(std::abs(std::pow(foam.film(FIRST).radius, 3) - VOLUME) < VOLUME * .02);
        CHECK(foam.film(FAR).live && std::abs(foam.film(FAR).radius - .03) < 1e-9);
    }

    // Drained films rupture on their own schedule rather than all at once.
    void foam_films_rupture_independently() {
        BubbleFoam foam;
        foam.reset(9, {}, 8);
        for (int i = 0; i < 8; ++i) {
            foam.spawn({.x = i * .3, .y = 0}, {}, .02, 600, 1, 10);
        }
        foam.expire(.5);
        const BubbleFoamFlow STILL;
        std::size_t previous = foam.live_count();
        std::size_t distinct = 0;
        for (int i = 0; i < 300; ++i) {
            foam.step(1.0 / 360, STILL, nullptr);
            if (foam.live_count() != previous) {
                ++distinct;
            }
            CHECK(previous - foam.live_count() <= 2);
            previous = foam.live_count();
        }
        CHECK(previous == 0 && distinct >= 4);
    }
}  // namespace

int main() {
    foam_coalescence_conserves_volume();
    foam_rejects_invalid_slots();
    foam_films_rupture_independently();
    return g_failures == 0 ? 0 : 1;
}
