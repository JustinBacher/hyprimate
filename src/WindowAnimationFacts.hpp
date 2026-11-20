#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "animations/shared/Core.hpp"
namespace hyprimate::config { enum class Style : std::uint8_t; }

namespace hyprimate::animation {
using Clock = std::chrono::steady_clock;
using WindowId = std::uint64_t;
using MonitorId = std::uint64_t;
using WorkspaceId = std::uint64_t;
using FrameId = std::uint64_t;

struct Policy {
    config::Style style{};
    bool enabled = false, inert = false;
    bool opening = false, movement = false, closing = false, workspace_movement = false;
    bool close_available = false;
};
struct Neighbourhood {
    std::span<const Box> tiled_goals, obstacles;
    bool alone = false;
    std::optional<Vec2> cursor{};
};
struct Place {
    Box body, capture, goal_body, rest_capture, screen, work_area;
    Neighbourhood near;
    std::uint64_t seed = 0;
    std::optional<Edge> opening_edge;
    MonitorId monitor = 0;
    WorkspaceId workspace = 0;
    bool pinned = false;
};
struct Motion {
    Box from_body, from_capture, body, capture;
    bool translated = false, resized = false, capture_changed = false;
    bool layout_animated = false, workspace_moving = false;
    bool workspace_shifted = false; // presentation moved relative to the wallpaper
    double sample_seconds = 0; // Observation interval, including the frame that acquired the style.
};
struct Step {
    double elapsed = 0;
    bool stalled = false;
    Box body, capture, goal_body, rest_capture, screen, work_area;
    std::optional<Motion> motion;
    bool movement_enabled = false, open_enabled = false;
    bool held = false, dying = false;
    bool workspace_visible = true, workspace_moving = false;
    MonitorId monitor = 0;
    WorkspaceId workspace = 0;
    bool pinned = false;
};
struct Effects {
    std::optional<Box> drive_body;
    std::optional<Edge> opening_edge;
    bool snap_to_goal = false, settle_native_open_alpha = false, release_body = false;
};
struct Frame {
    bool running = false;
    Box bounds{};
    bool backdrop = false, tail = false;
    bool interrupted = false; // terminal failure: an owned native fade must be released, not tombstoned
};
enum class CloseStart : std::uint8_t { Owned, Native, Declined };
enum class PassKind : std::uint8_t { Live, WorkspaceSource, Inert };
enum class End : std::uint8_t { Completed, Interrupted };

struct MeshVertex { float x = 0, y = 0, u = 0, v = 0; };
struct Rgba { float r = 0, g = 0, b = 0, a = 0; };
template<class V> concept Vertex = requires(V &vertex) {
    requires std::same_as<decltype(vertex.x), float>;
    requires std::same_as<decltype(vertex.y), float>;
    requires std::same_as<decltype(vertex.u), float>;
    requires std::same_as<decltype(vertex.v), float>;
};
template<Vertex V> struct MeshBuffers { std::vector<V> window{}, under{}, shade{}; };
template<Vertex V> struct MeshTarget {
    Box source, output;
    Vec2 image_origin, image_size;
    double scale = 1;
    // Non-null, borrowed for the synchronous mesh call; storage stays owned by the port.
    MeshBuffers<V> *buffers;
};
template<Vertex V> struct Mesh {
    const std::vector<V> *window = nullptr, *under = nullptr;
    Box under_output{};
    Rgba under_color{};
    const std::vector<V> *shade = nullptr;
    float opacity = 1;
    std::optional<Box> clip;
};
template<class Canvas> struct Draw {
    // Non-null borrows; the canvas and image handle outlive this synchronous draw.
    Canvas *canvas;
    const Canvas::Image *image;
    const Canvas::Backdrop *backdrop = nullptr;
    Box source, output, canvas_box;
    Vec2 image_origin, image_size;
    double scale = 1;
    float alpha = 1;
    bool real_pass = false;
    MonitorId monitor = 0;
    WindowId window = 0;
    FrameId frame = 0;
};
struct ScreenFacts {
    MonitorId monitor = 0;
    FrameId frame = 0;
    Clock::time_point now;
    Box screen, work_area;
    double scale = 1;
    bool enabled = false;
    WorkspaceId workspace = 0, special_workspace = 0;
};
template<class Canvas> struct ScreenDraw {
    // Non-null borrow; the canvas outlives this synchronous screen draw.
    Canvas *canvas;
    ScreenFacts screen;
    WindowId window = 0;
    bool real_pass = false;
};
} // namespace hyprimate::animation
