#include "ConstructionMotionPaint.hpp"
#include "studies/PieceRelocation.hpp"

namespace hyprimate::construction {
namespace {
using namespace motion_paint;
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
        do {
            ++i;
        } while (i < workers.size() && workers[i].piece == piece);
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
}  // namespace
Box emit_piece_batch(ConstructionBatch &batch, Box body, Box capture,
    const studies::PieceRelocation &pieces) noexcept {
    const Box local = begin_transport(batch, body, capture);
    if (!is_usable(local) || !pieces.active() || !same_box(pieces.body(), body)) return {};
    Paint paint(batch, local, capture);
    paint_pieces(paint, pieces, body, local, capture);
    return paint.finish();
}

}  // namespace hyprimate::construction
