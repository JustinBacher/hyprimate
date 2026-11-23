#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "Check.hpp"
#include "window/construction/ConstructionMotionSite.hpp"

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

void alternate_transport_yields_to_an_intact_resize() {
    const Box moved{HOME.x + 35, HOME.y, HOME.w, HOME.h};
    const Box enlarged{moved.x - 20, moved.y - 10, moved.w + 20, moved.h + 10};
    MotionSite native(42, TransportVariant::Native);
    CHECK(!native.advance(first(HOME, moved)).running && native.batch().count == 0);
    for (auto variant : {TransportVariant::Crane, TransportVariant::PieceByPiece}) {
        MotionSite site(42, variant);
        CHECK(site.advance(first(HOME, moved)).running);
        CHECK(site.kind() == (variant == TransportVariant::Crane ? MotionSite::Kind::Crane : MotionSite::Kind::Pieces));
        const auto resize = resizing(first(moved, enlarged));
        const auto frame = site.advance(resize);
        intact_publication(site, frame, resize, true);
        CHECK(site.kind() == MotionSite::Kind::Resize && frame.native_presentation);
        completes_after_release(site, enlarged);
    }
}

void crane_uses_the_exposed_owner_edge_before_handoff() {
    // A visible neighbor blocks the shorter left exit even across the gap.
    constexpr std::array outputs{Box{-520, 0, 400, 900}, SCREEN};
    const Box moved{HOME.x + 35, HOME.y, HOME.w, HOME.h};
    MotionSite site(42, TransportVariant::Crane);
    auto press = first(HOME, moved);
    press.visible_output_owner = SCREEN;
    press.visible_outputs = outputs;
    intact_publication(site, site.advance(press), press, false);
    auto pause = tick(moved);
    pause.visible_output_owner = SCREEN;
    pause.visible_outputs = outputs;
    bool posed = false;
    for (int i = 0; i < 520; ++i) {
        intact_publication(site, site.advance(pause), pause, false);
        posed |= std::any_of(site.batch().quads.begin(), site.batch().quads.begin() + site.batch().count,
            [](const ConstructionQuad &quad) { return quad.surface == ConstructionSurface::Client && !identity(quad); });
    }
    CHECK(posed);

    auto reverse = first(moved, HOME);
    reverse.visible_output_owner = SCREEN;
    reverse.visible_outputs = outputs;
    intact_publication(site, site.advance(reverse), reverse, false);
    auto release = tick(HOME, false);
    release.visible_output_owner = SCREEN;
    release.visible_outputs = outputs;
    bool seated_at_outer_edge = false;
    for (int i = 0; i < 720; ++i) {
        const auto frame = site.advance(release);
        CHECK(!frame.interrupted);
        if (!frame.running) {
            CHECK(seated_at_outer_edge && site.batch().count == 0);
            return;
        }
        const bool identity_face = std::all_of(site.batch().quads.begin(), site.batch().quads.begin() + site.batch().count,
            [](const ConstructionQuad &quad) { return quad.surface != ConstructionSurface::Client || identity(quad); });
        const auto left = release.capture.x + frame.bounds.x;
        const auto right = left + frame.bounds.w;
        seated_at_outer_edge |= identity_face && site.batch().count > 1 &&
            left > outputs.front().x + outputs.front().w && right > SCREEN.w - 12;
    }
    CHECK(false);  // A completed departure stops scheduling.
}

void crane_declines_a_blocked_route_and_does_not_switch_after_acquisition() {
    constexpr Box left{-450, 0, 400, 900}, right{1490, 0, 400, 900};
    constexpr std::array blocked{left, SCREEN, right};
    constexpr std::array only_right_open{left, SCREEN}, only_left_open{SCREEN, right};
    const Box moved{HOME.x + 35, HOME.y, HOME.w, HOME.h};
    auto press = first(HOME, moved);
    press.visible_output_owner = SCREEN;
    press.visible_outputs = blocked;
    MotionSite surrounded(42, TransportVariant::Crane);
    const auto declined = surrounded.advance(press);
    CHECK(declined.interrupted && !declined.running && surrounded.batch().count == 0);

    press.visible_outputs = only_right_open;
    MotionSite changed(42, TransportVariant::Crane);
    CHECK(changed.advance(press).running);
    auto release = tick(moved, false);
    release.visible_output_owner = SCREEN;
    release.visible_outputs = only_left_open;
    const auto stopped = changed.advance(release);
    CHECK(stopped.interrupted && !stopped.running && changed.batch().count == 0);
}

