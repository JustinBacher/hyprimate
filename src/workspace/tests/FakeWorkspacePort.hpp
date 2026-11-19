#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

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

    Composition composition(const std::shared_ptr<FakeWorkspace> &normal,
                            const std::shared_ptr<FakeWorkspace> &special = {}) {
        return {.identity = identities.get(normal, special), .workspace = special ? special : normal,
                .numbered = normal ? std::optional<std::uint32_t>(normal->number) : std::nullopt,
                .special = static_cast<bool>(special)};
    }
    bool buffers(bool active) {
        buffer_checks.push_back(active);
        return buffers_ready;
    }
    double duration(const JourneyEndpoint &from, const JourneyEndpoint &to, double base) {
        timed_pair = {from.identity, to.identity};
        return base;
    }
    [[nodiscard]] bool should_refresh(CaptureKind kind, double progress) const {
        return (kind == CaptureKind::SourceIsolated ? progress : 1.0 - progress) <= live_boundary;
    }
    bool refresh_inactive_source(const Composition &value) {
        inactive_source = value.identity;
        return true;
    }
    bool capture(CaptureKind kind, const Composition &value) {
        captures.push_back({.kind = kind, .composition = value});
        if (!capture_succeeds) {
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
    std::vector<Capture> captures;
    std::vector<bool> buffer_checks;
    Slot source;
    Slot destination;
    Wallpaper next_wallpaper;
    std::pair<std::int64_t, std::int64_t> timed_pair{};
    double live_boundary = 1.0;
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
