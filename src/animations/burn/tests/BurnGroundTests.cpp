#include <algorithm>
#include <chrono>
#include <cmath>

#include "Check.hpp"
#include "animations/burn/BurnGround.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;
namespace {
    const Box SCREEN{0, 0, 960, 640};
    ScreenFacts screen(double seconds, MonitorId monitor = 1, WorkspaceId workspace = 11) {
        return {.monitor = monitor,
            .now = Clock::time_point{} + std::chrono::duration_cast<Clock::duration>(
                                             std::chrono::duration<double>(seconds)),
            .screen = monitor == 1 ? SCREEN : Box{960, 0, 960, 640},
            .scale = 1,
            .enabled = true,
            .workspace = workspace};
    }
    Step step() {
        return {.elapsed = .016,
            .capture = {400, 400, 100, 100},
            .screen = SCREEN,
            .movement_enabled = true,
            .monitor = 1,
            .workspace = 11};
    }
    const CBurnGround::Area &area(
        const CBurnGround &ground, MonitorId monitor = 1, WorkspaceId workspace = 11) {
        const auto FOUND = std::ranges::find_if(ground.areas(), [&](const auto &item) {
            return item && item->monitor == monitor && item->workspace == workspace;
        });
        CHECK(FOUND != ground.areas().end());
        return **FOUND;
    }
    const CBurnGround::Cell &cell(const CBurnGround::Area &area, Vec2 point) {
        const auto COLUMN = static_cast<std::size_t>(
            (point.x - area.screen.x) / area.screen.w * CBurnGround::WIDTH);
        const auto ROW = static_cast<std::size_t>(
            (point.y - area.screen.y) / area.screen.h * CBurnGround::HEIGHT);
        return area.cells[ROW * CBurnGround::WIDTH + COLUMN];
    }
    float total(const CBurnGround::Area &area) {
        float result = 0;
        for (const auto &cell : area.cells) result += cell.damage();
        return result;
    }
}  // namespace
int main() {
    const BurnParams params;
    {
        CBurnGround ground;
        ground.tick(screen(0));
        auto motion = step();
        motion.motion = Motion{
            .from_capture = {100, 100, 100, 100}, .capture = motion.capture, .translated = true};
        motion.motion->workspace_shifted = true;
        ground.stamp({motion.capture, 1, false, 17}, params, motion, false);
        CHECK(!ground.active(1));  // Switching desktops did not burn a path across them.
        motion.motion->workspace_shifted = false;
        ground.stamp({motion.capture, 1, false, 17}, params, motion, false);
        const auto &field = area(ground);
        CHECK(cell(field, {255, 255}).damage() > 0);
        CHECK(
            cell(field, {105, 455}).damage() == 0);  // Not the diagonal sweep's bounding rectangle.
        CHECK(field.opening_count == 0);  // A pure move is never an opening.
        CHECK(field.particle_count > 0);
        CHECK(field.particle_count <= CBurnGround::MAX_PARTICLES);
        ground.tick(screen(0));
        const float INITIAL_HEAT = cell(field, {255, 255}).heat();
        ground.tick(
            screen(.5, 1, 12));  // A hidden workspace ages independently; it is not discarded.
        CHECK(cell(field, {255, 255}).heat() < INITIAL_HEAT);
        CHECK(cell(field, {255, 255}).damage() > 0);
        ground.cancel(17);  // Window interruption must not erase deposited movement history.
        CHECK(cell(field, {255, 255}).damage() > 0);
        ground.tick(screen(1.5));
        CHECK(cell(field, {255, 255}).damage() > 0);
        ground.tick(screen(3.3));
        CHECK(!ground.active(
            1));  // Includes the last erase transition: tick must observe before/after.
        // Fresh cells have different force, size, and lifetime samples, not identical round motes.
        CBurnGround slow;
        auto gentle = step();
        gentle.motion = Motion{.from_capture = {100, 100, 100, 100},
            .capture = {101, 100, 100, 100},
            .translated = true};
        for (int frame = 0; frame < 512; ++frame) {
            slow.stamp({gentle.capture, 1, false, 71}, params, gentle, false);
        }
        const auto &flecks = area(slow);
        CHECK(flecks.particle_count >
              1);  // Every individual emission has a fractional expected count.
        CHECK(flecks.particle_count <= CBurnGround::MAX_PARTICLES);
        if (flecks.particle_count > 1) {
            CHECK(flecks.particles[0].size != flecks.particles[1].size);
            CHECK(flecks.particles[0].gravity != flecks.particles[1].gravity);
        }
    }
    {
        CBurnGround ground;
        auto opening = step();
        opening.movement_enabled = false;
        ground.stamp({{100, 100, 300, 300}, .45, false, 27}, params, opening);
        CHECK(area(ground).opening_count == 1);
        CHECK(total(area(ground)) ==
              0);  // Exact output-resolution front, never rasterized into coarse cells.
        CHECK(area(ground).openings[0].front.progress == .45);
        ground.cancel(27);
        CHECK(area(ground).opening_count == 0);
        CHECK(ground.active(1));  // Erasing a transient rim still requests one final damaged frame.
        ground.tick(screen(0));
        CHECK(!ground.active(1));
    }
    {
        CBurnGround ground;
        ground.tick(screen(0));
        auto closing = step();
        closing.movement_enabled = false;
        closing.dying = true;
        const Box CAPTURE{100, 100, 400, 400};
        ground.stamp({CAPTURE, .6, true, 31}, params, closing);
        ground.tick(screen(0));
        CHECK(cell(area(ground), {105, 105}).damage() > 0);
        CHECK(cell(area(ground), {295, 295}).damage() == 0);
        ground.tick(screen(.4));
        const double EDGE_AGE = cell(area(ground), {105, 105}).age;
        ground.stamp({CAPTURE, 0, true, 31}, params, closing);
        CHECK(cell(area(ground), {295, 295}).damage() > 0);
        CHECK(cell(area(ground), {105, 105}).age ==
              EDGE_AGE);  // The advancing front never reheats old rim.
        const float FINAL_SCORCH = total(area(ground));
        ground.cancel(31);
        CHECK(total(area(ground)) == FINAL_SCORCH);
        ground.tick(screen(4));
        CHECK(!ground.active(1));
    }
    {
        CBurnGround ground;
        ground.tick(screen(0));
        ground.tick(screen(0, 2, 22));
        auto closing = step();
        closing.movement_enabled = false;
        closing.dying = true;
        ground.stamp({{900, 100, 160, 300}, 0, true, 41}, params, closing);
        CHECK(total(area(ground, 1, 11)) > 0);
        CHECK(total(area(ground, 2, 22)) >
              0);  // Same front interval replicated to each overlapping output.
        CHECK(!ground.active(3));
        ground.clear(2);
        ground.tick(screen(0, 2, 22));
        CHECK(!ground.active(2));
        CHECK(ground.active(1));
    }
    {
        CBurnGround ground;
        auto hidden = step();
        hidden.workspace_visible = false;
        ground.stamp({hidden.capture, 0, true, 51}, params, hidden);
        CHECK(!ground.active(1));
        hidden.workspace_visible = true;
        hidden.workspace_moving = true;
        ground.stamp({hidden.capture, 0, true, 51}, params, hidden);
        CHECK(!ground.active(1));
        hidden.workspace_moving = false;
        hidden.stalled = true;
        ground.stamp({hidden.capture, 0, true, 51}, params, hidden);
        CHECK(!ground.active(1));
    }
    return g_failures == 0 ? 0 : 1;
}
