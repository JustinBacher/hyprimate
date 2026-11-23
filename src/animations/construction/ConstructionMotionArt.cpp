#include "ConstructionMotionArt.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "ConstructionArt.hpp"
#include "studies/EdgeStretch.hpp"
#include "studies/IntactTransport.hpp"
#include "studies/PieceRelocation.hpp"

namespace hyprimate::construction {
namespace {
using studies::CarryBearerPose;
using studies::EdgeWorkerPose;

constexpr Tint WHITE{255, 255, 255, 255};
constexpr std::array<Tint, 4> VESTS{WHITE, Tint{132, 245, 255, 255}, Tint{255, 179, 158, 255},
    Tint{226, 240, 255, 255}};
constexpr std::array THIGHS{NEAR_THIGH, FAR_THIGH};
constexpr std::array SHINS{NEAR_SHIN, FAR_SHIN};
constexpr std::array BOOTS{NEAR_BOOT, FAR_BOOT};
constexpr std::array SLEEVES{NEAR_SLEEVE, FAR_SLEEVE};
constexpr std::array FOREARMS{NEAR_FOREARM, FAR_FOREARM};
constexpr double AA = 2;
// Same threshold as the opening site: larger outsets can be scene composition rather than trim.
constexpr double MAX_TRIM = 48;
// Keep headroom for the sum of an origin and two axes in the vertex shader.
constexpr double PACKED_LIMIT = std::numeric_limits<float>::max() / 4.0;

bool packable(Vec3 p) noexcept {
    return std::abs(p.x) <= PACKED_LIMIT && std::abs(p.y) <= PACKED_LIMIT && std::abs(p.z) <= PACKED_LIMIT;
}

double right(Box b) noexcept { return b.x + b.w; }
double bottom(Box b) noexcept { return b.y + b.h; }

bool valid_box(Box b) noexcept {
    return is_usable(b) && right(b) > b.x && bottom(b) > b.y && std::isfinite(right(b)) &&
        std::isfinite(bottom(b)) && b.w >= std::numeric_limits<float>::min() &&
        b.h >= std::numeric_limits<float>::min() && b.w <= PACKED_LIMIT && b.h <= PACKED_LIMIT;
}

bool same_box(Box a, Box b) noexcept {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

class Paint {
  public:
    Paint(ConstructionBatch &batch, Box body, Box capture) noexcept
        : m_batch(batch), m_anchor{capture.x, capture.y, 0}, m_capture{0, 0, capture.w, capture.h},
          m_center{static_cast<float>(body.x + body.w * .5), static_cast<float>(body.y + body.h * .5)},
          m_inverse_focal(static_cast<float>(1 / (4 * std::max({body.w, body.h, 64.0})))) {}

    Vec3 local(Vec3 p) const noexcept { return p - m_anchor; }
    void reject() noexcept { m_valid = false; }

    void add(Vec3 p, Vec3 u, Vec3 v, Tint color,
        ConstructionSurface surface = ConstructionSurface::Solid, std::array<float, 4> uv = UNIT_UV) noexcept {
        if (!packable(p) || !packable(u) || !packable(v)) {
            reject();
            return;
        }
        quad(m_batch, p, u, v, color, surface, uv);
    }

    void client(Box source, Vec3 p, Vec3 u, Vec3 v) noexcept {
        add(p, u, v, WHITE, ConstructionSurface::Client,
            {static_cast<float>(source.x), static_cast<float>(source.y),
                static_cast<float>(source.w), static_cast<float>(source.h)});
    }

    void identity(Box source) noexcept {
        if (source.w <= 0 || source.h <= 0) return;
        client(source, {source.x, source.y, 0}, {source.w, 0, 0}, {0, source.h, 0});
    }

    void backdrop(Box body) noexcept {
        // No body pixels are sampled at identity under a moving face.
        identity({0, 0, m_capture.w, body.y});
        identity({0, body.y, body.x, body.h});
        identity({right(body), body.y, m_capture.w - right(body), body.h});
        identity({0, bottom(body), m_capture.w, m_capture.h - bottom(body)});
    }

    void atlas(Part part, Vec3 pivot, Vec3 u, Vec3 v, Tint color) noexcept {
        // Motion needs tinted and mirrored cutouts; UVs use the opening atlas's same padded cells.
        const std::array uv{static_cast<float>(part.col * 64 + 4) / ATLAS_WIDTH,
            static_cast<float>(part.row * 64 + 4) / ATLAS_HEIGHT, 56.F / ATLAS_WIDTH, 56.F / ATLAS_HEIGHT};
        add(local(pivot) + u * (4 - part.pivot.x) + v * (4 - part.pivot.y), u * 56, v * 56,
            color, ConstructionSurface::Atlas, uv);
    }

    void link(Part part, Vec3 root, Vec3 end, double facing, Tint color) noexcept {
        // Fit both painted anchors. Preserve the model's depth along the link, but turn
        // its width in the camera plane. The material projects the result exactly once.
        const Vec3 delta = end - root;
        const Vec3 side{delta.y * facing, -delta.x * facing, 0};
        const double dx = part.tip.x - part.pivot.x, dy = part.tip.y - part.pivot.y;
        const double inverse = 1 / (dx * dx + dy * dy);
        atlas(part, root, (delta * dx + side * dy) * inverse, (delta * dy - side * dx) * inverse, color);
    }

    Box finish() noexcept {
        if (m_valid && !m_batch.overflow && m_batch.count != 0) {
            const Box bounds = projected_bounds();
            if (is_usable(bounds)) return bounds;
        }
        m_batch.count = 0;
        return {};
    }

  private:
    Box projected_bounds() const noexcept {
        // Bound the actual packed instance corners, using the same pivot and focal
        // uniform as the shader. Positive homogeneous w keeps each edge monotonic.
        double left = 0, top = 0, right_edge = m_capture.w, bottom_edge = m_capture.h;
        for (std::size_t i = 0; i < m_batch.count; ++i) {
            const auto &q = m_batch.quads[i];
            for (const double u : {0., 1.}) {
                for (const double v : {0., 1.}) {
                    const double depth = (q.origin[2] + u * q.u[2] + v * q.v[2]) * m_inverse_focal;
                    if (std::abs(depth) >= .125) return {};
                    const double scale = 1 / (1 - depth);
                    const double x = m_center.x + (q.origin[0] + u * q.u[0] + v * q.v[0] - m_center.x) * scale;
                    const double y = m_center.y + (q.origin[1] + u * q.u[1] + v * q.v[1] - m_center.y) * scale;
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right_edge = std::max(right_edge, x);
                    bottom_edge = std::max(bottom_edge, y);
                }
            }
        }
        return {left - AA, top - AA, right_edge - left + 2 * AA, bottom_edge - top + 2 * AA};
    }

    ConstructionBatch &m_batch;
    Vec3 m_anchor;
    Box m_capture;
    Vec2 m_center;
    float m_inverse_focal;
    bool m_valid = true;
};

bool visible(double opacity) noexcept { return opacity >= .5 / 255; }

void paint_boot(Paint &paint, unsigned i, Vec3 ankle, double height, double facing, double pitch, Tint color) noexcept {
    const double pixel = .0042 * height;
    const double x = std::cos(pitch) * pixel, y = std::sin(pitch) * pixel;
    // Model ankle offsets account for the .25 texel keyline outside the authored sole.
    paint.atlas(BOOTS[i], ankle, {facing * x, facing * y, 0}, {-y, x, 0}, color);
}

void paint_leg(Paint &paint, unsigned i, const auto &leg, double height, double facing, double pitch, Tint color) noexcept {
    paint_boot(paint, i, leg.ankle, height, facing, pitch, color);
    paint.link(THIGHS[i], leg.hip, leg.knee, facing, color);
    paint.link(SHINS[i], leg.knee, leg.ankle, facing, color);
}

void paint_hand(Paint &paint, Vec3 wrist, Vec3 end, double facing, Tint color) noexcept {
    paint.link(GLOVE, wrist, end, facing, color);
}

void paint_arm(Paint &paint, unsigned i, const auto &arm, Vec3 glove, double facing, Tint color,
    Tint vest, bool show_glove) noexcept {
    paint.link(SLEEVES[i], arm.shoulder, arm.elbow, facing, i == 0 ? vest : color);
    paint.link(FOREARMS[i], arm.elbow, arm.wrist, facing, color);
    if (show_glove) paint_hand(paint, arm.wrist, glove, facing, color);
}

void paint_upper_body(Paint &paint, Vec3 hip, Vec3 neck, Vec3 head, double height, double facing, Tint vest) noexcept {
    paint.link(TORSO, hip, neck, facing, vest);
    const double pixel = height * .0049;
    paint.atlas(HEAD, head, {facing * pixel, 0, 0}, {0, pixel, 0}, vest);
}

void paint_worker_shadow(Paint &paint, Vec3 center, double height, double opacity) noexcept {
    paint.atlas(CONTACT_SHADOW, center, {height * .019, 0, 0}, {0, height * .0035, 0},
        tint(WHITE, opacity * .56));
}

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

Vec3 flat(Vec2 p) noexcept { return {p.x, p.y, 0}; }

bool has_scene_outset(Box body, Box capture) noexcept {
    return std::max({body.x, body.y, capture.w - right(body), capture.h - bottom(body)}) > MAX_TRIM;
}

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

void paint_piece_worker(Paint &paint, const studies::PieceRelocation::Worker &worker, unsigned variant) noexcept {
    const double height = worker.height, facing = worker.facing;
    const Tint color = tint(WHITE, worker.visibility), vest = tint(VESTS[variant % VESTS.size()], worker.visibility);
    const auto leg = [&](unsigned i) {
        const auto &l = worker.legs[i];
        const Vec3 ankle = l.tip - Vec3{0, .0042 * height * (BOOTS[i].tip.y + .25 - BOOTS[i].pivot.y), 0};
        paint_boot(paint, i, ankle, height, facing, 0, color);
        paint.link(THIGHS[i], l.root, l.joint, facing, color);
        paint.link(SHINS[i], l.joint, ankle, facing, color);
    };
    const auto arm = [&](unsigned i) {
        const auto &a = worker.arms[i];
        // The study's hand tip is a contact socket, not a wrist. Put the painted palm
        // there and start the forearm at the elbow, shortening the cutout to the wrist.
        constexpr double PALM = .76;
        const Vec3 delta = a.tip - a.joint;
        const double distance = length(delta);
        const Vec3 to_palm = distance > 0 ? delta * std::min(.055 * height / distance, .5) : Vec3{};
        const Vec3 wrist = a.tip - to_palm;
        paint.link(SLEEVES[i], a.root, a.joint, facing, i == 0 ? vest : color);
        paint.link(FOREARMS[i], a.joint, wrist, facing, color);
        paint_hand(paint, wrist, wrist + to_palm * (1 / PALM), facing, color);
    };
    leg(1);
    arm(1);
    leg(0);
    paint_upper_body(paint, worker.pelvis, worker.neck, worker.head, height, facing, vest);
    arm(0);
}

void paint_piece_face(Paint &paint, const studies::PieceRelocation::Face &face, Box body,
    Box local_body, Box capture, bool separate_backdrop) noexcept {
    const Box source = face.source;
    // The model tiles the body in double precision. Allow only roundoff at a shared outer edge.
    const double x_eps = 2 * std::numeric_limits<double>::epsilon() * body.w;
    const double y_eps = 2 * std::numeric_limits<double>::epsilon() * body.h;
    if (!valid_box(source) || source.x < 0 || source.y < 0 ||
        right(source) > body.w + x_eps || bottom(source) > body.h + y_eps) {
        paint.reject();
        return;
    }
    const double left = local_body.x + source.x, top = local_body.y + source.y;
    const double far = local_body.x + std::min(body.w, right(source));
    const double bottom_edge = local_body.y + std::min(body.h, bottom(source));
    // Ordinary trim belongs to its nearest outer piece, never a flat rectangle underneath.
    // Oversized scene outsets instead stay outside the body at identity, as in crew carry.
    const double x0 = !separate_backdrop && source.x == 0 ? 0 : left;
    const double y0 = !separate_backdrop && source.y == 0 ? 0 : top;
    const double x1 = !separate_backdrop && right(source) >= body.w - x_eps ? capture.w : far;
    const double y1 = !separate_backdrop && bottom(source) >= body.h - y_eps ? capture.h : bottom_edge;
    const Box crop{x0, y0, x1 - x0, y1 - y0};
    if (face.origin.x == body.x + source.x && face.origin.y == body.y + source.y && face.origin.z == 0 &&
        face.u.x == source.w && face.u.y == 0 && face.u.z == 0 &&
        face.v.x == 0 && face.v.y == source.h && face.v.z == 0) {
        paint.identity(crop);
    } else {
        const Vec3 u = face.u * (1 / source.w), v = face.v * (1 / source.h);
        paint.client(crop, paint.local(face.origin) + u * (x0 - left) + v * (y0 - top), u * crop.w, v * crop.h);
    }
}

void paint_pieces(Paint &paint, const studies::PieceRelocation &model, Box body, Box local_body, Box capture) noexcept {
    using Pieces = studies::PieceRelocation;
    const bool separate_backdrop = has_scene_outset(local_body, capture);
    if (separate_backdrop) paint.backdrop(local_body);
    // Each model publication groups a piece's rectangular faces and its crew. Keep the
    // model's far-to-near order, interleaving that crew ahead of its piece but behind
    // any nearer one, so later loads can naturally occlude earlier workers.
    struct Crew {
        std::size_t start = 0, count = 0;
    };
    std::array<Crew, Pieces::MAX_FACES> crews{};
    const auto workers = model.workers();
    for (std::size_t i = 0; i < workers.size();) {
        const std::size_t first = i, piece = workers[i].piece;
        if (piece >= crews.size()) {
            paint.reject();
            return;
        }
        while (++i < workers.size() && workers[i].piece == piece) {}
        crews[piece] = {first, i - first};
    }
    const auto faces = model.faces();
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto &face = faces[i];
        paint_piece_face(paint, face, body, local_body, capture, separate_backdrop);
        const auto piece = face.piece;
        if (piece >= crews.size()) {
            paint.reject();
            return;
        }
        if (i + 1 < faces.size() && faces[i + 1].piece == piece) continue;
        const auto [start, count] = crews[piece];
        for (std::size_t j = 0; j < count; ++j) {
            const auto &worker = workers[start + j];
            if (visible(worker.visibility)) paint_piece_worker(paint, worker, static_cast<unsigned>(piece + j));
        }
    }
}

Box begin_transport(ConstructionBatch &batch, Box body, Box capture) noexcept {
    batch.count = 0;
    batch.overflow = false;
    if (!valid_box(body) || !valid_box(capture)) return {};
    const Box local{body.x - capture.x, body.y - capture.y, body.w, body.h};
    if (local.x < 0 || local.y < 0 || right(local) > capture.w || bottom(local) > capture.h) return {};
    return local;
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

Box emit_crane_batch(ConstructionBatch &batch, Box body, Box capture,
    const studies::IntactTransport &crane) noexcept {
    const Box local = begin_transport(batch, body, capture);
    if (!is_usable(local) || !crane.active() || !crane.crane() || !same_box(crane.face().source, body)) return {};
    Paint paint(batch, local, capture);
    paint_crane(paint, crane, local, capture);
    return paint.finish();
}

Box emit_piece_batch(ConstructionBatch &batch, Box body, Box capture,
    const studies::PieceRelocation &pieces) noexcept {
    const Box local = begin_transport(batch, body, capture);
    if (!is_usable(local) || !pieces.active() || !same_box(pieces.body(), body)) return {};
    Paint paint(batch, local, capture);
    paint_pieces(paint, pieces, body, local, capture);
    return paint.finish();
}
}  // namespace hyprimate::construction
