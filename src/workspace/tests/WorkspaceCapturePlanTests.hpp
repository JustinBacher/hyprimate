#pragma once

#include <array>
#include <limits>
#include <memory>
#include <vector>

#include "Check.hpp"
#include "FakeWorkspacePort.hpp"

namespace hyprimate::workspace::test::capture_plan_tests {

    struct Fixture {
        FakeWorkspacePort port;
        WorkspaceTurn<FakeWorkspacePort> turn{port};
        std::shared_ptr<FakeWorkspace> workspace = std::make_shared<FakeWorkspace>(1);
        FakeWorkspacePort::Composition composition = port.composition(workspace);

        Fixture() {
            port.capture_scene.enabled = true;
            port.capture_scene.features = {.wallpaper = true, .layered = true, .individual = true};
            port.capture_scene.events = {CaptureBoundary::Wallpaper,
                CaptureBoundary::BeforeWindows,
                CaptureBoundary::AfterWindows};
        }
        bool stage() {
            return turn.stage(composition);
        }
    };

    inline CaptureWindowFacts window(CapturePixelBounds bounds) {
        return {.present = true,
            .mapped = true,
            .visible = true,
            .bounds = bounds,
            .output_width = 1920,
            .output_height = 1080};
    }

    inline void compact_planes_and_overlap() {
        Fixture fixture;
        auto &scene = fixture.port.capture_scene;
        scene.events.insert(scene.events.begin() + 2,
            window({.left = 10.2, .top = 20.8, .right = 300.1, .bottom = 220.2}));
        scene.events.insert(scene.events.begin() + 3,
            window({.left = 100.0, .top = 100.0, .right = 400.0, .bottom = 300.0}));
        CHECK(fixture.stage());
        const auto LAYOUT = capture_plane_layout(scene.features);
        CHECK(LAYOUT.layered);
        CHECK(!LAYOUT.separate_windows);
        CHECK(LAYOUT.separate_top_backdrop);
        CHECK((scene.boundaries == std::vector<CapturePlane>{CapturePlane::Wallpaper,
                                       CapturePlane::Bottom,
                                       CapturePlane::TopBackdrop}));
        CHECK(scene.crops.size() == 2);
        CHECK(scene.native_window_draws == 2);
        CHECK(scene.crops[0].x == 10 && scene.crops[0].y == 20);
        CHECK(scene.crops[0].width == 291 && scene.crops[0].height == 201);
        CHECK(scene.crops[1].x == 100 && scene.crops[1].width == 300);
        CHECK(scene.retained_bytes == 8ULL * ((291ULL * 201ULL) + (300ULL * 200ULL)));
        const auto RETAINED = scene.retained_bytes;
        fixture.turn.release_gesture();
        CHECK(fixture.stage());
        CHECK(scene.retained_bytes == RETAINED);

        Fixture aggregate;
        aggregate.port.capture_scene.features.individual = false;
        aggregate.port.capture_scene.events = scene.events;
        CHECK(aggregate.stage());
        CHECK(aggregate.port.capture_scene.crops.empty());
        CHECK(aggregate.port.capture_scene.native_window_draws == 2);
        CHECK((aggregate.port.capture_scene.boundaries ==
               std::vector<CapturePlane>{
                   CapturePlane::Wallpaper, CapturePlane::Bottom, CapturePlane::Windows}));
        CHECK(capture_plane_layout(aggregate.port.capture_scene.features).separate_windows);
    }

