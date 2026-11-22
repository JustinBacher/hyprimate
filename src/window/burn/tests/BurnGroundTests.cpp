#include <algorithm>
#include <chrono>
#include <cmath>
#include <span>

#include "Check.hpp"
#include "window/burn/BurnGround.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;
using Particle = CBurnGround::Particle;
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
    void move(CBurnGround &ground,
        Box from,
        Box to,
        const BurnParams &params,
        std::uint64_t seed,
        WindowId window = 0,
        double sample_seconds = .016) {
        auto frame = step();
        frame.elapsed = 0;  // A real movement sample can acquire style on this frame.
        frame.window = window;
        frame.capture = to;
        frame.motion = Motion{.from_capture = from,
            .capture = to,
            .translated = from.x != to.x || from.y != to.y,
            .resized = from.w != to.w || from.h != to.h,
            .sample_seconds = sample_seconds};
        ground.stamp({to, 1, false, seed}, params, frame, false);
    }
    std::span<const Particle> particles(const CBurnGround::Area &field) {
        return {field.particles.data(), field.particle_count};
    }
    const Particle *find_particle(const CBurnGround::Area &field, const Particle &sample) {
        const auto LIVE = particles(field);
        const auto FOUND = std::ranges::find_if(LIVE, [&](const Particle &p) {
            return p.owner == sample.owner && p.kind == sample.kind &&
                   p.source.x == sample.source.x && p.source.y == sample.source.y &&
                   p.shape == sample.shape;
        });
        return FOUND == LIVE.end() ? nullptr : &*FOUND;
    }
    BurnParams debris_settings() {
        BurnParams result;
        result.fragment_density = 2;
        result.ember_density = 1;
        result.smoke_density = 1;
        result.wind_strength = 0;
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
        CHECK(cell(field, {255, 255}).movement_damage() > 0);
        CHECK(cell(field, {105, 455}).movement_damage() == 0);  // Outside the diagonal sweep.
        CHECK(cell(field, {455, 455}).movement_damage() == 0);  // Still under the moving window.
        CHECK(total(field) == 0);  // Movement never writes the closing scorch.
        CHECK(field.opening_count == 0);  // A pure move is never an opening.
        CHECK(field.particle_count > 0);
        CHECK(field.particle_count <= CBurnGround::MAX_PARTICLES);
        ground.tick(screen(0));
        const float INITIAL_HEAT = cell(field, {255, 255}).movement_heat();
        ground.tick(screen(.5, 1, 12));  // A hidden workspace ages without being discarded.
        CHECK(cell(field, {255, 255}).movement_heat() < INITIAL_HEAT);
        CHECK(cell(field, {255, 255}).movement_damage() > 0);
        ground.cancel(17);  // Interruption leaves deposited movement history.
        CHECK(cell(field, {255, 255}).movement_damage() > 0);
        ground.tick(screen(1.5));
        CHECK(cell(field, {255, 255}).movement_damage() > 0);
        ground.tick(screen(3.3));
        CHECK(!ground.active(
            1));  // Includes the last erase transition: tick must observe before/after.
    }
    {
        CBurnGround ground;
        ground.tick(screen(0));
        const BurnParams debris = debris_settings();
        const Box FROM{100.5, 70, 180, 400}, TO{101, 70, 180, 400};
        move(ground, FROM, TO, debris, 71);
        const auto &field = area(ground);
        const float INITIAL = cell(field, {105, 215}).movement_damage();
        CHECK(INITIAL > 0);  // A subpixel exposure need not reach the cell center.
        CHECK(INITIAL < .5);  // Nor should half a pixel get a full-strength char.
        CHECK(!particles(field).empty());
        const std::size_t COUNT = field.particle_count;
        ground.tick(screen(.05));
        const float COOLED = cell(field, {105, 215}).movement_heat();
        for (int index = 0; index < 64; ++index) move(ground, FROM, TO, debris, 71);
        CHECK(field.particle_count == COUNT);
        CHECK(cell(field, {105, 215}).movement_heat() == COOLED);
        CHECK(cell(field, {105, 215}).movement_damage() == INITIAL);

        move(ground, TO, {110, 70, 180, 400}, debris, 71);
        CHECK(cell(field, {105, 215}).movement_damage() > INITIAL);
        CHECK(cell(field, {165, 215}).movement_damage() == 0);
        CHECK(field.particle_count <= CBurnGround::MAX_PARTICLES);
    }
    {
        // Paper sources lie in the wallpaper revealed by the trailing edge, not
        // in the current client box. With no wind they fly away from that edge.
        const BurnParams debris = debris_settings();
        for (const bool RIGHT : {true, false}) {
            CBurnGround ground;
            ground.tick(screen(0));
            const Box FROM{RIGHT ? 170. : 260., 100, 160, 350};
            const Box TO{RIGHT ? 260. : 170., 100, 160, 350};
            move(ground, FROM, TO, debris, 73);
            const auto &field = area(ground);
            const auto LIVE = particles(field);
            const auto paper = std::ranges::find(LIVE, Particle::Kind::Paper, &Particle::kind);
            CHECK(paper != LIVE.end());
            CHECK(std::ranges::count(LIVE, Particle::Kind::Smoke, &Particle::kind) > 0);
            CHECK(std::ranges::count(LIVE, Particle::Kind::Ember, &Particle::kind) > 0);
            CHECK(std::ranges::count(LIVE, Particle::Kind::Paper, &Particle::kind) >
                  std::ranges::count(LIVE, Particle::Kind::Smoke, &Particle::kind));
            for (const auto &p : LIVE) {
                CHECK(p.source.x >= (RIGHT ? 170 : 330));
                CHECK(p.source.x <= (RIGHT ? 260 : 420));
                CHECK(p.source.y >= 100 && p.source.y <= 450);
                CHECK(p.size > 0 && p.age == 0 && p.lifetime > .3);
                CHECK(p.fold >= -1 && p.fold <= 1);
                CHECK(p.char_depth >= 0 && p.char_depth <= 1);
                CHECK(p.shape >= 0 && p.shape <= 1);
            }
            if (paper == LIVE.end()) continue;
            const Particle INITIAL = *paper;
            ground.tick(screen(.3));  // A stopped window's last clinging fragments still peel.
            const auto *in_flight = find_particle(field, INITIAL);
            CHECK(in_flight && (RIGHT ? in_flight->position.x < INITIAL.position.x
                                      : in_flight->position.x > INITIAL.position.x));
            CHECK(in_flight && in_flight->angle != INITIAL.angle);
            ground.tick(screen(4));
            CHECK(!ground.active(1));
        }
    }
    {
        // Identical fresh strips inherit different signed velocity from the
        // observation interval, even though all three acquire at elapsed=0.
        const BurnParams debris = debris_settings();
        for (const bool RIGHT : {true, false}) {
            CBurnGround slow, fast, subdivided;
            const Box FROM{RIGHT ? 180. : 204., 60, 180, 400};
            const Box TO{RIGHT ? 204. : 180., 60, 180, 400};
            slow.tick(screen(0));
            fast.tick(screen(0));
            subdivided.tick(screen(0));
            move(slow, FROM, TO, debris, 81, 0, .18);
            move(fast, FROM, TO, debris, 81, 0, .01);
            move(subdivided, FROM, TO, debris, 81, 0, .01);
            const auto SLOW = particles(area(slow));
            const auto paper = std::ranges::find(SLOW, Particle::Kind::Paper, &Particle::kind);
            CHECK(paper != SLOW.end());
            if (paper == SLOW.end()) continue;
            const auto *faster = find_particle(area(fast), *paper);
            CHECK(faster && (RIGHT ? faster->velocity.x > paper->velocity.x
                                   : faster->velocity.x < paper->velocity.x));

            // Drag and flutter integrate consistently across partial ticks,
            // including a tick that crosses the last attachment instant.
            fast.tick(screen(.3));
            for (int i = 1; i <= 15; ++i) subdivided.tick(screen(i * .02));
            const auto *single = find_particle(area(fast), *paper);
            const auto *split = find_particle(area(subdivided), *paper);
            CHECK(single && split);
            if (single && split) {
                CHECK(std::abs(single->position.x - split->position.x) < .00001);
                CHECK(std::abs(single->position.y - split->position.y) < .00001);
                CHECK(std::abs(single->angle - split->angle) < .00001);
                CHECK(std::abs(single->fold - split->fold) < .00001);
            }
        }
    }
    {
        // Observe a clinging flake by its edge-following motion, not by an
        // implementation flag. Its source stays fixed through reversal/cancel.
        CBurnGround ground;
        ground.tick(screen(0));
        const BurnParams debris = debris_settings();
        const Box UPPER{160, 30, 150, 275}, LOWER{160, 335, 150, 275};
        const auto translated = [](Box box, double distance) {
            box.x += distance;
            return box;
        };
        move(ground, UPPER, translated(UPPER, 45), debris, 201, 74);
        move(ground, LOWER, translated(LOWER, 45), debris, 202, 75);
        const auto &field = area(ground);
        const auto before = field.particles;
        const auto count_before = field.particle_count;
        move(ground, translated(UPPER, 45), translated(UPPER, 49), debris, 203, 74);
        move(ground, translated(LOWER, 45), translated(LOWER, 49), debris, 204, 75);
        const auto following = [&](std::uint64_t owner) -> const Particle * {
            for (std::size_t i = 0; i < count_before; ++i) {
                const auto &sample = before[i];
                if (sample.owner != owner || sample.kind != Particle::Kind::Paper) continue;
                const auto *p = find_particle(field, sample);
                if (p && p->position.x > sample.position.x + 3.99) return p;
            }
            return nullptr;
        };
        const auto *upper = following(74);
        const auto *lower = following(75);
        CHECK(upper && lower);
        if (upper && lower) {
            const Particle UPPER_PAPER = *upper, LOWER_PAPER = *lower;
            move(ground, translated(UPPER, 49), translated(UPPER, 43), debris, 205, 74);
            ground.cancel(75);  // A coincident acquisition seed is not the live window ID.
            ground.tick(screen(.03));
            const auto *reversed = find_particle(field, UPPER_PAPER);
            CHECK(reversed && reversed->position.x < UPPER_PAPER.position.x);
            const auto *still_held = find_particle(field, LOWER_PAPER);
            CHECK(still_held && still_held->position.x == LOWER_PAPER.position.x);
            auto hidden = step();
            hidden.window = 75;
            hidden.workspace_visible = false;
            ground.stamp({hidden.capture, 1, false, 206}, debris, hidden, false);
            ground.tick(screen(.06));
            const auto *cancelled = find_particle(field, LOWER_PAPER);
            CHECK(cancelled && cancelled->position.x < LOWER_PAPER.position.x);
        }
        ground.clear(1);
        CHECK(!ground.active(1));
    }
    {
        // Movement's local healing and reheating don't touch a closing scorch
        // at the same world position.
        CBurnGround ground;
        ground.tick(screen(0));
        auto closing = step();
        closing.movement_enabled = false;
        closing.dying = true;
        ground.stamp({{100, 100, 180, 180}, 0, true, 76}, params, closing);
        const auto &field = area(ground);
        const Vec2 TEST_POINT{165, 165};
        const float CLOSED = cell(field, TEST_POINT).damage();
        CHECK(CLOSED > 0);
        CHECK(cell(field, TEST_POINT).movement_damage() == 0);

        auto debris = debris_settings();
        debris.scorch_lifetime = .06;
        debris.healing_duration = .16;
        const Box FROM{120, 120, 180, 160}, TO{210, 120, 180, 160};
        move(ground, FROM, TO, debris, 77);
        const float BURNED = cell(field, TEST_POINT).movement_damage();
        CHECK(BURNED > .8);
        CHECK(cell(field, TEST_POINT).damage() == CLOSED);
        ground.tick(screen(.04));
        const float COOLING = cell(field, TEST_POINT).movement_heat();
        move(ground, FROM, TO, debris, 77);
        CHECK(cell(field, TEST_POINT).movement_heat() == COOLING);
        ground.tick(screen(.148));  // About 55% through healing, not yet a fresh field.
        const float HEALING = cell(field, TEST_POINT).movement_damage();
        CHECK(HEALING > 0 && HEALING < BURNED);
        const float HEALING_HEAT = cell(field, TEST_POINT).movement_heat();
        move(ground, TO, FROM, debris, 77);
        move(ground, FROM, TO, debris, 77);
        CHECK(cell(field, TEST_POINT).movement_damage() > HEALING);
        CHECK(cell(field, TEST_POINT).movement_heat() > HEALING_HEAT);
        CHECK(std::ranges::any_of(particles(field), [](const Particle &p) {
            return p.kind == Particle::Kind::Paper && p.age == 0 && p.source.x < 210;
        }));
        ground.tick(screen(.4));
        CHECK(cell(field, TEST_POINT).movement_damage() == 0);
        CHECK(cell(field, TEST_POINT).movement_heat() == 0);
        CHECK(cell(field, TEST_POINT).damage() == CLOSED);
        const float CLOSING_HEAT = cell(field, TEST_POINT).heat();
        move(ground, FROM, TO, debris, 77);
        CHECK(cell(field, TEST_POINT).movement_damage() == BURNED);
        CHECK(cell(field, TEST_POINT).movement_heat() > 0);
        CHECK(cell(field, TEST_POINT).heat() == CLOSING_HEAT);
        CHECK(std::ranges::any_of(particles(field),
            [](const Particle &p) { return p.kind == Particle::Kind::Paper && p.age == 0; }));
    }
    {
        // Expanding edges reveal nothing; shrinking on any side exposes its
        // own strip. Turning emission off does not turn off movement damage.
        CBurnGround ground;
        ground.tick(screen(0));
        auto no_debris = params;
        no_debris.fragment_density = no_debris.ember_density = no_debris.smoke_density = 0;
        const Box SMALL{190, 190, 100, 100}, LARGE{170, 170, 140, 140};
        move(ground, SMALL, LARGE, no_debris, 78);
        CHECK(!ground.active(1));
        move(ground, LARGE, SMALL, no_debris, 78);
        const auto &field = area(ground);
        CHECK(cell(field, {175, 225}).movement_damage() > 0);
        CHECK(cell(field, {305, 225}).movement_damage() > 0);
        CHECK(cell(field, {225, 175}).movement_damage() > 0);
        CHECK(cell(field, {225, 305}).movement_damage() > 0);
        CHECK(cell(field, {225, 225}).movement_damage() == 0);
        CHECK(particles(field).empty());
    }
    {
        // Swept bounds and sources are screen-logical across outputs. Filling
        // the particle pool must not stop subsequent wallpaper deposition.
        CBurnGround ground;
        ground.tick(screen(0));
        ground.tick(screen(0, 2, 22));
        const BurnParams debris = debris_settings();
        for (int i = 0; i < 27; ++i) {
            const double X = 40 * static_cast<double>(i);
            move(ground, {X, 100, 180, 300}, {X + 40, 100, 180, 300}, debris, 79);
        }
        const auto &first = area(ground);
        const auto &second = area(ground, 2, 22);
        CHECK(first.particle_count == CBurnGround::MAX_PARTICLES);
        CHECK(cell(first, {905, 205}).movement_damage() > .8);
        CHECK(cell(second, {1035, 205}).movement_damage() > .8);
        CHECK(cell(second, {1155, 205}).movement_damage() == 0);
        CHECK(!particles(second).empty());
        for (const auto &p : particles(second)) {
            CHECK(p.source.x >= 960 && p.source.x <= 1080);
        }
        auto changed_output = screen(0, 2, 22);
        changed_output.screen.x += 100;
        ground.tick(changed_output);
        CHECK(!ground.active(2));
        CHECK(ground.active(1));
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
