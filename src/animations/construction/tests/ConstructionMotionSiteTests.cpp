#include <array>
#include <limits>

#include "Check.hpp"
#include "animations/construction/ConstructionMotionSite.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;
using namespace hyprimate::construction;

namespace {
constexpr Box SCREEN{0, 0, 1440, 900};
constexpr Box HOME{320.25, 210.5, 638, 364};
constexpr double DT = 1.0 / 120;

Box capture(Box body) {
    return {body.x - 6, body.y - 4, body.w + 15, body.h + 12};
}

Motion sample(Box before, Box after) {
    return {.from_body = before, .from_capture = capture(before), .body = after, .capture = capture(after),
        .translated = before.x != after.x || before.y != after.y,
        .resized = before.w != after.w || before.h != after.h, .capture_changed = true,
        .sample_seconds = DT};
}

Step tick(Box body, bool held = true) {
    return {.elapsed = DT, .body = body, .capture = capture(body), .screen = SCREEN, .work_area = SCREEN,
        .movement_enabled = true, .held = held, .monitor = 3, .workspace = 9};
}

Step first(Box before, Box after) {
    auto step = tick(after);
    step.elapsed = 0;
    step.motion = sample(before, after);
    return step;
}

Step resizing(Step step) {
    step.resize_held = true;
    step.held = false;
    return step;
}

bool same(Box first, Box second) {
    return first.x == second.x && first.y == second.y && first.w == second.w && first.h == second.h;
}

bool identity(const ConstructionQuad &quad) {
    return quad.origin == std::array{quad.uv[0], quad.uv[1], 0.F} &&
        quad.u == std::array{quad.uv[2], 0.F, 0.F} && quad.v == std::array{0.F, quad.uv[3], 0.F};
}

void intact_publication(const MotionSite &site, Frame frame, const Step &step, bool must_be_identity) {
    CHECK(frame.running && !frame.interrupted);
    CHECK(same(site.body(), step.body) && same(site.capture(), step.capture));
    CHECK(frame.bounds.x <= 0 && frame.bounds.y <= 0);
    CHECK(frame.bounds.x + frame.bounds.w >= step.capture.w - 1e-4);
    CHECK(frame.bounds.y + frame.bounds.h >= step.capture.h - 1e-4);
    CHECK(!site.batch().overflow && site.batch().count > 0);
    std::size_t intact_faces = 0;
    const auto bx = static_cast<float>(step.body.x - step.capture.x);
    const auto by = static_cast<float>(step.body.y - step.capture.y);
    const auto right = bx + static_cast<float>(step.body.w);
    const auto bottom = by + static_cast<float>(step.body.h);
    for (std::size_t i = 0; i < site.batch().count; ++i) {
        const auto &quad = site.batch().quads[i];
        if (quad.surface != ConstructionSurface::Client) continue;
        CHECK(quad.uv[0] >= 0 && quad.uv[1] >= 0);
        CHECK(quad.uv[0] + quad.uv[2] <= static_cast<float>(step.capture.w) + 1e-4F);
        CHECK(quad.uv[1] + quad.uv[3] <= static_cast<float>(step.capture.h) + 1e-4F);
        if (quad.uv[0] <= bx && quad.uv[1] <= by &&
            quad.uv[0] + quad.uv[2] >= right && quad.uv[1] + quad.uv[3] >= bottom) ++intact_faces;
        if (must_be_identity) CHECK(identity(quad));
    }
    CHECK(intact_faces == 1);
}

void completes_after_release(MotionSite &site, Box body) {
    const auto idle = tick(body, false);
    Frame frame;
    int frames = 0;
    do {
        frame = site.advance(idle);
        CHECK(!frame.interrupted);
    } while (frame.running && ++frames < 240);
    CHECK(frames > 0 && !frame.running && site.batch().count == 0);
    auto moved = body;
    moved.x += 22;
    CHECK(!site.advance(first(body, moved)).running);  // A late callback cannot resurrect it.
}

void carry_holds_settles_and_can_restart_on_new_geometry() {
    auto moved = HOME;
    moved.x += 30;
    MotionSite carry(42);
    const auto press = first(HOME, moved);
    const auto pickup = carry.advance(press);
    intact_publication(carry, pickup, press, false);
    CHECK(pickup.native_presentation && carry.batch().count == 1);
    CHECK(carry.kind() == MotionSite::Kind::Carry);

    bool drawn = false;
    bool lifted = false;
    for (int i = 0; i < 120; ++i) {
        const auto paused = tick(moved);
        const auto frame = carry.advance(paused);
        intact_publication(carry, frame, paused, false);
        drawn |= !frame.native_presentation && carry.batch().count > 1;
        for (std::size_t k = 0; k < carry.batch().count; ++k) {
            const auto &quad = carry.batch().quads[k];
            lifted |= quad.surface == ConstructionSurface::Client && !identity(quad);
        }
    }
    CHECK(drawn && lifted);

    const auto reverse = first(moved, HOME);
    intact_publication(carry, carry.advance(reverse), reverse, false);
    completes_after_release(carry, HOME);

    MotionSite still(42);
    CHECK(!still.advance(tick(HOME)).running);  // Held alone does not acquire or replay a pickup.
    MotionSite programmatic(42);
    auto ipc = press;
    ipc.held = false;
    ipc.motion->sample_seconds = STALL_SECONDS + 2;  // First move after a dormant output.
    CHECK(MotionSite::admits(*ipc.motion));
    intact_publication(programmatic, programmatic.advance(ipc), ipc, false);
    completes_after_release(programmatic, moved);
}

void resize_seeds_the_previous_edges_and_never_deforms_the_face() {
    // Near-edge growth together with a common left/up translation; no far-edge work.
    const Box bigger{HOME.x - 37, HOME.y - 30, HOME.w + 30, HOME.h + 24};
    MotionSite stretch(1307);
    const auto grow = resizing(first(HOME, bigger));
    const auto first_pull = stretch.advance(grow);
    intact_publication(stretch, first_pull, grow, true);
    CHECK(first_pull.native_presentation && stretch.batch().count == 1);
    CHECK(stretch.kind() == MotionSite::Kind::Resize);

    Box body = bigger;
    bool left = false, top = false;
    bool drawn = false;
    for (int i = 0; i < 90; ++i) {
        auto held = tick(body);
        if (i == 20) {
            body.x += 10;
            body.y += 12;
            held = first(bigger, body);
        }
        if (i == 40) { // Reversal is a contraction of those same near edges.
            const Box before = body;
            body.x += 12;
            body.y += 10;
            body.w -= 12;
            body.h -= 10;
            held = first(before, body);
        }
        const auto resize = resizing(held);
        const auto frame = stretch.advance(resize);
        intact_publication(stretch, frame, resize, true);
        drawn |= !frame.native_presentation && stretch.batch().count > 1;
        CHECK(stretch.kind() == MotionSite::Kind::Resize);
        for (std::size_t k = 0; k < stretch.batch().count; ++k) {
            const auto &quad = stretch.batch().quads[k];
            if (quad.surface != ConstructionSurface::Atlas) continue;
            const double x = quad.origin[0] + .5 * (quad.u[0] + quad.v[0]);
            const double y = quad.origin[1] + .5 * (quad.u[1] + quad.v[1]);
            left |= x < body.x - held.capture.x;
            top |= y < body.y - held.capture.y;
            CHECK(x < body.x - held.capture.x + body.w);
            CHECK(y < body.y - held.capture.y + body.h);
        }
    }
    CHECK(drawn && left && top);
    completes_after_release(stretch, body);
}

void quiet_resize_and_held_move_with_incidental_configure() {
    const Box moved{HOME.x + 32, HOME.y, HOME.w, HOME.h};
    const Box larger{moved.x, moved.y, moved.w + 96, moved.h + 48};
    const Box translated{larger.x - 19, larger.y + 13, larger.w, larger.h};
    MotionSite quiet(1307);
    auto ipc = first(HOME, larger);
    ipc.held = false;
    ipc.motion->sample_seconds = STALL_SECONDS + 2;
    CHECK(MotionSite::admits(*ipc.motion));
    intact_publication(quiet, quiet.advance(ipc), ipc, true);
    CHECK(quiet.kind() == MotionSite::Kind::Resize);

    MotionSite site(42);
    CHECK(site.advance(first(HOME, moved)).running);
    const auto incidental = first(moved, larger);
    intact_publication(site, site.advance(incidental), incidental, true);
    CHECK(site.kind() == MotionSite::Kind::Carry);
    const auto subsequent = first(larger, translated);
    intact_publication(site, site.advance(subsequent), subsequent, false);
    CHECK(site.kind() == MotionSite::Kind::Carry);

    auto genuine = resizing(first(translated, moved));
    genuine.held = true;  // Even if both facts arrive, the actual resize grab wins.
    intact_publication(site, site.advance(genuine), genuine, true);
    CHECK(site.kind() == MotionSite::Kind::Resize);
}

void workspace_motion_admission_and_single_crew_switching() {
    const Box moved{HOME.x + 30, HOME.y, HOME.w, HOME.h};
    const Box bigger{moved.x, moved.y, moved.w + 50, moved.h + 20};
    auto wallpaper = sample(HOME, moved);
    wallpaper.workspace_shifted = true;
    CHECK(!MotionSite::admits(wallpaper));
    auto trim = sample(HOME, HOME);
    trim.capture = {HOME.x - 10, HOME.y - 10, HOME.w + 20, HOME.h + 20};
    CHECK(!MotionSite::admits(trim));
    MotionSite idle_resize(42);
    CHECK(!idle_resize.advance(resizing(tick(HOME))).running);
    MotionSite translation_only_resize(42);
    CHECK(!translation_only_resize.advance(resizing(first(HOME, moved))).running);

    MotionSite static_offset(42);
    auto offset = first(HOME, moved);
    offset.motion->body.x += 1200;
    offset.motion->from_body.x += 1200;
    intact_publication(static_offset, static_offset.advance(offset), offset, false);
    CHECK(static_offset.kind() == MotionSite::Kind::Carry);

    // A shifting wallpaper on first acquisition conveys size but no usable prior local origin.
    MotionSite ambiguous(42);
    auto shifted = resizing(first(HOME, bigger));
    shifted.motion->body.x += 1200;
    shifted.motion->workspace_shifted = true;
    CHECK(MotionSite::admits(*shifted.motion));
    intact_publication(ambiguous, ambiguous.advance(shifted), shifted, true);
    CHECK(ambiguous.kind() == MotionSite::Kind::Resize);

    MotionSite site(42);
    const auto translation = first(HOME, moved);
    CHECK(site.advance(translation).running && site.kind() == MotionSite::Kind::Carry);
    auto resize = resizing(first(moved, bigger));
    resize.motion->body.x -= 1200;
    resize.motion->workspace_shifted = true;
    intact_publication(site, site.advance(resize), resize, true);
    CHECK(site.kind() == MotionSite::Kind::Resize);

    for (int i = 0; i < 60; ++i) CHECK(site.advance(resizing(tick(bigger))).running);
    CHECK(site.advance(tick(bigger, false)).running); // Let the old held resize release.
    auto after = bigger;
    after.y -= 30;
    const auto next_drag = first(bigger, after);
    CHECK(site.advance(next_drag).running && site.kind() == MotionSite::Kind::Carry);
    auto decorations = tick(after);
    decorations.capture.x -= 2.5;
    decorations.capture.w += 2.5;
    intact_publication(site, site.advance(decorations), decorations, false);
}

void invalid_facts_handoff_without_a_stale_batch() {
    const Box moved{HOME.x + 30, HOME.y, HOME.w, HOME.h};
    const auto start = first(HOME, moved);
    auto stale = *start.motion;
    stale.sample_seconds = -DT;
    CHECK(!MotionSite::admits(stale));
    stale.sample_seconds = std::numeric_limits<double>::infinity();
    CHECK(!MotionSite::admits(stale));

    for (int reason = 0; reason < 9; ++reason) {
        MotionSite site(42);
        CHECK(site.advance(start).running);
        auto interrupt = tick(moved);
        switch (reason) {
            case 0: interrupt.monitor++; break;
            case 1: interrupt.workspace++; break;
            case 2: interrupt.stalled = true; break;
            case 3: interrupt.movement_enabled = false; break;
            case 4: interrupt.workspace_visible = false; break;
            case 5: interrupt.dying = true; break;
            case 6: interrupt.body.x += 2000; break; // Tracker withholds the teleport sample.
            case 7:
                interrupt.motion = sample(HOME, moved);
                interrupt.motion->workspace_shifted = true; // Actual local body did not move.
                break;
            case 8:
                interrupt.body = HOME;
                interrupt.capture = capture(HOME);
                interrupt.motion = sample(moved, HOME);
                interrupt.motion->sample_seconds = STALL_SECONDS + DT;
                break;
        }
        const auto stopped = site.advance(interrupt);
        CHECK(stopped.interrupted && !stopped.running && !is_usable(stopped.bounds));
        CHECK(site.batch().count == 0);
    }
}
}  // namespace

int main() {
    carry_holds_settles_and_can_restart_on_new_geometry();
    resize_seeds_the_previous_edges_and_never_deforms_the_face();
    quiet_resize_and_held_move_with_incidental_configure();
    workspace_motion_admission_and_single_crew_switching();
    invalid_facts_handoff_without_a_stale_batch();
}
