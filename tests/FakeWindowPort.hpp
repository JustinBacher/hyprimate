#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include "WindowAnimationPort.hpp"

namespace hyprimate::animation::test {
struct FakeCanvas {
    using Vertex = MeshVertex;
    struct Image { int id = 0; Vec2 size{.x = 110, .y = 90}; };
    struct Backdrop { int id = 0; };
    PassKind pass_kind = PassKind::Live;
    [[nodiscard]] PassKind pass() const { return pass_kind; }
    std::vector<int> draws;
    std::vector<Box> composites;
    static Vec2 image_size(const Image &image) { return image.size; }
    void composite(const Box &box, float /*alpha*/) { composites.push_back(box); }
    bool submit_mesh(const Mesh<Vertex> & /*mesh*/, const Draw<FakeCanvas> &draw) {
        draws.push_back(draw.image->id);
        return true;
    }
};

struct FakeWindowPort {
    using WindowRef = WindowId;
    using Canvas = FakeCanvas;
    using Backdrop = Canvas::Backdrop;
    using RetainedImage = Canvas::Image;
    struct VisualCache {
        MeshBuffers<Canvas::Vertex> scratch_buffers;
        MeshBuffers<Canvas::Vertex> buffers;
        std::optional<Canvas::Image> image;
        int live_backdrops = 0, scratch_backdrops = 0;
        Canvas::Backdrop live_backdrop{1}, scratch_backdrop{2};
        Vec2 origin{};
        Box capture{};
        FrameId frame = 0;
    };
    struct Visual { Handle handle; VisualCache cache; };
    struct QueuedTail { Handle handle; Canvas::Image image; TailDraw draw; };
    std::vector<QueuedTail> queued_tails;
    std::map<WindowId, Observation> windows;
    std::set<WindowId> unobservable;
    std::map<MonitorId, ScreenFacts> monitors;
    std::map<std::uint64_t, FadeoutFacts> fadeouts;
    std::map<WindowId, Handle> transformers;
    std::vector<Visual> visuals;
    std::vector<Box> damage_log;
    std::vector<MonitorId> scheduled, scanout;
    std::vector<WindowId> detached, forgotten, alpha_settled, backdrop_captures;
    std::set<std::uint64_t> suppressed;
    std::vector<MonitorId> overlapping;
    Clock::time_point now;
    bool fail_acquire = false;
    bool fail_bind = false;
    std::vector<int> *lifecycle_events = nullptr;

