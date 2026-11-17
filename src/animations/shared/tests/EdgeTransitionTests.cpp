#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "Check.hpp"
#include "animations/liquid/LiquidMesh.hpp"
#include "animations/liquid/LiquidPose.hpp"
#include "animations/shared/Core.hpp"
#include "animations/shared/EdgeTransition.hpp"

namespace {

    using hyprimate::Transition;
    using hyprimate::TransitionParams;
    using hyprimate::Vec2;

    constexpr double STRENGTH = 0.65;

    // Task 2: edge selection.
    constexpr hyprimate::Box DEFAULT_SCREEN{.x = 0.0, .y = 0.0, .w = 1920.0, .h = 1080.0};
    hyprimate::Box with_shadow(hyprimate::Box box) {
        return {.x = box.x - 12.0, .y = box.y - 12.0, .w = box.w + 24.0, .h = box.h + 24.0};
    }
    std::optional<hyprimate::Edge> pick(hyprimate::Box body,
        hyprimate::Edge fallback,
        bool first,
        std::span<const hyprimate::Box> neighbours = {},
        hyprimate::Box screen = DEFAULT_SCREEN) {
        const auto FRAME = hyprimate::resolveEdgeFrame(
            body, with_shadow(body), screen, neighbours, fallback, first);
        return FRAME ? std::optional{FRAME->edge} : std::nullopt;
    }

    void test_edge_follows_dwindle_splits() {
        using hyprimate::Edge;
        CHECK(pick({24, 24, 1872, 1032}, Edge::Bottom, true) ==
              Edge::Bottom);  // first window: configured side
        CHECK(pick({24, 24, 1872, 1032}, Edge::Left, true) == Edge::Left);
        CHECK(pick({972, 24, 924, 1032}, Edge::Bottom, false) == Edge::Right);  // right half
        CHECK(pick({972, 548, 924, 508}, Edge::Bottom, false) ==
              Edge::Bottom);  // bottom-right quarter
        CHECK(pick({1438, 548, 458, 508}, Edge::Bottom, false) == Edge::Right);  // right eighth
    }

    void test_edge_ties_and_validity() {
        using hyprimate::Edge;
        CHECK(pick({760, 340, 400, 400}, Edge::Top, false) ==
              Edge::Top);  // top/bottom tie -> fallback
        CHECK(pick({760, 340, 400, 400}, Edge::Right, false) ==
              Edge::Bottom);  // fallback not tied -> order
        CHECK(pick({500, 900, 400, 300}, Edge::Bottom, true) ==
              Edge::Left);  // fallback invalid (crosses bottom)
        CHECK(!pick({-10, -10, 1940, 1100}, Edge::Bottom, false));  // crosses every edge
        CHECK(!pick({0, 0, 0, 100}, Edge::Bottom, false));  // unusable
    }

    void test_frame_geometry() {
        const hyprimate::Box BODY{.x = 972, .y = 548, .w = 924, .h = 508};
        const auto FRAME = hyprimate::resolveEdgeFrame(
            BODY, with_shadow(BODY), DEFAULT_SCREEN, {}, hyprimate::Edge::Bottom, false);
        CHECK(FRAME && FRAME->edge == hyprimate::Edge::Bottom);
        // Capture centre (1434, 802): 278 px above the bottom edge; capture half height 266.
        CHECK(std::abs(FRAME->edgeDistance - 278.0) < 1e-9 &&
              std::abs(FRAME->captureFar - 544.0) < 1e-9);
        CHECK(std::abs(FRAME->bodyNear - 24.0) < 1e-9 && std::abs(FRAME->bodyFar - 532.0) < 1e-9);
        CHECK(std::abs(FRAME->bodyT0 + 462.0) < 1e-9 && std::abs(FRAME->bodyT1 - 462.0) < 1e-9);
        // The frame maps round-trip, and n grows into the screen.
        const hyprimate::Vec2 POINT = hyprimate::edgePoint(*FRAME, 40.0, 100.0);
        CHECK(std::abs(hyprimate::edgeNormal(*FRAME, POINT) - 100.0) < 1e-9 &&
              std::abs(hyprimate::edgeTangent(*FRAME, POINT) - 40.0) < 1e-9);
        CHECK(std::abs(POINT.y - 178.0) < 1e-9);  // 278 - 100, y down from the capture centre
    }

    // close_side = origin: the origin edge wins when valid, otherwise the nearest valid edge.
    void test_origin_edge_when_valid_else_nearest() {
        using hyprimate::Edge;
        CHECK(pick({972, 548, 924, 508}, Edge::Top, true) ==
              Edge::Top);  // body nearest Bottom, origin Top valid
        CHECK(pick({760, 340, 400, 400}, Edge::Left, true) ==
              Edge::Left);  // origin Left valid, Top/Bottom tied
        CHECK(pick({500, 900, 400, 300}, Edge::Bottom, true) ==
              Edge::Left);  // origin Bottom invalid -> nearest (Left)
        CHECK(pick({972, 548, 924, 508}, Edge::Right, true) ==
              Edge::Right);  // origin Right valid though Bottom is nearer
        CHECK(pick({1438, 24, 458, 300}, Edge::Right, true) ==
              Edge::Right);  // origin Right valid (24 px gap)
        CHECK(pick({1000, 24, 300, 300}, Edge::Right, true) ==
              Edge::Right);  // origin Right valid: preferred over nearer Top
        CHECK(pick({1000, 24, 300, 300}, Edge::Left, true) ==
              Edge::Left);  // origin Left valid, preferred
        CHECK(pick({-10, -10, 1940, 1100}, Edge::Right, true) ==
              std::nullopt);  // origin invalid and no valid edge
    }

    // Contact rule: a tiled window emerges from the side away from the neighbours it sits beside.
    constexpr hyprimate::Box WIDE_SCREEN{.x = 0.0, .y = 0.0, .w = 1600.0, .h = 900.0};

