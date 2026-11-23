#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>

#include "Check.hpp"
#include "window/construction/ConstructionArt.hpp"
#include "window/construction/ConstructionMotionArt.hpp"
#include "window/construction/studies/EdgeStretch.hpp"
#include "window/construction/studies/IntactTransport.hpp"

namespace {
using namespace hyprimate;
using namespace hyprimate::construction;
using namespace hyprimate::construction::studies;

constexpr Box AREA{-960, 180, 1280, 800};
constexpr Box HOME{-782.75, 474.5, 654, 402};
constexpr double DT = 1. / 120;
constexpr double EPS = .001;

Box decorated(Box body) {
    return {body.x - 3.25, body.y - 8.5, body.w + 18.25, body.h + 21};
}

Box local_body(Box body, Box capture) {
    return {body.x - capture.x, body.y - capture.y, body.w, body.h};
}

Vec3 local(Vec3 p, Box capture) {
    return {p.x - capture.x, p.y - capture.y, p.z};
}

bool near(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) < EPS;
}

Vec3 point(const ConstructionQuad &quad, double s, double t) {
    return {quad.origin[0] + s * quad.u[0] + t * quad.v[0],
        quad.origin[1] + s * quad.u[1] + t * quad.v[1],
        quad.origin[2] + s * quad.u[2] + t * quad.v[2]};
}

Vec3 anchor(const ConstructionQuad &quad, Vec2 atlas_point) {
    return point(quad, (atlas_point.x - 4) / 56, (atlas_point.y - 4) / 56);
}

Vec3 project(Vec3 p, Box body) {
    const Vec3 center{body.x + body.w / 2, body.y + body.h / 2, 0};
    const double w = 1 - p.z / (4 * std::max({body.w, body.h, 64.0}));
    return {center.x + (p.x - center.x) / w, center.y + (p.y - center.y) / w, 0};
}

std::span<const ConstructionQuad> quads(const ConstructionBatch &batch) {
    return {batch.quads.data(), batch.count};
}

bool has_uv(const ConstructionQuad &quad, Box b) {
    return quad.uv == std::array{static_cast<float>(b.x), static_cast<float>(b.y),
        static_cast<float>(b.w), static_cast<float>(b.h)};
}

bool is_identity(const ConstructionQuad &quad) {
    return quad.surface == ConstructionSurface::Client &&
        quad.origin == std::array{quad.uv[0], quad.uv[1], 0.F} &&
        quad.u == std::array{quad.uv[2], 0.F, 0.F} &&
        quad.v == std::array{0.F, quad.uv[3], 0.F};
}

bool is_part(const ConstructionQuad &quad, Part part) {
    return quad.surface == ConstructionSurface::Atlas &&
        quad.uv[0] * ATLAS_WIDTH == part.col * 64 + 4 &&
        quad.uv[1] * ATLAS_HEIGHT == part.row * 64 + 4;
}

const ConstructionQuad *paint(const ConstructionBatch &batch, Part part, Vec2 from, Vec3 at) {
    for (const auto &q : quads(batch))
        if (is_part(q, part) && near(anchor(q, from), at)) return &q;
    return nullptr;
}

void check_bounds(const ConstructionBatch &batch, Box bounds, Box body, Box capture) {
    CHECK(!batch.overflow && is_usable(bounds));
    CHECK(bounds.x <= -2 && bounds.y <= -2);
    CHECK(bounds.x + bounds.w >= capture.w + 2 && bounds.y + bounds.h >= capture.h + 2);
    for (const auto &q : quads(batch)) {
        for (int u : {0, 1}) {
            for (int v : {0, 1}) {
                const Vec3 p = project(point(q, u, v), local_body(body, capture));
                CHECK(p.x >= bounds.x + 2 - EPS && p.x <= bounds.x + bounds.w - 2 + EPS);
                CHECK(p.y >= bounds.y + 2 - EPS && p.y <= bounds.y + bounds.h - 2 + EPS);
            }
        }
    }
}

const ConstructionQuad *only_client(const ConstructionBatch &batch, Box source) {
    const ConstructionQuad *face = nullptr;
    for (const auto &q : quads(batch)) {
        if (q.surface != ConstructionSurface::Client) continue;
        CHECK(!face);
        face = &q;
    }
    CHECK(face && has_uv(*face, source));
    return face;
}

