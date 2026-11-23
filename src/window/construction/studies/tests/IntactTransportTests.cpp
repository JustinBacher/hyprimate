#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

#include "Check.hpp"
#include "window/construction/studies/IntactTransport.hpp"

namespace {
    using namespace hyprimate;
    using namespace hyprimate::construction::studies;

    constexpr Box SCREEN{0, 0, 1280, 800};
    constexpr Box START{177, 294, 654, 402};
    constexpr double TICK = 1.0 / 120;

    bool near(double a, double b, double epsilon = 1e-7) {
        return std::abs(a - b) < epsilon;
    }
    bool near(Vec2 a, Vec2 b, double epsilon = 1e-7) {
        return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon);
    }
    bool near(Vec3 a, Vec3 b, double epsilon = 1e-7) {
        return near(Vec2{a.x, a.y}, {b.x, b.y}, epsilon) && near(a.z, b.z, epsilon);
    }
    bool same(Box a, Box b) {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    }
    double distance(Vec3 a, Vec3 b) {
        return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    animation::Step input(Box body, bool held, double dt = TICK) {
        return {.elapsed = dt,
            .body = body,
            .capture = body,
            .goal_body = body,
            .rest_capture = body,
            .screen = SCREEN,
            .work_area = SCREEN,
            .motion = {},
            .movement_enabled = true,
            .held = held};
    }

    // The sampled itinerary from movement-comparison-01; no production clock or pointer.
    Box approved_path(double t) {
        const double out = smoothstep(1.23, 2.55, t), back = smoothstep(2.85, 3.77, t);
        const double wave = std::sin(std::numbers::pi * out);
        const double arc = 11 * wave * wave;
        return {START.x + 379 * out - 186 * back,
            START.y - 92 * out - arc + 71 * back,
            START.w,
            START.h};
    }

    void check_face(const IntactTransportFace &face, Box body) {
        CHECK(same(face.source, body));
        CHECK(near(std::hypot(face.u.x, face.u.y), body.w));
        CHECK(near(std::hypot(face.v.x, face.v.y), body.h));
        CHECK(near(face.u.x * face.v.x + face.u.y * face.v.y, 0));
        CHECK(face.lift >= 0 && face.lift <= 3);
        CHECK(near(face.point({body.w / 2, body.h / 2}),
            {body.x + body.w / 2, body.y + body.h / 2 - face.lift}));
    }

    void check_identity(const IntactTransport &study, Box body) {
        CHECK(same(study.face().source, body));
        CHECK(study.face().origin.x == body.x && study.face().origin.y == body.y);
        CHECK(study.face().u.x == body.w && study.face().u.y == 0);
        CHECK(study.face().v.x == 0 && study.face().v.y == body.h);
        CHECK(study.face().lift == 0);
    }

    void check_bearer(const CarryBearerPose &bearer, const IntactTransportFace &face) {
        CHECK(bearer.opacity > 0 && bearer.opacity <= 1);
        for (const CarryArmPose &arm : bearer.arms) {
            CHECK(near(distance(arm.shoulder, arm.elbow), .179 * bearer.height));
            CHECK(near(distance(arm.elbow, arm.wrist), .184 * bearer.height));
            if (!arm.grip) continue;
            CHECK(near(Vec3{arm.grip->x, arm.grip->y, 0}, arm.palm));
            const double dx = arm.grip->x - face.origin.x, dy = arm.grip->y - face.origin.y;
            const double u = (dx * face.u.x + dy * face.u.y) / face.source.w;
            const double v = (dx * face.v.x + dy * face.v.y) / face.source.h;
            CHECK(
                u >= -1e-7 && u <= face.source.w + 1e-7 && v >= -1e-7 && v <= face.source.h + 1e-7);
            CHECK(near(u, 0) || near(u, face.source.w) || near(v, face.source.h));
        }
        for (const CarryLegPose &leg : bearer.legs) {
            CHECK(near(distance(leg.hip, leg.knee), .232 * bearer.height));
            CHECK(near(distance(leg.knee, leg.ankle), .247 * bearer.height));
            if (leg.planted) CHECK(leg.boot_pitch == 0);
        }
    }

    void crew_gathers_lifts_plants_and_disperses(double hz) {
        IntactTransport model{IntactTransportKind::CrewCarry, 1307};
        std::array<std::array<std::optional<Vec3>, 2>, 6> planted{};
        bool saw_gather = false, saw_dispersal = false, asymmetric_gaits = false;
        int stable_plants = 0, latched_walking = 0, peak_crew = 0;
        double peak_lift = 0, pause_min_lift = 3, pause_max_sway = 0;
        for (int i = 0; i <= static_cast<int>(5 * hz); ++i) {
            const double t = i / hz;
            const Box body = approved_path(t);
            model.step(input(body, .5 <= t && t < 3.83, i == 0 ? 0 : 1 / hz));
            const auto &face = model.face();
            check_face(face, body);
            CHECK(!model.crane());
            peak_lift = std::max(peak_lift, face.lift);
            const auto actors = model.bearers();
            peak_crew = std::max(peak_crew, static_cast<int>(actors.size()));
            saw_gather |= !actors.empty() && face.lift == 0 && t < 1.23;
            saw_dispersal |=
                !actors.empty() && face.lift == 0 && actors.front().opacity < 1 && t > 3.83;
            if (2.60 < t && t < 2.84) {
                pause_min_lift = std::min(pause_min_lift, face.lift);
                // The approved replay measures one corner against its untilted, lifted position.
                // face.u.y is the vertical difference between two corners, not corner sway.
                const double sway =
                    std::hypot(face.origin.x - body.x, face.origin.y - body.y + face.lift);
                pause_max_sway = std::max(pause_max_sway, sway);
            }
            int walking_actors = 0;
            for (std::size_t j = 0; j < actors.size(); ++j) {
                const auto &actor = actors[j];
                check_bearer(actor, face);
                if (actors.size() != 6 || t < 1.24 || t > 3.77) continue;
                const bool gripping = actor.arms[0].grip || actor.arms[1].grip;
                const bool walking = actor.legs[0].boot_pitch != 0 || actor.legs[1].boot_pitch != 0;
                walking_actors += walking;
                latched_walking += gripping && walking && face.lift > 1;
                for (std::size_t k = 0; k < 2; ++k) {
                    const auto &leg = actor.legs[k];
                    if (planted[j][k] && leg.planted) {
                        CHECK(near(*planted[j][k], leg.sole));
                        ++stable_plants;
                    }
                    planted[j][k] = leg.planted ? std::optional{leg.sole} : std::nullopt;
                }
            }
            asymmetric_gaits |= walking_actors > 0 && walking_actors < peak_crew;
        }
        CHECK(peak_crew == 6 && saw_gather && saw_dispersal);
        CHECK(peak_lift > 2.2);
        CHECK(pause_min_lift > 1.8);
        // Approved normal replay peaks at 2.425 px of corner sway; allow for seeded gait and
        // sampling.
        CHECK(pause_max_sway < 3.0);
        CHECK(asymmetric_gaits && latched_walking > 20 && stable_plants > 20);
        CHECK(!model.active() && model.bearers().empty() && !is_usable(model.load_shadow()));
        check_identity(model, approved_path(5));
    }

    void crew_regrab_preserves_pose_and_seed_varies_the_cast() {
        IntactTransport first{IntactTransportKind::CrewCarry, 1307};
        IntactTransport repeat{IntactTransportKind::CrewCarry, 1307};
        IntactTransport other{IntactTransportKind::CrewCarry, 1308};
        for (int k = 0; k < 120; ++k) {
            const auto held = input(START, true, k ? TICK : 0);
            first.step(held);
            repeat.step(held);
            other.step(held);
            CHECK(first.face().lift == repeat.face().lift);
            CHECK(first.bearers().size() == repeat.bearers().size());
            for (std::size_t i = 0; i < first.bearers().size() && i < repeat.bearers().size();
                ++i) {
                CHECK(first.bearers()[i].height == repeat.bearers()[i].height);
                CHECK(near(first.bearers()[i].head, repeat.bearers()[i].head));
            }
        }
        CHECK(!first.bearers().empty() && !other.bearers().empty());
        if (first.bearers().empty() || other.bearers().empty()) return;
        CHECK(first.bearers().front().height != other.bearers().front().height);
        first.step(input(START, false, .04));
        const double lowering = first.face().lift;
        CHECK(lowering > 0 && lowering < repeat.face().lift);
        const auto head = first.bearers().front().head;
        first.step(input(START, true, 0));
        CHECK(near(first.face().lift, lowering));
        CHECK(!first.bearers().empty());
        if (!first.bearers().empty()) CHECK(near(first.bearers().front().head, head));
        for (int k = 0; k < 60; ++k) first.step(input(START, true));
        CHECK(first.face().lift > lowering);
    }

    bool intersects(Box a, Box b) {
        return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
    }

    void check_wires(const CarryCranePose &rig, const IntactTransportFace &face) {
        CHECK(rig.wire_count == 0 || rig.wire_count == 2);
        if (rig.wire_count != 2) return;
        const auto &left = rig.wires[0], &right = rig.wires[1];
        CHECK(left.attached == right.attached && left.taut == right.taut);
        CHECK(left.end.x < right.end.x);
        for (std::size_t i = 0; i < rig.wire_count; ++i) {
            const auto &wire = rig.wires[i];
            CHECK(near(wire.sheave, rig.sheave));
            CHECK(wire.width > 0 && wire.width < 2);
            CHECK(wire.sheave.y < wire.end.y);
            if (wire.attached) CHECK(near(wire.end, face.point({i == 0 ? 0 : face.source.w, 0})));
            if (wire.taut) CHECK(near(wire.midpoint.y, (wire.sheave.y + wire.end.y) / 2));
        }
    }

    void crane_reverses_slackens_and_leaves(
        Box start, std::span<const Box> outputs, int facing, bool cross_gap) {
        IntactTransport model{IntactTransportKind::Crane, 1307};
        bool lifted = false, lowering = false, slack = false, detached = false, retracted = false;
        bool left_gap = false, visited_secondary_after_gap = false, cleared = false;
        double right_aim = 0, left_aim = 0, right_track_y = 0, left_track_y = 0;
        for (int i = 0; i < 1200; ++i) {
            const double t = i * TICK;
            const double move = smoothstep(2, 3, t), reverse = smoothstep(3.35, 4.35, t);
            const Box body{start.x + 100 * move - 130 * reverse,
                start.y - 40 * move + 50 * reverse,
                start.w,
                start.h};
            const bool held = t < 4.45;
            model.step(input(body, held, i ? TICK : 0), outputs);
            check_face(model.face(), body);
            CHECK(model.bearers().empty());
            if (!model.crane()) {
                CHECK(!held && retracted && cleared);
                check_identity(model, body);
                break;
            }
            const auto &rig = *model.crane();
            CHECK(rig.facing == facing);
            check_wires(rig, model.face());
            if (t >= 3.10 && t < 3.10 + TICK) {
                right_aim = rig.sheave.x;
                right_track_y = rig.track.y;
            }
            if (t >= 4.40 && t < 4.40 + TICK) {
                left_aim = rig.sheave.x;
                left_track_y = rig.track.y;
            }
            if (held && model.face().lift > 2.6) {
                lifted = true;
                CHECK(rig.wire_count == 2 && rig.wires[0].taut);
            }
            if (!held && model.face().lift > 0 && model.face().lift < 2.6) lowering = true;
            if (!held && rig.wire_count == 2 && rig.wires[0].attached && !rig.wires[0].taut) {
                CHECK(lowering && model.face().lift == 0);
                slack = true;
            }
            if (!held && rig.wire_count == 2 && !rig.wires[0].attached) {
                CHECK(slack && near(rig.extension, 1));
                detached = true;
            }
            if (rig.departing) {
                CHECK(detached && rig.extension == 0 && rig.wire_count == 0);
                retracted = true;
                const bool in_any = std::ranges::any_of(
                    outputs, [&](Box rect) { return intersects(rig.bounds, rect); });
                if (!in_any && facing == 1 && rig.bounds.x + rig.bounds.w < SCREEN.x)
                    left_gap = true;
                if (cross_gap && left_gap && intersects(rig.bounds, outputs.front()))
                    visited_secondary_after_gap = true;
                cleared = !in_any;
            }
        }
        CHECK(retracted && lifted && lowering && slack && detached && !model.active());
        CHECK(right_aim - left_aim > 65);
        CHECK(std::abs(right_track_y - left_track_y) < 8);
        CHECK(!cross_gap || visited_secondary_after_gap);
    }

    void crane_regrab_reuses_the_displayed_rig(bool during_departure) {
        IntactTransport model{IntactTransportKind::Crane, 4};
        for (int k = 0; k < 240; ++k) model.step(input(START, true));
        bool caught = false;
        for (int k = 0; k < 160 && model.active(); ++k) {
            model.step(input(START, false));
            if (!model.crane()) break;
            const auto &rig = *model.crane();
            if (during_departure ? !rig.departing : rig.wire_count != 2 || rig.wires[0].attached)
                continue;
            caught = true;
            const Vec2 track = rig.track, sheave = rig.sheave, wire_tip = rig.wires[0].end;
            const std::size_t wire_count = rig.wire_count;
            model.step(input(START, true, 0));
            CHECK(model.crane().has_value());
            if (!model.crane()) break;
            CHECK(near(model.crane()->track, track) && near(model.crane()->sheave, sheave));
            CHECK(model.crane()->wire_count == wire_count);
            if (wire_count) CHECK(near(model.crane()->wires[0].end, wire_tip));
            const Box regrab{START.x - 35, START.y - 18, START.w, START.h};
            for (int j = 0; j < 210; ++j) model.step(input(regrab, true));
            check_face(model.face(), regrab);
            CHECK(model.crane() && model.crane()->wire_count == 2 && model.crane()->wires[0].taut);
            if (model.crane()) check_wires(*model.crane(), model.face());
            CHECK(model.face().lift > 2.6);
            break;
        }
        CHECK(caught);
    }

    std::size_t gripping_bearers(const IntactTransport &model) {
        return static_cast<std::size_t>(
            std::ranges::count_if(model.bearers(), [](const CarryBearerPose &bearer) {
                return bearer.arms[0].grip || bearer.arms[1].grip;
            }));
    }

    void short_move_picks_up_then_sets_down(
        IntactTransportKind kind, bool quick_drag, std::span<const Box> outputs = {}) {
        IntactTransport model{kind, 1307};
        model.step(input(START, false, 0), outputs);
        const Box destination{START.x + 33, START.y - 14, START.w, START.h};
        auto observation = input(destination, quick_drag, 0);
        observation.motion = animation::Motion{.from_body = START,
            .from_capture = START,
            .body = destination,
            .capture = destination,
            .translated = true,
            .sample_seconds = TICK};
        model.step(observation, outputs);
        check_identity(model, destination);
        CHECK(model.active());

        bool lifted = false, lowered = false;
        double previous_lift = 0;
        for (int k = 0; k < 1200 && model.active(); ++k) {
            model.step(input(destination, false), outputs);
            check_face(model.face(), destination);
            const double lift = model.face().lift;
            if (lift > 0) {
                if (kind == IntactTransportKind::CrewCarry) {
                    CHECK(gripping_bearers(model) >= 2);
                } else {
                    CHECK(model.crane() && model.crane()->wire_count == 2 &&
                          model.crane()->wires[0].taut);
                    if (model.crane()) check_wires(*model.crane(), model.face());
                }
            }
            lifted |= lift > 2.3;
            lowered |= lifted && lift > 0 && lift < previous_lift;
            previous_lift = lift;
        }
        CHECK(lifted && lowered && !model.active());
        CHECK(model.bearers().empty() && !model.crane());
        check_identity(model, destination);
    }

    void losing_every_grip_sets_down_on_the_reversal_frame() {
        IntactTransport model{IntactTransportKind::CrewCarry, 1307};
        for (int k = 0; k < 150; ++k) model.step(input(START, true));
        CHECK(gripping_bearers(model) >= 2 && model.face().lift > 2.3);

        const Box ahead{START.x + 49, START.y, START.w, START.h};
        model.step(input(ahead, true));
        CHECK(gripping_bearers(model) == 0);
        check_identity(model, ahead);

        const Box back{START.x - 48, START.y + 5, START.w, START.h};
        model.step(input(back, true));
        CHECK(gripping_bearers(model) == 0);
        check_identity(model, back);

        for (int k = 0; k < 120; ++k) {
            model.step(input(back, true));
            check_face(model.face(), back);
            if (model.face().lift > 0) CHECK(gripping_bearers(model) >= 2);
        }
        CHECK(gripping_bearers(model) >= 2 && model.face().lift > 2.3);
    }

    void more_bearers_join_when_the_window_enters_open_space() {
        IntactTransport model{IntactTransportKind::CrewCarry, 1307};
        const Box corner{SCREEN.x, SCREEN.y + SCREEN.h - START.h, START.w, START.h};
        for (int k = 0; k < 100; ++k) model.step(input(corner, true));
        CHECK(model.bearers().size() == 1 && gripping_bearers(model) == 1);
        check_identity(model, corner);
        if (model.bearers().size() != 1) return;

        const double initial_height = model.bearers().front().height;
        const int initial_facing = model.bearers().front().facing;
        Vec3 previous_head = model.bearers().front().head;
        bool joined = false, lifted = false;
        for (int k = 1; k <= 220; ++k) {
            const double advance = smoothstep(0, 1, k * TICK);
            const Box body{corner.x + 200 * advance, corner.y - 133 * advance, corner.w, corner.h};
            model.step(input(body, true));
            check_face(model.face(), body);
            CHECK(!model.bearers().empty());
            if (model.bearers().empty()) break;
            const auto &original = model.bearers().front();
            CHECK(original.height == initial_height && original.facing == initial_facing &&
                  original.opacity == 1);
            CHECK(distance(original.head, previous_head) < 18);
            previous_head = original.head;
            joined |= model.bearers().size() > 1;
            const auto gripping = gripping_bearers(model);
            if (model.face().lift > 0) CHECK(gripping >= 2);
            lifted |= gripping >= 2 && model.face().lift > 2.3;
        }
        CHECK(joined && lifted);
    }

    void native_authority_at_acquisition_and_interruption(IntactTransportKind kind) {
        IntactTransport model{kind, 40};
        auto wallpaper = input(START, false, 0);
        const Box shifted_from{20, 20, START.w, START.h};
        wallpaper.motion = animation::Motion{.from_body = shifted_from,
            .from_capture = shifted_from,
            .body = START,
            .capture = START,
            .translated = true,
            .workspace_shifted = true,
            .sample_seconds = TICK};
        model.step(wallpaper);
        CHECK(!model.active());
        check_identity(model, START);

        const Box resized{START.x - 20, START.y, START.w + 20, START.h};
        auto edge = input(resized, true);
        edge.motion = animation::Motion{.from_body = START,
            .from_capture = START,
            .body = resized,
            .capture = resized,
            .translated = true,
            .resized = true};
        model.step(edge);
        CHECK(!model.active());
        check_identity(model, resized);

        IntactTransport hop{kind, 40};
        const Box moved{START.x + 18, START.y - 12, START.w, START.h};
        auto first = input(moved, false, 0);
        first.motion = animation::Motion{.from_body = START,
            .from_capture = START,
            .body = moved,
            .capture = moved,
            .translated = true,
            .sample_seconds = TICK};
        hop.step(first);
        CHECK(hop.active());
        check_identity(hop, moved);
        for (int k = 0; k < 240; ++k) hop.step(input(moved, true));
        CHECK(hop.active() && hop.face().lift > 0);

        const Box wider{moved.x, moved.y, moved.w + 23, moved.h};
        hop.step(input(wider, true));
        check_identity(hop, wider);
        auto bad_time = input(wider, true, std::numeric_limits<double>::quiet_NaN());
        hop.step(bad_time);
        CHECK(!hop.active() && !hop.crane() && hop.bearers().empty());
        check_identity(hop, wider);
    }
    void worker_scale_and_hidden_simulation() {
        const Box body{1000, 1000, 800, 400}, large{1000, 1000, 1600, 800};
        const Box apron{0, 0, 5000, 4000};
        IntactTransport normal{IntactTransportKind::CrewCarry, 1307};
        IntactTransport doubled{IntactTransportKind::CrewCarry, 1307, 2};
        IntactTransport hidden{IntactTransportKind::CrewCarry, 1307, 0};
        const auto step = [&](Box box, bool held) {
            auto sample = input(box, held);
            sample.screen = sample.work_area = apron;
            return sample;
        };
        for (int i = 0; i < 240; ++i) {
            normal.step(step(body, true));
            doubled.step(step(body, true));
            hidden.step(step(body, true));
        }
        CHECK(!normal.bearers().empty() && !doubled.bearers().empty());
        CHECK(hidden.active() && hidden.bearers().empty());
        if (normal.bearers().empty() || doubled.bearers().empty()) return;
        const double height = normal.bearers()[0].height;
        CHECK(height > 28 && height < 33);
        CHECK(near(doubled.bearers()[0].height, 2 * height));
        for (const auto &a : doubled.bearers()) check_bearer(a, doubled.face());
        for (int i = 0; i < 240; ++i) normal.step(step(large, true));
        CHECK(!normal.bearers().empty());
        if (!normal.bearers().empty()) CHECK(near(normal.bearers()[0].height, 2 * height));
        check_face(normal.face(), large);
        for (const auto &a : normal.bearers()) check_bearer(a, normal.face());
        for (int i = 0; i < 240; ++i) hidden.step(step(body, false));
        CHECK(!hidden.active());
        check_identity(hidden, body);
    }

}  // namespace

