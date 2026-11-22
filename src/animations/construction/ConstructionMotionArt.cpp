#include "ConstructionMotionArt.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "ConstructionArt.hpp"
#include "studies/EdgeStretch.hpp"
#include "studies/IntactTransport.hpp"

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