    inline void popup_selection_and_hud() {
        Fixture fixture;
        auto popup = window({.left = 0, .top = 0, .right = 100, .bottom = 100});
        popup.popup_pass = true;
        fixture.port.capture_scene.events.insert(
            fixture.port.capture_scene.events.begin() + 2, popup);
        popup.visible_popup = true;
        fixture.port.capture_scene.events.insert(
            fixture.port.capture_scene.events.begin() + 3, popup);
        popup.visible_popup = false;
        popup.overlay = true;
        fixture.port.capture_scene.events.insert(
            fixture.port.capture_scene.events.begin() + 4, popup);
        popup.hidden = true;
        fixture.port.capture_scene.events.insert(
            fixture.port.capture_scene.events.begin() + 5, popup);
        CHECK(fixture.stage());
        CHECK(fixture.port.capture_scene.native_window_draws == 4);
        CHECK(fixture.port.capture_scene.crops.size() == 2);

        Fixture hud;
        hud.port.capture_scene.features.native_hud = true;
        hud.port.capture_scene.events.emplace_back(CaptureLayerFacts{.background = true});
        hud.port.capture_scene.events.emplace_back(CaptureLayerFacts{});
        hud.port.capture_scene.events.emplace_back(
            CaptureLayerFacts{.background = true, .popup = true});
        CHECK(hud.stage());
        CHECK(hud.port.capture_scene.native_layer_draws == 1);
    }

    inline void bounded_crops_and_allocation_failures() {
        Fixture clipped;
        clipped.port.capture_scene.events.insert(clipped.port.capture_scene.events.begin() + 2,
            window({.left = -30, .top = -40, .right = 4000, .bottom = 3000}));
        CHECK(clipped.stage());
        CHECK(clipped.port.capture_scene.crops.size() == 1);
        CHECK(clipped.port.capture_scene.crops[0].x == 0);
        CHECK(clipped.port.capture_scene.crops[0].width == 1920);
        CHECK(clipped.port.capture_scene.crops[0].height == 1080);

        Fixture offscreen;
        offscreen.port.capture_scene.events.insert(offscreen.port.capture_scene.events.begin() + 2,
            window({.left = 2000, .top = 0, .right = 2100, .bottom = 100}));
        CHECK(offscreen.stage());
        CHECK(offscreen.port.capture_scene.crops.empty());
        CHECK(offscreen.port.capture_scene.native_window_draws == 1);

        for (const bool WIDE : {false, true}) {
            Fixture exact;
            auto &scene = exact.port.capture_scene;
            scene.wide_color = WIDE;
            const std::uint64_t LIMIT = (WIDE ? 1024ULL : 512ULL) * 1024 * 1024;
            const std::uint64_t CROP_PAIR_BYTES = 2ULL * 100 * 100 * (WIDE ? 8U : 4U);
            scene.retained_bytes = LIMIT - CROP_PAIR_BYTES;
            scene.events.insert(scene.events.begin() + 2,
                window({.left = 0, .top = 0, .right = 100, .bottom = 100}));
            CHECK(exact.stage());
            CHECK(scene.retained_bytes == LIMIT);

            Fixture over;
            over.port.capture_scene = scene;
            over.port.capture_scene.targets.clear();
            over.port.capture_scene.retained_bytes = (LIMIT - CROP_PAIR_BYTES) + 1;
            CHECK(!over.stage());
            CHECK(over.port.capture_scene.native_window_draws == 1);
            CHECK(over.port.source.identity == 0);
        }

        Fixture global;
        global.port.capture_scene.other_bytes = 2048ULL * 1024 * 1024;
        global.port.capture_scene.events.insert(global.port.capture_scene.events.begin() + 2,
            window({.left = 0, .top = 0, .right = 100, .bottom = 100}));
        CHECK(!global.stage());
        CHECK(global.port.capture_scene.crops.empty());

        Fixture oversize;
        auto huge = window({.left = 0, .top = 0, .right = 20000, .bottom = 20000});
        huge.output_width = huge.output_height = 20000;
        oversize.port.capture_scene.events.insert(
            oversize.port.capture_scene.events.begin() + 2, huge);
        CHECK(!oversize.stage());
        CHECK(oversize.port.capture_scene.retained_bytes == 0);
        CHECK(oversize.port.capture_scene.native_window_draws == 1);

        Fixture failed;
        failed.port.capture_scene.allocation_succeeds = false;
        failed.port.capture_scene.events.insert(failed.port.capture_scene.events.begin() + 2,
            window({.left = 0, .top = 0, .right = 100, .bottom = 100}));
        CHECK(!failed.stage());
        CHECK(failed.port.capture_scene.native_window_draws == 1);
    }

