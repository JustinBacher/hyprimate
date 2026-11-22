#include <memory>
#include <stdexcept>

#include "Bench.hpp"
#include "WindowAnimationFacts.hpp"
#include "animations/construction/ConstructionSite.hpp"

namespace {
using namespace hyprimate;
constexpr Box BODY{240, 150, 1280, 720};
constexpr Box CAPTURE{238, 148, 1284, 724};
constexpr Box OUTPUT{0, 0, 1920, 1080};
constexpr double DURATION = .62;
constexpr unsigned HZ = 120;
constexpr unsigned TICKS = 75; // Includes the first tick past 620 ms.

// Observe the published data as well as its size: a pose that stops updating must change the checksum.
double sample(const ConstructionSite &site) {
    const auto &batch = site.batch();
    const auto bounds = site.bounds();
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
    double sum = sample(*site);
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
        sum += sample(*site);
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
}
} // namespace bench