    void test_dwindle_contact_keeps_dwindle_edges() {
        using hyprimate::Edge;
        const std::array<hyprimate::Box, 1> LEFT_HALF{{{.x = 24, .y = 24, .w = 924, .h = 1032}}};
        CHECK(pick({972, 24, 924, 1032}, Edge::Bottom, false, LEFT_HALF) ==
              Edge::Right);  // right half beside left half
        const std::array<hyprimate::Box, 1> TOP_RIGHT{{{.x = 972, .y = 24, .w = 924, .h = 508}}};
        CHECK(pick({972, 548, 924, 508}, Edge::Bottom, false, TOP_RIGHT) ==
              Edge::Bottom);  // bottom-right quarter below top-right
        const std::array<hyprimate::Box, 1> BOTTOM_RIGHT{
            {{.x = 972, .y = 548, .w = 924, .h = 508}},
        };
        CHECK(pick({972, 24, 924, 508}, Edge::Bottom, false, BOTTOM_RIGHT) ==
              Edge::Top);  // top-right quarter above bottom-right
        const std::array<hyprimate::Box, 1> SIBLING{{{.x = 972, .y = 548, .w = 458, .h = 508}}};
        CHECK(pick({1438, 548, 458, 508}, Edge::Bottom, false, SIBLING) ==
              Edge::Right);  // right eighth beside its sibling
    }

    void test_scrolling_columns_emerge_away_from_neighbours() {
        using hyprimate::Edge;
        // 1600x900 monitor, full-height columns: gaps_out top 15, bottom 30, sides 30; gaps_in 12.
        const std::array<hyprimate::Box, 1> LEFT{{{.x = 30, .y = 15, .w = 700, .h = 855}}};
        CHECK(pick({742, 15, 800, 855}, Edge::Bottom, false, LEFT, WIDE_SCREEN) ==
              Edge::Right);  // new column right of the left column
        const std::array<hyprimate::Box, 1> RIGHT{{{.x = 870, .y = 15, .w = 700, .h = 855}}};
        CHECK(pick({58, 15, 800, 855}, Edge::Bottom, false, RIGHT, WIDE_SCREEN) ==
              Edge::Left);  // new column left of the right column
        const std::array<hyprimate::Box, 2> AROUND{
            {{.x = 30, .y = 15, .w = 400, .h = 855}, {.x = 1060, .y = 15, .w = 510, .h = 855}},
        };
        CHECK(pick({442, 15, 600, 855}, Edge::Bottom, false, AROUND, WIDE_SCREEN) ==
              Edge::Left);  // between two columns, nearer Left
        const std::array<hyprimate::Box, 2> AROUND2{
            {{.x = 30, .y = 15, .w = 600, .h = 855}, {.x = 1000, .y = 15, .w = 570, .h = 855}},
        };
        CHECK(pick({640, 15, 360, 855}, Edge::Bottom, false, AROUND2, WIDE_SCREEN) ==
              Edge::Right);  // between two columns, nearer Right
        CHECK(pick({742, 15, 800, 855}, Edge::Bottom, false, {}, WIDE_SCREEN) ==
              Edge::Top);  // no neighbours: nearest edge
        CHECK(pick({742, 15, 800, 855}, Edge::Top, true, LEFT, WIDE_SCREEN) ==
              Edge::Top);  // first window: side beats contact
    }

    void test_invalid_contact_candidate_falls_back_to_nearest() {
        using hyprimate::Edge;
        // The body crosses the right screen edge, so the Right candidate is invalid; the nearest
        // valid edge is Top.
        const std::array<hyprimate::Box, 1> LEFT{{{.x = 600, .y = 24, .w = 380, .h = 400}}};
        CHECK(pick({1000, 24, 1000, 400}, Edge::Bottom, false, LEFT) == Edge::Top);
        CHECK(pick({1000, 24, 1000, 400}, Edge::Bottom, false) == Edge::Top);
    }

    void test_only_touching_neighbours_count() {
        using hyprimate::Edge;
        const hyprimate::Box BODY{.x = 972, .y = 548, .w = 924, .h = 508};
        const std::array<hyprimate::Box, 1> GAP64{
            {{.x = 600, .y = 548, .w = 308, .h = 508}},
        };  // facing gap 64 px: counts
        CHECK(pick(BODY, Edge::Bottom, false, GAP64) == Edge::Right);
        const std::array<hyprimate::Box, 1> GAP100{
            {{.x = 600, .y = 548, .w = 272, .h = 508}},
        };  // facing gap 100 px: ignored
        CHECK(pick(BODY, Edge::Bottom, false, GAP100) == Edge::Bottom);
        const std::array<hyprimate::Box, 1> ASIDE{
            {{.x = 600, .y = 24, .w = 360, .h = 400}},
        };  // 12 px gap, no vertical overlap: ignored
        CHECK(pick(BODY, Edge::Bottom, false, ASIDE) == Edge::Bottom);
        const std::array<hyprimate::Box, 1> OVERLAPPING{
            {{.x = 1200, .y = 300, .w = 600, .h = 600}},
        };  // floating-like overlap: ignored
        CHECK(pick(BODY, Edge::Bottom, false, OVERLAPPING) == Edge::Bottom);
    }