void piece_relocation_declines_cross_output_coverage_before_painting() {
    constexpr std::array outputs{Box{-800, 0, 800, 900}, SCREEN};
    const Box outside{-700, HOME.y, HOME.w, HOME.h};
    const Box moved{50, HOME.y, HOME.w, HOME.h};
    const Box straddling{-30, HOME.y, HOME.w, HOME.h};
    for (const auto &start : {first(outside, moved), first(straddling, moved), first(moved, straddling)}) {
        auto press = start;
        press.visible_output_owner = SCREEN;
        press.visible_outputs = outputs;
        MotionSite site(42, TransportVariant::PieceByPiece);
        const auto stopped = site.advance(press);
        CHECK(stopped.interrupted && !stopped.running && site.batch().count == 0);
    }

    MotionSite site(42, TransportVariant::PieceByPiece);
    auto press = first(HOME, moved);
    press.visible_output_owner = SCREEN;
    press.visible_outputs = outputs;
    CHECK(site.advance(press).running);
    auto drag = first(moved, straddling);
    drag.visible_output_owner = SCREEN;
    drag.visible_outputs = outputs;
    CHECK(!site.advance(drag).running && site.batch().count == 0);
}

void piece_relocation_retargets_and_seats_the_current_image() {
    const Box away{HOME.x + 205, HOME.y + 46, HOME.w, HOME.h};
    MotionSite site(42, TransportVariant::PieceByPiece);
    CHECK(site.advance(first(HOME, away)).running);
    bool split_and_displaced = false, reassembled = false;
    const auto inspect = [&](const Step &step, Frame frame) {
        CHECK(frame.running && !frame.interrupted && site.kind() == MotionSite::Kind::Pieces);
        CHECK(same(site.body(), step.body) && same(site.capture(), step.capture));
        std::size_t displaced = 0, faces = 0;
        double covered = 0;
        for (std::size_t i = 0; i < site.batch().count; ++i) {
            const auto &quad = site.batch().quads[i];
            if (quad.surface != ConstructionSurface::Client) continue;
            const auto [l, t, w, h] = quad.uv;
            CHECK(l >= 0 && t >= 0 && w > 0 && h > 0);
            CHECK(l + w <= step.capture.w + 1e-4 && t + h <= step.capture.h + 1e-4);
            const auto overlap_w = std::max(0.0, std::min(double(l + w), HOME.w + 6) - std::max(double(l), 6.0));
            const auto overlap_h = std::max(0.0, std::min(double(t + h), HOME.h + 4) - std::max(double(t), 4.0));
            if (overlap_w > 0 && overlap_h > 0) {
                covered += overlap_w * overlap_h;
                ++faces;
                displaced += !identity(quad);
            }
        }
        CHECK(std::abs(covered - HOME.w * HOME.h) < .25);
        split_and_displaced |= faces > 1 && displaced > 0;
        reassembled |= !step.held && displaced == 0;
    };
    Box body = away;
    for (int i = 0; i < 150; ++i) {
        if (i == 26 || i == 51 || i == 78) {
            const auto target = same(body, HOME) ? away : HOME;
            const auto reverse = first(body, target);
            inspect(reverse, site.advance(reverse));
            body = target;
        } else {
            const auto held = tick(body);
            inspect(held, site.advance(held));
        }
    }
    CHECK(split_and_displaced);
    const auto released = tick(body, false);
    for (int i = 0; i < 720; ++i) {
        const auto frame = site.advance(released);
        CHECK(!frame.interrupted);
        if (!frame.running) {
            CHECK(reassembled && site.batch().count == 0);
            CHECK(!site.advance(first(body, away)).running);
            return;
        }
        inspect(released, frame);
    }
    CHECK(false);
}
void worker_multiplier_keeps_every_motion_variant_running() {
    const Box moved{HOME.x + 35, HOME.y, HOME.w, HOME.h};
    const Box enlarged{HOME.x, HOME.y, HOME.w + 40, HOME.h + 20};
    for (auto variant : {TransportVariant::CrewCarry, TransportVariant::Crane, TransportVariant::PieceByPiece}) {
        for (double multiplier : {0., .5, 1., 2.}) {
            for (bool resize : {false, true}) {
                MotionSite site(1307, variant, multiplier);
                const Box body = resize ? enlarged : moved;
                const auto press = resize ? resizing(first(HOME, body)) : first(HOME, body);
                auto frame = site.advance(press);
                CHECK(frame.running && !frame.interrupted);
                for (int i = 0; i < 120; ++i) {
                    frame = site.advance(resize ? resizing(tick(body)) : tick(body));
                    CHECK(frame.running && !frame.interrupted && is_usable(frame.bounds));
                    CHECK(!site.batch().overflow);
                }
                completes_after_release(site, body);
            }
        }
    }
}

}  // namespace

int main() {
    worker_multiplier_keeps_every_motion_variant_running();
    carry_holds_settles_and_can_restart_on_new_geometry();
    resize_seeds_the_previous_edges_and_never_deforms_the_face();
    quiet_resize_and_held_move_with_incidental_configure();
    workspace_motion_admission_and_single_crew_switching();
    invalid_facts_handoff_without_a_stale_batch();
    alternate_transport_yields_to_an_intact_resize();
    crane_uses_the_exposed_owner_edge_before_handoff();
    crane_declines_a_blocked_route_and_does_not_switch_after_acquisition();
    piece_relocation_declines_cross_output_coverage_before_painting();
    piece_relocation_retargets_and_seats_the_current_image();
}
