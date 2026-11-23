#include <array>
#include <memory>
#include <stdexcept>

#include "Bench.hpp"
#include "window/WindowAnimationFacts.hpp"
#include "window/construction/ConstructionMotionSite.hpp"
#include "window/construction/ConstructionSite.hpp"
#include "window/construction/studies/RegionalClose.hpp"

namespace {
using namespace hyprimate;
constexpr Box BODY{240, 150, 1280, 720};
constexpr Box CAPTURE{238, 148, 1284, 724};
constexpr Box OUTPUT{0, 0, 1920, 1080};
constexpr double DURATION = .62;
constexpr unsigned HZ = 120;
constexpr unsigned TICKS = 75; // Includes the first tick past 620 ms.

// Observe the published data as well as its size: a pose that stops updating must change the checksum.
double sample(const ConstructionBatch &batch, Box bounds) {
    double sum = bounds.w + bounds.h + static_cast<double>(batch.count);
    if (batch.count != 0) {
        const auto &q = batch.quads[batch.count / 2];
        sum += q.origin[0] + q.origin[1] + q.origin[2] + q.u[1] + q.v[0] + q.uv[0];
    }
    bench::keep(batch);
    return sum;
}

double opening(bool admission_only) {
    auto site = std::make_unique<ConstructionSite>();
    site->build(BODY, CAPTURE, OUTPUT, 37, DURATION);
    if (!site->active()) { throw std::runtime_error("Construction benchmark did not acquire window"); }
    double sum = sample(site->batch(), site->bounds());
    if (admission_only) { return sum; }
    animation::Step step{};
    step.elapsed = 1.0 / HZ;
    step.body = step.goal_body = BODY;
    step.capture = step.rest_capture = CAPTURE;
    step.work_area = step.screen = OUTPUT;
    step.open_enabled = true;
    for (unsigned i = 0; i < TICKS; ++i) {
        site->step(step);
        if (site->batch().overflow || site->active() != (i + 1 < TICKS)) {
            throw std::runtime_error("Construction benchmark lost its active publication");
        }
        sum += sample(site->batch(), site->bounds());
    }
    return sum;
}

using Mode = construction::TransportVariant;
constexpr unsigned DRAG_TICKS = 96;

template <std::size_t N>
double drag(const std::array<Mode, N> &modes, bool resize = false) {
    std::array<std::unique_ptr<construction::MotionSite>, N> sites;
    for (std::size_t i = 0; i < N; ++i) sites[i] = std::make_unique<construction::MotionSite>(37 + i, modes[i]);
    Box previous = BODY, capture = CAPTURE;
    double sum = 0;
    for (unsigned t = 0; t < DRAG_TICKS; ++t) {
        auto body = previous, next = capture;
        const double delta = t < DRAG_TICKS / 2 ? 3 : -3;
        if (resize) { body.w += delta; next.w += delta; }
        else { body.x += delta; next.x += delta; }
        animation::Step step{};
        step.elapsed = t ? 1.0 / HZ : 0;
        step.body = step.goal_body = body;
        step.capture = step.rest_capture = next;
        step.visible_outputs = {&OUTPUT, 1};
        step.work_area = step.screen = OUTPUT;
        step.held = !resize;
        step.resize_held = resize;
        step.movement_enabled = true;
        step.motion = animation::Motion{.from_body=previous,.from_capture=capture,.body=body,.capture=next,
            .translated=!resize,.resized=resize,.capture_changed=true,.sample_seconds=1.0/HZ};
        for (auto &site : sites) {
            const auto frame = site->advance(step);
            if (!frame.running || site->batch().overflow) throw std::runtime_error("lost Construction drag");
            sum += sample(site->batch(), frame.bounds);
        }
        previous = body;
        capture = next;
    }
    return sum;
}

double collapse() {
    using Model = construction::studies::RegionalClose;
    const Box body{0, 0, BODY.w, BODY.h};
    const auto face = Model::VisibleFace::flat(body);
    Model model;
    auto frame = std::make_unique_for_overwrite<Model::Frame>();
    if (!model.start(body, {&face, 1}, 37, 1.0)) throw std::runtime_error("lost Construction close");
    double sum = 0;
    for (unsigned i = 0; i < HZ; ++i) {
        model.emit({.crop=body, .authorized=true}, *frame);
        const auto vertices = frame->source_vertices();
        sum += static_cast<double>(frame->source_faces().size());
        if (!vertices.empty()) sum += vertices[vertices.size()/2].position.y;
        bench::keep(*frame);
        model.advance(1.0/HZ);
    }
    return sum;
}
} // namespace

namespace bench {
void register_construction(Registry &registry) {
    registry.push_back({"construction.admit", "1280x720 site allocation, layout and initial publication", 1,
        [] { return opening(true); }});
    registry.push_back({"construction.build", "1280x720 opening, admission plus 75 model ticks at 120 Hz", 1 + TICKS,
        [] { return opening(false); }});
    registry.push_back({"construction.carry", "Held intact carry with direction reversal", DRAG_TICKS,
        [] { return drag(std::array{Mode::CrewCarry}); }});
    registry.push_back({"construction.crane", "Held crane transport with direction reversal", DRAG_TICKS,
        [] { return drag(std::array{Mode::Crane}); }});
    registry.push_back({"construction.piece_by_piece", "Concurrent trips to a changing destination", DRAG_TICKS,
        [] { return drag(std::array{Mode::PieceByPiece}); }});
    registry.push_back({"construction.resize_reverse", "Held intact resize, grow then shrink", DRAG_TICKS,
        [] { return drag(std::array{Mode::CrewCarry}, true); }});
    registry.push_back({"construction.demolish", "Connected regional deformation, including geometry emission", HZ, collapse});
    registry.push_back({"construction.four_sites", "Four concurrent moving sites per tick", DRAG_TICKS,
        [] { return drag(std::array{Mode::Crane,Mode::CrewCarry,Mode::PieceByPiece,Mode::PieceByPiece}); }});
}
} // namespace bench
