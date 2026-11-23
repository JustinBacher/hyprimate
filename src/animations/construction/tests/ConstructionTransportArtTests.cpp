#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include "Check.hpp"
#include "animations/construction/ConstructionArt.hpp"
#include "animations/construction/ConstructionMotionArt.hpp"
#include "animations/construction/studies/IntactTransport.hpp"
#include "animations/construction/studies/PieceRelocation.hpp"

namespace {
using namespace hyprimate;
using namespace hyprimate::construction;
using namespace hyprimate::construction::studies;

constexpr Box SCREEN{0, 0, 1280, 800};
constexpr Box HOME{177.25, 294.5, 654, 402};
constexpr double DT = 1. / 120;
constexpr double EPS = .002;

Box decorated(Box body, double margin = 7.5) {
    return {body.x - margin, body.y - 3.25, body.w + margin + 8, body.h + 15};
}

Vec3 local(Vec3 p, Box capture) { return {p.x - capture.x, p.y - capture.y, p.z}; }
Vec3 local(Vec2 p, Box capture) { return local(Vec3{p.x, p.y, 0}, capture); }
bool near(Vec3 a, Vec3 b) { return length(a - b) < EPS; }

std::span<const ConstructionQuad> quads(const ConstructionBatch &batch) {
    return {batch.quads.data(), batch.count};
}

Vec3 point(const ConstructionQuad &q, double u, double v) {
    return {q.origin[0] + u * q.u[0] + v * q.v[0], q.origin[1] + u * q.u[1] + v * q.v[1],
        q.origin[2] + u * q.u[2] + v * q.v[2]};
}

Vec3 anchor(const ConstructionQuad &q, Vec2 texel) {
    return point(q, (texel.x - 4) / 56, (texel.y - 4) / 56);
}

bool is_part(const ConstructionQuad &q, Part art) {
    return q.surface == ConstructionSurface::Atlas &&
        q.uv[0] * ATLAS_WIDTH == art.col * 64 + 4 && q.uv[1] * ATLAS_HEIGHT == art.row * 64 + 4;
}

bool is_identity(const ConstructionQuad &q) {
    return q.surface == ConstructionSurface::Client && q.origin == std::array{q.uv[0], q.uv[1], 0.F} &&
        q.u == std::array{q.uv[2], 0.F, 0.F} && q.v == std::array{0.F, q.uv[3], 0.F};
}

Vec3 source_point(const ConstructionQuad &q, double x, double y) {
    return point(q, (x - q.uv[0]) / q.uv[2], (y - q.uv[1]) / q.uv[3]);
}

Vec3 project(Vec3 p, Vec3 center, double focal) {
    return center + (p - center) * (1 / (1 - p.z / focal));
}

bool covers(const ConstructionQuad &q, Vec3 p, Vec3 center, double focal) {
    const Vec3 a = project(point(q, 0, 0), center, focal), b = project(point(q, 1, 0), center, focal),
        c = project(point(q, 1, 1), center, focal), d = project(point(q, 0, 1), center, focal);
    const auto cross = [p](Vec3 start, Vec3 end) {
        return (end.x - start.x) * (p.y - start.y) - (end.y - start.y) * (p.x - start.x);
    };
    const std::array sides{cross(a, b), cross(b, c), cross(c, d), cross(d, a)};
    return std::ranges::all_of(sides, [](double side) { return side > 0; }) ||
        std::ranges::all_of(sides, [](double side) { return side < 0; });
}

void check_bounds(const ConstructionBatch &batch, Box bounds, Box body, Box capture) {
    CHECK(!batch.overflow && is_usable(bounds));
    CHECK(bounds.x <= -2 && bounds.y <= -2);
    CHECK(bounds.x + bounds.w >= capture.w + 2 && bounds.y + bounds.h >= capture.h + 2);
    const Vec3 center = local(Vec3{body.x + body.w * .5, body.y + body.h * .5, 0}, capture);
    for (const auto &q : quads(batch)) {
        for (double u : {0., 1.}) {
            for (double v : {0., 1.}) {
                const Vec3 xy = project(point(q, u, v), center, 4 * std::max({body.w, body.h, 64.}));
                CHECK(xy.x >= bounds.x + 2 - EPS && xy.x <= bounds.x + bounds.w - 2 + EPS);
                CHECK(xy.y >= bounds.y + 2 - EPS && xy.y <= bounds.y + bounds.h - 2 + EPS);
            }
        }
    }
}

animation::Step sample(Box body, Box capture, bool held, double dt) {
    return {.elapsed = dt, .body = body, .capture = capture, .goal_body = body, .rest_capture = capture,
        .screen = SCREEN, .work_area = SCREEN, .motion = {}, .movement_enabled = true, .held = held};
}

bool cable(const ConstructionQuad &q, Vec3 from, Vec3 to) {
    return q.surface == ConstructionSurface::Solid && std::hypot(q.v[0], q.v[1]) < 2 &&
        near(point(q, 0, .5), from) && near(point(q, 1, .5), to);
}

void crane_tracks_live_corners_and_renders_through_exit(bool right_side, bool large_outset) {
    const Box start = right_side ? Box{640.25, 230.5, 240, 225} : HOME;
    const std::array outputs{Box{-720, 0, 420, 800}, SCREEN, Box{1400, 0, 360, 800}};
    IntactTransport crane{IntactTransportKind::Crane, 1307};
    ConstructionBatch batch;
    bool carried = false, slackened = false, departing = false, seen_secondary = false;
    Vec3 last_track;
    for (int i = 0; i < 1200; ++i) {
        const double t = i * DT;
        const double out = smoothstep(2, 3, t), back = smoothstep(3.35, 4.35, t);
        const Box body{start.x + 100 * out - 130 * back, start.y - 40 * out + 50 * back, start.w, start.h};
        const Box capture = decorated(body, large_outset ? 80.5 : 7.5);
        crane.step(sample(body, capture, t < 4.45, i != 0 ? DT : 0),
            right_side ? std::span<const Box>{} : std::span<const Box>{outputs});
        const Box bounds = emit_crane_batch(batch, body, capture, crane);
        if (!crane.active()) {
            CHECK(i > 0 && batch.count == 0 && !is_usable(bounds));
            break;
        }
        check_bounds(batch, bounds, body, capture);
        CHECK(crane.crane());
        if (!crane.crane()) continue;
        const auto &rig = *crane.crane();
        CHECK(rig.facing == (right_side ? -1 : 1));
        const auto &face = crane.face();
        const double sx = body.x - capture.x, sy = body.y - capture.y;
        const auto commands = quads(batch);
        const auto front = std::find_if(commands.begin(), commands.end(), [&](const auto &q) {
            return q.surface == ConstructionSurface::Client && q.uv[0] <= sx && q.uv[1] <= sy &&
                q.uv[0] + q.uv[2] >= sx + body.w && q.uv[1] + q.uv[3] >= sy + body.h;
        });
        CHECK(front != commands.end());
        if (front == commands.end()) continue;
        CHECK(near(source_point(*front, sx, sy), local(face.origin, capture)));
        CHECK(near(source_point(*front, sx + body.w, sy), local(face.point({body.w, 0}), capture)));
        CHECK(face.lift > 0 || is_identity(*front));
        if (face.lift > 0) {
            carried = true;
            CHECK(rig.wire_count == 2 && rig.wires[0].taut);
            CHECK(std::count_if(commands.begin(), commands.end(), [](const ConstructionQuad &q) {
                return q.surface == ConstructionSurface::Client;
            }) == (large_outset ? 5 : 1));
        }
        const auto sheave = std::find_if(front + 1, commands.end(), [&](const auto &q) {
            return is_part(q, SHEAVE) && near(anchor(q, SHEAVE.pivot), local(rig.sheave, capture));
        });
        CHECK(sheave != commands.end());
        CHECK(std::none_of(commands.begin(), commands.end(), [](const auto &q) {
            return is_part(q, HEAD) || is_part(q, TORSO) || is_part(q, TIMBER);
        }));
        CHECK(rig.wire_count == 0 || rig.wire_count == 2);
        for (std::size_t j = 0; j < rig.wire_count; ++j) {
            const auto &wire = rig.wires[j];
            const Vec3 root = local(wire.sheave, capture), mid = local(wire.midpoint, capture),
                tip = local(wire.end, capture);
            if (length(tip - root) <= .02) continue;
            if (wire.taut) {
                CHECK(std::count_if(front + 1, sheave, [&](const auto &q) { return cable(q, root, tip); }) == 1);
            } else if (!near(mid, mix(root, tip, .5))) {
                CHECK(std::count_if(front + 1, sheave, [&](const auto &q) { return cable(q, root, mid); }) == 1);
                CHECK(std::count_if(front + 1, sheave, [&](const auto &q) { return cable(q, mid, tip); }) == 1);
                slackened |= wire.attached && t > 4.45;
            }
            if (wire.attached) CHECK(near(tip, source_point(*front, sx + static_cast<double>(j) * body.w, sy)));
        }
        if (rig.departing) {
            CHECK(rig.wire_count == 0 && rig.extension == 0);
            const auto track = std::find_if(front + 1, commands.end(), [](const auto &q) { return is_part(q, CHASSIS); });
            CHECK(track != commands.end());
            if (departing) CHECK((rig.track.x - last_track.x) * rig.facing < 0);
            departing = true;
            last_track = {rig.track.x, rig.track.y, 0};
            seen_secondary |= rig.track.x > -720 && rig.track.x < -300;
        }
    }
    CHECK(carried && slackened && departing && (right_side || seen_secondary) && !crane.active());
}

void pieces_are_the_only_client_geometry_and_gloves_meet_the_print() {
    using Placement = PieceRelocation::Placement;
    PieceRelocation model;
    const Box far{1120.5, 212.25, HOME.w, HOME.h};
    const Box left{-318.75, -57.5, HOME.w, HOME.h};
    model.reset(HOME, 1307);
    model.step(far, 0, true);
    ConstructionBatch batch;
    bool hole = false, tilted = false, hands = false, seated = false;
    for (int i = 0; i < 850 && model.active(); ++i) {
        const Box body = i < 118 ? far : left;
        const bool large_outset = (i & 1) != 0;
        const Box capture = decorated(body, large_outset ? 78.5 : 7.5);
        model.step(body, i == 0 ? 0 : DT, i < 200);
        const Box bounds = emit_piece_batch(batch, body, capture, model);
        if (!model.active()) {
            CHECK(batch.count == 0 && !is_usable(bounds));
            break;
        }
        check_bounds(batch, bounds, body, capture);
        const auto commands = quads(batch);
        const double sx = body.x - capture.x, sy = body.y - capture.y;
        const auto front = [&](const ConstructionQuad &q) {
            return q.surface == ConstructionSurface::Client && q.uv[0] < sx + body.w && q.uv[1] < sy + body.h &&
                q.uv[0] + q.uv[2] > sx && q.uv[1] + q.uv[3] > sy;
        };
        auto next = commands.begin();
        bool all_seated = true;
        for (const auto &f : model.faces()) {
            next = std::find_if(next, commands.end(), front);
            CHECK(next != commands.end());
            if (next == commands.end()) break;
            const auto &q = *next++;
            const double x = sx + f.source.x, y = sy + f.source.y;
            CHECK(near(source_point(q, x, y), local(f.origin, capture)));
            CHECK(near(source_point(q, x + f.source.w, y), local(f.origin + f.u, capture)));
            CHECK(near(source_point(q, x, y + f.source.h), local(f.origin + f.v, capture)));
            CHECK(std::abs(std::max<double>(q.uv[0], sx) - x) < EPS);
            CHECK(std::abs(std::max<double>(q.uv[1], sy) - y) < EPS);
            CHECK(std::abs(std::min<double>(q.uv[0] + q.uv[2], sx + body.w) - (x + f.source.w)) < EPS);
            CHECK(std::abs(std::min<double>(q.uv[1] + q.uv[3], sy + body.h) - (y + f.source.h)) < EPS);
            tilted |= f.placement == Placement::Carried && std::abs(q.v[2]) > .1;
            if (!hole && !large_outset && f.placement == Placement::Carried &&
                model.faces().front().placement == Placement::Source) {
                const Vec3 vacated = local(Vec3{HOME.x + f.source.x + f.source.w * .5,
                    HOME.y + f.source.y + f.source.h * .5, 0}, capture);
                const Vec3 center{sx + body.w * .5, sy + body.h * .5, 0};
                const double focal = 4 * std::max({body.w, body.h, 64.});
                hole = std::none_of(commands.begin(), commands.end(), [&](const auto &cmd) {
                    return covers(cmd, vacated, center, focal);
                });
            }
            all_seated &= f.placement == Placement::Seated && is_identity(q);
        }
        CHECK(std::none_of(next, commands.end(), front));
        seated |= all_seated;
        for (const auto &q : commands) {
            if (q.surface == ConstructionSurface::Client) {
                CHECK(q.uv[0] >= 0 && q.uv[1] >= 0 && q.uv[0] + q.uv[2] <= capture.w + EPS &&
                    q.uv[1] + q.uv[3] <= capture.h + EPS);
                if (!front(q)) CHECK(large_outset && is_identity(q));
            } else {
                CHECK(q.surface == ConstructionSurface::Atlas &&
                    (q.uv[1] * ATLAS_HEIGHT < 64 || is_part(q, FAR_THIGH) || is_part(q, FAR_SHIN) ||
                        is_part(q, NEAR_BOOT) || is_part(q, FAR_BOOT) || is_part(q, GLOVE)));
            }
        }
        for (const auto &w : model.workers()) {
            if (!w.gripping || w.visibility < .5) continue;
            for (const auto &arm : w.arms) {
                constexpr Vec2 PALM{46.6, 33}; // The painted knuckle pad, not the edge of the transparent cell.
                const auto glove = std::find_if(commands.begin(), commands.end(), [&](const auto &q) {
                    return is_part(q, GLOVE) && near(anchor(q, PALM), local(arm.tip, capture));
                });
                CHECK(glove != commands.end());
                hands |= glove != commands.end();
            }
            for (std::size_t k = 0; k < w.legs.size(); ++k) {
                const Part boot = k == 0 ? NEAR_BOOT : FAR_BOOT;
                const Vec2 sole{boot.tip.x, boot.tip.y + .25};
                CHECK(std::ranges::any_of(commands, [&](const auto &q) {
                    return is_part(q, boot) && near(anchor(q, sole), local(w.legs[k].tip, capture));
                }));
            }
        }
    }
    CHECK(hole && tilted && hands && seated && !model.active());
    CHECK(!is_usable(emit_piece_batch(batch, far, decorated(far), model)) && batch.count == 0);
}

void stale_geometry_clears_both_batches() {
    const Box moved{HOME.x + 27.5, HOME.y - 12.25, HOME.w, HOME.h};
    const Box capture = decorated(moved);
    IntactTransport crane{IntactTransportKind::Crane, 1307};
    crane.step(sample(moved, capture, true, 0));
    PieceRelocation pieces;
    pieces.reset(HOME, 1307);
    pieces.step(moved, 0, true);
    ConstructionBatch batch;
    CHECK(is_usable(emit_crane_batch(batch, moved, capture, crane)) && batch.count > 0);
    CHECK(!is_usable(emit_crane_batch(batch, HOME, decorated(HOME), crane)));
    CHECK(batch.count == 0 && !batch.overflow);
    CHECK(is_usable(emit_piece_batch(batch, moved, capture, pieces)) && batch.count > 0);
    CHECK(!is_usable(emit_piece_batch(batch, moved, {}, pieces)));
    CHECK(batch.count == 0 && !batch.overflow);
}
}  // namespace

int main() {
    crane_tracks_live_corners_and_renders_through_exit(false, false);
    crane_tracks_live_corners_and_renders_through_exit(true, true);
    pieces_are_the_only_client_geometry_and_gloves_meet_the_print();
    stale_geometry_clears_both_batches();
    return g_failures == 0 ? 0 : 1;
}