animation::Step sample(Box body, bool held, double dt = DT) {
    return {.elapsed = dt, .body = body, .capture = decorated(body), .goal_body = body,
        .rest_capture = decorated(body), .screen = AREA, .work_area = AREA, .motion = {},
        .movement_enabled = true, .held = held};
}

void native_acquisition_and_decline() {
    IntactTransport carry{IntactTransportKind::CrewCarry, 1307};
    const Box moved{HOME.x + 18, HOME.y - 12, HOME.w, HOME.h};
    const Box capture = decorated(moved);
    auto first = sample(moved, true, 0);
    first.motion = animation::Motion{.from_body = HOME, .from_capture = decorated(HOME),
        .body = moved, .capture = capture, .translated = true, .sample_seconds = DT};
    carry.step(first);
    CHECK(carry.active());

    ConstructionBatch batch;
    // Reusing the publication must clear failure and any old artwork without clearing the array.
    batch.count = batch.quads.size();
    batch.overflow = true;
    const Box bounds = emit_motion_batch(batch, moved, capture, &carry, nullptr);
    check_bounds(batch, bounds, moved, capture);
    const auto *face = only_client(batch, {0, 0, capture.w, capture.h});
    CHECK(batch.count == 1 && face && is_identity(*face));

    CHECK(!is_usable(emit_motion_batch(batch, HOME, capture, &carry, nullptr))); // Stale publication.
    CHECK(batch.count == 0 && !batch.overflow);
    CHECK(!is_usable(emit_motion_batch(batch, moved, {moved.x + 1, moved.y, moved.w, moved.h}, &carry, nullptr)));
    CHECK(!is_usable(emit_motion_batch(batch, moved,
        {capture.x, capture.y, std::numeric_limits<double>::max(), capture.h}, &carry, nullptr)));
    CHECK(!is_usable(emit_motion_batch(batch, moved,
        {capture.x, capture.y, capture.w, std::numeric_limits<double>::quiet_NaN()}, &carry, nullptr)));
    CHECK(!is_usable(emit_motion_batch(batch, moved, capture, nullptr, nullptr)));
    CHECK(batch.count == 0);

    auto disabled = sample(moved, true);
    disabled.movement_enabled = false;
    carry.step(disabled);
    CHECK(!carry.active());
    CHECK(!is_usable(emit_motion_batch(batch, moved, capture, &carry, nullptr)));
    CHECK(batch.count == 0);
}

Box carried_itinerary(double t) {
    const double out = smoothstep(1.23, 2.55, t), back = smoothstep(2.85, 3.77, t);
    const double wave = std::sin(std::numbers::pi * out);
    return {HOME.x + 379 * out - 186 * back, HOME.y - 92 * out - 11 * wave * wave + 71 * back,
        HOME.w, HOME.h};
}

void inspect_carry_art(const ConstructionBatch &batch, const ConstructionQuad &face,
    const IntactTransport &carry, Box capture) {
    int rear = 0, front = 0, contacts = 0, planted = 0;
    for (const auto &actor : carry.bearers()) {
        const auto *head = paint(batch, HEAD, HEAD.pivot, local(actor.head, capture));
        CHECK(head && (head < &face) == actor.behind_face);
        const auto *torso = paint(batch, TORSO, TORSO.pivot, local(actor.hips, capture));
        CHECK(torso && near(anchor(*torso, TORSO.tip), local(actor.neck, capture)));
        (actor.behind_face ? rear : front) += head != nullptr;
        const auto *shadow = paint(batch, CONTACT_SHADOW, CONTACT_SHADOW.pivot,
            local({actor.station.x, actor.station.y, -.086 * actor.height}, capture));
        CHECK(shadow && shadow < &face);
        for (unsigned i : {0U, 1U}) {
            const auto &leg = actor.legs[i];
            const auto boot = i == 0 ? NEAR_BOOT : FAR_BOOT;
            const auto *shoe = paint(batch, boot, boot.pivot, local(leg.ankle, capture));
            CHECK(shoe && near(anchor(*shoe, {boot.tip.x, boot.tip.y + .25}), local(leg.sole, capture)));
            planted += leg.planted;
            const auto &arm = actor.arms[i];
            const auto sleeve = i == 0 ? NEAR_SLEEVE : FAR_SLEEVE;
            const auto forearm = i == 0 ? NEAR_FOREARM : FAR_FOREARM;
            const auto *upper = paint(batch, sleeve, sleeve.pivot, local(arm.shoulder, capture));
            const auto *lower = paint(batch, forearm, forearm.pivot, local(arm.elbow, capture));
            CHECK(upper && near(anchor(*upper, sleeve.tip), local(arm.elbow, capture)));
            CHECK(lower && near(anchor(*lower, forearm.tip), local(arm.wrist, capture)));
            if (!arm.grip) continue;
            constexpr Vec2 palm{46.6, 33}; // Center of the painted glove's knuckle pad.
            const Vec3 grip = local({arm.grip->x, arm.grip->y, 0}, capture);
            const auto *glove = paint(batch, GLOVE, palm, grip);
            CHECK(glove && glove > &face);
            if (glove) {
                CHECK(near(anchor(*glove, GLOVE.pivot), local(arm.wrist, capture)));
                CHECK(near(project(anchor(*glove, palm), local_body(carry.face().source, capture)), grip));
                ++contacts;
            }
        }
    }
    CHECK(rear >= 1 && front >= 1 && contacts >= 4 && planted >= 2);
}

