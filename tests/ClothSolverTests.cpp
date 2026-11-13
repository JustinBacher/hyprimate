#include "Check.hpp"
#include "ClothSolver.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace liquid_glass;
namespace {
constexpr int    CELLS = 16;
constexpr double DT = 1.0 / 120;
double           distance(Vec3 a, Vec3 b) { return std::hypot(std::hypot(a.x - b.x, a.y - b.y), a.z - b.z); }

// Towed to the right through gusty air while hanging from the middle of its top edge.
ClothSolver::Forces towed() {
    ClothSolver::Forces f;
    f.acceleration = {0, -30, 0};
    f.wind = {-2, 0, 0};
    f.drag = 9;
    f.gust = .4;
    return f;
}

void restAndDeterminism() {
    ClothSolver still(CELLS, {1, 1}, {.5, 0});
    for (int step = 0; step < 120; ++step)
        still.step(DT, {});
    ClothSolver a(CELLS, {1, 1}, {.5, 0}), b(CELLS, {1, 1}, {.5, 0});
    auto        forces = towed();
    for (int step = 0; step < 120; ++step) {
        forces.airOffset.x += 2 * DT;
        forces.phase += DT;
        a.step(DT, forces);
        b.step(DT, forces);
    }
    for (int y = 0; y <= CELLS; ++y)
        for (int x = 0; x <= CELLS; ++x) {
            CHECK(distance(still.particle(x, y).position, still.particle(x, y).rest) == 0);
            const auto p = a.particle(x, y).position, q = b.particle(x, y).position;
            CHECK(p.x == q.x && p.y == q.y && p.z == q.z);
            CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        }
}

// Only the pinch holds the sheet, and the weave does not stretch however hard it is pulled.
void pinAndWeave() {
    for (const Vec2 size : {Vec2{1, 1}, Vec2{1.2, 1 / 1.2}}) {
        ClothSolver sheet(CELLS, size, {.5, 0});
        CHECK(sheet.particle(CELLS / 2, 0).pinned && !sheet.particle(0, 0).pinned && !sheet.particle(CELLS, 0).pinned);
        auto forces = towed();
        forces.acceleration = {60, -90, 20};
        double stretch = 0;
        for (int step = 0; step < 180; ++step) {
            sheet.step(DT, forces);
            for (int y = 0; y <= CELLS; ++y)
                for (int x = 0; x <= CELLS; ++x) {
                    const auto &p = sheet.particle(x, y);
                    if (p.pinned)
                        CHECK(distance(p.position, p.rest) == 0);
                    for (const auto &[dx, dy] : {std::pair{1, 0}, std::pair{0, 1}})
                        if (x + dx <= CELLS && y + dy <= CELLS) {
                            const auto &q = sheet.particle(x + dx, y + dy);
                            stretch = std::max(stretch, distance(p.position, q.position) / distance(p.rest, q.rest));
                        }
                }
        }
        CHECK(stretch < 1.10); // at three times the weight a moving window puts on it
    }
}

// The free sheet streams behind its pinch and folds out of its plane; nothing confines it to one side.
void trailsAndFolds() {
    ClothSolver sheet(CELLS, {1, 1}, {.5, 0});
    auto        forces = towed();
    double      front = 0, back = 0;
    for (int step = 0; step < 180; ++step) {
        forces.airOffset.x += 2 * DT;
        forces.phase += DT;
        sheet.step(DT, forces);
        for (int y = 0; y <= CELLS; ++y)
            for (int x = 0; x <= CELLS; ++x) {
                front = std::max(front, sheet.particle(x, y).position.z);
                back = std::min(back, sheet.particle(x, y).position.z);
            }
    }
    double centre = 0;
    for (int y = 0; y <= CELLS; ++y)
        for (int x = 0; x <= CELLS; ++x)
            centre += sheet.particle(x, y).position.x / ((CELLS + 1) * (CELLS + 1));
    CHECK(centre < -.1);
    CHECK(front > .03 && back < -.03);
    // The hold eases it back towards a window, without a spring to bounce on.
    const auto spread = [&] {
        double sum = 0;
        for (int y = 0; y <= CELLS; ++y)
            for (int x = 0; x <= CELLS; ++x)
                sum += distance(sheet.particle(x, y).position, sheet.particle(x, y).rest);
        return sum;
    };
    const double loose = spread();
    forces = {};
    forces.hold = 8;
    forces.damping = 4;
    double previous = loose;
    for (int step = 0; step < 120; ++step) {
        sheet.step(DT, forces);
        if (step > 30)
            CHECK(spread() <= previous);
        previous = spread();
    }
    CHECK(previous < .2 * loose);
}

// A resize carries the pose with it and leaves a sheet that still rests flat at its new proportions.
void resize() {
    ClothSolver sheet(CELLS, {1, 1}, {0, 0});
    sheet.resize({1.25, .8});
    for (int step = 0; step < 30; ++step)
        sheet.step(DT, {});
    const auto corner = sheet.particle(CELLS, CELLS).position;
    CHECK(std::abs(corner.x - .625) < 1e-9 && std::abs(corner.y + .8) < 1e-9 && corner.z == 0);
}
} // namespace

int main() {
    restAndDeterminism();
    pinAndWeave();
    trailsAndFolds();
    resize();
    return g_failures == 0 ? 0 : 1;
}
