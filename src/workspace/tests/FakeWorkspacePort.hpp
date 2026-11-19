#pragma once

#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#include "FakeWorkspaceCapture.hpp"
#include "workspace/WorkspaceCapturePolicy.hpp"
#include "workspace/WorkspaceIdentity.hpp"
#include "workspace/WorkspaceTurn.hpp"

namespace hyprimate::workspace::test {

struct FakeWorkspace {
    int number;
};

struct FakeWorkspacePort {
    using Workspace = std::weak_ptr<FakeWorkspace>;
    using Composition = TurnComposition<Workspace>;
    struct Preview {
        JourneyEndpoint endpoint;
        std::shared_ptr<int> image;
        std::shared_ptr<int> previous_image;
        float live_mix = 1.F;
    };
    struct Capture {
        CaptureKind kind;
        Composition composition;
    };
    struct Wallpaper {
        int texture = 0;
        int generation = 0;
        bool operator==(const Wallpaper &) const = default;
    };
    struct Slot {
        std::int64_t identity = 0;
        Wallpaper wallpaper;
    };
    struct PresentationPort {
        std::vector<FloatingFacts> old;
        std::vector<FloatingFacts> live;
        std::vector<FloatingPresentation> emitted;
        bool allocation_succeeds = true;
        [[nodiscard]] std::size_t old_count() const { return old.size(); }
        [[nodiscard]] std::size_t live_count() const { return live.size(); }
        [[nodiscard]] FloatingFacts old_window(std::size_t index) const { return old[index]; }
        [[nodiscard]] FloatingFacts live_window(std::size_t index) const { return live[index]; }
        void begin(std::size_t count) {
            if (!allocation_succeeds) {
                throw std::bad_alloc();
            }
            emitted.clear();
            emitted.reserve(count);
        }
        void emit(const FloatingPresentation &command) { emitted.push_back(command); }
    };

    Composition composition(const std::shared_ptr<FakeWorkspace> &normal,
                            const std::shared_ptr<FakeWorkspace> &special = {}) {
        return {.identity = identities.get(normal, special), .workspace = special ? special : normal,
                .numbered = normal ? std::optional<std::uint32_t>(normal->number) : std::nullopt,
                .special = static_cast<bool>(special)};
    }
    bool buffers(bool active) {
        buffer_checks.push_back(active);
        auto facts = capture_buffer_facts;
        facts.native_ready = facts.native_ready && buffers_ready;
        facts.active = active;
        facts.scene_safe = scene_safe();
        facts.hud_safe = hud_safe();
        const auto FEATURES = capture_scene.enabled ? capture_scene.features :
            CaptureFeatures{.layered = layered(), .native_hud = native_hud()};
        buffer_action = capture_buffer_plan(facts, FEATURES);
        if (buffer_action == CaptureBufferAction::Allocate && buffer_allocation_succeeds) {
            ++buffer_allocations;
            capture_buffer_facts.base.fill(true);
            capture_buffer_facts.planes.fill(true);
            facts.base = capture_buffer_facts.base;
            facts.planes = capture_buffer_facts.planes;
        }
        return capture_buffer_plan(facts, FEATURES) == CaptureBufferAction::Reuse;
    }
    double duration(const JourneyEndpoint &from, const JourneyEndpoint &destination, double base) {
        timed_pair = {from.identity, destination.identity};
        return base;
    }
    [[nodiscard]] double live_boundary() const { return boundary; }
    [[nodiscard]] bool layered() const { return layered_capture; }
    [[nodiscard]] bool native_hud() const { return hud_enabled; }
    [[nodiscard]] double now() const { return time; }
    [[nodiscard]] bool scene_safe() const { return capture_scene_safe(safety_facts); }
    [[nodiscard]] bool hud_safe() const { return capture_hud_safe(retiring_shell); }
    [[nodiscard]] InactiveSourceFacts inactive_source_facts(const Composition & /*unused*/) const { return inactive_facts; }
    bool clone_capture(CaptureKind kind) {
        clones.push_back(kind);
        return clone_succeeds;
    }
    void release_blend(CaptureKind kind) { releases.push_back(kind); }
    bool capture_inactive_source(const Composition &value) {
        inactive_source = value.identity;
        return inactive_capture_succeeds && (!capture_scene.enabled || capture_scene.run());
    }
    bool capture(CaptureKind kind, const Composition &value) {
        captures.push_back({.kind = kind, .composition = value});
        if (!capture_succeeds || (capture_scene.enabled && !capture_scene.run())) {
            return false;
        }
        auto &slot = kind == CaptureKind::SourceIsolated ? source : destination;
        slot.identity = value.identity;
        slot.wallpaper = next_wallpaper;
        return true;
    }
    void swap_slots() {
        std::swap(source, destination);
        ++swaps;
    }
    void damage() { ++damages; }
    void present_native() { ++native_presentations; }
    void advance(double seconds) { time += seconds; }

    CompositionIdentities<std::shared_ptr<FakeWorkspace>, Workspace> identities;
    FakeWorkspaceCapture capture_scene;
    CaptureSafetyFacts safety_facts;
    bool retiring_shell = false;
    CaptureBufferFacts capture_buffer_facts{.native_ready = true, .scene_safe = true,
        .base = {true, true, true, true, true, true}, .planes = {true, true, true, true, true, true}};
    CaptureBufferAction buffer_action = CaptureBufferAction::Reuse;
    bool buffer_allocation_succeeds = true;
    int buffer_allocations = 0;
    std::vector<Capture> captures;
    std::vector<bool> buffer_checks;
    std::vector<CaptureKind> clones;
    std::vector<CaptureKind> releases;
    InactiveSourceFacts inactive_facts;
    bool layered_capture = true;
    bool hud_enabled = false;
    bool clone_succeeds = true;
    bool inactive_capture_succeeds = true;
    Slot source;
    Slot destination;
    Wallpaper next_wallpaper;
    std::pair<std::int64_t, std::int64_t> timed_pair;
    double boundary = 1.0;
    std::int64_t inactive_source = 0;
    bool buffers_ready = true;
    bool capture_succeeds = true;
    int swaps = 0;
    int damages = 0;
    int native_presentations = 0;
    double time = 0.0;
};

struct RecordingWorkspaceStyle {
    struct Draw {
        double progress;
        Direction direction;
        bool matching_wallpaper;
        double live_value;
    };
    bool draw(double progress, Direction direction, bool matching_wallpaper) {
        draws.push_back({.progress = progress, .direction = direction,
                         .matching_wallpaper = matching_wallpaper, .live_value = live_value});
        return succeeds;
    }
    std::vector<Draw> draws;
    double live_value = 0.0;
    bool succeeds = true;
};

} // namespace hyprimate::workspace::test
