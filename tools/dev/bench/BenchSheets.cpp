// Cloth and swarm workloads. Cloth recording runs on a worker thread, so its wall time is
// dominated by the simulation and is reported as threaded.
#include <cmath>
#include <memory>
#include <stdexcept>

#include "Bench.hpp"
#include "window/cloth/ClothFlutter.hpp"
#include "window/shared/SheetMesh.hpp"
#include "window/swarm/Swarm.hpp"

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

    // Project once, then measure only construction into reused compositor-style buffers.
    double cloth_unshaded_mesh() {
        static const SheetFrame frame = [] {
            SheetGrid world{};
            for (std::size_t vertex = 0; vertex < world.size(); ++vertex) {
                const auto uv = sheet_uv(vertex);
                world[vertex] = {.x = (uv.x - 0.5) * WINDOW.w,
                    .y = (uv.y - 0.5) * WINDOW.h,
                    .z = (uv.x - 0.5) * (uv.y - 0.5) * 100};
            }
            SheetFrame projected;
            project_sheet(world, {WINDOW.w, WINDOW.h}, 0, projected);
            return projected;
        }();
        static animation::MeshBuffers<animation::MeshVertex> buffers;
        const animation::MeshTarget<animation::MeshVertex> target{
            .source = WINDOW, .output = SCREEN,
            .image_origin = {WINDOW.x, WINDOW.y}, .image_size = {WINDOW.w, WINDOW.h},
            .scale = 1, .buffers = &buffers};
        double sum = 0;
        for (int i = 0; i < 90; ++i) {
            const auto mesh = fill_sheet_mesh(frame, target, false, 1.F);
            bench::keep(mesh.window->data());
            sum += (*mesh.window)[static_cast<std::size_t>(i)].x;
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
    double swarm_sprint() {
        constexpr int FRAMES = 5;
        constexpr int MAX_STEPS = 30;
        constexpr std::uint64_t COUNT_BASE = MAX_STEPS + 1;
        SwarmParams params;
        params.count = SWARM_MAX_AGENTS;
        params.speed = 100.0;
        EdgeSet sides;
        sides.add(Edge::Left);
        Swarm swarm;
        swarm.begin(Transition::Open, params, 11, placement(0), sides, 1000.0);

        // Base-31 digits preserve the directly decodable actual step count for every frame.
        std::uint64_t checksum = 0;
        for (int frame = 0; frame < FRAMES; ++frame) {
            const double BEFORE = swarm.agents().front().due;
            const bool ACTIVE = swarm.advance(0.2);
            const double ELAPSED = BEFORE - swarm.agents().front().due;
            const int STEPS = static_cast<int>(std::lround(ELAPSED * 120.0));
            if (STEPS < 0 || STEPS > MAX_STEPS || (frame < FRAMES - 1 && STEPS != MAX_STEPS) ||
                (frame == FRAMES - 1 && STEPS != 0) ||
                (ACTIVE != (frame < FRAMES - 1))) {
                throw std::runtime_error("swarm.sprint: unexpected substep count or transition state");
            }
            checksum = (checksum * COUNT_BASE) + static_cast<std::uint64_t>(STEPS);
        }
        return static_cast<double>(checksum);
    }
}  // namespace

namespace bench {
    void register_sheets(Registry &registry) {
        registry.push_back({"cloth.record_open", "Cloth open recording (worker thread, wall time)", 1, cloth_record, true});
        registry.push_back({"cloth.playback", "Cloth recording playback + frame()", 90, cloth_playback});
        registry.push_back({"cloth.unshaded_mesh", "Unshaded mesh projection into reusable buffers", 90, cloth_unshaded_mesh});
        registry.push_back({"swarm.open", "Swarm 250 agents opening", 120, swarm_open});
        registry.push_back({"swarm.move_settle", "Swarm 250 agents drag then settle", 240, swarm_movement});
        registry.push_back({"swarm.sprint", "Swarm 400 agents with timed catch-up capped at 30 steps/frame", 5, swarm_sprint});
    }
}  // namespace bench