    // Both materials call this close policy, including an in-place cloth opening's logical origin.
    void test_shared_close_direction_policy() {
        using hyprimate::Box;
        using hyprimate::CloseSide;
        using hyprimate::Edge;
        const Box BODY{.x = 972, .y = 548, .w = 924, .h = 508};
        const std::array<Box, 1> LEFT{{{.x = 600, .y = 548, .w = 308, .h = 508}}};
        const auto CLOSE = [&](CloseSide side, std::optional<Edge> origin = std::nullopt) {
            const auto FRAME = hyprimate::resolveCloseEdgeFrame(
                BODY, with_shadow(BODY), DEFAULT_SCREEN, LEFT, Edge::Bottom, side, origin);
            return FRAME ? std::optional{FRAME->edge} : std::nullopt;
        };
        CHECK(CLOSE(CloseSide::Auto) == Edge::Right);
        CHECK(CLOSE(CloseSide::Nearest) == Edge::Bottom);  // ignores tiled contact
        CHECK(CLOSE(CloseSide::Origin, Edge::Top) == Edge::Top);
        CHECK(CLOSE(CloseSide::Origin) == CLOSE(CloseSide::Auto));
        const auto LAST = hyprimate::resolveCloseEdgeFrame(BODY,
            with_shadow(BODY),
            DEFAULT_SCREEN,
            {},
            Edge::Top,
            CloseSide::Auto,
            std::nullopt,
            true);
        CHECK(LAST && LAST->edge == Edge::Top);
        const auto NEAREST = hyprimate::resolveCloseEdgeFrame(BODY,
            with_shadow(BODY),
            DEFAULT_SCREEN,
            {},
            Edge::Top,
            CloseSide::Nearest,
            std::nullopt,
            true);
        CHECK(NEAREST && NEAREST->edge == Edge::Bottom);
        for (const auto &[SIDE, EDGE] : std::array<std::pair<CloseSide, Edge>, 4>{
                 {
                     {CloseSide::Left, Edge::Left},
                     {CloseSide::Right, Edge::Right},
                     {CloseSide::Top, Edge::Top},
                     {CloseSide::Bottom, Edge::Bottom},
                 },
             }) {
            CHECK(CLOSE(SIDE) == EDGE);
            CHECK(CLOSE(SIDE, Edge::Top) ==
                  EDGE);  // an opening origin does not override a fixed direction
        }

        const Box CROSSING_BOTTOM{.x = 500, .y = 900, .w = 400, .h = 300};
        const auto FALLBACK = hyprimate::resolveCloseEdgeFrame(CROSSING_BOTTOM,
            with_shadow(CROSSING_BOTTOM),
            DEFAULT_SCREEN,
            {},
            Edge::Bottom,
            CloseSide::Bottom);
        CHECK(FALLBACK &&
              FALLBACK->edge == Edge::Left);  // fixed preference invalid: normal resolver fallback
        const auto ORIGIN_FALLBACK = hyprimate::resolveCloseEdgeFrame(CROSSING_BOTTOM,
            with_shadow(CROSSING_BOTTOM),
            DEFAULT_SCREEN,
            {},
            Edge::Bottom,
            CloseSide::Origin,
            Edge::Bottom);
        CHECK(ORIGIN_FALLBACK && ORIGIN_FALLBACK->edge == Edge::Left);
    }

    void test_close_monitor_and_work_area_bounds() {
        using hyprimate::Box;
        using hyprimate::CloseSide;
        using hyprimate::Edge;
        const Box BODY{.x = 760, .y = 340, .w = 400, .h = 400};
        const auto SCREEN = hyprimate::resolveCloseEdgeFrame(
            BODY, with_shadow(BODY), DEFAULT_SCREEN, {}, Edge::Bottom, CloseSide::Nearest);
        const auto WORK = hyprimate::resolveCloseEdgeFrame(BODY,
            with_shadow(BODY),
            {.x = 0, .y = 100, .w = 1920, .h = 980},
            {},
            Edge::Bottom,
            CloseSide::Nearest);
        CHECK(SCREEN && SCREEN->edge == Edge::Bottom);
        CHECK(WORK && WORK->edge == Edge::Top);

        // Translating the monitor, window, capture and neighbours must preserve every direction and
        // boundary distance.
        const Box SHIFTED_BODY{.x = BODY.x - 1920, .y = BODY.y - 200, .w = BODY.w, .h = BODY.h};
        for (CloseSide side : {
                 CloseSide::Auto,
                 CloseSide::Nearest,
                 CloseSide::Origin,
                 CloseSide::Top,
                 CloseSide::Bottom,
                 CloseSide::Left,
                 CloseSide::Right,
             }) {
            const auto BASE_FRAME = hyprimate::resolveCloseEdgeFrame(
                BODY, with_shadow(BODY), DEFAULT_SCREEN, {}, Edge::Bottom, side, Edge::Right);
            const auto SHIFTED_FRAME = hyprimate::resolveCloseEdgeFrame(SHIFTED_BODY,
                with_shadow(SHIFTED_BODY),
                {.x = -1920, .y = -200, .w = 1920, .h = 1080},
                {},
                Edge::Bottom,
                side,
                Edge::Right);
            CHECK(BASE_FRAME && SHIFTED_FRAME && BASE_FRAME->edge == SHIFTED_FRAME->edge);
            CHECK(BASE_FRAME && SHIFTED_FRAME &&
                  std::abs(BASE_FRAME->edgeDistance - SHIFTED_FRAME->edgeDistance) < 1e-9);
        }
        CHECK(!hyprimate::resolveCloseEdgeFrame(
            BODY, with_shadow(BODY), {}, {}, Edge::Bottom, CloseSide::Auto));
        CHECK(!hyprimate::resolveCloseEdgeFrame({-10, -10, 1940, 1100},
            DEFAULT_SCREEN,
            DEFAULT_SCREEN,
            {},
            Edge::Bottom,
            CloseSide::Right));
    }

    // Bead transitions.
    using hyprimate::EdgeFrame;
    using hyprimate::EdgeTransition;
    using hyprimate::LiquidMesh;
    constexpr double STEP = 1.0 / 240.0;

    EdgeFrame frame_for(hyprimate::Box body,
        hyprimate::Edge fallback = hyprimate::Edge::Bottom,
        bool first = false) {
        return *hyprimate::resolveEdgeFrame(
            body, with_shadow(body), DEFAULT_SCREEN, {}, fallback, first);
    }
    const EdgeFrame QUARTER = frame_for({.x = 972, .y = 548, .w = 924, .h = 508});  // Bottom edge
    // Capture (decorated + shadow) size of QUARTER: 948 x 532, half extents 474 x 266.
    constexpr Vec2 CAPTURE{.x = 948.0, .y = 532.0};

    // Displayed centre displacement (logical px) from the rest capture centre.
    Vec2 centre_of(const EdgeTransition &transition) {
        return transition.map({.x = 0.0, .y = 0.0}, CAPTURE);
    }

    // Movement-field gain for `mesh` and `edge_transition` at the rest capture size (no grid).
    double fit_gain(const LiquidMesh &mesh,
        const EdgeTransition &edge_transition,
        Vec2 comp = {.x = 1.0, .y = 1.0}) {
        return hyprimate::fitLiquidPoseGain(
            hyprimate::LiquidPose{
                .mesh = mesh, .transition = edge_transition, .compensation = comp},
            CAPTURE,
            nullptr);
    }

