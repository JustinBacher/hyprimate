#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/WindowFadeout.hpp>

#include "HyprAnimationCanvas.hpp"
#include "HyprWindowAnimations.hpp"
#include "gl/CompositorContext.hpp"
#include "gl/NativeResource.hpp"
#include "window/WindowAnimationPort.hpp"

class CHyprWindowPort {
  public:
    using WindowRef = PHLWINDOWREF;
    using Canvas = CHyprAnimationCanvas;
    using Backdrop = Canvas::Backdrop;
    using RetainedImage = Canvas::Image;
    Canvas::Resources &canvas_resources() {
        return m_canvas_resources;
    }
    struct VisualCache {
        ~VisualCache() {
            hyprimate::gl::release_native(image, image_owner);
            hyprimate::gl::release_native(fadeout_image, fadeout_owner);
        }
        hyprimate::animation::MeshBuffers<Canvas::Vertex> buffers, scratch_buffers;
        Backdrop backdrop, scratch_backdrop;
        RetainedImage image, fadeout_image;
        // Drop borrowed native leases without extending host pool lifetime; exposure
        // still verifies the creating backend so replacement cannot use stale names.
        mutable hyprimate::gl::CompositorContextOwner image_owner, fadeout_owner;
        hyprimate::Vec2 image_origin{};
        hyprimate::Box capture{};
        hyprimate::animation::FrameId frame = 0;
    };
    void bind(HyprWindowAnimations &module);
    [[nodiscard]] hyprimate::animation::WindowId identity(const WindowRef &ref) const;
    [[nodiscard]] static bool window_alive(const WindowRef &window) {
        return !window.expired();
    }
    [[nodiscard]] hyprimate::animation::MonitorId monitor_identity(const PHLMONITOR &monitor) const;
    [[nodiscard]] hyprimate::animation::WorkspaceId workspace_identity(
        const PHLWORKSPACE &workspace) const;
    [[nodiscard]] std::uint64_t register_fadeout(const SP<Desktop::CWindowFadeout> &fadeout);
    [[nodiscard]] std::uint64_t fadeout_identity(const SP<Desktop::IFadeout> &fadeout) const;
    [[nodiscard]] std::optional<hyprimate::animation::Observation> observe(
        const WindowRef &ref) const;
    [[nodiscard]] static bool mapped_blur_required(const WindowRef &ref);
    [[nodiscard]] static bool session_unlocked();
    [[nodiscard]] bool tail_source_allowed(
        const WindowRef &window, hyprimate::animation::WindowId expected, bool capture) const;
    hyprimate::animation::Neighbourhood neighbourhood(const WindowRef &ref);
    static void warp_body(const WindowRef &ref, const hyprimate::Box &box);
    static void settle_open_alpha(const WindowRef &ref);
    void ensure_transformer(const WindowRef &ref, hyprimate::animation::Handle handle);
    static void detach_transformer(const WindowRef &ref);
    static void damage(const hyprimate::Box &box);
    static void damage_window(const WindowRef &ref);
    void schedule(hyprimate::animation::MonitorId monitor_id);
    void recheck_scanout(hyprimate::animation::MonitorId monitor_id);
    VisualCache &acquire_visuals(hyprimate::animation::Handle handle);
    VisualCache &visual_cache(hyprimate::animation::Handle handle);
    [[nodiscard]] const VisualCache *find_visuals(hyprimate::animation::Handle handle) const;
    void release_visuals(hyprimate::animation::Handle handle);
    [[nodiscard]] const Backdrop *backdrop(
        hyprimate::animation::Handle handle, hyprimate::animation::PassKind pass) const;
    void capture_backdrop(
        hyprimate::animation::Handle handle, const WindowRef &window, Canvas &canvas);
    void retain_mapped_image(hyprimate::animation::Handle handle,
        const Canvas::Image &image,
        hyprimate::Vec2 origin,
        hyprimate::Box capture,
        hyprimate::animation::FrameId frame);
    void release_mapped_image(hyprimate::animation::Handle handle);
    void queue_tail(hyprimate::animation::Handle handle,
        Canvas &canvas,
        const Canvas::Image &image,
        const hyprimate::animation::TailDraw &draw);
    [[nodiscard]] std::optional<hyprimate::animation::RetainedFrame<Canvas>> retained_mapped_image(
        hyprimate::animation::Handle handle) const;
    [[nodiscard]] bool bind_fadeout(
        hyprimate::animation::Handle handle, std::uint64_t fadeout, RetainedImage image);
    [[nodiscard]] RetainedImage fadeout_image(hyprimate::animation::Handle handle) const;
    [[nodiscard]] std::optional<hyprimate::animation::FadeoutFacts> fadeout_facts(
        std::uint64_t fadeout_id) const;
    void suppress_native(std::uint64_t fadeout_id, bool suppress);
    void forget_window_reference(hyprimate::animation::WindowId window);
    [[nodiscard]] std::optional<hyprimate::animation::ScreenFacts> monitor_facts(
        hyprimate::animation::MonitorId monitor_id) const;
    std::span<const hyprimate::animation::MonitorId> monitors_overlapping(
        const hyprimate::Box &box);
    [[nodiscard]] std::span<const hyprimate::Box> visible_outputs(
        hyprimate::Vec2 presentation_offset);

  private:
    Canvas::Resources m_canvas_resources;
    template <class Weak>
    struct Identity {
        std::uint64_t id;
        Weak object;
    };
    [[nodiscard]] PHLMONITOR monitor_for(hyprimate::animation::MonitorId monitor_id) const;
    struct Visual {
        hyprimate::animation::Handle handle;
        std::unique_ptr<VisualCache> cache;
    };
    mutable std::vector<Identity<PHLWINDOWREF>> m_windows;
    mutable std::vector<Identity<PHLMONITORREF>> m_monitors;
    mutable std::vector<Identity<PHLWORKSPACEREF>> m_workspaces;
    std::vector<Identity<WP<Desktop::IFadeout>>> m_fadeouts;
    mutable std::uint64_t m_next_window = 1, m_next_monitor = 1, m_next_workspace = 1;
    std::uint64_t m_next_fadeout = 1;
    std::vector<Visual> m_visuals;
    std::vector<hyprimate::Box> m_neighbours, m_obstacles;
    std::vector<hyprimate::Box> m_visible_outputs;
    std::vector<hyprimate::animation::MonitorId> m_overlapping;
    std::vector<std::uint64_t> m_suppressed;
    HyprWindowAnimations *m_module = nullptr;
};
