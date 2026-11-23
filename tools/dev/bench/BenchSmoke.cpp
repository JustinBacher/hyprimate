// Smoke model workloads. The GPU fluid is not exercised here, only the CPU drive state.
#include "Bench.hpp"
#include "window/smoke/Smoke.hpp"

namespace {
    using namespace hyprimate;
    constexpr double FRAME = 1.0 / 60.0;
    constexpr Vec2 SIZE{.x = 800, .y = 500};

    double smoke_transition(Transition kind) {
        Smoke smoke;
        smoke.seed(42);
        smoke.begin(kind, 1.0, SIZE);
        double sum = 0;
        for (int i = 0; i < 70; ++i) {
            smoke.advance(FRAME, SIZE);
            const SmokeFrame OUT = smoke.frame(SIZE);
            sum += OUT.smoke + OUT.head.x + smoke.bounds(SIZE).w;
        }
        return sum;
    }

    double smoke_movement() {
        Smoke smoke;
        smoke.seed(17);
        double sum = 0;
        for (int i = 0; i < 240; ++i) {
            const double SPEED = i < 60 ? 900.0 : 0.0;
            smoke.drive({.x = SPEED * FRAME, .y = 0.2 * SPEED * FRAME});
            smoke.advance(FRAME, SIZE);
            const SmokeFrame OUT = smoke.frame(SIZE);
            sum += OUT.smoke + OUT.mass + OUT.position.x;
        }
        return sum;
    }
}  // namespace

namespace bench {
    void register_smoke(Registry &registry) {
        registry.push_back({"smoke.open", "Smoke open drive state + frame()", 70, [] { return smoke_transition(Transition::Open); }});
        registry.push_back({"smoke.close", "Smoke close drive state + frame()", 70, [] { return smoke_transition(Transition::Close); }});
        registry.push_back({"smoke.move_settle", "Smoke drag 1 s then settle", 240, smoke_movement});
    }
}  // namespace bench