    Clock::time_point advance(double seconds) {
        now += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
        return now;
    }
    [[nodiscard]] static WindowId identity(WindowRef window) { return window; }
    [[nodiscard]] bool window_alive(WindowRef window) const { return windows.contains(window); }
    [[nodiscard]] std::optional<Observation> observe(WindowRef window) const {
        if (unobservable.contains(window)) { return std::nullopt; }
        const auto ITER = windows.find(window);
        return ITER == windows.end() ? std::nullopt : std::optional<Observation>{ITER->second};
    }
    static Neighbourhood neighbourhood(WindowRef /*window*/) { return {}; }
    void warp_body(WindowRef window, const Box &box) {
        auto &obs = windows.at(window);
        const auto OLD = obs.body;
        obs.capture = {.x = box.x + obs.capture.x - OLD.x, .y = box.y + obs.capture.y - OLD.y,
                       .w = box.w + obs.capture.w - OLD.w, .h = box.h + obs.capture.h - OLD.h};
        obs.body = box;
        obs.goal_body = box;
    }
    void settle_open_alpha(WindowRef window) { alpha_settled.push_back(window); }
    void ensure_transformer(WindowRef window, Handle handle) { transformers.insert_or_assign(window, handle); }
    void detach_transformer(WindowRef window) {
        transformers.erase(window); detached.push_back(window);
        if (lifecycle_events != nullptr) { lifecycle_events->push_back(6); }
    }
    void damage(const Box &box) { damage_log.push_back(box); }
    void damage_window(WindowRef window) { if (auto obs = observe(window)) { damage(obs->capture); } }
    void schedule(MonitorId monitor) { scheduled.push_back(monitor); }
    void recheck_scanout(MonitorId monitor) { scanout.push_back(monitor); }
    VisualCache &acquire_visuals(Handle handle) {
        if (fail_acquire) { throw std::runtime_error("fake visual allocation failure"); }
        visuals.push_back({.handle = handle, .cache = {}});
        return visuals.back().cache;
    }
    VisualCache &visual_cache(Handle handle) {
        return std::ranges::find_if(visuals, [handle](const auto &visual) { return visual.handle == handle; })->cache;
    }
    [[nodiscard]] const VisualCache *find_visuals(Handle handle) const {
        const auto ITER = std::ranges::find_if(visuals, [handle](const auto &visual) { return visual.handle == handle; });
        return ITER == visuals.end() ? nullptr : &ITER->cache;
    }
    void release_visuals(Handle handle) {
        std::erase_if(visuals, [handle](const auto &visual) { return visual.handle == handle; });
        if (lifecycle_events != nullptr) { lifecycle_events->push_back(8); }
    }
    void retain_mapped_image(Handle handle, const Canvas::Image &image, Vec2 origin, const Box &capture, FrameId frame) {
        auto &cache = visual_cache(handle);
        if (!cache.image) { cache.image = image; cache.origin = origin; cache.capture = capture; cache.frame = frame; }
    }
    void release_mapped_image(Handle handle) { visual_cache(handle).image.reset(); }
    void queue_tail(Handle handle, Canvas & /*canvas*/, const Canvas::Image &image, const TailDraw &draw) {
        queued_tails.push_back({.handle = handle, .image = image, .draw = draw});
    }
    void bind_fadeout(Handle handle, std::uint64_t /*fadeout*/, RetainedImage image) {
        if (fail_bind) { throw std::runtime_error("fake capture failure"); }
        visual_cache(handle).image = image;
    }
    [[nodiscard]] std::optional<FadeoutFacts> fadeout_facts(std::uint64_t fadeout) const {
        const auto ITER = fadeouts.find(fadeout);
        return ITER == fadeouts.end() ? std::nullopt : std::optional<FadeoutFacts>{ITER->second};
    }
    void suppress_native(std::uint64_t fadeout, bool suppress) {
        if (suppress) { suppressed.insert(fadeout); } else { suppressed.erase(fadeout); }
    }
    void forget_window_reference(WindowId window) { forgotten.push_back(window); }
    [[nodiscard]] std::optional<ScreenFacts> monitor_facts(MonitorId monitor) const {
        const auto ITER = monitors.find(monitor);
        return ITER == monitors.end() ? std::nullopt : std::optional<ScreenFacts>{ITER->second};
    }
    std::span<const MonitorId> monitors_overlapping(const Box & /*box*/) { return overlapping; }
    void capture_backdrop(Handle handle, WindowRef window, Canvas &canvas) {
        backdrop_captures.push_back(window);
        auto &cache = visual_cache(handle);
        if (canvas.pass() == PassKind::Live) { ++cache.live_backdrops; }
        else { ++cache.scratch_backdrops; }
    }
    [[nodiscard]] const Canvas::Backdrop *backdrop(Handle handle, PassKind pass) const {
        const auto *cache = find_visuals(handle);
        if (cache == nullptr || pass == PassKind::Inert) { return nullptr; }
        if (pass == PassKind::WorkspaceSource) {
            return cache->scratch_backdrops != 0 ? &cache->scratch_backdrop : nullptr;
        }
        return cache->live_backdrops != 0 ? &cache->live_backdrop : nullptr;
    }
    [[nodiscard]] std::optional<RetainedFrame<Canvas>> retained_mapped_image(Handle handle) const {
        const auto *cache = find_visuals(handle);
        if (cache == nullptr || !cache->image) { return std::nullopt; }
        return RetainedFrame<Canvas>{.image = &*cache->image, .image_origin = cache->origin,
                                     .image_size = cache->image->size, .capture = cache->capture, .frame = cache->frame};
    }
};
} // namespace hyprimate::animation::test