void carried_face_trims_and_backdrop() {
    IntactTransport carry{IntactTransportKind::CrewCarry, 1307};
    Box body;
    for (int i = 0; i <= 324; ++i) {
        const double t = i * DT;
        body = carried_itinerary(t);
        carry.step(sample(body, t >= .5, i == 0 ? 0 : DT));
    }
    CHECK(carry.face().lift > 1.8);
    CHECK(std::abs(carry.face().u.y) > EPS); // This sample exercises lift and rotation.
    const Box capture = decorated(body);
    const Box b = local_body(body, capture);
    ConstructionBatch batch;
    const Box bounds = emit_motion_batch(batch, body, capture, &carry, nullptr);
    check_bounds(batch, bounds, body, capture);
    CHECK(bounds.h > capture.h + 6); // Feet and projected cutout corners extend past the capture.
    const auto *face = only_client(batch, {0, 0, capture.w, capture.h});
    if (!face) return;
    CHECK(!is_identity(*face));
    for (Vec2 p : std::array{Vec2{0, 0}, Vec2{body.w, 0}, Vec2{0, body.h},
             Vec2{body.w, body.h}, Vec2{-b.x, -b.y}, Vec2{capture.w - b.x, capture.h - b.y}}) {
        const Vec2 expected = carry.face().point(p);
        CHECK(near(point(*face, (p.x + b.x) / capture.w, (p.y + b.y) / capture.h),
            local({expected.x, expected.y, 0}, capture)));
    }
    inspect_carry_art(batch, *face, carry, capture);

    const Box shadow = carry.load_shadow();
    bool has_load_shadow = false;
    for (const auto &q : quads(batch)) {
        if (q.surface != ConstructionSurface::Shadow) continue;
        has_load_shadow |= &q < face && near(point(q, 0, 0), local({shadow.x, shadow.y, 0}, capture)) &&
            near(point(q, 1, 1), local({shadow.x + shadow.w, shadow.y + shadow.h, 0}, capture));
    }
    CHECK(has_load_shadow);

    // Scene-sized outsets can contain unrelated composition. Only the authorized body moves;
    // the rest remains in place, without sampling an old body as part of the backdrop.
    const Box in_scene = local_body(body, AREA);
    const Box scene_bounds = emit_motion_batch(batch, body, AREA, &carry, nullptr);
    check_bounds(batch, scene_bounds, body, AREA);
    constexpr int MARGINS = 4;
    int stationary = 0, moving = 0;
    double source_area = 0;
    const ConstructionQuad *posed = nullptr;
    for (const auto &q : quads(batch)) {
        if (q.surface != ConstructionSurface::Client) continue;
        source_area += static_cast<double>(q.uv[2]) * q.uv[3];
        if (has_uv(q, in_scene)) {
            CHECK(!is_identity(q));
            const Vec2 origin = carry.face().origin;
            CHECK(near(point(q, 0, 0), local({origin.x, origin.y, 0}, AREA)));
            posed = &q;
            ++moving;
        } else {
            CHECK(!posed && is_identity(q));
            CHECK(q.uv[0] >= 0 && q.uv[1] >= 0 && q.uv[0] + q.uv[2] <= AREA.w && q.uv[1] + q.uv[3] <= AREA.h);
            CHECK(q.uv[0] + q.uv[2] <= in_scene.x || q.uv[0] >= in_scene.x + in_scene.w ||
                  q.uv[1] + q.uv[3] <= in_scene.y || q.uv[1] >= in_scene.y + in_scene.h);
            ++stationary;
        }
    }
    CHECK(moving == 1 && stationary == MARGINS && std::abs(source_area - AREA.w * AREA.h) < .1);

    for (int i = 0; i < 60 && carry.face().lift > 0; ++i) carry.step(sample(body, false));
    CHECK(carry.active() && carry.face().lift == 0); // Set down before the workers disperse.
    CHECK(is_usable(emit_motion_batch(batch, body, capture, &carry, nullptr)));
    const auto *seated = only_client(batch, {0, 0, capture.w, capture.h});
    CHECK(seated && is_identity(*seated));
    for (int i = 0; i < 120; ++i) carry.step(sample(body, false));
    CHECK(!is_usable(emit_motion_batch(batch, body, capture, &carry, nullptr)));
    CHECK(batch.count == 0);
}