    inline void invalid_crop_geometry() {
        const double NAN_VALUE = std::numeric_limits<double>::quiet_NaN();
        const double INFINITY_VALUE = std::numeric_limits<double>::infinity();
        CHECK(capture_extent_bytes(100, 50, false) == 20000);
        CHECK(capture_extent_bytes(100, 50, true) == 40000);
        const double EXTENT_MAX = std::numeric_limits<int>::max();
        CHECK(capture_extent_bytes(EXTENT_MAX, EXTENT_MAX, false).has_value());
        CHECK(!capture_extent_bytes(EXTENT_MAX, EXTENT_MAX, true).has_value());
        for (const double invalid :
            {NAN_VALUE, INFINITY_VALUE, -INFINITY_VALUE, -1.0, 0.0, 1.5, EXTENT_MAX + 1}) {
            for (const bool wide : {false, true}) {
                CHECK(!capture_extent_bytes(invalid, 10, wide).has_value());
                CHECK(!capture_extent_bytes(10, invalid, wide).has_value());
            }
        }
        const auto VALID = window({.left = 0, .top = 0, .right = 10, .bottom = 10});
        for (const double BAD : {NAN_VALUE, INFINITY_VALUE, -INFINITY_VALUE}) {
            for (int field = 0; field < 6; ++field) {
                auto invalid = VALID;
                std::array<double *, 6> values{&invalid.bounds.left,
                    &invalid.bounds.top,
                    &invalid.bounds.right,
                    &invalid.bounds.bottom,
                    &invalid.output_width,
                    &invalid.output_height};
                *values[field] = BAD;
                CaptureSession session{{.layered = true, .individual = true}};
                session.boundary(CaptureBoundary::BeforeWindows);
                CHECK(!session.window(invalid).crop.has_value());
                session.boundary(CaptureBoundary::AfterWindows);
                CHECK(!session.complete({.native_captured = true}));
            }
        }

        for (int field = 0; field < 6; ++field) {
            auto invalid = VALID;
            if (field == 0) invalid.output_width = -1;
            if (field == 1) invalid.output_height = 0;
            if (field == 2) invalid.bounds.right = -1;
            if (field == 3) invalid.bounds.bottom = -1;
            if (field == 4) invalid.output_width = 1920.5;
            if (field == 5) invalid.output_height = 1080.5;
            CaptureSession session{{.layered = true, .individual = true}};
            session.boundary(CaptureBoundary::BeforeWindows);
            CHECK(!session.window(invalid).crop.has_value());
            session.boundary(CaptureBoundary::AfterWindows);
            CHECK(!session.complete({.native_captured = true}));
        }

        const double MAX_SIZE = std::numeric_limits<int>::max();
        CaptureSession maximum{{.layered = true, .individual = true}};
        maximum.boundary(CaptureBoundary::BeforeWindows);
        auto large = window({.left = 0, .top = 0, .right = MAX_SIZE, .bottom = 1});
        large.output_width = MAX_SIZE;
        CHECK(maximum.window(large).crop.has_value());
        large.bounds = {.left = MAX_SIZE - 1, .top = 0, .right = MAX_SIZE, .bottom = 1};
        CHECK(maximum.window(large).crop.has_value());
        maximum.boundary(CaptureBoundary::AfterWindows);
        CHECK(maximum.complete({.native_captured = true}));

        for (const bool vertical : {false, true}) {
            CaptureSession outside{{.layered = true, .individual = true}};
            outside.boundary(CaptureBoundary::BeforeWindows);
            auto distant = VALID;
            if (vertical) {
                distant.output_height = MAX_SIZE + 2;
                distant.bounds.top = MAX_SIZE + 1;
                distant.bounds.bottom = MAX_SIZE + 2;
            } else {
                distant.output_width = MAX_SIZE + 2;
                distant.bounds.left = MAX_SIZE + 1;
                distant.bounds.right = MAX_SIZE + 2;
            }
            CHECK(!outside.window(distant).crop.has_value());
            outside.boundary(CaptureBoundary::AfterWindows);
            CHECK(!outside.complete({.native_captured = true}));
        }

        CaptureSession overflow{{.layered = true, .individual = true}};
        overflow.boundary(CaptureBoundary::BeforeWindows);
        large.bounds = {.left = 0, .top = 0, .right = MAX_SIZE, .bottom = MAX_SIZE};
        large.output_width = large.output_height = MAX_SIZE;
        CHECK(!overflow.window(large).crop.has_value());
        overflow.boundary(CaptureBoundary::AfterWindows);
        CHECK(!overflow.complete({.native_captured = true}));

        Fixture empty;
        auto &events = empty.port.capture_scene.events;
        events.insert(
            events.begin() + 2, window({.left = 10.2, .top = 2, .right = 10.2, .bottom = 5}));
        events.insert(
            events.begin() + 3, window({.left = 2, .top = 10.2, .right = 5, .bottom = 10.2}));
        events.insert(events.begin() + 4, VALID);
        CHECK(empty.stage());
        CHECK(empty.port.capture_scene.crops.size() == 1);
        CHECK(empty.port.capture_scene.native_window_draws == 3);

        Fixture stale;
        auto &scene = stale.port.capture_scene;
        scene.events.insert(scene.events.begin() + 2, VALID);
        CHECK(stale.stage());
        stale.turn.release_gesture();
        auto invalid = VALID;
        invalid.bounds.left = NAN_VALUE;
        scene.events.insert(scene.events.begin() + 2, invalid);
        CHECK(!stale.stage());
        CHECK(scene.crops.empty());
        CHECK(scene.native_window_draws == 2);
    }

