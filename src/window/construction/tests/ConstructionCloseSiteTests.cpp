#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <span>

#include "Check.hpp"
#include "window/construction/ConstructionArt.hpp"
#include "window/construction/ConstructionCloseSite.hpp"
#include "window/construction/ConstructionMotionSite.hpp"
#include "window/construction/ConstructionSite.hpp"

using namespace hyprimate;
using namespace hyprimate::animation;
using namespace hyprimate::construction;

namespace {
using RegionalFrame = studies::RegionalClose::Frame;
constexpr Box BODY{324.25, 184.5, 712, 430};
constexpr Box CAPTURE{318.25, 174.5, 724, 451};
constexpr Box LOCAL{0, 0, CAPTURE.w, CAPTURE.h};
constexpr Box SCREEN{100, 50, 1600, 960};
constexpr double DT = 1.0 / 120;
constexpr double DURATION = 1.0;

Step tick(double elapsed = 0, Box screen = SCREEN) {
    return {.elapsed = elapsed, .body = BODY, .capture = CAPTURE, .screen = screen, .dying = true};
}

bool equal(Box a, Box b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

bool near(double a, double b, double tolerance = 1e-4) {
    return std::abs(a - b) <= tolerance;
}

bool inside(Vec2 p, Box b, double epsilon = 0) {
    return p.x >= b.x - epsilon && p.x <= b.x + b.w + epsilon &&
        p.y >= b.y - epsilon && p.y <= b.y + b.h + epsilon;
}

double printed_area(const RegionalFrame &frame) {
    double area = 0;
    const auto vertices = frame.source_vertices();
    for (const auto &tri : frame.source_faces()) {
        const auto &a = vertices[tri[0]].source, &b = vertices[tri[1]].source, &c = vertices[tri[2]].source;
        area += ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) / 2;
    }
    return area;
}

void check_bounds(const RegionalFrame &mesh, const ConstructionBatch &effects, Box bounds, Box screen) {
    const Box view{screen.x - CAPTURE.x, screen.y - CAPTURE.y, screen.w, screen.h};
    CHECK(inside({bounds.x, bounds.y}, view, 1e-7));
    CHECK(inside({bounds.x + bounds.w, bounds.y + bounds.h}, view, 1e-7));
    constexpr double PX = BODY.x - CAPTURE.x + BODY.w / 2;
    constexpr double PY = BODY.y - CAPTURE.y + BODY.h / 2;
    constexpr double FOCAL = 4 * BODY.w;
    const auto check = [&](Vec3 p) {
        const double w = 1 - p.z / FOCAL;
        const Vec2 projected{PX + (p.x - PX) / w, PY + (p.y - PY) / w};
        if (inside(projected, view)) CHECK(inside(projected, bounds, 1e-5));
    };
    for (const auto &vertex : mesh.source_vertices()) check(vertex.position);
    for (const auto &quad : std::span{effects.quads}.first(effects.count)) {
        CHECK(quad.surface != ConstructionSurface::Client);
        CHECK(quad.tint[0] <= quad.tint[3] && quad.tint[1] <= quad.tint[3] && quad.tint[2] <= quad.tint[3]);
        for (const double u : {0., 1.}) {
            for (const double v : {0., 1.}) {
                check({quad.origin[0] + u * quad.u[0] + v * quad.v[0],
                    quad.origin[1] + u * quad.u[1] + v * quad.v[1],
                    quad.origin[2] + u * quad.u[2] + v * quad.v[2]});
            }
        }
    }
}

void check_saved_pose(const RegionalFrame &frame, std::span<const ConstructionQuad> client) {
    for (const auto &vertex : frame.source_vertices()) {
        bool belongs = false;
        for (const auto &quad : client) {
            const Box source{quad.uv[0], quad.uv[1], quad.uv[2], quad.uv[3]};
            if (!inside(vertex.source, source, .005)) continue;
            const double u = (vertex.source.x - source.x) / source.w;
            const double v = (vertex.source.y - source.y) / source.h;
            belongs |= near(quad.origin[0] + u * quad.u[0] + v * quad.v[0], vertex.position.x, .005) &&
                near(quad.origin[1] + u * quad.u[1] + v * quad.v[1], vertex.position.y, .005) &&
                near(quad.origin[2] + u * quad.u[2] + v * quad.v[2], vertex.position.z, .005);
        }
        CHECK(belongs);
    }
}

void complete_capture_animates_with_local_cue_and_contact_dust() {
    const auto site = std::make_unique<CloseSite>();
    const auto frame = std::make_unique_for_overwrite<RegionalFrame>();
    ConstructionBatch effects;
    CHECK(site->start(BODY, CAPTURE, std::nullopt, 19, DURATION));
    CHECK(equal(site->body(), BODY) && equal(site->capture(), CAPTURE));
    const auto first = site->advance(tick());
    CHECK(first.running && !first.interrupted);
    CHECK(first.bounds.w < SCREEN.w && first.bounds.h < SCREEN.h);
    CHECK(site->emit(LOCAL, *frame, effects));
    CHECK(near(printed_area(*frame), CAPTURE.w * CAPTURE.h));  // Includes real decoration pixels.
    for (const auto &vertex : frame->source_vertices()) {
        CHECK(near(vertex.source.x, vertex.position.x) && near(vertex.source.y, vertex.position.y));
        CHECK(vertex.position.z == 0);
    }
    CHECK(std::ranges::count(std::span{effects.quads}.first(effects.count), ConstructionSurface::Atlas,
        &ConstructionQuad::surface) >= 20);  // Both posed workers use the original cutouts.
    check_bounds(*frame, effects, first.bounds, SCREEN);

    auto mapped = tick(.2);
    mapped.dying = false;
    CHECK(!site->advance(mapped).running);
    CHECK(site->emit(LOCAL, *frame, effects));
    CHECK(!frame->flash && near(printed_area(*frame), CAPTURE.w * CAPTURE.h) && !frame->crew().empty());

    // Use a fractional crop and a nonzero, straddled output origin.
    const Box screen{400.5, 190.25, 990, 620};
    const Box crop{82.25, 16.5, 610.75, 434.5};
    bool saw_fold = false, saw_flash = false, saw_dust = false;
    for (int i = 1; i != 120; ++i) {
        const auto next = site->advance(tick(DT, screen));
        CHECK(next.running && !next.interrupted);
        const auto source = i >= 50 ? crop : LOCAL;
        CHECK(site->emit(source, *frame, effects));
        check_bounds(*frame, effects, next.bounds, screen);
        for (const auto &vertex : frame->source_vertices()) {
            CHECK(inside(vertex.source, source));
            if (vertex.position.y > vertex.source.y + 12) saw_fold = true;
        }
        if (frame->flash) {
            saw_flash = true;
            CHECK(frame->dust().empty() && frame->crew().empty() && effects.count > 0);
            for (const auto &quad : std::span{effects.quads}.first(effects.count)) {
                CHECK(quad.surface == ConstructionSurface::Shadow);
                CHECK(near(quad.origin[0] + quad.u[0] / 2, frame->flash->position.x));
                CHECK(near(quad.origin[1] + quad.v[1] / 2, frame->flash->position.y));
            }
        }
        if (!frame->dust().empty() && effects.count) {
            saw_dust = true;
            CHECK(!frame->flash && frame->crew().empty());
            for (const auto &quad : std::span{effects.quads}.first(effects.count))
                CHECK(quad.surface == ConstructionSurface::Shadow);
        }
    }
    CHECK(saw_fold && saw_flash && saw_dust);
    const auto terminal = site->advance(tick(DT));
    CHECK(!terminal.running && !terminal.interrupted);
    CHECK(!site->emit(LOCAL, *frame, effects));
    CHECK(frame->source_vertices().empty() && frame->source_faces().empty() && frame->crew().empty());
    CHECK(frame->dust().empty() && !frame->flash && effects.count == 0 && !effects.overflow);
}

void partial_opening_starts_on_the_actual_print_and_preserves_its_pose() {
    const auto opening = std::make_unique<ConstructionSite>();
    opening->build(BODY, CAPTURE, SCREEN, 47, .62);
    CHECK(opening->active());
    std::array<ConstructionQuad, studies::RegionalClose::MAX_VISIBLE_FACES> client;
    std::size_t count = 0;
    bool posed = false;
    double area = 0;
    for (int i = 0; i != 45 && !posed; ++i) {
        opening->step({.elapsed = DT});
        count = 0;
        area = 0;
        for (const auto &quad : std::span{opening->batch().quads}.first(opening->batch().count)) {
            if (quad.surface != ConstructionSurface::Client) continue;
            if (count == client.size()) break;
            client[count++] = quad;
            area += static_cast<double>(quad.uv[2]) * quad.uv[3];
            posed |= quad.origin != std::array{quad.uv[0], quad.uv[1], 0.F};
        }
    }
    CHECK(posed && count > 0 && area < CAPTURE.w * CAPTURE.h);
    const auto site = std::make_unique<CloseSite>();
    const auto frame = std::make_unique_for_overwrite<RegionalFrame>();
    ConstructionBatch effects;
    CHECK(site->start(BODY, CAPTURE, std::span<const ConstructionQuad>{client}.first(count), 47, DURATION));
    const auto first = site->advance(tick());
    CHECK(first.running);
    CHECK(site->emit(LOCAL, *frame, effects));
    CHECK(near(printed_area(*frame), area, .1));
    check_saved_pose(*frame, std::span{client}.first(count));
    check_bounds(*frame, effects, first.bounds, SCREEN);

    const Box last_crop{1, 2, LOCAL.w - 2, LOCAL.h - 4};
    CHECK(site->emit(last_crop, *frame, effects));
    for (const auto &vertex : frame->source_vertices()) CHECK(inside(vertex.source, last_crop));
    CHECK(!site->emit({}, *frame, effects));  // Refusal cannot replay a previously populated scratch.
    CHECK(frame->source_vertices().empty() && frame->source_faces().empty() && frame->crew().empty());
    CHECK(frame->dust().empty() && !frame->flash && effects.count == 0);
}

void fractional_piece_motion_handoff_rejoins_only_rounded_source_edges() {
    constexpr Box body{525.25, 256.5, 638 + 1.0 / 3, 364 + 2.0 / 3};
    constexpr Box capture{body.x - 6, body.y - 4, body.w + 15, body.h + 12};
    constexpr Box from{body.x - 205, body.y - 46, body.w, body.h};
    constexpr Box from_capture{capture.x - 205, capture.y - 46, capture.w, capture.h};
    const auto motion = std::make_unique<MotionSite>(42, TransportVariant::PieceByPiece);
    Step step{.body = body, .capture = capture, .screen = SCREEN, .work_area = SCREEN,
        .motion = Motion{.from_body = from, .from_capture = from_capture, .body = body,
            .capture = capture, .translated = true, .capture_changed = true, .sample_seconds = DT},
        .movement_enabled = true, .held = true};
    CHECK(motion->advance(step).running);
    step.motion.reset();
    step.elapsed = DT;
    for (int i = 0; i != 24; ++i) CHECK(motion->advance(step).running);
    std::array<ConstructionQuad, studies::RegionalClose::MAX_VISIBLE_FACES> faces;
    std::size_t count = 0;
    bool displaced = false;
    for (const auto &quad : std::span{motion->batch().quads}.first(motion->batch().count)) {
        if (quad.surface != ConstructionSurface::Client) continue;
        CHECK(count < faces.size());
        if (count == faces.size()) break;
        faces[count++] = quad;
        displaced |= quad.origin != std::array{quad.uv[0], quad.uv[1], 0.F};
    }
    CHECK(count > 1 && displaced);
    const auto visible = std::span<const ConstructionQuad>{faces}.first(count);
    const auto site = std::make_unique<CloseSite>();
    const auto frame = std::make_unique_for_overwrite<RegionalFrame>();
    ConstructionBatch effects;
    CHECK(site->start(body, capture, visible, 42, DURATION));
    CHECK(site->advance({.body = body, .capture = capture, .screen = SCREEN, .dying = true}).running);
    CHECK(site->emit({0, 0, capture.w, capture.h}, *frame, effects));
    CHECK(near(printed_area(*frame), capture.w * capture.h, .1));
    check_saved_pose(*frame, visible);
}

void shared_source_seams_do_not_fill_a_real_gap() {
    const auto site = std::make_unique<CloseSite>();
    const auto frame = std::make_unique_for_overwrite<RegionalFrame>();
    ConstructionBatch effects;
    std::array faces{
        ConstructionQuad{{40, 30, 0}, {80, 0, 0}, {0, 120, 0},
            {40, 30, std::nextafter(80.F, 81.F), 120}, {255, 255, 255, 255}, ConstructionSurface::Client},
        ConstructionQuad{{120, 30, 0}, {80, 0, 0}, {0, 120, 0},
            {120, 30, 80, 120}, {255, 255, 255, 255}, ConstructionSurface::Client}};
    CHECK(site->start(BODY, CAPTURE, std::span{faces}, 33, DURATION));
    CHECK(site->emit({119.75, 30, .5, 120}, *frame, effects));
    CHECK(near(printed_area(*frame), 60));

    faces[1].uv[0] += .5F;
    CHECK(site->start(BODY, CAPTURE, std::span{faces}, 33, DURATION));
    CHECK(site->emit({120.125, 30, .25, 120}, *frame, effects));
    CHECK(frame->source_faces().empty() && effects.count == 0);
}

void blank_ownership_is_not_native_fallback() {
    const auto site = std::make_unique<CloseSite>();
    const auto frame = std::make_unique_for_overwrite<RegionalFrame>();
    ConstructionBatch effects;
    CHECK(site->start(BODY, CAPTURE, std::nullopt, 23, DURATION));
    CHECK(site->emit(LOCAL, *frame, effects) && !frame->source_faces().empty() && effects.count > 0);
    CHECK(site->start(BODY, CAPTURE, std::span<const ConstructionQuad>{}, 23, DURATION));
    const auto blank = site->advance(tick());
    CHECK(!blank.running && !blank.interrupted && !is_usable(blank.bounds));
    CHECK(site->start(BODY, CAPTURE, std::span<const ConstructionQuad>{}, 23, DURATION));
    ConstructionBatch old_worker;
    stamp(old_worker, HEAD, {50, 80, 0}, .17);
    CHECK(site->depart(old_worker));
    CHECK(site->emit(LOCAL, *frame, effects));
    CHECK(frame->source_vertices().empty() && frame->source_faces().empty() && frame->crew().empty());
    CHECK(frame->dust().empty() && !frame->flash && effects.count == 1);
    CHECK(effects.quads[0].origin == old_worker.quads[0].origin);
    CHECK(site->advance(tick(.12)).running);
    CHECK(site->emit(LOCAL, *frame, effects) && frame->source_faces().empty());
    CHECK(effects.count == 1 && effects.quads[0].tint[3] < old_worker.quads[0].tint[3]);
    const auto clear = site->advance(tick(.13));
    CHECK(!clear.running && !clear.interrupted);
}

void snapshot_must_cover_print_that_moved_inside_its_output() {
    // The first 40 source pixels are off the snapshot's left edge, but the
    // whole moved patch was visible on that output when the client closed.
    constexpr Box snapshot{100, 0, LOCAL.w - 100, LOCAL.h};
    const ConstructionQuad moving{{180, 30, 4}, {80, 0, 0}, {0, 120, 0}, {60, 30, 80, 120},
        {255, 255, 255, 255}, ConstructionSurface::Client};
    const auto site = std::make_unique<CloseSite>();
    const auto frame = std::make_unique_for_overwrite<RegionalFrame>();
    ConstructionBatch effects;
    CHECK(site->start(BODY, CAPTURE, std::span{&moving, 1}, 19, DURATION));
    CHECK(!site->can_capture(snapshot, 1.5));
    const Box taps_clipped{59.9, 0, LOCAL.w - 59.9, LOCAL.h};
    CHECK(!site->can_capture(taps_clipped, 1.5));
    const Box complete{59.5, 0, LOCAL.w - 59.5, LOCAL.h};
    CHECK(site->can_capture(complete, 1.5));
    CHECK(site->emit(complete, *frame, effects));
    CHECK(near(printed_area(*frame), 80 * 120));
    check_saved_pose(*frame, std::span{&moving, 1});

    // For identity the omitted source would also be outside the output. A
    // straddling native window keeps normal clipping instead of declining.
    CHECK(site->start(BODY, CAPTURE, std::nullopt, 19, DURATION));
    CHECK(site->can_capture(snapshot, 1.5));
    CHECK(site->emit(snapshot, *frame, effects));
    CHECK(near(printed_area(*frame), snapshot.w * snapshot.h));
    for (const auto &vertex : frame->source_vertices()) {
        CHECK(inside(vertex.source, snapshot));
        CHECK(vertex.position.x == vertex.source.x && vertex.position.y == vertex.source.y);
    }
}

void handoff_keeps_only_contact_art_in_its_first_pose_then_retires_it() {
    ConstructionBatch published;
    stamp(published, HEAD, {1065, 350, 6}, .18, .92);
    stamp(published, CAB, {1080, 345, 0}, .7);
    stamp(published, GLOVE, {1078, 366, 7}, .15, .85);
    quad(published, {0, 0, 0}, {LOCAL.w, 0, 0}, {0, LOCAL.h, 0},
        {255, 255, 255, 255}, ConstructionSurface::Client);
    const auto site = std::make_unique<CloseSite>();
    const auto frame = std::make_unique_for_overwrite<RegionalFrame>();
    ConstructionBatch effects;
    CHECK(site->start(BODY, CAPTURE, std::nullopt, 19, DURATION));
    CHECK(site->depart(published));
    const auto first = site->advance(tick());
    CHECK(first.running && !first.interrupted);
    CHECK(site->emit(LOCAL, *frame, effects));
    CHECK(effects.count == 2);
    for (const std::size_t i : {0U, 1U}) {
        const auto &old = published.quads[2 * i], &current = effects.quads[i];
        CHECK(current.surface == ConstructionSurface::Atlas);
        CHECK(current.origin == old.origin && current.u == old.u && current.v == old.v);
        CHECK(current.tint == old.tint && current.uv == old.uv);
    }
    check_bounds(*frame, effects, first.bounds, SCREEN);

    CHECK(site->advance(tick(.13)).running);
    CHECK(site->emit(LOCAL, *frame, effects));
    CHECK(effects.count == 2);
    for (const std::size_t i : {0U, 1U}) {
        const auto &old = published.quads[2 * i], &current = effects.quads[i];
        CHECK(current.origin[1] > old.origin[1]);
        CHECK(current.origin[0] == old.origin[0] && current.origin[2] == old.origin[2]);
        CHECK(current.tint[3] > 0 && current.tint[3] < old.tint[3] && current.uv == old.uv);
    }
    CHECK(site->advance(tick(.12)).running);
    CHECK(site->emit(LOCAL, *frame, effects));
    CHECK(effects.count == 0 && near(printed_area(*frame), LOCAL.w * LOCAL.h));

    CHECK(site->start(BODY, CAPTURE, std::nullopt, 19, DURATION));
    published.quads[0].origin[1] = std::numeric_limits<float>::infinity();
    CHECK(!site->depart(published));
}

void incompatible_source_and_dying_facts_decline() {
    const auto site = std::make_unique<CloseSite>();
    const auto frame = std::make_unique_for_overwrite<RegionalFrame>();
    ConstructionBatch effects;
    ConstructionQuad client{{40, 30, 0}, {80, 0, 0}, {0, 120, 0}, {40, 30, 80, 120},
        {255, 255, 255, 255}, ConstructionSurface::Client};
    CHECK(site->start(BODY, CAPTURE, std::span{&client, 1}, 15, DURATION));
    CHECK(site->emit({500, 80, 100, 100}, *frame, effects));  // Authorized, but misses all visible print.
    CHECK(frame->source_faces().empty() && effects.count == 0);
    CHECK(site->emit(LOCAL, *frame, effects) && !frame->source_faces().empty());
    CHECK(!site->emit({LOCAL.w, 80, 100, 100}, *frame, effects));  // No actual capture intersection.
    CHECK(frame->source_faces().empty() && effects.count == 0);
    CHECK(site->emit(LOCAL, *frame, effects) && !frame->source_faces().empty());
    auto incompatible = tick(DT);
    incompatible.capture.w -= 1;
    const auto declined = site->advance(incompatible);
    CHECK(!declined.running && declined.interrupted);
    CHECK(!site->emit(LOCAL, *frame, effects) && frame->source_faces().empty() && effects.count == 0);

    client.uv[0] = static_cast<float>(CAPTURE.w) - 30;
    CHECK(!site->start(BODY, CAPTURE, std::span{&client, 1}, 15, DURATION));
    client.uv[0] = 40;
    client.origin[2] = std::numeric_limits<float>::infinity();
    CHECK(!site->start(BODY, CAPTURE, std::span{&client, 1}, 15, DURATION));
    client.origin[2] = 0;
    const std::array overlap{client, client};
    CHECK(!site->start(BODY, CAPTURE, std::span{overlap}, 15, DURATION));
    const Box dim{0, 0, SCREEN.w, SCREEN.h};
    CHECK(!site->start(BODY, dim, std::nullopt, 15, DURATION));
    CHECK(site->start(BODY, CAPTURE, std::nullopt, 15, DURATION));
    auto stalled = tick(DT);
    stalled.stalled = true;
    const auto aborted = site->advance(stalled);
    CHECK(!aborted.running && aborted.interrupted);
    CHECK(!site->emit(LOCAL, *frame, effects) && frame->source_faces().empty() && effects.count == 0);
}
}  // namespace

int main() {
    complete_capture_animates_with_local_cue_and_contact_dust();
    partial_opening_starts_on_the_actual_print_and_preserves_its_pose();
    fractional_piece_motion_handoff_rejoins_only_rounded_source_edges();
    shared_source_seams_do_not_fill_a_real_gap();
    blank_ownership_is_not_native_fallback();
    snapshot_must_cover_print_that_moved_inside_its_output();
    handoff_keeps_only_contact_art_in_its_first_pose_then_retires_it();
    incompatible_source_and_dying_facts_decline();
    return g_failures == 0 ? 0 : 1;
}