    // Centre displacement into the screen along the edge normal (0 at rest, negative toward and
    // behind the edge).
    double centre_inward(const EdgeTransition &transition) {
        const Vec2 CENTRE = centre_of(transition);
        const Vec2 INWARD = hyprimate::inwardNormal(transition.frame().edge);
        return (CENTRE.x * INWARD.x) + (CENTRE.y * INWARD.y);
    }

    // Displayed n (into the screen) of the body near side at the body's tangential centre.
    double near_side(const EdgeTransition &transition) {
        const EdgeFrame &edge_frame = transition.frame();
        const Vec2 POINT = hyprimate::edgePoint(
            edge_frame, 0.5 * (edge_frame.bodyT0 + edge_frame.bodyT1), edge_frame.bodyNear);
        return hyprimate::edgeNormal(edge_frame, transition.map(POINT, CAPTURE));
    }
    double centre_t(const EdgeFrame &frame) {
        return 0.5 * (frame.bodyT0 + frame.bodyT1);
    }

    void test_open_starts_hidden_and_settles_exactly() {
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        CHECK(transition.active());
        // Hidden start: every capture corner is on the far side of the edge (n <= 0).
        for (const double SIGN_X : {-1.0, 1.0}) {
            for (const double SIGN_Y : {-1.0, 1.0}) {
                CHECK(hyprimate::edgeNormal(QUARTER,
                          transition.map({SIGN_X * 474.0, SIGN_Y * 266.0}, CAPTURE)) <= 1e-6);
            }
        }
        const Vec2 FIRST = transition.takeTravel();
        CHECK(FIRST.x == 0.0 && FIRST.y == 0.0);  // begin is not travel
        double max_inward = -1e9;
        for (int i = 0; i < 960 && transition.active(); ++i) {
            transition.advance(STEP);
            max_inward = std::max(max_inward, centre_inward(transition));
        }
        CHECK(!transition.active());
        // Exact identity once settled.
        const Vec2 CENTRE = centre_of(transition);
        CHECK(CENTRE.x == 0.0 && CENTRE.y == 0.0);
        CHECK(transition.map({123.0, -45.0}, CAPTURE).x == 123.0 &&
              transition.map({123.0, -45.0}, CAPTURE).y == -45.0);
        CHECK(max_inward > 5.0);  // visible pop overshoot
    }

    // The frame's capture is taken when the open begins, before the shadow reports its extents; the
    // live capture is larger. Once the spring is nearly settled, the window must fill the live
    // capture along the normal, not the frame's.
    void test_open_fills_live_capture_along_normal() {
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        constexpr Vec2 K_LIVE{
            .x = CAPTURE.x,
            .y = CAPTURE.y + 24.0,
        };  // Bottom edge: the normal is vertical
        double worst = 0.0;
        int checked = 0;
        for (int i = 0; i < 960 && transition.active(); ++i) {
            transition.advance(STEP);
            if (i < 240 || !transition.active()) {
                continue;  // from 1 s of 240 Hz steps the spring is within a fraction of a pixel
            }
            const double FAR = hyprimate::edgeNormal(
                QUARTER, transition.map({.x = 0.0, .y = -0.5 * K_LIVE.y}, K_LIVE));
            const double NEAR = hyprimate::edgeNormal(
                QUARTER, transition.map({.x = 0.0, .y = 0.5 * K_LIVE.y}, K_LIVE));
            worst = std::max(worst, std::abs((FAR - NEAR) - K_LIVE.y));
            ++checked;
        }
        CHECK(checked > 0);
        CHECK(worst < 1.0);
    }

    // The map never folds the image: every cell of a 33x33 grid keeps its positive orientation
    // during the open.
    void test_bead_map_does_not_fold() {
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        constexpr int K_SIDE = 33;
        for (int frame = 0; frame < 240 && transition.active(); ++frame) {
            transition.advance(STEP);
            if (!transition.active()) {
                break;
            }
            std::array<Vec2, K_SIDE * K_SIDE> mapped{};
            for (int j = 0; j < K_SIDE; ++j) {
                for (int i = 0; i < K_SIDE; ++i) {
                    const double TEX_U = static_cast<double>(i) / (K_SIDE - 1);
                    const double TEX_V = static_cast<double>(j) / (K_SIDE - 1);
                    mapped[(j * K_SIDE) + i] = transition.map(
                        {.x = (TEX_U - 0.5) * CAPTURE.x, .y = (TEX_V - 0.5) * CAPTURE.y}, CAPTURE);
                }
            }
            for (int j = 0; j + 1 < K_SIDE; ++j) {
                for (int i = 0; i + 1 < K_SIDE; ++i) {
                    const Vec2 TOP_LEFT = mapped[(j * K_SIDE) + i];
                    const Vec2 TOP_RIGHT = mapped[(j * K_SIDE) + i + 1];
                    const Vec2 BOTTOM_LEFT = mapped[((j + 1) * K_SIDE) + i];
                    const double AREA =
                        ((TOP_RIGHT.x - TOP_LEFT.x) * (BOTTOM_LEFT.y - TOP_LEFT.y)) -
                        ((TOP_RIGHT.y - TOP_LEFT.y) * (BOTTOM_LEFT.x - TOP_LEFT.x));
                    CHECK(AREA > 0.0);
                }
            }
        }
    }

    void test_open_edges_point_the_right_way() {
        using hyprimate::Edge;
        const std::array<std::pair<Edge, Vec2>, 4> CASES{
            {
                {Edge::Bottom, {.x = 0, .y = 1}},
                {Edge::Top, {.x = 0, .y = -1}},
                {Edge::Left, {.x = -1, .y = 0}},
                {Edge::Right, {.x = 1, .y = 0}},
            },
        };
        for (const auto &[EDGE, SIGN] : CASES) {
            EdgeTransition transition;
            transition.begin(Transition::Open,
                frame_for({.x = 24, .y = 24, .w = 1872, .h = 1032}, EDGE, true),
                TransitionParams{},
                STRENGTH);
            const Vec2 CENTRE = centre_of(transition);
            CHECK(CENTRE.x * SIGN.x + CENTRE.y * SIGN.y > 200.0);  // starts hidden beyond that edge
        }
    }

