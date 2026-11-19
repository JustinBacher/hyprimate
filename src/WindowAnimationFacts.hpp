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
};
struct Place {
    Box body, capture, goal_body, rest_capture, screen, work_area;
    Neighbourhood near;
    std::uint64_t seed = 0;
};
struct Motion {
    Box from_body, from_capture, body, capture;
    bool translated = false, resized = false, capture_changed = false;
    bool layout_animated = false, workspace_moving = false;
};
struct Step {
    double elapsed = 0;
    bool stalled = false;
    Box body, capture, goal_body, rest_capture, screen, work_area;
    std::optional<Motion> motion;
    bool movement_enabled = false, open_enabled = false;
    bool held = false, dying = false;
    bool workspace_visible = true, workspace_moving = false;
};
struct Effects {
    std::optional<Box> drive_body;
    bool snap_to_goal = false, settle_native_open_alpha = false, release_body = false;
};
struct Frame {
    bool running = false;
    Box bounds{};
    bool backdrop = false, tail = false;
};
enum class CloseStart : std::uint8_t { Owned, Native, Declined };
enum class PassKind : std::uint8_t { Live, WorkspaceSource, Inert };
enum class End : std::uint8_t { Completed, Interrupted };

struct MeshVertex { float x = 0, y = 0, u = 0, v = 0; };
struct Rgba { float r = 0, g = 0, b = 0, a = 0; };
template<class V> concept Vertex = requires(V &v) {
    requires std::same_as<decltype(v.x), float>;
    requires std::same_as<decltype(v.y), float>;
    requires std::same_as<decltype(v.u), float>;
    requires std::same_as<decltype(v.v), float>;
};
template<Vertex V> struct MeshBuffers { std::vector<V> window, under, shade; };
template<Vertex V> struct MeshTarget {
    Box source, output;
    Vec2 image_origin, image_size;
    double scale = 1;
    MeshBuffers<V> &buffers;
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
    Canvas &canvas;
    const typename Canvas::Image &image;
    const typename Canvas::Backdrop *backdrop = nullptr;
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
    Clock::time_point now{};
    Box screen, work_area;
    double scale = 1;
    bool enabled = false;
};
template<class Canvas> struct ScreenDraw {
    Canvas &canvas;
    ScreenFacts screen;
    WindowId window = 0;
    bool real_pass = false;
};
} // namespace hyprimate::animation
