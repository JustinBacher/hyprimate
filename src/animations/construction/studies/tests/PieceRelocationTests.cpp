#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "Check.hpp"
#include "animations/construction/studies/PieceRelocation.hpp"

using namespace hyprimate;
using namespace hyprimate::construction::studies;

namespace {
    using Face = PieceRelocation::Face;
    using Placement = PieceRelocation::Placement;
    using Worker = PieceRelocation::Worker;
    constexpr Box PREVIEW{177, 294, 654, 402};
    constexpr double EPS = 1e-8;

    bool same(Box a, Box b) {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    }

    bool same(Vec3 a, Vec3 b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    double distance(Vec3 a, Vec3 b) {
        return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    Vec3 point(const Face &face, double s, double t) {
        return {face.origin.x + s * face.u.x + t * face.v.x,
            face.origin.y + s * face.u.y + t * face.v.y,
            face.origin.z + s * face.u.z + t * face.v.z};
    }

    Vec2 parameters(const Face &face, Vec3 p) {
        const double x = p.x - face.origin.x, y = p.y - face.origin.y;
        const double area = face.u.x * face.v.y - face.u.y * face.v.x;
        return {(x * face.v.y - y * face.v.x) / area, (face.u.x * y - face.u.y * x) / area};
    }

    bool contains(const Face &face, Vec3 p) {
        const auto [u, v] = parameters(face, p);
        return u >= -EPS && u <= 1 + EPS && v >= -EPS && v <= 1 + EPS;
    }

    bool exact_seated(std::span<const Face> faces, Box dest) {
        return !faces.empty() && std::all_of(faces.begin(), faces.end(), [&](const Face &face) {
            const auto &uv = face.source;
            return face.placement == Placement::Seated &&
                   same(face.origin, {dest.x + uv.x, dest.y + uv.y, 0}) &&
                   same(face.u, {uv.w, 0, 0}) && same(face.v, {0, uv.h, 0});
        });
    }

    const Face *match(std::span<const Face> faces, const Face &sample) {
        const auto i = std::find_if(faces.begin(), faces.end(), [&](const Face &f) {
            return same(f.source, sample.source);
        });
        return i == faces.end() ? nullptr : &*i;
    }

    std::vector<Face> snapshot(const PieceRelocation &model) {
        return {model.faces().begin(), model.faces().end()};
    }

    void check_tiling(std::span<const Face> faces, Box body) {
        CHECK(!faces.empty());
        CHECK(faces.size() <= PieceRelocation::MAX_FACES);
        double area = 0;
        for (const auto &f : faces) {
            const auto &uv = f.source;
            CHECK(is_usable(uv));
            CHECK(uv.x >= 0 && uv.y >= 0 && uv.x + uv.w <= body.w + EPS &&
                  uv.y + uv.h <= body.h + EPS);
            area += uv.w * uv.h;
            for (const auto &g : faces) {
                if (&f == &g) continue;
                const auto &other = g.source;
                const double w = std::min(uv.x + uv.w, other.x + other.w) - std::max(uv.x, other.x);
                const double h = std::min(uv.y + uv.h, other.y + other.h) - std::max(uv.y, other.y);
                CHECK(w <= EPS || h <= EPS);
                if (f.placement != Placement::Seated || f.piece == g.piece) continue;
                if (std::abs(uv.y + uv.h - other.y) < EPS && w > EPS)
                    CHECK(g.placement == Placement::Seated);
            }
        }
        CHECK(std::abs(area - body.w * body.h) <= EPS * body.w * body.h);
    }

    void no_jump(std::span<const Face> before, std::span<const Face> after) {
        CHECK(before.size() == after.size());
        for (const auto &face : before) {
            const auto *next = match(after, face);
            CHECK(next != nullptr);
            if (!next) continue;
            CHECK(face.piece == next->piece);
            CHECK(same(face.origin, next->origin));
            CHECK(same(face.u, next->u));
            CHECK(same(face.v, next->v));
        }
    }

    void check_frame(const PieceRelocation &model, Box body, std::span<const Face> original) {
        CHECK(same(model.body(), body));
        const auto faces = model.faces();
        const auto workers = model.workers();
        CHECK(faces.size() == original.size());
        CHECK(workers.size() <= PieceRelocation::MAX_WORKERS);
        for (const auto &f : faces) {
            const auto *old = match(original, f);
            CHECK(old && old->piece == f.piece);
            const auto center = point(f, .5, .5);
            CHECK(std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(center.z));
            if (f.placement == Placement::Carried) {
                CHECK(std::any_of(workers.begin(), workers.end(), [&](const auto &w) {
                    return w.piece == f.piece && w.gripping && w.visibility > 0;
                }));
            }
        }
        for (const auto &w : workers) {
            CHECK(w.height > 0 && w.height <= 42);
            CHECK(w.visibility >= 0 && w.visibility <= 1);
            for (const auto &leg : w.legs) {
                CHECK(std::abs(distance(leg.root, leg.joint) - .232 * w.height) <= EPS);
                CHECK(std::abs(distance(leg.joint, leg.tip) - .247 * w.height) <= EPS);
            }
            for (const auto &arm : w.arms) {
                CHECK(std::abs(distance(arm.root, arm.joint) - .22 * w.height) <= EPS);
                CHECK(std::abs(distance(arm.joint, arm.tip) - .24 * w.height) <= EPS);
            }
            if (!w.gripping) continue;
            CHECK(distance(w.arms[0].tip, w.arms[1].tip) > 0);
            for (const auto &arm : w.arms) {
                CHECK(std::any_of(faces.begin(), faces.end(), [&](const auto &f) {
                    if (f.piece != w.piece || !contains(f, arm.tip)) return false;
                    const auto [u, v] = parameters(f, arm.tip);
                    return v >= .70 && distance(arm.tip, point(f, u, v)) < EPS;
                }));
            }
        }
    }

    void deterministic_mixed_tiling() {
        const Box target{577, 198, PREVIEW.w, PREVIEW.h};
        PieceRelocation a, b, c;
        a.reset(PREVIEW, 1307);
        b.reset(PREVIEW, 1307);
        c.reset(PREVIEW, 372);
        a.step(target, 0, true);
        b.step(target, 0, true);
        c.step(target, 0, true);
        check_tiling(a.faces(), PREVIEW);
        check_tiling(c.faces(), PREVIEW);
        CHECK(a.faces().size() >= 5);
        CHECK(std::any_of(a.faces().begin(), a.faces().end(), [&](const Face &f) {
            return !match(c.faces(), f);
        }));
        no_jump(a.faces(), b.faces());
        for (const auto &f : a.faces()) {
            CHECK(f.placement == Placement::Source);
            CHECK(same(f.origin, {PREVIEW.x + f.source.x, PREVIEW.y + f.source.y, 0}));
        }
        for (int i = 0; i < 180; ++i) {
            a.step(target, 1.0 / 120, i < 90);
            b.step(target, 1.0 / 120, i < 90);
            no_jump(a.faces(), b.faces());
            CHECK(a.workers().size() == b.workers().size());
            for (std::size_t j = 0; j < std::min(a.workers().size(), b.workers().size()); ++j) {
                CHECK(a.workers()[j].piece == b.workers()[j].piece);
                CHECK(same(a.workers()[j].arms[0].tip, b.workers()[j].arms[0].tip));
                CHECK(same(a.workers()[j].legs[0].tip, b.workers()[j].legs[0].tip));
            }
        }
    }

    void overlapping_trips_retarget_without_jumps() {
        PieceRelocation model;
        const Box right{896, 211, PREVIEW.w, PREVIEW.h};
        const Box left{-218, 326, PREVIEW.w, PREVIEW.h};
        model.reset(PREVIEW, 1307);
        model.step(right, 0, true);
        const auto original = snapshot(model);
        bool overlap = false, receiving = false, across_right = false, across_left = false;
        bool tilted = false, planted = false, final = false;
        std::vector<Worker> previous_workers;
        auto before = original;
        for (int frame = 0; frame < 720 && model.active(); ++frame) {
            const auto destination = frame < 118 ? right : left;
            if (frame == 117) {  // Release without consuming travel, then regrab toward the left.
                model.step(right, 0, false);
                no_jump(before, model.faces());
            }
            if (frame == 118) {
                model.step(left, 0, true);
                CHECK(same(model.body(), left));
                no_jump(before, model.faces());
            }
            model.step(destination, 1.0 / 120, frame < 200);
            if (!model.active()) break;
            check_frame(model, destination, original);
            check_tiling(model.faces(), destination);
            std::array<bool, PieceRelocation::MAX_FACES> trips{};
            for (const auto &f : model.faces()) {
                const auto *p = match(before, f);
                CHECK(p != nullptr);
                if (!p) continue;
                const auto now = point(f, .5, .5), last = point(*p, .5, .5);
                CHECK(distance(now, last) <
                      35);  // Includes rotation and the final subpixel alignment.
                if (f.placement != Placement::Carried) continue;
                if (f.piece < trips.size()) trips[f.piece] = true;
                across_right |= frame < 118 && now.x - last.x > 1;
                across_left |= frame >= 118 && now.x - last.x < -1;
                tilted |= std::abs(f.v.z) > .1;
                receiving |=
                    now.x > destination.x && now.x < destination.x + destination.w &&
                    std::any_of(
                        model.faces().begin(),
                        model.faces().end(),
                        [](const Face &other) { return other.placement == Placement::Seated; });
            }
            const auto count = std::count(trips.begin(), trips.end(), true);
            CHECK(count <= static_cast<std::ptrdiff_t>(PieceRelocation::MAX_TRIPS));
            overlap |= count >= 2;
            for (const auto &w : model.workers()) {
                const auto i = std::find_if(previous_workers.begin(),
                    previous_workers.end(),
                    [&](const auto &p) { return p.piece == w.piece && p.facing == w.facing; });
                if (i == previous_workers.end() || !w.gripping || !i->gripping) continue;
                for (std::size_t k = 0; k < 2; ++k) {
                    if (!w.planted[k] || !i->planted[k]) continue;
                    CHECK(same(w.legs[k].tip, i->legs[k].tip));
                    planted |= distance(w.pelvis, i->pelvis) > .1;
                }
            }
            final |= exact_seated(model.faces(), left);
            before = snapshot(model);
            previous_workers.assign(model.workers().begin(), model.workers().end());
        }
        CHECK(overlap && receiving && tilted && planted && across_right && across_left);
        CHECK(final && !model.active());
        CHECK(model.workers().empty() && model.faces().empty());
    }

    void long_held_round_trip_converges() {
        constexpr double TICK = 1.0 / 120;
        const Box outbound{PREVIEW.x + 7680, PREVIEW.y, PREVIEW.w, PREVIEW.h};
        PieceRelocation model;
        model.reset(PREVIEW, 1307);
        model.step(outbound, 0, true);
        const auto original = snapshot(model);
        for (int i = 0; i < 12 * 120 && model.active(); ++i) model.step(outbound, TICK, true);
        CHECK(model.active());
        const auto at_turn = snapshot(model);
        CHECK(std::any_of(at_turn.begin(), at_turn.end(), [&](const Face &face) {
            const auto *start = match(original, face);
            return start && face.placement == Placement::Carried &&
                   point(face, .5, .5).x - point(*start, .5, .5).x > 7500;
        }));

        model.step(PREVIEW, 0, true);
        CHECK(same(model.body(), PREVIEW));
        no_jump(at_turn, model.faces());
        int return_ticks = 0;
        const auto returned = [&] {
            return std::any_of(model.faces().begin(), model.faces().end(), [&](const Face &face) {
                const auto *start = match(original, face);
                return start && face.placement == Placement::Carried &&
                       std::abs(point(face, .5, .5).x - point(*start, .5, .5).x) < 100;
            });
        };
        for (; return_ticks < 9 * 120 && model.active() && !returned(); ++return_ticks)
            model.step(PREVIEW, TICK, true);
        CHECK(model.active() && returned());
        const auto at_release = snapshot(model);
        model.step(PREVIEW, 0, false);
        no_jump(at_release, model.faces());

        bool seated = false;
        for (; return_ticks < 10 * 120 && model.active(); ++return_ticks) {
            model.step(PREVIEW, TICK, false);
            seated |= exact_seated(model.faces(), PREVIEW);
        }
        CHECK(seated && !model.active());
        CHECK(model.faces().empty() && model.workers().empty());
    }

    void tiny_worker_approaches_grips_and_releases() {
        constexpr Box source{15, 70, .75, 1.25};
        constexpr Box target{135.75, 29, source.w, source.h};
        PieceRelocation model;
        model.reset(source, 372);
        model.step(target, 0, false);
        const auto original = snapshot(model);
        bool approaching = false, latched = false, releasing = false;
        for (int i = 0; i < 5 * 120 && model.active(); ++i) {
            model.step(target, 1.0 / 120, false);
            if (!model.active()) break;
            check_frame(model, target, original);
            for (const auto &worker : model.workers()) {
                if (worker.visibility == 0) continue;
                approaching |= !latched && !worker.gripping;
                latched |= worker.gripping;
                releasing |=
                    !worker.gripping &&
                    std::any_of(model.faces().begin(), model.faces().end(), [&](const Face &face) {
                        return face.piece == worker.piece && face.placement == Placement::Seated;
                    });
            }
        }
        CHECK(approaching && latched && releasing && !model.active());
    }

    void converges(Box source, std::uint64_t seed, double hz) {
        PieceRelocation model;
        const Box dest{source.x + source.w + 120, source.y - 41, source.w, source.h};
        model.reset(source, seed);
        model.step(source, 1 / hz, true);
        CHECK(!model.active());
        model.step(
            dest, 0, false);  // A programmatic move, with no held grab and zero first delta time.
        const auto original = snapshot(model);
        check_tiling(original, source);
        bool final = false;
        const int limit = static_cast<int>(hz * (8 + source.w / 500));
        for (int i = 0; i < limit && model.active(); ++i) {
            model.step(dest, 1 / hz, false);
            if (!model.active()) break;
            check_frame(model, dest, original);
            if (exact_seated(model.faces(), dest)) {
                const Vec3 old_center{source.x + .5 * source.w, source.y + .5 * source.h, 0};
                CHECK(std::none_of(model.faces().begin(), model.faces().end(), [&](const auto &f) {
                    return contains(f, old_center);
                }));
                final = true;
            }
        }
        CHECK(final && !model.active());
        for (int i = 0; i < 60; ++i) model.step(dest, 1 / hz, false);
        CHECK(same(model.body(), dest) && model.workers().empty() && model.faces().empty());
    }

    void regrab_during_exit_and_interruptions() {
        PieceRelocation model;
        model.reset(PREVIEW, 40);
        const Box right{420, 400, PREVIEW.w, PREVIEW.h};
        const Box left{100, 400, PREVIEW.w, PREVIEW.h};
        model.step(right, 0, false);
        const auto dispersing = [&] {
            return exact_seated(model.faces(), right) && !model.workers().empty() &&
                   std::none_of(model.workers().begin(), model.workers().end(), [](const auto &w) {
                       return w.gripping;
                   });
        };
        for (int i = 0; i < 480 && !dispersing(); ++i) model.step(right, 1.0 / 120, false);
        CHECK(dispersing());
        const Box corrected{right.x + 1, right.y, right.w, right.h};
        const auto before = snapshot(model);
        auto remaining = model.workers().size();
        model.step(corrected, 0, true);
        CHECK(same(model.body(), corrected));
        no_jump(before, model.faces());
        bool retargeted = false;
        for (int i = 0; i < 480 && model.active(); ++i) {
            CHECK(std::all_of(model.faces().begin(), model.faces().end(), [](const auto &face) {
                return face.placement == Placement::Seated;
            }));
            CHECK(model.workers().size() <= remaining);
            remaining = model.workers().size();
            retargeted |= exact_seated(model.faces(), corrected);
            model.step(corrected, 1.0 / 120, true);
        }
        CHECK(retargeted && !model.active());

        // A fresh major move after idle starts a new relocation from the corrected body.
        model.step(left, 0, false);
        CHECK(model.active() && same(model.body(), left));
        for (const auto &face : model.faces()) {
            CHECK(face.placement == Placement::Source);
            CHECK(same(face.origin, {corrected.x + face.source.x, corrected.y + face.source.y, 0}));
        }
        model.step(left, STALL_SECONDS, false);
        CHECK(!model.active() && model.faces().empty() && model.workers().empty());
        model.step(right, 0, false);
        CHECK(model.active());
        const Box resized{right.x, right.y, right.w + 2, right.h};
        model.step(resized, 1.0 / 120, false);
        CHECK(same(model.body(), resized) && !model.active() && model.faces().empty());
        model.step({10, 10, resized.w, resized.h}, std::numeric_limits<double>::quiet_NaN(), false);
        CHECK(!model.active());
    }
}  // namespace

int main() {
    deterministic_mixed_tiling();
    overlapping_trips_retarget_without_jumps();
    regrab_during_exit_and_interruptions();
    long_held_round_trip_converges();
    tiny_worker_approaches_grips_and_releases();
    converges(PREVIEW, 1307, 60);
    converges({-48.25, 38.75, 480.5, 320.25}, 43, 144);
    converges({15, 70, .75, 1.25}, 372, 120);
    converges({-57, 19.5, 7680, 320}, 3, 60);
    converges({110, 200, 32, 8192}, 96, 144);
    return g_failures == 0 ? 0 : 1;
}
