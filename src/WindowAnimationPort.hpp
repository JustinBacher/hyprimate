#pragma once

#include <concepts>
#include <optional>
#include <span>
#include <utility>

#include "WindowAnimationEntry.hpp"

namespace hyprimate::animation {
struct Observation {
    WindowId window = 0;
    MonitorId monitor = 0;
    WorkspaceId workspace = 0;
    Box body, capture, goal_body, screen, work_area;
    Vec2 workspace_offset{}, floating_offset{};
    double scale = 1;
    bool mapped = false, fullscreen = false, hidden = false, floating = false;
    bool animating_in = false, layout_animated = false, held = false;
    bool workspace_visible = true, workspace_moving = false, pinned = false;
    bool no_screen_share = false;
};
template<class Canvas> struct RetainedFrame {
    const Canvas::Image *image = nullptr;
    Vec2 image_origin{};
    Vec2 image_size{};
    Box capture{};
    FrameId frame = 0;
};
struct TailDraw {
    Box source, output, canvas_box;
    Vec2 image_origin, image_size;
    double scale = 1;
    float alpha = 1;
    MonitorId monitor = 0;
    FrameId frame = 0;
};
struct FrameStamp { MonitorId monitor = 0; FrameId frame = 0; Clock::time_point now; };
struct FadeoutFacts {
    Clock::time_point now;
    std::uint64_t fadeout = 0;
    MonitorId monitor = 0;
    WorkspaceId workspace = 0;
    bool workspace_bound = false;
    Box native_box;
    Vec2 image_size;
    double scale = 1;
    float alpha = 1;
    bool alive = false, blurred = false, image_available = false;
};
struct DyingView {
    Handle handle;
    MonitorId monitor = 0;
    WorkspaceId workspace = 0;
    std::uint64_t fadeout = 0;
    CloseStart ownership = CloseStart::Declined;
    Box source_device, render_box, bounds_device;
    Vec2 image_size;
    double scale = 1;
    float alpha = 1;
    bool workspace_bound = false, no_screen_share = false;
};
template<class P> concept WindowPort = requires(P &port, const P &constant,
    P::WindowRef window, WindowId window_id, MonitorId monitor, Handle handle,
    const Box &box, const P::Canvas::Image &image,
    P::RetainedImage retained, FrameId frame, std::uint64_t fadeout,
    P::Canvas &canvas, const Mesh<typename P::Canvas::Vertex> &mesh,
    const Draw<typename P::Canvas> &draw, const TailDraw &tail) {
    typename P::Canvas;
    typename P::RetainedImage;
    typename P::Backdrop;
    typename P::VisualCache;
    { std::as_const(canvas).pass() } -> std::same_as<PassKind>;
    { canvas.composite(box, 1.0F) } -> std::same_as<void>;
    { std::as_const(canvas).image_size(image) } -> std::same_as<Vec2>;
    { canvas.submit_mesh(mesh, draw) } -> std::same_as<bool>;
    requires std::same_as<decltype(port.visual_cache(handle).buffers), MeshBuffers<typename P::Canvas::Vertex>>;
    requires std::same_as<decltype(port.visual_cache(handle).scratch_buffers), MeshBuffers<typename P::Canvas::Vertex>>;
    { constant.identity(window) } -> std::same_as<WindowId>;
    { constant.window_alive(window) } -> std::same_as<bool>;
    { constant.observe(window) } -> std::same_as<std::optional<Observation>>;
    { port.neighbourhood(window) } -> std::same_as<Neighbourhood>;
    { port.warp_body(window, box) } -> std::same_as<void>;
    { port.settle_open_alpha(window) } -> std::same_as<void>;
    { port.ensure_transformer(window, handle) } -> std::same_as<void>;
    { port.detach_transformer(window) } -> std::same_as<void>;
    { port.damage(box) } -> std::same_as<void>;
    { port.damage_window(window) } -> std::same_as<void>;
    { port.schedule(monitor) } -> std::same_as<void>;
    { port.recheck_scanout(monitor) } -> std::same_as<void>;
    { port.release_visuals(handle) } -> std::same_as<void>;
    { port.acquire_visuals(handle) } -> std::same_as<typename P::VisualCache &>;
    { port.visual_cache(handle) } -> std::same_as<typename P::VisualCache &>;
    { port.capture_backdrop(handle, window, canvas) } -> std::same_as<void>;
    { constant.backdrop(handle, PassKind::Live) } -> std::same_as<const typename P::Canvas::Backdrop *>;
    { constant.retained_mapped_image(handle) } -> std::same_as<std::optional<RetainedFrame<typename P::Canvas>>>;
    { constant.find_visuals(handle) } -> std::same_as<const typename P::VisualCache *>;
    { port.retain_mapped_image(handle, image, Vec2{}, box, frame) } -> std::same_as<void>;
    { port.release_mapped_image(handle) } -> std::same_as<void>;
    { port.queue_tail(handle, canvas, image, tail) } -> std::same_as<void>;
    { port.bind_fadeout(handle, fadeout, std::move(retained)) } -> std::same_as<void>;
    { constant.fadeout_facts(fadeout) } -> std::same_as<std::optional<FadeoutFacts>>;
    { port.suppress_native(fadeout, true) } -> std::same_as<void>;
    { port.forget_window_reference(window_id) } -> std::same_as<void>;
    { constant.monitor_facts(monitor) } -> std::same_as<std::optional<ScreenFacts>>;
    { port.monitors_overlapping(box) } -> std::same_as<std::span<const MonitorId>>;
};
} // namespace hyprimate::animation