    inline void invalid_boundaries_keep_native_drawing() {
        Fixture early;
        early.port.capture_scene.events.insert(early.port.capture_scene.events.begin(),
            window({.left = 0, .top = 0, .right = 100, .bottom = 100}));
        CHECK(!early.stage());
        CHECK(early.port.capture_scene.native_window_draws == 1);
        CHECK(early.port.capture_scene.boundaries.size() == 3);

        Fixture repeated;
        repeated.port.capture_scene.events.insert(
            repeated.port.capture_scene.events.begin() + 2, CaptureBoundary::BeforeWindows);
        repeated.port.capture_scene.events.insert(repeated.port.capture_scene.events.begin() + 3,
            window({.left = 0, .top = 0, .right = 100, .bottom = 100}));
        CHECK(!repeated.stage());
        CHECK(repeated.port.capture_scene.native_window_draws == 1);
        CHECK(repeated.port.capture_scene.crops.empty());

        Fixture late;
        late.port.capture_scene.events.emplace_back(
            window({.left = 0, .top = 0, .right = 100, .bottom = 100}));
        CHECK(!late.stage());
        CHECK(late.port.capture_scene.native_window_draws == 1);

        Fixture failed;
        failed.port.capture_scene.events.insert(
            failed.port.capture_scene.events.begin() + 2, FakeCaptureFailure{});
        failed.port.capture_scene.events.insert(failed.port.capture_scene.events.begin() + 3,
            window({.left = 0, .top = 0, .right = 100, .bottom = 100}));
        CHECK(!failed.stage());
        CHECK(failed.port.capture_scene.native_window_draws == 1);
        CHECK(failed.port.capture_scene.boundaries.back() == CapturePlane::TopBackdrop);

        for (std::size_t missing = 0; missing < 3; ++missing) {
            Fixture incomplete;
            incomplete.port.capture_scene.events.erase(
                incomplete.port.capture_scene.events.begin() +
                static_cast<std::ptrdiff_t>(missing));
            CHECK(!incomplete.stage());
        }
    }