    // Meniscus during open: the neck attaches, snaps once, and recoils.
    void test_open_neck_attaches_snaps_once_and_recoils() {
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        int snaps = 0;
        bool tucked = false;
        bool saw_zero_after_snap = false;
        bool second_lobe = false;
        double prev_peak = transition.meniscusPeak();
        double prev_near = near_side(transition);
        for (int i = 0; i < 480 && transition.active(); ++i) {
            transition.advance(STEP);
            const double PEAK = transition.meniscusPeak();
            const double NEAR_SIDE = near_side(transition);
            CHECK(PEAK >= 0.0);
            // Tolerance 8 px: the recoil spring legitimately moves ~5 px per substep. Snaps to or
            // from zero (>= Fs+Lp ~ 44 px) still fail.
            CHECK(
                std::abs(PEAK - prev_peak) <= std::abs(NEAR_SIDE - prev_near) + 8.0);  // continuous
            if (NEAR_SIDE >= 0.0 && PEAK > NEAR_SIDE + 1.0) {
                tucked = true;  // tip under the window
            }
            if (tucked && PEAK < NEAR_SIDE - 1.0 && snaps == 0) {
                ++snaps;  // gap opened: snapped
            } else if (snaps == 1 && PEAK > NEAR_SIDE + 1.0) {
                ++snaps;  // must never re-attach
            }
            if (snaps == 1 && PEAK == 0.0) {
                saw_zero_after_snap = true;
            }
            if (saw_zero_after_snap && PEAK > 0.5) {
                second_lobe = true;  // the bar's small rebound bump
            }
            prev_peak = PEAK;
            prev_near = NEAR_SIDE;
        }
        CHECK(tucked);
        CHECK(snaps == 1);
        CHECK(second_lobe);
        CHECK(transition.meniscusPeak() == 0.0);
    }

    void test_meniscus_shape_bounds() {
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        for (int i = 0; i < 240 && transition.active(); ++i) {
            transition.advance(STEP);
            const double LOW = transition.meniscusLow();
            const double HIGH = transition.meniscusHigh();
            CHECK(LOW <= HIGH);
            CHECK(transition.meniscus(LOW - 1.0) == 0.0 && transition.meniscus(HIGH + 1.0) == 0.0);
            for (int sample = 0; sample <= 64; ++sample) {
                CHECK(transition.meniscus(LOW + (HIGH - LOW) * sample / 64.0) <=
                      transition.meniscusPeak() + 1e-9);
            }
            CHECK(transition.meniscus(centre_t(QUARTER)) ==
                  transition.meniscusPeak());  // peak at the window centre column
        }
    }

    // Refresh-rate agreement.
    void test_open_agrees_across_refresh_rates() {
        EdgeTransition at240_hz;
        EdgeTransition at144_hz;
        at240_hz.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        at144_hz.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        for (int k = 1; k <= 24; ++k) {  // compare every 1/48 s for 0.5 s
            for (int i = 0; i < 5; ++i) {
                at240_hz.advance(1.0 / 240.0);  // 240 Hz-equivalent chunks (60 Hz = 4 of these)
            }
            for (int i = 0; i < 3; ++i) {
                at144_hz.advance(1.0 / 144.0);
            }
            CHECK(std::abs(centre_of(at240_hz).y - centre_of(at144_hz).y) < 0.5);
            CHECK(std::abs(at240_hz.meniscusPeak() - at144_hz.meniscusPeak()) < 0.5);
        }
    }

    // Movement-field feed: summed travel over the open equals the centre's total inward
    // displacement.
    void test_open_travel_sums_to_centre_displacement() {
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        const Vec2 INWARD = hyprimate::inwardNormal(QUARTER.edge);
        const double START = centre_inward(transition);
        double sum = 0.0;
        for (int i = 0; i < 960 && transition.active(); ++i) {
            transition.advance(STEP);
            const Vec2 TRAVEL = transition.takeTravel();
            sum += (TRAVEL.x * INWARD.x) + (TRAVEL.y * INWARD.y);
        }
        CHECK(std::abs(sum - (0.0 - START)) < 0.5);  // from the start displacement back to rest
    }

    // Close.
    // Substep of the drain from rest at defaults (0.46 s): no liquid before it.
    constexpr int DRAIN_SUBSTEP = 111;

    void test_close_from_rest_drains_into_the_edge() {
        EdgeTransition transition;
        transition.begin(Transition::Close, QUARTER, TransitionParams{}, STRENGTH);
        CHECK(transition.active() && std::abs(centre_of(transition).x) < 1e-9 &&
              std::abs(centre_of(transition).y) < 1e-9);
        double prev = centre_inward(transition);
        for (int i = 0; i < 480 && transition.active(); ++i) {
            transition.advance(STEP);
            if (!transition.active()) {
                break;
            }
            CHECK(centre_inward(transition) <= prev + 1e-9);
            prev = centre_inward(transition);  // always toward the edge
        }
        CHECK(transition.closed());
        CHECK(transition.meniscusPeak() == 0.0);
    }

    // Shrinking into a bead must not brake to a hover before the dive.
    void test_close_keeps_moving_through_the_shrink() {
        for (const double MOMENTUM : {0.0, 0.5, 1.0}) {
            EdgeTransition transition;
            transition.begin(Transition::Close,
                QUARTER,
                TransitionParams{.momentum = MOMENTUM},
                STRENGTH,
                false);
            double previous_travel = 0.0;
            bool continuous = true;
            for (int i = 0; i < 100; ++i) {
                transition.advance(STEP);
                const Vec2 TRAVEL = transition.takeTravel();
                const Vec2 INWARD = hyprimate::inwardNormal(QUARTER.edge);
                const double OUTWARD = -((TRAVEL.x * INWARD.x) + (TRAVEL.y * INWARD.y));
                if (i >= 24) {
                    continuous = continuous && OUTWARD > 0.0 && OUTWARD >= 0.95 * previous_travel;
                }
                previous_travel = OUTWARD;
            }
            CHECK(continuous);
        }
    }