int main() {
    worker_scale_and_hidden_simulation();
    for (double hz : {60., 120., 144.}) crew_gathers_lifts_plants_and_disperses(hz);
    crew_regrab_preserves_pose_and_seed_varies_the_cast();
    const std::array single{SCREEN};
    const std::array multiple{
        Box{-720, 0, 420, 800}, SCREEN, Box{1400, 0, 360, 800}, Box{-2200, -500, 800, 300}};
    crane_reverses_slackens_and_leaves(START, multiple, 1, true);
    crane_reverses_slackens_and_leaves({640, 230, 240, 225}, single, -1, false);
    crane_regrab_reuses_the_displayed_rig(false);
    crane_regrab_reuses_the_displayed_rig(true);
    for (bool quick_drag : {false, true}) {
        short_move_picks_up_then_sets_down(IntactTransportKind::CrewCarry, quick_drag);
        short_move_picks_up_then_sets_down(IntactTransportKind::Crane, quick_drag);
        short_move_picks_up_then_sets_down(IntactTransportKind::Crane, quick_drag, multiple);
    }
    losing_every_grip_sets_down_on_the_reversal_frame();
    more_bearers_join_when_the_window_enters_open_space();
    native_authority_at_acquisition_and_interruption(IntactTransportKind::CrewCarry);
    native_authority_at_acquisition_and_interruption(IntactTransportKind::Crane);
    return g_failures == 0 ? 0 : 1;
}