    inline void privacy_and_frozen_buffers() {
        constexpr std::array FLAGS{&CaptureSafetyFacts::locked,
            &CaptureSafetyFacts::exclusive_input,
            &CaptureSafetyFacts::input_method,
            &CaptureSafetyFacts::non_window_focus,
            &CaptureSafetyFacts::protected_content,
            &CaptureSafetyFacts::invalid_fadeout};
        for (const auto FLAG : FLAGS) {
            Fixture fixture;
            fixture.port.safety_facts.*FLAG = true;
            CHECK(!fixture.stage());
            CHECK(fixture.port.captures.empty());
        }
        for (const auto FLAG :
            {&CaptureSafetyFacts::session_available, &CaptureSafetyFacts::input_available}) {
            Fixture fixture;
            fixture.port.safety_facts.*FLAG = false;
            CHECK(!fixture.stage());
        }
        Fixture hud;
        hud.port.capture_scene.features.native_hud = true;
        hud.port.retiring_shell = true;
        CHECK(!hud.stage());
        hud.port.capture_scene.features.native_hud = false;
        CHECK(hud.stage());

        Fixture allocating;
        allocating.port.capture_buffer_facts.base[0] = false;
        CHECK(allocating.stage());
        CHECK(allocating.port.buffer_allocations == 1);
        const auto SECOND = std::make_shared<FakeWorkspace>(2);
        allocating.turn.begin(
            allocating.composition, allocating.port.composition(SECOND), 1.0, [](double time) {
                return time;
            });
        allocating.port.capture_buffer_facts.base[0] = false;
        CHECK(!allocating.stage());
        CHECK(allocating.port.buffer_action == CaptureBufferAction::Reject);
        CHECK(allocating.port.buffer_allocations == 1);

        Fixture planes;
        planes.port.capture_buffer_facts.planes[0] = false;
        CHECK(!planes.stage());
        CHECK(planes.port.buffer_allocations == 0);
        Fixture incompatible;
        incompatible.port.capture_buffer_facts.compatible = false;
        CHECK(!incompatible.stage());
    }

    inline void native_failures_and_inactive_capture() {
        Fixture fadeout;
        fadeout.port.capture_scene.selected_window_fadeout = true;
        CHECK(!fadeout.stage());
        CHECK(fadeout.port.capture_scene.native_window_draws == 0);
        fadeout.port.capture_scene.features.individual = false;
        CHECK(fadeout.stage());

        Fixture native;
        native.port.capture_scene.native_succeeds = false;
        CHECK(!native.stage());
        Fixture wallpaper;
        wallpaper.port.capture_scene.wallpaper_succeeds = false;
        CHECK(!wallpaper.stage());
        Fixture composite;
        composite.port.capture_scene.features = {};
        composite.port.capture_scene.events.clear();
        CHECK(composite.stage());

        Fixture inactive;
        CHECK(inactive.stage());
        const auto SECOND = std::make_shared<FakeWorkspace>(2);
        const auto DESTINATION = inactive.port.composition(SECOND);
        inactive.turn.begin(
            inactive.composition, DESTINATION, 1.0, [](double time) { return time; });
        inactive.turn.frame(DESTINATION, 0.0);
        CHECK(inactive.turn.drawable());
        inactive.turn.presented(true);
        inactive.port.capture_scene.events.emplace_back(
            window({.left = 0, .top = 0, .right = 100, .bottom = 100}));
        inactive.turn.frame(DESTINATION, .1);
        CHECK(!inactive.turn.active());
        CHECK(inactive.port.inactive_source == inactive.composition.identity);
        CHECK(inactive.port.capture_scene.native_window_draws == 1);
    }

    inline void run() {
        compact_planes_and_overlap();
        popup_selection_and_hud();
        bounded_crops_and_allocation_failures();
        invalid_crop_geometry();
        invalid_boundaries_keep_native_drawing();
        privacy_and_frozen_buffers();
        native_failures_and_inactive_capture();
    }

}  // namespace hyprimate::workspace::test::capture_plan_tests
