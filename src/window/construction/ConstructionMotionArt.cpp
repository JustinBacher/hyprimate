#include "ConstructionMotionPaint.hpp"

#include "studies/EdgeStretch.hpp"
#include "studies/IntactTransport.hpp"

namespace hyprimate::construction {
namespace {
using studies::CarryBearerPose;
using studies::EdgeWorkerPose;
using namespace motion_paint;

void paint_bearer(Paint &paint, const CarryBearerPose &actor) noexcept {
    const Tint color = tint(WHITE, actor.opacity);
    const Tint vest = tint(VESTS[actor.vest % VESTS.size()], actor.opacity);
    const auto leg = [&](unsigned i) {
        paint_leg(paint, i, actor.legs[i], actor.height, actor.facing, actor.legs[i].boot_pitch, color);
    };
    const auto arm = [&](unsigned i) {
        const auto &a = actor.arms[i];
        paint_arm(paint, i, a, a.fingertips, actor.facing, color, vest, !actor.behind_face || !a.grip);
    };
    leg(1);
    arm(1);
    leg(0);
    paint_upper_body(paint, actor.hips, actor.neck, actor.head, actor.height, actor.facing, vest);
    arm(0);
}

void paint_carry(Paint &paint, const studies::IntactTransport &carry, Box body, Box capture) noexcept {
    const auto &face = carry.face();
    if (!std::isfinite(face.lift) || face.lift < 0) {
        paint.reject();
        return;
    }
    const auto actors = carry.bearers();
    const Box cap{0, 0, capture.w, capture.h};
    const bool lifted = face.lift > 0;
    const bool separate_backdrop =
        std::max({body.x, body.y, cap.w - right(body), cap.h - bottom(body)}) > MAX_TRIM &&
        (lifted || std::ranges::any_of(actors, [](const auto &actor) { return visible(actor.opacity); }));
    if (separate_backdrop) paint.backdrop(body);

    for (const auto &actor : actors) {
        if (!visible(actor.opacity)) continue;
        paint_worker_shadow(paint, {actor.station.x, actor.station.y, -.086 * actor.height}, actor.height, actor.opacity);
    }
    if (lifted) {
        const Box shadow = carry.load_shadow();
        if (is_usable(shadow))
            paint.add(paint.local({shadow.x, shadow.y, 0}), {shadow.w, 0, 0}, {0, shadow.h, 0},
                tint({14, 21, 28, 255}, (105. / 255) * face.lift / 2.78), ConstructionSurface::Shadow);
    }
    for (const auto &actor : actors)
        if (actor.behind_face && visible(actor.opacity)) paint_bearer(paint, actor);

    if (!lifted) {
        paint.identity(separate_backdrop ? body : cap);
    } else {
        const Vec3 origin = paint.local({face.origin.x, face.origin.y, 0});
        const Vec3 u{face.u.x, face.u.y, 0}, v{face.v.x, face.v.y, 0};
        // The lower return is paint, not a second image. Use the same rigid basis as the grips.
        paint.add(origin + v, u, {0, .91 * face.lift, 0}, {74, 64, 53, 255});
        if (separate_backdrop) {
            paint.client(body, origin, u, v);
        } else {
            // A single affine quad includes the body and ordinary trim, so no subdivision
            // line can open along a moving decorated edge. Source UVs stay unposed.
            const Vec3 dx = u * (1 / body.w), dy = v * (1 / body.h);
            paint.client(cap, origin - dx * body.x - dy * body.y, dx * cap.w, dy * cap.h);
        }
    }

    for (const auto &actor : actors) {
        if (!visible(actor.opacity)) continue;
        if (!actor.behind_face) {
            paint_bearer(paint, actor);
        } else {
            // Retain rear silhouette occlusion, but put each latched glove over the rim.
            // Its painted palm (.76 of wrist→fingertips) is the model's z=0 grip point.
            const Tint color = tint(WHITE, actor.opacity);
            for (const auto &arm : actor.arms)
                if (arm.grip) paint_hand(paint, arm.wrist, arm.fingertips, actor.facing, color);
        }
    }
}

Vec3 stand(const EdgeWorkerPose &actor) noexcept {
    return mix(actor.legs[0].sole, actor.legs[1].sole, .5);
}

void paint_stretch(Paint &paint, const studies::EdgeStretch &stretch, Box capture) noexcept {
    // Source and destination are today's native capture. The crew alone suggests strain.
    paint.identity({0, 0, capture.w, capture.h});
    struct Layer {
        const EdgeWorkerPose *actor;
        double ground_y;
    };
    std::array<Layer, 8> ordered;  // Up to two slots per edge.
    std::size_t count = 0;
    for (const auto &actor : stretch.workers()) {
        if (!visible(actor.opacity)) continue;
        const Vec3 feet = stand(actor);
        if (count == ordered.size() || !std::isfinite(feet.y)) {
            paint.reject();
            return;
        }
        ordered[count++] = {&actor, feet.y};
        paint_worker_shadow(paint, feet, actor.height, actor.opacity);
    }
    const auto end = ordered.begin() + static_cast<std::ptrdiff_t>(count);
    std::ranges::sort(ordered.begin(), end, {}, &Layer::ground_y);
    for (auto it = ordered.begin(); it != end; ++it) {
        const auto &actor = *it->actor;
        const Tint color = tint(WHITE, actor.opacity);
        const Tint vest = tint(VESTS[actor.variant % VESTS.size()], actor.opacity);
        const double height = actor.height, facing = actor.facing;
        paint_leg(paint, 1, actor.legs[1], height, facing, 0, color);
        paint_arm(paint, 1, actor.arms[1], actor.arms[1].glove, facing, color, vest, true);
        paint_leg(paint, 0, actor.legs[0], height, facing, 0, color);
        const Vec3 head = actor.neck + Vec3{facing * height * .02, -height * .012, height * .024};
        paint_upper_body(paint, actor.hip, actor.neck, head, height, facing, vest);
        paint_arm(paint, 0, actor.arms[0], actor.arms[0].glove, facing, color, vest, true);
    }
}

}  // namespace

Box emit_motion_batch(ConstructionBatch &batch, Box body, Box capture,
    const studies::IntactTransport *carry, const studies::EdgeStretch *stretch) noexcept {
    batch.count = 0;
    batch.overflow = false;
    if ((carry == nullptr) == (stretch == nullptr) || !valid_box(body) || !valid_box(capture)) return {};
    if (carry && (!carry->active() || carry->crane() || !same_box(carry->face().source, body))) return {};
    if (stretch && (!stretch->active() || !same_box(stretch->body(), body))) return {};

    const Box local{body.x - capture.x, body.y - capture.y, body.w, body.h};
    if (local.x < 0 || local.y < 0 || right(local) > capture.w || bottom(local) > capture.h) return {};
    Paint paint(batch, local, capture);
    if (carry) paint_carry(paint, *carry, local, capture);
    else paint_stretch(paint, *stretch, capture);
    return paint.finish();
}

}  // namespace hyprimate::construction