    void test_close_continues_an_interrupted_open() {
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        for (int i = 0; i < 30; ++i) {
            transition.advance(STEP);
        }
        const Vec2 BEFORE = centre_of(transition);
        const double PEAK_BEFORE = transition.meniscusPeak();
        const auto OTHER = frame_for({.x = 24, .y = 24, .w = 900, .h = 500},
            hyprimate::Edge::Left,
            true);  // different edge: ignored
        transition.begin(Transition::Close, OTHER, TransitionParams{}, STRENGTH);
        CHECK(transition.frame().edge == hyprimate::Edge::Bottom);
        CHECK(std::abs(centre_of(transition).x - BEFORE.x) < 1e-9 &&
              std::abs(centre_of(transition).y - BEFORE.y) < 1e-9);
        CHECK(std::abs(transition.meniscusPeak() - PEAK_BEFORE) <
              1e-9);  // Open->Close begin is continuous
        for (int i = 0; i < 480 && transition.active(); ++i) {
            transition.advance(STEP);
        }
        CHECK(transition.closed());
    }

    // A close from rest shows no liquid until the drain (0.46 s at default elasticity = substep
    // 111).
    void test_close_from_rest_has_no_liquid_before_drain() {
        constexpr int K_DRAIN_STEP = 111;
        EdgeTransition transition;
        transition.begin(Transition::Close, QUARTER, TransitionParams{}, STRENGTH);
        for (int i = 1; i < K_DRAIN_STEP && transition.active(); ++i) {
            transition.advance(STEP);
            CHECK(transition.meniscusPeak() == 0.0);
        }
    }

    // Close momentum changes the dive even when no opening was interrupted.
    void test_close_momentum_changes_dive() {
        EdgeTransition low;
        EdgeTransition high;
        low.begin(Transition::Close, QUARTER, TransitionParams{.momentum = 0.0}, STRENGTH, false);
        high.begin(Transition::Close, QUARTER, TransitionParams{.momentum = 1.0}, STRENGTH, false);
        for (int i = 0; i < 90; ++i) {
            low.advance(STEP);
            high.advance(STEP);
        }
        const double LOW_NORMAL =
            hyprimate::edgeNormal(QUARTER, low.map({.x = 0, .y = 0}, CAPTURE));
        const double HIGH_NORMAL =
            hyprimate::edgeNormal(QUARTER, high.map({.x = 0, .y = 0}, CAPTURE));
        CHECK(HIGH_NORMAL < LOW_NORMAL);
    }

    // A window whose real box follows the pop-out must sit exactly where the outline map puts the
    // capture's extremes, on every step, and be the rest box once the open has settled.
    void test_bead_box_matches_map() {
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        double worst = 0.0;
        for (int i = 0; i < 960 && transition.active(); ++i) {
            transition.advance(STEP);
            if (!transition.active()) {
                break;
            }
            const Vec2 BEAD_CENTRE = transition.beadCentre();
            const Vec2 BEAD_HALF = transition.beadHalf(CAPTURE);
            const Vec2 TOP = transition.map({.x = 0.0, .y = -0.5 * CAPTURE.y}, CAPTURE);
            const Vec2 BOTTOM = transition.map({.x = 0.0, .y = 0.5 * CAPTURE.y}, CAPTURE);
            const Vec2 LEFT = transition.map({.x = -0.5 * CAPTURE.x, .y = 0.0}, CAPTURE);
            const Vec2 RIGHT = transition.map({.x = 0.5 * CAPTURE.x, .y = 0.0}, CAPTURE);
            worst = std::max({
                worst,
                std::abs(TOP.y - (BEAD_CENTRE.y - BEAD_HALF.y)),
                std::abs(BOTTOM.y - (BEAD_CENTRE.y + BEAD_HALF.y)),
                std::abs(LEFT.x - (BEAD_CENTRE.x - BEAD_HALF.x)),
                std::abs(RIGHT.x - (BEAD_CENTRE.x + BEAD_HALF.x)),
            });
        }
        CHECK(worst < 1e-6);
        const Vec2 CENTRE = transition.beadCentre();
        const Vec2 HALF_SIZE = transition.beadHalf(CAPTURE);
        CHECK(CENTRE.x == 0.0 && CENTRE.y == 0.0);
        CHECK(HALF_SIZE.x == 0.5 * CAPTURE.x && HALF_SIZE.y == 0.5 * CAPTURE.y);
    }

    // Movement-field guard: idle and settled meshes give 1; a moving mesh gives a power-of-two gain
    // in [0, 1].
    void test_displacement_gain_guard() {
        EdgeTransition idle;
        LiquidMesh fresh;
        CHECK(fresh.settled());
        CHECK(fit_gain(fresh, idle) == 1.0);

        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        CHECK(fit_gain(fresh, transition) == 1.0);  // settled mesh: no gain reduction

        LiquidMesh moving;
        moving.recordTranslation({.x = 40.0, .y = 0.0}, CAPTURE, STRENGTH);
        for (int i = 0; i < 24; ++i) {
            moving.advance(STEP, CAPTURE);
        }
        const double GAIN = fit_gain(moving, transition);
        CHECK(GAIN >= 0.0 && GAIN <= 1.0);
        if (GAIN > 0.0) {
            CHECK(std::abs(std::log2(GAIN) - std::round(std::log2(GAIN))) < 1e-9);  // halvings of 1
        }
    }

    // Render mesh the renderer draws at displacement gain g, filled through the same LiquidPose
    // path. Output is in logical px (unit output device), so triangle-area ratios equal those in
    // device px.
    std::vector<hyprimate::LiquidVertex> render_mesh(const EdgeTransition &edge_transition,
        const LiquidMesh &mesh,
        double gain,
        Vec2 size,
        Vec2 centre) {
        hyprimate::LiquidPose pose{
            .mesh = mesh, .transition = edge_transition, .compensation = centre};
        pose.displacementGain = gain;
        hyprimate::LiquidPoseGrid grid;
        std::vector<hyprimate::LiquidVertex> out;
        const hyprimate::Box LOGICAL{.x = 0.0, .y = 0.0, .w = size.x, .h = size.y};
        hyprimate::fillLiquidRenderMesh(pose,
            LOGICAL,
            1.0,
            hyprimate::Box{.x = 0.0, .y = 0.0, .w = 1.0, .h = 1.0},
            Vec2{.x = 0.0, .y = 0.0},
            size,
            grid,
            out);
        return out;
    }

