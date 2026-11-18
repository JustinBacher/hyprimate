// Cloth (Jolt) and swarm workloads. Cloth recording runs on a worker thread, so its wall time is
// dominated by the simulation and is reported as threaded.
#include <memory>

#include "Bench.hpp"
#include "animations/cloth/ClothFlutter.hpp"
#include "animations/swarm/Swarm.hpp"

namespace {
    using namespace hyprimate;
    constexpr double FRAME = 1.0 / 60.0;
    constexpr Box SCREEN{.x = 0, .y = 0, .w = 1920, .h = 1080};
    constexpr Box WINDOW{.x = 400, .y = 200, .w = 900, .h = 600};

    ClothPhysics &physics() {
        static ClothPhysics instance;
        return instance;
    }

    // Open recording: begin() until the immutable recording is complete.
    double cloth_record() {
        physics();
        ClothFlutter cloth;
        cloth.begin(Transition::Open, {}, 7, WINDOW, {});
        bench::keep(cloth.wait_for_recording(60.0));
        return cloth.active() ? 1.0 : 0.0;
    }

    // Playback of a finished recording: what the compositor thread pays per frame.
    double cloth_playback() {
        physics();
        static ClothFlutter recorded = [] {
            ClothFlutter cloth;
            cloth.begin(Transition::Open, {}, 7, WINDOW, {});
            cloth.wait_for_recording(60.0);
            return cloth;
        }();
        ClothFlutter cloth = recorded;
        SheetFrame out;
        double sum = 0;
        for (int i = 0; i < 90 && cloth.advance(FRAME); ++i) {
            cloth.frame({.x = 940, .y = 640}, out);
            sum += out.position[200].x + static_cast<double>(out.order[1]);
        }
        return sum;
    }

    SwarmPlacement placement(double shift) {
        return {.capture = {.x = 380 + shift, .y = 180, .w = 940, .h = 640},
            .body = {.x = 400 + shift, .y = 200, .w = 900, .h = 600},
            .screen = SCREEN};
    }

    double swarm_open() {
        Swarm swarm;
        EdgeSet sides;
        sides.add(Edge::Left);
        sides.add(Edge::Bottom);
        swarm.begin(Transition::Open, {}, 11, placement(0), sides);
        double sum = 0;
        for (int i = 0; i < 120 && swarm.advance(1.0 / 144.0); ++i) {
            sum += swarm.bounds().w + swarm.rounding();
        }
        return sum;
    }

    double swarm_movement() {
        Swarm swarm;
        swarm.begin_movement({}, 11, placement(0));
        double sum = 0;
        for (int i = 0; i < 240; ++i) {
            if (i < 60) {
                swarm.update_movement(placement(i * 15.0));
            }
            swarm.advance(1.0 / 144.0, i < 60);
            sum += swarm.agents()[10].home.x + swarm.rounding();
        }
        return sum;
    }
}  // namespace

namespace bench {
    void register_sheets(Registry &registry) {
        registry.push_back({"cloth.record_open", "Jolt cloth open recording (worker thread, wall time)", 1, cloth_record, true});
        registry.push_back({"cloth.playback", "Cloth recording playback + frame()", 90, cloth_playback});
        registry.push_back({"swarm.open", "Swarm 250 agents opening", 120, swarm_open});
        registry.push_back({"swarm.move_settle", "Swarm 250 agents drag then settle", 240, swarm_movement});
    }
}  // namespace bench