void resize_retains_current_identity_and_grips() {
    EdgeStretch stretch{42};
    EdgeSet grab;
    grab.add(Edge::Right);
    auto step = [&](Box body, double elapsed, EdgeSet edges) {
        stretch.step({.body = body, .work_area = AREA, .elapsed = elapsed, .grab = edges});
    };
    Box body = HOME;
    step(body, 0, grab);
    ConstructionBatch batch;
    CHECK(is_usable(emit_motion_batch(batch, body, body, nullptr, &stretch)));
    const auto *first = only_client(batch, {0, 0, body.w, body.h});
    CHECK(batch.count == 1 && first && is_identity(*first));

    for (int i = 0; i < 100; ++i) step(body, DT, grab);
    for (int i = 0; i < 12; ++i) {
        body.w += .125;
        step(body, DT, grab);
    }
    const Box capture = decorated(body);
    const Box bounds = emit_motion_batch(batch, body, capture, nullptr, &stretch);
    check_bounds(batch, bounds, body, capture);
    const auto *face = only_client(batch, {0, 0, capture.w, capture.h});
    CHECK(face && is_identity(*face));
    CHECK(bounds.x + bounds.w > capture.w + 6); // Not clipped to the native resize box.
    if (!face) return;

    int attached = 0, boots = 0;
    for (const auto &actor : stretch.workers()) {
        for (unsigned i : {0U, 1U}) {
            const auto &leg = actor.legs[i];
            const auto boot = i == 0 ? NEAR_BOOT : FAR_BOOT;
            const auto *shoe = paint(batch, boot, boot.pivot, local(leg.ankle, capture));
            if (shoe && leg.planted) {
                CHECK(shoe > face);
                CHECK(near(anchor(*shoe, {boot.tip.x, boot.tip.y + .25}), local(leg.sole, capture)));
                ++boots;
            }
            const auto &arm = actor.arms[i];
            if (!arm.gripping) continue;
            const auto *glove = paint(batch, GLOVE, GLOVE.tip, local(arm.glove, capture));
            CHECK(glove && glove > face);
            if (glove) {
                const Vec3 tip = project(anchor(*glove, GLOVE.tip), local_body(body, capture));
                CHECK(std::abs(tip.x - (body.x + body.w - capture.x)) <= .5 + EPS);
                ++attached;
            }
        }
    }
    CHECK(attached > 0 && boots > 0);

    const IntactTransport carry{IntactTransportKind::CrewCarry, 42};
    CHECK(!is_usable(emit_motion_batch(batch, body, capture, &carry, &stretch)));
    CHECK(batch.count == 0);
    for (int i = 0; i < 120; ++i) step(body, DT, {});
    CHECK(!is_usable(emit_motion_batch(batch, body, capture, nullptr, &stretch)));
    CHECK(batch.count == 0);
}
}  // namespace

int main() {
    native_acquisition_and_decline();
    carried_face_trims_and_backdrop();
    resize_retains_current_identity_and_grips();
    return g_failures == 0 ? 0 : 1;
}