    // Smallest triangle-area ratio (moved / gain-0) over the emitted render mesh. A ratio under 0.2
    // is a fold that pinches the window.
    double min_render_ratio(const EdgeTransition &transition,
        const LiquidMesh &mesh,
        double gain,
        Vec2 size,
        Vec2 centre) {
        const auto BASE = render_mesh(transition, mesh, 0.0, size, centre);
        const auto MOVED = render_mesh(transition, mesh, gain, size, centre);
        const auto AREA = [](const hyprimate::LiquidVertex &first,
                              const hyprimate::LiquidVertex &box,
                              const hyprimate::LiquidVertex &third) {
            return ((static_cast<double>(box.x) - first.x) *
                       (static_cast<double>(third.y) - first.y)) -
                   ((static_cast<double>(box.y) - first.y) *
                       (static_cast<double>(third.x) - first.x));
        };
        double worst = 1.0;
        for (std::size_t triangle = 0; triangle + 2 < BASE.size();
            triangle += 3) {  // six vertices per cell: two triangles of three
            const double REF = AREA(BASE[triangle], BASE[triangle + 1], BASE[triangle + 2]);
            if (std::abs(REF) < 1e-9) {
                continue;  // degenerate at rest: nothing to fold
            }
            worst = std::min(
                worst, AREA(MOVED[triangle], MOVED[triangle + 1], MOVED[triangle + 2]) / REF);
        }
        return worst;
    }

    // Regression for the renderer-order fold: the gain must keep every triangle >= 20% of its
    // gain-0 area at any open scale.
    void test_displacement_gain_keeps_triangles_when_compensated() {
        LiquidMesh mesh;
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        for (int i = 0; i < 60 && transition.active(); ++i) {
            transition.advance(STEP);  // mid-grow
        }
        for (int i = 0; i < 12; ++i) {
            mesh.recordTranslation({.x = -60.0, .y = 0.0}, CAPTURE, STRENGTH);
            mesh.advance(STEP, CAPTURE);
        }
        for (const double COMPENSATION : {1.0, 1.5, 2.0}) {
            const Vec2 COMP{.x = COMPENSATION, .y = COMPENSATION};
            const double GAIN = fit_gain(mesh, transition, COMP);
            CHECK(GAIN < 1.0);
            CHECK(min_render_ratio(transition, mesh, GAIN, CAPTURE, COMP) >= 0.2 - 1e-9);
        }
    }

    // Closing from rest returns the dive's final travel when it reaches Closed; nothing is returned
    // afterwards.
    void test_close_returns_final_travel() {
        EdgeTransition transition;
        transition.begin(Transition::Close, QUARTER, TransitionParams{}, STRENGTH, false);
        const Vec2 INWARD = hyprimate::inwardNormal(QUARTER.edge);
        bool saw_closed = false;
        double closing_travel = 0.0;
        for (int i = 0; i < 480 && transition.active(); ++i) {
            transition.advance(STEP);
            const bool REACHED = transition.closed();
            const Vec2 TRAVEL = transition.takeTravel();
            if (REACHED && !saw_closed) {
                saw_closed = true;
                closing_travel = (TRAVEL.x * INWARD.x) + (TRAVEL.y * INWARD.y);
            }
        }
        CHECK(saw_closed);
        CHECK(std::abs(closing_travel) > 1e-3);
        const Vec2 AFTER = transition.takeTravel();
        CHECK(AFTER.x == 0.0 && AFTER.y == 0.0);
    }

    // Close begun on the same substep as an open: the open's neck is still hidden, so there is no
    // liquid until the drain.
    void test_close_after_open_begin_has_no_liquid() {
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        transition.begin(Transition::Close, QUARTER, TransitionParams{}, STRENGTH);
        CHECK(transition.meniscusPeak() == 0.0);
        for (int i = 1; i < DRAIN_SUBSTEP && transition.active(); ++i) {
            transition.advance(STEP);
            CHECK(transition.meniscusPeak() == 0.0);
        }
    }

    constexpr double BEAD_RADIUS = 0.14 * 508.0;  // QUARTER bead radius (px)

    // Local maxima of the meniscus above 5% of `peak`, sampled every 0.02 r across +-2 r of the
    // window centre `centre`.
    std::vector<double> lobe_centres(const EdgeTransition &transition, double centre, double peak) {
        constexpr int K_HALF = 100;
        std::vector<double> heights((2 * K_HALF) + 1);
        for (int k = -K_HALF; k <= K_HALF; ++k) {
            heights[k + K_HALF] = transition.meniscus(centre + (k * 0.02 * BEAD_RADIUS));
        }
        std::vector<double> centres;
        for (int k = 1; k + 1 < static_cast<int>(heights.size()); ++k) {
            if (heights[k] > 0.05 * peak && heights[k] > heights[k - 1] &&
                heights[k] >= heights[k + 1]) {
                centres.push_back(centre + ((k - K_HALF) * 0.02 * BEAD_RADIUS));
            }
        }
        return centres;
    }

    // After the drain the splash rises as a single bump centred where the window went under (no
    // side ripples), then returns to exactly zero and the transition is closed.
    void test_close_splash_after_drain() {
        EdgeTransition transition;
        transition.begin(Transition::Close, QUARTER, TransitionParams{}, STRENGTH);
        const double CENTRE = centre_t(QUARTER);
        double splash_peak = 0.0;
        double prev_peak = 0.0;
        double prev_near = near_side(transition);
        int first_splash = -1;
        int last_splash = -1;
        bool one_centred_lobe = false;
        for (int i = 1; i <= 600 && transition.active(); ++i) {
            transition.advance(STEP);
            const double PEAK = transition.meniscusPeak();
            const double NEAR_SIDE = near_side(transition);
            CHECK(std::abs(PEAK - prev_peak) <= std::abs(NEAR_SIDE - prev_near) + 8.0);
            prev_peak = PEAK;
            prev_near = NEAR_SIDE;
            if (PEAK > 0.0) {
                if (first_splash < 0) {
                    first_splash = i;
                }
                last_splash = i;
            }
            if (PEAK > splash_peak) {
                splash_peak = PEAK;
                const auto LOBES = lobe_centres(transition, CENTRE, PEAK);
                one_centred_lobe =
                    LOBES.size() == 1 && std::abs(LOBES[0] - CENTRE) < 0.1 * BEAD_RADIUS;
            }
        }
        CHECK(splash_peak > 0.3 * BEAD_RADIUS);
        CHECK(one_centred_lobe);
        CHECK(first_splash >= DRAIN_SUBSTEP);
        CHECK(last_splash - first_splash >= 95 &&
              last_splash - first_splash <= 110);  // 0.42 s = ~101 substeps
        CHECK(transition.meniscusPeak() == 0.0);
        CHECK(transition.closed());
    }

