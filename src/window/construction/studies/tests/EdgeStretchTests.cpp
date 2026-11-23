#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "Check.hpp"
#include "window/construction/studies/EdgeStretch.hpp"

using namespace hyprimate;
using namespace hyprimate::construction::studies;

namespace {
    constexpr Box AREA{0, 0, 1280, 800};
    constexpr Box HOME{280, 225, 638, 364};
    constexpr double EPS = 1e-7;

    constexpr EdgeSet edges(Edge first, Edge second) {
        EdgeSet result;
        result.add(first);
        result.add(second);
        return result;
    }
    constexpr EdgeSet only(Edge edge) {
        return edges(edge, edge);
    }

    bool same(Box a, Box b) {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    }
    double distance(Vec3 a, Vec3 b) {
        return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    void tick(EdgeStretch &stretch, Box body, double dt, EdgeSet grab) {
        stretch.step({.body = body, .work_area = AREA, .elapsed = dt, .grab = grab});
        CHECK(same(stretch.body(), body));
    }

    const EdgeWorkerPose *worker(const EdgeStretch &stretch, Edge edge, unsigned slot = 0) {
        for (const auto &pose : stretch.workers()) {
            if (pose.edge == edge && pose.slot == slot) return &pose;
        }
        return nullptr;
    }

    // Observe the drawn endpoints, not a separate ideal-contact diagnostic.
    void geometry(const EdgeStretch &stretch) {
        const Box b = stretch.body();
        for (const auto &pose : stretch.workers()) {
            const auto s = pose.height;
            CHECK(s >= 37 && s <= 41);
            CHECK(pose.opacity >= 0 && pose.opacity <= 1);
            for (const auto &arm : pose.arms) {
                CHECK(std::abs(distance(arm.shoulder, arm.elbow) - .179 * s) < EPS);
                CHECK(std::abs(distance(arm.elbow, arm.wrist) - .184 * s) < EPS);
                CHECK(std::isfinite(arm.glove.x) && std::isfinite(arm.glove.y));
                if (!arm.gripping) continue;
                CHECK(pose.stage == EdgeWorkerStage::Support && arm.glove.z == 0);
                CHECK(arm.glove.x >= b.x - EPS && arm.glove.x <= b.x + b.w + EPS);
                CHECK(arm.glove.y >= b.y - EPS && arm.glove.y <= b.y + b.h + EPS);
                switch (pose.edge) {
                    case Edge::Left:
                        CHECK(arm.glove.x - b.x <= .5 + EPS);
                        break;
                    case Edge::Right:
                        CHECK(b.x + b.w - arm.glove.x <= .5 + EPS);
                        break;
                    case Edge::Top:
                        CHECK(arm.glove.y - b.y <= .5 + EPS);
                        break;
                    case Edge::Bottom:
                        CHECK(b.y + b.h - arm.glove.y <= .5 + EPS);
                        break;
                }
            }
            for (const auto &leg : pose.legs) {
                CHECK(std::abs(distance(leg.hip, leg.knee) - .232 * s) < EPS);
                CHECK(std::abs(distance(leg.knee, leg.ankle) - .247 * s) < EPS);
                CHECK(leg.sole.y > leg.ankle.y);
            }
        }
    }

    void filmed_corner_actions(double hz) {
        EdgeStretch stretch(1307);
        const auto SE = edges(Edge::Right, Edge::Bottom), NW = edges(Edge::Left, Edge::Top);
        struct Move {
            double start, stop;
            Box from, to;
        };
        constexpr Box BIG{280, 225, 844, 488}, SMALL{280, 225, 658, 371}, WEST{135, 145, 803, 451},
            FINAL{200, 184, 738, 412};
        constexpr std::array moves{Move{.50, 1.98, HOME, BIG},
            Move{2.40, 3.65, BIG, SMALL},
            Move{4.72, 5.84, SMALL, WEST},
            Move{5.84, 6.53, WEST, FINAL}};
        unsigned pulls = 0, pushes = 0, holds = 0, paired_grips = 0;
        std::array<bool, 4> contact_on_edge{};
        bool walked = false, leaned_back = false, leaned_in = false, work_on_plant = false;
        struct Feet {
            std::array<Vec3, 2> sole{};
            std::array<bool, 2> planted{};
        };
        std::array<std::array<Feet, 2>, 4> previous{};
        for (int frame = 0; frame <= 8 * hz; ++frame) {
            const double t = frame / hz;
            Box body = HOME;
            for (const auto &move : moves) {
                if (t < move.start) break;
                const double k = smoothstep(move.start, move.stop, t);
                body = {std::lerp(move.from.x, move.to.x, k),
                    std::lerp(move.from.y, move.to.y, k),
                    std::lerp(move.from.w, move.to.w, k),
                    std::lerp(move.from.h, move.to.h, k)};
            }
            const auto grab = t >= .17 && t < 3.77 ? SE : t >= 4.38 && t < 6.66 ? NW : EdgeSet{};
            tick(stretch, body, 1 / hz, grab);
            geometry(stretch);
            std::array<std::array<Feet, 2>, 4> current{};
            for (const auto &p : stretch.workers()) {
                const auto e = static_cast<unsigned>(p.edge);
                auto &feet = current[e][p.slot];
                for (unsigned i = 0; i < 2; ++i) {
                    feet.sole[i] = p.legs[i].sole;
                    feet.planted[i] = p.opacity > .5 && p.legs[i].planted;
                    if (feet.planted[i] && previous[e][p.slot].planted[i]) {
                        CHECK(distance(feet.sole[i], previous[e][p.slot].sole[i]) < EPS);
                        work_on_plant |= p.stage == EdgeWorkerStage::Support &&
                                         p.action != EdgeStretchAction::Hold;
                    }
                }
                walked |= p.opacity > .5 && (p.legs[0].planted != p.legs[1].planted);
                if (!p.arms[0].gripping && !p.arms[1].gripping) continue;
                CHECK(grab.has(p.edge));
                contact_on_edge[static_cast<unsigned>(p.edge)] = true;
                paired_grips += p.arms[0].gripping && p.arms[1].gripping;
                pulls += p.action == EdgeStretchAction::Pull;
                pushes += p.action == EdgeStretchAction::Push;
                holds += p.action == EdgeStretchAction::Hold;
                const double lean = (p.neck.x - p.hip.x) * p.facing;
                leaned_back |= p.action == EdgeStretchAction::Pull && lean < -.08 * p.height;
                leaned_in |= p.action == EdgeStretchAction::Push && lean > .19 * p.height;
            }
            previous = current;
            if (t >= 4.12 && t < 4.38) CHECK(!stretch.active() && stretch.workers().empty());
        }
        CHECK(pulls > hz / 2 && pushes > hz / 2 && holds > hz / 4 && paired_grips > hz / 2);
        CHECK(std::all_of(
            contact_on_edge.begin(), contact_on_edge.end(), [](bool engaged) { return engaged; }));
        CHECK(walked && leaned_back && leaned_in && work_on_plant);
        CHECK(!stretch.active() && stretch.workers().empty() && same(stretch.body(), FINAL));
    }

    void pause_translate_and_regrab() {
        constexpr double DT = 1. / 120;
        constexpr auto GRAB = only(Edge::Right);
        EdgeStretch stretch(42);
        for (int i = 0; i < 100; ++i) tick(stretch, HOME, DT, GRAB);
        const auto *pose = worker(stretch, Edge::Right);
        CHECK(pose && pose->arms[0].gripping && pose->legs[0].planted && pose->legs[1].planted);
        if (!pose) return;
        const auto before = *pose;
        const Box translated{HOME.x + 1, HOME.y - 1, HOME.w, HOME.h};
        tick(stretch, translated, DT, GRAB);
        pose = worker(stretch, Edge::Right);
        CHECK(pose && pose->action == EdgeStretchAction::Hold && pose->arms[0].gripping);
        if (!pose) return;
        CHECK(std::abs(pose->arms[0].glove.x - before.arms[0].glove.x - 1) < EPS);
        CHECK(std::abs(pose->arms[0].glove.y - before.arms[0].glove.y + 1) < EPS);
        for (unsigned i = 0; i < 2; ++i) {
            CHECK(pose->legs[i].planted);
            CHECK(distance(pose->legs[i].sole, before.legs[i].sole) < EPS);
        }
        // Turn around while still in reach: no magical reset to a free hand.
        Box body = translated;
        bool pushed = false, pulled = false;
        for (int i = 0; i < 2; ++i) {
            for (int n = 0; n < 10; ++n) {
                body.w += i == 0 ? .12 : -.12;
                tick(stretch, body, DT, GRAB);
                pose = worker(stretch, Edge::Right);
                CHECK(pose && pose->arms[0].gripping);
                if (!pose) return;
                pushed |= pose->action == EdgeStretchAction::Push;
                pulled |= pose->action == EdgeStretchAction::Pull;
            }
        }
        CHECK(pushed && pulled);
        for (int i = 0; i < 75; ++i) tick(stretch, body, DT, GRAB);
        pose = worker(stretch, Edge::Right);
        CHECK(pose && pose->action == EdgeStretchAction::Hold && pose->arms[0].gripping);
        for (int i = 0; i < 11; ++i) tick(stretch, body, DT, {});
        pose = worker(stretch, Edge::Right);
        CHECK(pose && pose->stage == EdgeWorkerStage::Release && !pose->arms[0].gripping);
        if (!pose) return;
        const auto releasing = *pose;
        tick(stretch, body, 0, GRAB);
        pose = worker(stretch, Edge::Right);
        CHECK(pose && pose->stage != EdgeWorkerStage::Release);
        if (!pose) return;
        CHECK(releasing.opacity == pose->opacity && distance(releasing.hip, pose->hip) < EPS);
        CHECK(distance(releasing.arms[0].glove, pose->arms[0].glove) < EPS);
        for (int i = 0; i < 70; ++i) tick(stretch, body, DT, GRAB);
        pose = worker(stretch, Edge::Right);
        CHECK(pose && pose->arms[0].gripping);
    }

    void sampled_resize_not_translation() {
        EdgeStretch stretch(9);
        Box body = HOME;
        stretch.step({.body = body, .work_area = AREA});
        body.x += 14;
        body.y -= 11;
        stretch.step({.body = body, .work_area = AREA, .elapsed = 0, .sample_seconds = .016});
        CHECK(!stretch.active() && same(stretch.body(), body));
        // Near/far x deltas 8/15 include a common +8; y deltas -12/-7
        // include a common -7. Only right and top should work.
        body = {body.x + 8, body.y - 12, body.w + 7, body.h + 5};
        stretch.step({.body = body, .work_area = AREA, .sample_seconds = .016});
        CHECK(stretch.active() && same(stretch.body(), body));
        CHECK(worker(stretch, Edge::Right) && worker(stretch, Edge::Top));
        CHECK(!worker(stretch, Edge::Left) && !worker(stretch, Edge::Bottom));
        const auto *right = worker(stretch, Edge::Right);
        const auto *top = worker(stretch, Edge::Top);
        CHECK(right && right->action == EdgeStretchAction::Pull && top &&
              top->action == EdgeStretchAction::Pull);
        for (int i = 0; i < 35; ++i)
            stretch.step({.body = body, .work_area = AREA, .elapsed = .01, .held = true});
        right = worker(stretch, Edge::Right);
        CHECK(right && right->action == EdgeStretchAction::Hold && right->arms[0].gripping);
        stretch.step({.body = body, .work_area = AREA, .elapsed = .01});
        right = worker(stretch, Edge::Right);
        CHECK(right && right->stage == EdgeWorkerStage::Release);
        for (int i = 0; i < 36; ++i)
            stretch.step({.body = body, .work_area = AREA, .elapsed = .01});
        CHECK(!stretch.active() && stretch.workers().empty());

        body.w += 20;
        stretch.step({.body = body, .work_area = AREA, .elapsed = .01});
        CHECK(worker(stretch, Edge::Right));
        for (int i = 0; i < 9; ++i) stretch.step({.body = body, .work_area = AREA, .elapsed = .01});
        right = worker(stretch, Edge::Right);
        CHECK(right && right->stage == EdgeWorkerStage::Release);
        stretch.step({.body = body, .work_area = AREA, .elapsed = .20, .stalled = true});
        CHECK(!stretch.active() && stretch.workers().empty() && same(stretch.body(), body));
    }

    void centered_resize_and_edge_handoff() {
        EdgeStretch stretch(108);
        Box body = HOME;
        stretch.step({.body = body, .work_area = AREA});
        EdgeSet engaging;
        for (int i = 0; i < 65; ++i) {
            --body.x;
            body.y -= .6;
            body.w += 2;
            body.h += 1.2;
            stretch.step({.body = body, .work_area = AREA, .elapsed = .01, .held = true});
            CHECK(same(stretch.body(), body) && stretch.workers().size() == 6);
            geometry(stretch);
            for (const auto &p : stretch.workers()) {
                CHECK(p.stage != EdgeWorkerStage::Release);
                if (p.action == EdgeStretchAction::Pull && p.stage == EdgeWorkerStage::Support)
                    engaging.add(p.edge);
            }
        }
        CHECK(engaging.has(Edge::Left) && engaging.has(Edge::Right) && engaging.has(Edge::Top) &&
              engaging.has(Edge::Bottom));
        const auto *top = worker(stretch, Edge::Top);
        CHECK(top);
        if (!top) return;
        const auto previous = *top;
        tick(stretch, body, 0, only(Edge::Top));
        top = worker(stretch, Edge::Top);
        CHECK(top && distance(top->arms[0].glove, previous.arms[0].glove) < EPS);
        for (const auto &p : stretch.workers()) {
            if (p.edge != Edge::Top)
                CHECK(p.stage == EdgeWorkerStage::Release && !p.arms[0].gripping &&
                      !p.arms[1].gripping);
        }
        for (int i = 0; i < 35; ++i) tick(stretch, body, .01, only(Edge::Top));
        CHECK(stretch.active());
        for (const auto &p : stretch.workers()) CHECK(p.edge == Edge::Top);
    }

    void abrupt_resize_and_small_geometry() {
        EdgeStretch stretch(72);
        const auto grab = edges(Edge::Right, Edge::Bottom);
        for (int i = 0; i < 50; ++i) tick(stretch, HOME, .01, grab);
        const auto *right = worker(stretch, Edge::Right);
        CHECK(right && right->arms[0].gripping);
        const Box jumped{HOME.x, HOME.y, HOME.w + 280, HOME.h};
        tick(stretch, jumped, .01, grab);
        right = worker(stretch, Edge::Right);
        CHECK(right && !right->arms[0].gripping && !right->arms[1].gripping);
        geometry(stretch);
        for (int i = 0; i < 105; ++i) tick(stretch, jumped, .01, grab);
        right = worker(stretch, Edge::Right);
        CHECK(right && right->arms[0].gripping);

        for (const Box tiny :
            {Box{-.3, .25, .7, 1.2}, Box{550.4, 420.2, 1.3, .75}, Box{400, 300, 14, 22}}) {
            EdgeStretch compact(72);
            unsigned contacts = 0;
            for (int frame = 0; frame < 115; ++frame) {
                const Box sized{tiny.x, tiny.y, tiny.w + frame * .004, tiny.h + frame * .003};
                tick(compact, sized, .01, grab);
                geometry(compact);
                CHECK(compact.workers().size() <= 2);  // No pairs piled on a sub-boot span.
                for (const auto &p : compact.workers())
                    contacts += p.arms[0].gripping || p.arms[1].gripping;
            }
            CHECK(contacts > 0);
            compact.step({.body = tiny,
                .work_area = AREA,
                .elapsed = std::numeric_limits<double>::quiet_NaN(),
                .grab = grab});
            CHECK(!compact.active() && compact.workers().empty() && same(compact.body(), tiny));
        }
    }

    void compact_inferred_edge_switch() {
        EdgeStretch stretch(72);
        Box body{400, 300, 14, 14};
        const auto held_tick = [&] {
            stretch.step({.body = body, .work_area = AREA, .elapsed = .01, .held = true});
            CHECK(same(stretch.body(), body));
            geometry(stretch);
        };
        held_tick();
        body.x -= 2;
        body.w += 2;  // Only the left edge moves.
        held_tick();
        for (int i = 0; i < 45; ++i) held_tick();
        const auto *left = worker(stretch, Edge::Left);
        CHECK(left && left->stage == EdgeWorkerStage::Support &&
              left->action == EdgeStretchAction::Hold);
        CHECK(!worker(stretch, Edge::Right));

        body.w += 3;  // The left edge is still; the right now does the work.
        held_tick();
        left = worker(stretch, Edge::Left);
        const auto *right = worker(stretch, Edge::Right);
        CHECK(left && left->stage == EdgeWorkerStage::Release && !left->arms[0].gripping &&
              !left->arms[1].gripping);
        CHECK(right && right->stage == EdgeWorkerStage::Approach &&
              right->action == EdgeStretchAction::Pull);

        for (int i = 0; i < 45; ++i) held_tick();
        right = worker(stretch, Edge::Right);
        CHECK(!worker(stretch, Edge::Left));
        CHECK(right && right->stage == EdgeWorkerStage::Support &&
              right->action == EdgeStretchAction::Hold);
    }

    void seeded_stations_and_gaits() {
        EdgeStretch a(1307), repeat(1307), b(1308);
        const auto grab = edges(Edge::Left, Edge::Bottom);
        bool different_glove = false, different_swing = false, distinct_coworkers = false;
        for (int i = 0; i < 100; ++i) {
            const Box body{HOME.x - i * .38, HOME.y, HOME.w + i * .38, HOME.h + i * .22};
            for (auto *s : {&a, &repeat, &b}) tick(*s, body, 1. / 120, grab);
            CHECK(a.workers().size() == repeat.workers().size());
            for (const auto &p : a.workers()) {
                const auto *r = worker(repeat, p.edge, p.slot);
                const auto *q = worker(b, p.edge, p.slot);
                CHECK(r && q);
                if (!r || !q) continue;
                CHECK(p.height == r->height && p.opacity == r->opacity && p.stage == r->stage);
                CHECK(distance(p.hip, r->hip) == 0 &&
                      distance(p.arms[0].glove, r->arms[0].glove) == 0);
                CHECK(distance(p.legs[0].sole, r->legs[0].sole) == 0);
                different_glove |= p.arms[0].gripping && q->arms[0].gripping &&
                                   distance(p.arms[0].glove, q->arms[0].glove) > 1;
                different_swing |=
                    p.opacity > .5 && q->opacity > .5 && p.legs[0].planted != q->legs[0].planted;
                if (const auto *colleague = worker(a, p.edge, 1); p.slot == 0 && colleague)
                    distinct_coworkers |= p.legs[0].planted != colleague->legs[0].planted;
            }
        }
        CHECK(different_glove && different_swing && distinct_coworkers);
    }
}  // namespace

int main() {
    for (double hz : {60., 120., 144.}) filmed_corner_actions(hz);
    pause_translate_and_regrab();
    sampled_resize_not_translation();
    centered_resize_and_edge_handoff();
    abrupt_resize_and_small_geometry();
    compact_inferred_edge_switch();
    seeded_stations_and_gaits();
    return g_failures == 0 ? 0 : 1;
}
