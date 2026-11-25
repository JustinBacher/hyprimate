#include "ConstructionMotionPaint.hpp"
#include "studies/IntactTransport.hpp"

namespace hyprimate::construction {
namespace {
using namespace motion_paint;
Vec3 flat(Vec2 p) noexcept { return {p.x, p.y, 0}; }
void atlas_box(Paint &paint, Part part, Box box, int facing, Tint color = WHITE) noexcept {
    const Vec3 x{facing * box.w / 56, 0, 0}, y{0, box.h / 56, 0};
    const Vec3 corner{facing == 1 ? box.x : right(box), box.y, 0};
    paint.atlas(part, corner + x * (part.pivot.x - 4) + y * (part.pivot.y - 4), x, y, color);
}

Vec3 normal(Vec3 delta) noexcept { return unit({-delta.y, delta.x, 0}, {}); }

void ribbon(Paint &paint, Vec3 from, Vec3 to, Vec3 width, Tint color) noexcept {
    paint.add(paint.local(from - width * .5), to - from, width, color);
}

void stroke(Paint &paint, Vec3 from, Vec3 to, double width, Tint color) noexcept {
    ribbon(paint, from, to, normal(to - from) * width, color);
}

void paint_boom(Paint &paint, const studies::CarryCranePose &rig) noexcept {
    constexpr Tint OUTLINE{34, 49, 58, 255}, GOLD{226, 164, 65, 255}, LIGHT{255, 212, 115, 255},
        STEEL{144, 178, 177, 255};
    const Vec3 heel = flat(rig.heel), collar = flat(rig.collar), nose = flat(rig.nose), sheave = flat(rig.sheave);
    const Vec3 side = normal(collar - heel) * 5.5;
    // Twin chords and diagonal bracing keep the heel readable as an open lattice against the desktop.
    const double span = length(collar - heel);
    const int cells = 1 + static_cast<int>(std::clamp(span / 32, 0., 7.));
    Vec3 previous = heel - side;
    for (int i = 1; i <= cells; ++i) {
        const Vec3 next = mix(heel, collar, static_cast<double>(i) / cells) + side * (i % 2 ? 1 : -1);
        stroke(paint, previous, next, 3, OUTLINE);
        stroke(paint, previous, next, 1.5, GOLD);
        previous = next;
    }
    stroke(paint, heel - side, collar - side, 4, OUTLINE);
    stroke(paint, heel + side, collar + side, 4, OUTLINE);
    stroke(paint, heel - side, collar - side, 2, LIGHT);
    stroke(paint, heel + side, collar + side, 2, GOLD);
    stroke(paint, collar, nose, 9, OUTLINE);
    stroke(paint, collar, nose, 5, GOLD);
    stroke(paint, nose, sheave, 6, OUTLINE);
    stroke(paint, nose, sheave, 3, STEEL);
    const Vec3 ram_base = flat(rig.ram_base), ram_tip = flat(rig.ram_tip), ram_rod = mix(ram_base, ram_tip, .57);
    stroke(paint, ram_base, ram_tip, 4, OUTLINE);
    stroke(paint, ram_base, ram_rod, 6, OUTLINE);
    stroke(paint, ram_base, ram_rod, 3.5, GOLD);
    stroke(paint, ram_rod, ram_tip, 2, STEEL);
}

void paint_crane(Paint &paint, const studies::IntactTransport &crane, Box body, Box capture) noexcept {
    const auto &face = crane.face();
    const auto &rig = *crane.crane();
    const Box cap{0, 0, capture.w, capture.h};
    const bool separate_backdrop = has_scene_outset(body, capture);
    if (separate_backdrop) paint.backdrop(body);

    atlas_box(paint, CONTACT_SHADOW, rig.track_shadow, 1, tint(WHITE, .70));
    if (face.lift > 0) {
        const Box shadow = crane.load_shadow();
        if (is_usable(shadow))
            paint.add(paint.local({shadow.x, shadow.y, 0}), {shadow.w, 0, 0}, {0, shadow.h, 0},
                tint({14, 21, 28, 255}, .32 * face.lift / 2.8), ConstructionSurface::Shadow);
        const Vec3 origin = paint.local(flat(face.origin)), u = flat(face.u), v = flat(face.v);
        paint.add(origin + v, u, {0, .85 * face.lift, 0}, {74, 64, 53, 255});
        if (separate_backdrop) {
            paint.client(body, origin, u, v);
        } else {
            const Vec3 dx = u * (1 / body.w), dy = v * (1 / body.h);
            paint.client(cap, origin - dx * body.x - dy * body.y, dx * cap.w, dy * cap.h);
        }
    } else {
        paint.identity(separate_backdrop ? body : cap);
    }

    // The crawler is in the foreground, including when the apron fits only inside the facade.
    // Put the cab over the boom heel, and the sheave bearing over both cable roots.
    atlas_box(paint, COUNTERWEIGHT, rig.ballast, rig.facing);
    atlas_box(paint, CHASSIS, rig.chassis, rig.facing);
    paint_boom(paint, rig);
    atlas_box(paint, CAB, rig.cab, rig.facing);
    for (std::size_t i = 0; i < rig.wheels.size(); ++i) {
        const Vec3 hub = flat(rig.wheels[i]);
        paint.atlas(FASTENER, hub, {.17, 0, 0}, {0, .17, 0}, tint({210, 231, 249, 255}, .92));
        stroke(paint, hub, flat(rig.tread_marks[i]), 1.35, {220, 233, 224, 255});
    }
    const double cab_x = rig.cab.x + rig.cab.w * (rig.facing == 1 ? .58 : .42);
    const Vec3 lever{cab_x, rig.cab.y + rig.cab.h * .68, 0};
    stroke(paint, lever, lever + Vec3{rig.facing * std::lerp(-2., 5., rig.lever), -6, 0}, 1.6, {239, 197, 107, 255});

    for (std::size_t i = 0; i < rig.wire_count; ++i) {
        const auto &wire = rig.wires[i];
        const Vec3 root = flat(wire.sheave), middle = flat(wire.midpoint), end = flat(wire.end);
        if (end.x == root.x && end.y == root.y) continue;
        constexpr Tint CABLE{79, 103, 119, 255};
        const Vec3 width = normal(end - root) * wire.width;
        if (wire.taut || middle.y == (root.y + end.y) * .5) {
            ribbon(paint, root, end, width, CABLE);
        } else {
            // A shared cross-section joins the slack segments with no crack or doubled line.
            ribbon(paint, root, middle, width, CABLE);
            ribbon(paint, middle, end, width, CABLE);
        }
    }
    paint.atlas(SHEAVE, flat(rig.sheave), {.38, 0, 0}, {0, .38, 0}, WHITE);
}
}  // namespace
Box emit_crane_batch(ConstructionBatch &batch, Box body, Box capture,
    const studies::IntactTransport &crane) noexcept {
    const Box local = begin_transport(batch, body, capture);
    if (!is_usable(local) || !crane.active() || !crane.crane() || !same_box(crane.face().source, body)) return {};
    Paint paint(batch, local, capture);
    paint_crane(paint, crane, local, capture);
    return paint.finish();
}

}  // namespace hyprimate::construction