    // closeSplash = false: no liquid at any time, and the transition is closed by the drain.
    void test_close_splash_off_leaves_no_liquid() {
        EdgeTransition transition;
        transition.begin(Transition::Close, QUARTER, TransitionParams{}, STRENGTH, false);
        int closed_at = -1;
        for (int i = 1; i <= 480 && transition.active(); ++i) {
            transition.advance(STEP);
            CHECK(transition.meniscusPeak() == 0.0);
            if (transition.closed() && closed_at < 0) {
                closed_at = i;
            }
        }
        CHECK(closed_at == DRAIN_SUBSTEP);
    }

    // A close interrupting an opening with a visible neck: the liquid snaps into the recoil within
    // the continuity tolerance, decays away, and only then does the splash rise.
    void test_close_interrupting_open_neck_decays_then_splashes() {
        EdgeTransition transition;
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        for (int i = 0; i < 240 && transition.active() && transition.meniscusPeak() < 5.0; ++i) {
            transition.advance(STEP);
        }
        CHECK(transition.meniscusPeak() >= 5.0);
        transition.begin(Transition::Close, QUARTER, TransitionParams{}, STRENGTH);
        double prev_peak = transition.meniscusPeak();
        double prev_near = near_side(transition);
        bool saw_quiet = false;
        bool splash_after_quiet = false;
        for (int i = 0; i < 960 && transition.active(); ++i) {
            transition.advance(STEP);
            const double PEAK = transition.meniscusPeak();
            const double NEAR_SIDE = near_side(transition);
            CHECK(std::abs(PEAK - prev_peak) <= std::abs(NEAR_SIDE - prev_near) + 8.0);
            prev_peak = PEAK;
            prev_near = NEAR_SIDE;
            if (PEAK <= 0.5) {
                saw_quiet = true;
            } else if (saw_quiet && PEAK > 0.3 * BEAD_RADIUS) {
                splash_after_quiet = true;
            }
        }
        CHECK(saw_quiet && splash_after_quiet);
        CHECK(transition.closed());
    }

    // The splash agrees between 60 Hz and 144 Hz frame clocks within 0.5 px, sampled every 1/12 s.
    void test_close_splash_agrees_across_refresh_rates() {
        EdgeTransition at60_hz;
        EdgeTransition at144_hz;
        at60_hz.begin(Transition::Close, QUARTER, TransitionParams{}, STRENGTH);
        at144_hz.begin(Transition::Close, QUARTER, TransitionParams{}, STRENGTH);
        for (int k = 1; k <= 12; ++k) {
            for (int i = 0; i < 5; ++i) {
                at60_hz.advance(1.0 / 60.0);
            }
            for (int i = 0; i < 12; ++i) {
                at144_hz.advance(1.0 / 144.0);
            }
            CHECK(std::abs(at60_hz.meniscusPeak() - at144_hz.meniscusPeak()) < 0.5);
        }
    }

    // Invalid input.
    void test_invalid_inputs() {
        constexpr double K_NA_N = std::numeric_limits<double>::quiet_NaN();
        EdgeTransition transition;
        transition.begin(Transition::Open, EdgeFrame{}, TransitionParams{}, STRENGTH);
        CHECK(!transition.active());
        transition.begin(
            Transition::Open, QUARTER, TransitionParams{.elasticity = K_NA_N}, STRENGTH);
        CHECK(!transition.active());
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, 0.0);
        CHECK(!transition.active());
        transition.begin(Transition::Open, QUARTER, TransitionParams{}, STRENGTH);
        transition.advance(K_NA_N);
        CHECK(!transition.active() && !transition.closed());
        CHECK(transition.map({0.0, 0.0}, CAPTURE).x == 0.0 &&
              transition.map({0.0, 0.0}, CAPTURE).y == 0.0);
        transition.begin(Transition::Close, QUARTER, TransitionParams{}, STRENGTH);
        transition.advance(0.3);
        CHECK(transition.closed());
        transition.begin(Transition::Close, EdgeFrame{}, TransitionParams{}, STRENGTH);
        CHECK(!transition.active() && !transition.closed());
    }

}  // namespace

int main() {
    test_edge_follows_dwindle_splits();
    test_edge_ties_and_validity();
    test_frame_geometry();
    test_origin_edge_when_valid_else_nearest();
    test_dwindle_contact_keeps_dwindle_edges();
    test_scrolling_columns_emerge_away_from_neighbours();
    test_invalid_contact_candidate_falls_back_to_nearest();
    test_only_touching_neighbours_count();
    test_shared_close_direction_policy();
    test_close_monitor_and_work_area_bounds();
    test_open_starts_hidden_and_settles_exactly();
    test_open_fills_live_capture_along_normal();
    test_bead_map_does_not_fold();
    test_open_edges_point_the_right_way();
    test_open_neck_attaches_snaps_once_and_recoils();
    test_bead_box_matches_map();
    test_meniscus_shape_bounds();
    test_open_agrees_across_refresh_rates();
    test_open_travel_sums_to_centre_displacement();
    test_close_from_rest_drains_into_the_edge();
    test_close_keeps_moving_through_the_shrink();
    test_close_continues_an_interrupted_open();
    test_close_from_rest_has_no_liquid_before_drain();
    test_close_momentum_changes_dive();
    test_displacement_gain_guard();
    test_displacement_gain_keeps_triangles_when_compensated();
    test_close_returns_final_travel();
    test_close_after_open_begin_has_no_liquid();
    test_close_splash_after_drain();
    test_close_splash_off_leaves_no_liquid();
    test_close_interrupting_open_neck_decays_then_splashes();
    test_close_splash_agrees_across_refresh_rates();
    test_invalid_inputs();
    return g_failures == 0 ? 0 : 1;
}
