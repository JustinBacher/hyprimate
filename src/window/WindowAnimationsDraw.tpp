#pragma once

#include <cmath>
#include <optional>
#include <utility>

namespace hyprimate::animation {
    template <class Port, class... Styles>
    Box CWindowAnimations<Port, StyleList<Styles...>>::extents(
        Handle handle, const Box &capture) const {
        const auto STATE = view(handle);
        if (!STATE || !STATE->running) {
            return {};
        }
        return detail::translated(STATE->bounds, {capture.x, capture.y});
    }
    template <class Port, class... Styles>
    Box CWindowAnimations<Port, StyleList<Styles...>>::planned_extents(
        Handle handle, const Box &capture) const {
        auto bounds = extents(handle, capture);
        if (!m_policy.during_workspace_switch) {
            // Planning has no render context. Include native input even when a
            // later workspace pass bypasses the animation transformer.
            const double left = std::min(bounds.x, capture.x);
            const double top = std::min(bounds.y, capture.y);
            const double right = std::max(bounds.x + bounds.w, capture.x + capture.w);
            const double bottom = std::max(bounds.y + bounds.h, capture.y + capture.h);
            bounds = {.x = left, .y = top, .w = right - left, .h = bottom - top};
        }
        return bounds;
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::tail_allowed(
        Handle handle, const Canvas &canvas, bool retained_no_screen_share) const {
        const auto *entry = record(handle);
        if (!allows_draw(canvas) || !m_policy.enabled || m_policy.inert || entry == nullptr ||
            entry->life != Life::Mapped || !entry->current.running || !entry->current.tail) {
            return false;
        }
        const auto *window = tracked(entry->origin);
        if (window == nullptr || window->handle != handle ||
            !m_port.tail_source_allowed(window->window, entry->origin, canvas.captures_pixels())) {
            return false;
        }
        return !canvas.captures_pixels() ||
               (!entry->retained_no_screen_share && !retained_no_screen_share);
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::draw_tail(
        Handle handle, const Draw<Canvas> &draw_info, bool retained_no_screen_share) {
        if (draw_info.canvas == nullptr ||
            !tail_allowed(handle, *draw_info.canvas, retained_no_screen_share)) {
            return false;
        }
        const auto retained = m_port.retained_mapped_image(handle);
        if (!retained || retained->image == nullptr || draw_info.image == nullptr ||
            !Canvas::same_image(*retained->image, *draw_info.image)) {
            return false;
        }
        return draw(handle, draw_info);
    }
    template <class Port, class... Styles>
    template <class Display>
    bool CWindowAnimations<Port, StyleList<Styles...>>::display_tail(Handle handle,
        const Draw<Canvas> &draw_info,
        bool retained_no_screen_share,
        Display &&display) {
        if (!draw_tail(handle, draw_info, retained_no_screen_share)) {
            return false;
        }
        const auto retained = m_port.retained_mapped_image(handle);
        if (!retained || retained->image == nullptr ||
            !Canvas::same_image(*retained->image, *draw_info.image) ||
            !tail_allowed(handle, *draw_info.canvas, retained_no_screen_share)) {
            return false;
        }
        std::forward<Display>(display)();
        return true;
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::draw(
        Handle handle, const Draw<Canvas> &draw_info) {
        auto *entry = record(handle);
        if (draw_info.canvas == nullptr || !allows_draw(*draw_info.canvas) || entry == nullptr ||
            !entry->current.running ||
            (entry->life == Life::Mapped && entry->current.native_presentation)) {
            return false;
        }
        if (entry->public_close_only && entry->life == Life::PendingCapture) {
            return false;
        }
        const bool PUBLIC_OWNED = entry->public_close_only && entry->life == Life::DyingOwned;
        const auto qualifies_real_pass = [&] {
            return draw_info.real_pass && draw_info.canvas->pass() == PassKind::Live &&
                   (!PUBLIC_OWNED || !draw_info.canvas->captures_pixels());
        };
        const bool REAL_PASS = qualifies_real_pass();
        if (PUBLIC_OWNED && public_close_revoked(*entry)) {
            if (REAL_PASS) {
                interrupt_public_close(handle);
            }
            return false;
        }
        const bool PUBLIC_MAPPED = entry->life == Life::Mapped && entry->public_mapped_only;
        const auto mapped_source_allowed = [&] {
            const auto *window = tracked(entry->origin);
            const auto observation = window != nullptr ? m_port.observe(window->window) : std::nullopt;
            return observation && public_mapped_source_allowed(*window, *observation) &&
                (entry->allows_mapped_blur || !m_port.mapped_blur_required(window->window));
        };
        if (PUBLIC_MAPPED && !mapped_source_allowed()) {
            return false;
        }
        const auto CALLBACK = callback_ticket();
        auto isolated_draw = draw_info;
        if (isolated_draw.monitor == 0) {
            isolated_draw.monitor = entry->monitor;
        }
        isolated_draw.window = entry->origin;
        if (isolated_draw.frame == 0) {
            isolated_draw.frame = entry->last_frame;
        }
        const bool MAPPED_OWNER = entry->life == Life::Mapped && entry->tracks_mapped_publication &&
                                  REAL_PASS && !draw_info.canvas->captures_pixels() &&
                                  isolated_draw.monitor == entry->monitor;
        isolated_draw.backdrop =
            entry->current.backdrop ? m_port.backdrop(handle, draw_info.canvas->pass()) : nullptr;
        if (entry->life == Life::Mapped && entry->current.tail && REAL_PASS &&
            !draw_info.canvas->captures_pixels()) {
            if (const auto *window = tracked(entry->origin);
                window != nullptr && m_port.session_unlocked()) {
                const auto observation = m_port.observe(window->window);
                if (observation && observation->window == entry->origin &&
                    observation->workspace_visible && observation->mapped &&
                    m_port.identity(window->window) == entry->origin) {
                    const Vec2 OFFSET{
                        observation->workspace_offset.x + observation->floating_offset.x,
                        observation->workspace_offset.y + observation->floating_offset.y};
                    if (!m_port.retained_mapped_image(handle)) {
                        entry->retained_no_screen_share = false;
                    }
                    // Retained framebuffers are shared, not immutable pixel copies.
                    // A later private draw can update storage retained by an earlier frame.
                    entry->retained_no_screen_share |= observation->no_screen_share ||
                        draw_info.includes_desktop_pixels;
                    m_port.retain_mapped_image(handle,
                        *draw_info.image,
                        draw_info.image_origin,
                        detail::translated(observation->capture, OFFSET),
                        isolated_draw.frame);
                }
            }
        }
        if (!is_usable(entry->current.bounds)) {
            return true;
        }
        isolated_draw.real_pass = REAL_PASS && (!PUBLIC_MAPPED ||
            (!draw_info.canvas->captures_pixels() && isolated_draw.monitor == entry->monitor));
        bool drawn = false;
        InFlightDraw flight{*this, handle};
        visit(handle, [&]<class Style>(auto &slot, auto &resources) {
            const auto PROFILE_KEY = profile_key(Style::KIND,
                entry->life == Life::Mapped ? "shared" : "close");
            const perf::CpuScope PROFILE{PROFILE_KEY, perf::Phase::Draw};
            if constexpr (requires { Style::warm(resources); }) {
                Style::warm(resources);
            }
            if (!CALLBACK.valid() || record(handle) == nullptr) {
                return;
            }
            if constexpr (requires { Style::invalidate_mapped_publication(slot.state, resources); }) {
                if (MAPPED_OWNER) {
                    Style::invalidate_mapped_publication(slot.state, resources);
                }
            }
            if constexpr (SelfDrawn<Style, Canvas>) {
                drawn = Style::draw(slot.state, isolated_draw, resources);
            } else {
                auto &cache = m_port.visual_cache(handle);
                auto &buffers = draw_info.canvas->pass() == PassKind::Live ? cache.buffers
                                                                           : cache.scratch_buffers;
                if (perf::enabled()) {
                    // These vectors belong to this handle's visual cache, not the native image.
                    perf::resource_sample(PROFILE_KEY, sizeof(slot.state) +
                        (buffers.window.capacity() + buffers.under.capacity() +
                            buffers.shade.capacity()) * sizeof(typename Canvas::Vertex), 0);
                }
                bool indexed = false;
                if constexpr (requires { Canvas::indexed_meshes(); }) {
                    indexed = Canvas::indexed_meshes();
                }
                const MeshTarget<typename Canvas::Vertex> TARGET{draw_info.source,
                    draw_info.output,
                    draw_info.image_origin,
                    draw_info.image_size,
                    draw_info.scale,
                    &buffers,
                    indexed};
                const auto MESH = [&] {
                    HYPRIMATE_CPU_SCOPE((perf::Key{
                        perf::Domain::Window, "sheet", "mesh-fill"}));
                    return Style::mesh(slot.state, TARGET, resources);
                }();
                drawn = draw_info.canvas->submit_mesh(MESH, isolated_draw);
            }
        });
        // Style/canvas callbacks can erase or grow m_entries, including ending this handle.
        // Never consult the borrowed pre-draw entry after returning from them.
        if (!CALLBACK.valid()) {
            return false;
        }
        auto *current = record(handle);
        if (current == nullptr ||
            (current->life == Life::Mapped && current->current.native_presentation)) {
            return false;
        }
        if (PUBLIC_MAPPED) {
            entry = current;
            if (!mapped_source_allowed()) {
                return false;
            }
        }
        if (PUBLIC_OWNED) {
            const bool REVOKED = public_close_revoked(*current);
            if (REAL_PASS && (!drawn || REVOKED)) {
                interrupt_public_close(handle);
            }
            return drawn && !REVOKED;
        }
        if (!drawn && REAL_PASS && current->life == Life::DyingOwned) {
            end(handle, End::Interrupted);
        }
        return drawn;
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::profile_transformed(
        Handle handle, MonitorId monitor) {
        if (!perf::enabled() || monitor == 0) return;
        auto *entry = record(handle);
        if (entry == nullptr || entry->life != Life::Mapped ||
            !entry->current.running || entry->current.native_presentation) return;
        entry->profile_monitor = monitor;
        entry->profile_generation = perf::generation();
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::profile_frame_presented(
        MonitorId monitor) {
        if (!perf::enabled() || monitor == 0) return;
        const auto SESSION = perf::generation();
        for (const auto &window : m_tracked) {
            if (!window.handle) continue;
            auto *entry = record(*window.handle);
            if (entry == nullptr || entry->profile_monitor != monitor) continue;
            entry->profile_monitor = 0;
            if (entry->profile_generation != SESSION || entry->life != Life::Mapped ||
                !entry->current.running || entry->current.native_presentation) continue;
            perf::touch(profile_key(window.handle->style), monitor);
        }
    }

    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::before_window(
        WindowId window_id, Canvas &canvas, MonitorId render_monitor, FrameId render_frame) {
        if (!allows_draw(canvas)) {
            return;
        }
        auto *window = tracked(window_id);
        if (window != nullptr && window->handle) {
            auto *entry = record(*window->handle);
            if (entry != nullptr) {
                entry->profile_monitor = 0;
            }
            if (entry != nullptr && entry->life == Life::Mapped && !entry->current.native_presentation) {
                if (canvas.pass() == PassKind::Live) {
                    m_port.ensure_transformer(window->window, entry->handle);
                }
                if (entry->current.backdrop && canvas.pass() != PassKind::Inert) {
                    m_port.capture_backdrop(entry->handle, window->window, canvas);
                }
            }
        }
        auto observation = window != nullptr ? m_port.observe(window->window) : std::nullopt;
        if (window != nullptr && window->handle && render_frame != 0) {
            auto *entry = record(*window->handle);
            if (entry != nullptr && entry->life == Life::Mapped && entry->tracks_mapped_publication &&
                entry->monitor == render_monitor && entry->publication_frame != render_frame &&
                canvas.pass() == PassKind::Live && !canvas.captures_pixels()) {
                // v0.57 emits this event for both main and popup passes; only open one
                // receipt opportunity per owner frame. A bypass leaves it unknown.
                entry->publication_frame = render_frame;
                visit(entry->handle, [&]<class Style>(auto &slot, auto &resources) {
                    if constexpr (requires { Style::invalidate_mapped_publication(slot.state, resources); }) {
                        static_assert(noexcept(Style::invalidate_mapped_publication(slot.state, resources)));
                        Style::invalidate_mapped_publication(slot.state, resources);
                        if constexpr (requires {
                                          Style::mapped_native_publication(
                                              slot.state, Box{}, Box{}, Box{}, 1.0, resources);
                                      }) {
                            if (entry->current.native_presentation && observation && observation->mapped &&
                                observation->monitor == render_monitor && is_usable(observation->body) &&
                                is_usable(observation->capture) && std::isfinite(observation->scale) &&
                                observation->scale > 0) {
                                const auto SOURCE = detail::source_device(*observation);
                                if (is_usable(SOURCE)) {
                                    static_assert(noexcept(Style::mapped_native_publication(slot.state,
                                        observation->body, observation->capture, SOURCE, observation->scale, resources)));
                                    Style::mapped_native_publication(slot.state,
                                        observation->body, observation->capture, SOURCE, observation->scale, resources);
                                }
                            }
                        }
                    }
                });
            }
        }
        if (!observation) {
            return;
        }
        auto facts = m_port.monitor_facts(observation->monitor);
        if (!facts) {
            return;
        }
        (
            [&] {
                if constexpr (requires(Styles::Resources &resources,
                                  const ScreenDraw<Canvas> &draw_info) {
                                  Styles::Screen::under_window(resources, draw_info);
                              }) {
                    const ScreenDraw<Canvas> INFO{
                        &canvas, *facts, window_id, canvas.pass() == PassKind::Live};
                    const perf::CpuScope PROFILE{profile_key(Styles::KIND), perf::Phase::Draw};
                    Styles::Screen::under_window(m_resources.template get<Styles>(), INFO);
                }
            }(),
            ...);
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::before_windows(
        MonitorId monitor, Canvas &canvas) {
        if (!allows_draw(canvas) || canvas.pass() != PassKind::Live) {
            return;
        }
        auto facts = m_port.monitor_facts(monitor);
        if (!facts) {
            return;
        }
        (
            [&] {
                if constexpr (requires(Styles::Resources &resources,
                                  const ScreenDraw<Canvas> &draw_info) {
                                  Styles::Screen::before_windows(resources, draw_info);
                              }) {
                    const ScreenDraw<Canvas> INFO{&canvas, *facts, 0, true};
                    const perf::CpuScope PROFILE{profile_key(Styles::KIND), perf::Phase::Draw};
                    Styles::Screen::before_windows(m_resources.template get<Styles>(), INFO);
                }
            }(),
            ...);
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::after_windows(
        MonitorId monitor, Canvas &canvas) {
        if (!allows_draw(canvas)) {
            return;
        }
        auto facts = m_port.monitor_facts(monitor);
        if (!facts) {
            return;
        }
        for (auto &window : m_tracked) {
            if (!window.handle || !window.previous || window.previous->monitor != monitor ||
                window.previous->workspace_visible) {
                continue;
            }
            const auto *entry = record(*window.handle);
            if (entry == nullptr ||
                !tail_allowed(entry->handle, canvas, entry->retained_no_screen_share)) {
                continue;
            }
            auto retained = m_port.retained_mapped_image(entry->handle);
            if (!retained || retained->image == nullptr) {
                continue;
            }
            const auto &capture =
                window.tail_capture_valid ? window.tail_capture : retained->capture;
            const auto &bounds = entry->current.bounds;
            const Box OUTPUT{(capture.x + bounds.x - facts->screen.x) * facts->scale,
                (capture.y + bounds.y - facts->screen.y) * facts->scale,
                bounds.w * facts->scale,
                bounds.h * facts->scale};
            const Box SOURCE{(retained->capture.x - facts->screen.x) * facts->scale,
                (retained->capture.y - facts->screen.y) * facts->scale,
                retained->capture.w * facts->scale,
                retained->capture.h * facts->scale};
            const TailDraw INFO{SOURCE,
                OUTPUT,
                OUTPUT,
                retained->image_origin,
                retained->image_size,
                facts->scale,
                1,
                monitor,
                facts->frame,
                entry->retained_no_screen_share};
            if (is_usable(OUTPUT)) {
                m_port.queue_tail(entry->handle, canvas, *retained->image, INFO);
            }
        }
        (
            [&] {
                if constexpr (requires(Styles::Resources &resources,
                                  const ScreenDraw<Canvas> &draw_info) {
                                  Styles::Screen::after_windows(resources, draw_info);
                              }) {
                    const ScreenDraw<Canvas> INFO{
                        &canvas, *facts, 0, canvas.pass() == PassKind::Live};
                    const perf::CpuScope PROFILE{profile_key(Styles::KIND), perf::Phase::Draw};
                    Styles::Screen::after_windows(m_resources.template get<Styles>(), INFO);
                }
            }(),
            ...);
    }
}  // namespace hyprimate::animation