#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace hyprimate::animation {
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::close(
        WindowId window_id, Clock::time_point now) {
        auto *window = tracked(window_id);
        if (window == nullptr) {
            return;
        }
        if (window->handle) {
            const auto *existing = record(*window->handle);
            if (existing != nullptr && existing->life != Life::Mapped) {
                return;
            }
        }
        auto observation = m_port.observe(window->window);
        if (!m_policy.enabled || m_policy.inert || !m_policy.closing || !m_policy.close_available ||
            !observation || !observation->mapped || observation->fullscreen ||
            !is_usable(observation->capture) || !is_usable(observation->body) ||
            !std::isfinite(observation->scale) || observation->scale <= 0) {
            if (window->handle) {
                end(*window->handle, End::Interrupted);
            }
            return;
        }
        const auto KIND = m_policy.close_style.value_or(m_policy.style);
        if (window->handle && window->handle->style != KIND) {
            end(*window->handle, End::Interrupted);
            observation = m_port.observe(window->window);
            if (!observation || !observation->mapped || observation->fullscreen ||
                !is_usable(observation->capture) || !is_usable(observation->body) ||
                !std::isfinite(observation->scale) || observation->scale <= 0) {
                return;
            }
        }
        auto handle = window->handle;
        if (!handle) {
            handle = acquire(*window, *observation, 0, KIND);
        }
        if (!handle) {
            return;
        }
        auto event_place = detail::place(*observation, m_port.neighbourhood(window->window), ++m_seed);
        event_place.opening_edge = window->opening_edge;
        if (window->driven) {
            if (is_usable(observation->goal_body) &&
                !detail::same_box(observation->goal_body, window->last_warp)) {
                window->goal = observation->goal_body;
            }
            event_place.goal_body = window->goal;
            event_place.rest_capture = detail::rest_capture(*observation, window->goal);
        }
        CloseStart result = CloseStart::Declined;
        bool public_only = false;
        bool allows_blur = false;
        bool allows_pre_blur = true;
        visit(*handle, [&]<class Style>(auto &slot, auto &resources) {
            if constexpr (requires { Style::PUBLIC_CLOSE_ONLY; }) {
                public_only = Style::PUBLIC_CLOSE_ONLY;
                if (public_only && (observation->no_screen_share || !m_port.session_unlocked() ||
                    observation->window != window->id || m_port.identity(window->window) != window->id)) {
                    return;
                }
            }
            const perf::CpuScope PROFILE{profile_key(Style::KIND, "close"), perf::Phase::Setup};
            result = Style::close(slot.state, event_place, resources);
            allows_blur = Style::DRAWS_UNDER_BLUR;
            if constexpr (requires { Style::close_blur_safe(slot.state); }) {
                allows_pre_blur = Style::close_blur_safe(slot.state);
            }
        });
        if (result == CloseStart::Declined) {
            end(*handle, End::Interrupted);
            return;
        }
        auto *entry = record(*handle);
        if (entry->monitor != observation->monitor) {
            entry->stall_count = 0;
        }
        entry->life = Life::PendingCapture;
        entry->monitor = observation->monitor;
        entry->workspace = observation->workspace;
        entry->ownership = result;
        entry->pending_since = now;
        entry->last_step = now;
        entry->stepped = true;
        entry->close_place = event_place;
        entry->close_place.near = {};
        entry->no_screen_share = observation->no_screen_share;
        entry->public_close_only = public_only;
        entry->allows_close_blur = allows_blur;
        entry->allows_close_pre_blur = allows_pre_blur;
        entry->captured_scale = observation->scale;
        entry->source_device = detail::source_device(*observation);
        m_port.detach_transformer(window->window);
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::bind_fadeout(
        WindowId window_id, const FadeoutFacts &fadeout, Port::RetainedImage image) {
        auto *window = tracked(window_id);
        if (window == nullptr || !window->handle) {
            return;
        }
        const auto CURRENT_HANDLE = *window->handle;
        auto *entry = record(CURRENT_HANDLE);
        if (entry == nullptr || entry->life != Life::PendingCapture) {
            return;
        }
        auto observation = m_port.observe(window->window);
        const auto COUNT = std::count_if(m_entries.begin(), m_entries.end(), [](const auto &item) {
            return item.life == Life::DyingNative || item.life == Life::DyingOwned;
        });
        const bool ALREADY_BOUND =
            std::any_of(m_entries.begin(),
                m_entries.end(),
                [&](const auto &item) {
                    return item.fadeout != 0 && item.fadeout == fadeout.fadeout;
                }) ||
            std::ranges::find(m_tombstones, fadeout.fadeout) != m_tombstones.end();
        if (!observation || observation->monitor != entry->monitor ||
            fadeout.monitor != entry->monitor ||
            (entry->public_close_only &&
                (entry->no_screen_share || observation->no_screen_share || fadeout.no_screen_share ||
                    observation->window != entry->origin || m_port.identity(window->window) != entry->origin ||
                    !m_port.session_unlocked() || !is_usable(entry->source_device) ||
                    (fadeout.workspace_bound && fadeout.workspace != entry->workspace))) ||
            observation->scale != entry->captured_scale ||
            fadeout.scale != entry->captured_scale || !std::isfinite(fadeout.scale) ||
            fadeout.scale <= 0 || fadeout.now < entry->pending_since ||
            std::chrono::duration<double>(fadeout.now - entry->pending_since).count() > 1.0 ||
            fadeout.fadeout == 0 || !fadeout.alive || !fadeout.image_available ||
            fadeout.alpha <= 0 || !std::isfinite(fadeout.alpha) || !is_usable(fadeout.native_box) ||
            !std::isfinite(fadeout.image_size.x) || !std::isfinite(fadeout.image_size.y) ||
            fadeout.image_size.x <= 0 || fadeout.image_size.y <= 0 ||
            !close_blur_allowed(*entry, fadeout) || ALREADY_BOUND || COUNT >= 16) {
            end(CURRENT_HANDLE, End::Interrupted);
            return;
        }
        const bool INITIALIZE_CLOSE = entry->public_close_only && entry->ownership == CloseStart::Owned;
        if (INITIALIZE_CLOSE) {
            bool source_allowed = true;
            visit(CURRENT_HANDLE, [&]<class Style>(auto &slot, auto &) {
                const auto &source = entry->source_device;
                const auto SCALE = entry->captured_scale;
                if constexpr (requires { Style::accepts_close_crop(slot.state, Box{}, Box{}, 1.0); }) {
                    const double LEFT = std::clamp(-source.x, 0.0, source.w);
                    const double TOP = std::clamp(-source.y, 0.0, source.h);
                    const double RIGHT = std::clamp(fadeout.image_size.x - source.x, LEFT, source.w);
                    const double BOTTOM = std::clamp(fadeout.image_size.y - source.y, TOP, source.h);
                    const Box CROP{LEFT / SCALE, TOP / SCALE, (RIGHT - LEFT) / SCALE, (BOTTOM - TOP) / SCALE};
                    static_assert(noexcept(Style::accepts_close_crop(slot.state, CROP, source, SCALE)));
                    source_allowed = Style::accepts_close_crop(slot.state, CROP, source, SCALE);
                }
            });
            if (!source_allowed) {
                end(CURRENT_HANDLE, End::Interrupted);
                return;
            }
        }
        bool retained = false;
        try {
            retained = m_port.bind_fadeout(CURRENT_HANDLE, fadeout.fadeout, image);
        } catch (...) {
            end(CURRENT_HANDLE, End::Interrupted);
            throw;
        }
        if (!retained) {
            end(CURRENT_HANDLE, End::Interrupted);
            return;
        }
        entry = record(CURRENT_HANDLE);
        if (entry == nullptr) return;
        if (INITIALIZE_CLOSE) {
            entry->last_step = fadeout.now;
            entry->last_frame = 0;
            entry->stall_count = 0;
            const auto STEP = detail::dying_step(entry->close_place, entry->source_device,
                entry->captured_scale,
                entry->public_close_only && entry->ownership == CloseStart::Owned,
                entry->monitor, 0);
            visit(CURRENT_HANDLE, [&]<class Style>(auto &slot, auto &resources) {
                if constexpr (requires { Style::bind_close_source(slot.state, entry->source_device, image); }) {
                    static_assert(noexcept(Style::bind_close_source(slot.state, entry->source_device, image)));
                    if (!m_port.session_unlocked() ||
                        !Style::bind_close_source(slot.state, entry->source_device, image)) {
                        entry->current = {.interrupted = true};
                        return;
                    }
                }
                if constexpr (requires { Style::PUBLIC_CLOSE_ONLY; }) {
                    if constexpr (Style::PUBLIC_CLOSE_ONLY) {
                        static_assert(noexcept(Style::initial_close_frame(slot.state, STEP, resources)));
                        const perf::CpuScope PROFILE{
                            profile_key(Style::KIND, "close"), perf::Phase::Update};
                        entry->current = Style::initial_close_frame(slot.state, STEP, resources);
                    }
                }
            });
            if (entry->current.interrupted) {
                end(CURRENT_HANDLE, End::Interrupted);
                return;
            }
        }
        entry->captured_alpha = fadeout.alpha;
        entry->no_screen_share |= observation->no_screen_share || fadeout.no_screen_share;
        entry->fadeout = fadeout.fadeout;
        entry->fadeout_info = fadeout;
        entry->life = entry->ownership == CloseStart::Owned ? Life::DyingOwned : Life::DyingNative;
        if (entry->life == Life::DyingOwned) {
            try {
                m_port.suppress_native(fadeout.fadeout, true);
            } catch (...) {
                if (INITIALIZE_CLOSE) {
                    end(CURRENT_HANDLE, End::Interrupted);
                }
                throw;
            }
        }
        release_drive(*window);
        window->handle.reset();
        m_port.forget_window_reference(window_id);
        std::erase_if(m_tracked, [window_id](const auto &item) { return item.id == window_id; });
        if (INITIALIZE_CLOSE) {
            if (!entry->current.running) {
                // An intentionally empty close still owns this fadeout; native must not flash.
                const auto MONITOR = entry->monitor;
                end(CURRENT_HANDLE, End::Completed);
                m_port.schedule(MONITOR);
                return;
            }
            entry->awaiting_close_presentation = true;
            if (is_usable(entry->footprint)) {
                m_port.damage(entry->footprint);
            }
            damage_dying(*entry);
        }
        m_port.schedule(entry->monitor);
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::refresh_fadeout(
        Record &entry, const std::optional<FadeoutFacts> &fadeout) {
        if (entry.fadeout == 0) {
            return entry.life != Life::DyingNative;
        }
        // The restriction belongs to the retained pixels, not the lifetime of the native fadeout.
        if (fadeout && fadeout->no_screen_share) {
            entry.no_screen_share = true;
        }
        if (fadeout && fadeout->alive) {
            // Public owned closes use the retained snapshot's original device placement.
            if (!entry.public_close_only || entry.life != Life::DyingOwned) {
                entry.fadeout_info = *fadeout;
            }
        } else if (entry.life == Life::DyingNative) {
            return false;
        } else {
            m_port.suppress_native(entry.fadeout, false);
        }
        return true;
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::damage_dying(Record &entry) {
        const auto BOUNDS = dying_view(entry).bounds_device;
        const auto &screen = entry.close_place.screen;
        entry.footprint = detail::dying_footprint(BOUNDS, screen, entry.captured_scale);
        if (is_usable(entry.footprint)) {
            m_port.damage(entry.footprint);
        }
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::close_blur_allowed(
        const Record &entry, const FadeoutFacts &fadeout) {
        return (!fadeout.blurred || entry.allows_close_blur) &&
               (!fadeout.pre_blur || entry.allows_close_pre_blur);
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::public_close_revoked(Record &entry) {
        if (const auto facts = m_port.fadeout_facts(entry.fadeout)) {
            entry.no_screen_share |= facts->no_screen_share;
            if (!close_blur_allowed(entry, *facts)) return true;
        }
        return entry.no_screen_share || !m_port.session_unlocked();
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::frame_dying(
        Record &entry, const FrameStamp &stamp) {
        if (entry.life == Life::Mapped || entry.monitor != stamp.monitor) {
            return;
        }
        const auto CURRENT_HANDLE = entry.handle;
        if (entry.life == Life::PendingCapture) {
            if (std::chrono::duration<double>(stamp.now - entry.pending_since).count() > 1.0) {
                end(CURRENT_HANDLE, End::Interrupted);
            }
            return;
        }
        if (entry.stepped && entry.last_frame == stamp.frame) {
            return;
        }
        const auto fadeout = entry.fadeout ? m_port.fadeout_facts(entry.fadeout) : std::nullopt;
        if (!refresh_fadeout(entry, fadeout)) {
            end(CURRENT_HANDLE, End::Completed);
            return;
        }
        if (entry.public_close_only && (entry.no_screen_share || !m_port.session_unlocked() ||
            (fadeout && !close_blur_allowed(entry, *fadeout)))) {
            if (entry.life == Life::DyingOwned) {
                interrupt_public_close(CURRENT_HANDLE);
            } else {
                end(CURRENT_HANDLE, End::Interrupted);
            }
            return;
        }
        if (entry.awaiting_close_presentation) {
            if (std::chrono::duration<double>(stamp.now - entry.last_step).count() >
                STALL_WINDOW_SECONDS) {
                interrupt_public_close(CURRENT_HANDLE);
                return;
            }
            // render.pre precedes pass execution. Neither a frame stamp nor a scratch draw
            // proves that the frozen pose actually reached the owner output.
            entry.last_frame = stamp.frame;
            if (is_usable(entry.footprint)) {
                m_port.damage(entry.footprint);
            }
            m_port.schedule(stamp.monitor);
            return;
        }
        const auto ELAPSED = detail::step_clock(entry.stepped, entry.last_step,
            entry.last_frame, entry.stall_count, entry.stalls, stamp);
        if (!ELAPSED) {
            end(CURRENT_HANDLE, End::Interrupted);
            return;
        }
        const auto STEP = detail::dying_step(entry.close_place, entry.source_device,
            entry.captured_scale,
            entry.public_close_only && entry.ownership == CloseStart::Owned,
            entry.monitor, *ELAPSED);
        if (is_usable(entry.footprint)) {
            m_port.damage(entry.footprint);
        }
        Effects effects;
        visit(CURRENT_HANDLE, [&]<class Style>(auto &slot, auto &resources) {
            const perf::CpuScope PROFILE{profile_key(Style::KIND, "close"), perf::Phase::Update};
            if (perf::enabled()) {
                perf::resource_sample(profile_key(Style::KIND, "close"), sizeof(slot.state), 0);
            }
            entry.current = Style::advance(slot.state, STEP, resources, effects);
        });
        damage_dying(entry);
        if (!entry.current.running) {
            end(CURRENT_HANDLE, entry.current.interrupted ? End::Interrupted : End::Completed);
            return;
        }
        m_port.schedule(stamp.monitor);
    }
    template <class Port, class... Styles>
    std::optional<Handle> CWindowAnimations<Port, StyleList<Styles...>>::pending_for(
        WindowId window_id) const {
        for (const auto &entry : m_entries) {
            if (entry.origin == window_id && entry.life == Life::PendingCapture) {
                return entry.handle;
            }
        }
        return std::nullopt;
    }
    template <class Port, class... Styles>
    std::optional<PendingPublicClose> CWindowAnimations<Port, StyleList<Styles...>>::pending_public_close(
        Handle handle) const {
        const auto *entry = record(handle);
        if (entry == nullptr || entry->life != Life::PendingCapture || !entry->public_close_only ||
            entry->ownership != CloseStart::Owned) {
            return std::nullopt;
        }
        return PendingPublicClose{entry->close_place, entry->source_device, entry->captured_scale};
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::interrupt_public_close(Handle handle) {
        const auto *entry = record(handle);
        if (entry == nullptr || !entry->public_close_only || entry->ownership != CloseStart::Owned ||
            (entry->life != Life::PendingCapture && entry->life != Life::DyingOwned)) {
            return;
        }
        const auto MONITOR = entry->monitor;
        const bool WAS_OWNED = entry->life == Life::DyingOwned;
        end(handle, End::Interrupted);
        if (WAS_OWNED) {
            m_port.schedule(MONITOR);
        }
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::confirm_public_close_presented(
        Handle handle, Clock::time_point now, bool submitted) {
        auto *entry = record(handle);
        if (entry == nullptr || !entry->awaiting_close_presentation || now < entry->last_step) {
            return;
        }
        if (!submitted) {
            interrupt_public_close(handle);
            return;
        }
        entry->awaiting_close_presentation = false;
        entry->last_step = now;
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::suppresses(std::uint64_t fadeout) const {
        if (fadeout == 0) {
            return false;
        }
        if (std::ranges::find(m_tombstones, fadeout) != m_tombstones.end()) {
            return true;
        }
        return std::any_of(m_entries.begin(), m_entries.end(), [fadeout](const auto &entry) {
            return entry.fadeout == fadeout && entry.life == Life::DyingOwned;
        });
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::native_finished(std::uint64_t fadeout) {
        std::erase(m_tombstones, fadeout);
        m_port.suppress_native(fadeout, false);
        for (std::size_t index = m_entries.size(); index > 0; --index) {
            const auto &entry = m_entries[index - 1];
            if (entry.fadeout == fadeout && entry.life == Life::DyingNative) {
                end(entry.handle, End::Completed);
            }
        }
    }
    template <class Port, class... Styles>
    DyingView CWindowAnimations<Port, StyleList<Styles...>>::dying_view(const Record &entry) const {
        const auto &facts = entry.fadeout_info;
        Box render = facts.native_box;
        bool fixed = false;
        visit(entry.handle,
            [&]<class Style>(const auto &, const auto &) { fixed = Style::FIXED_CLOSE_PLACEMENT; });
        if (entry.life == Life::DyingOwned) {
            render = entry.source_device;
            if (!fixed) {
                render.x = facts.native_box.x + ((facts.native_box.w - render.w) / 2);
                render.y = facts.native_box.y + ((facts.native_box.h - render.h) / 2);
            }
        }
        const auto BOUNDS = entry.current.bounds;
        return {entry.handle,
            entry.monitor,
            entry.workspace,
            entry.fadeout,
            entry.ownership,
            entry.source_device,
            render,
            {render.x + (BOUNDS.x * facts.scale),
                render.y + (BOUNDS.y * facts.scale),
                BOUNDS.w * facts.scale,
                BOUNDS.h * facts.scale},
            facts.image_size,
            facts.scale,
            entry.life == Life::DyingOwned ? entry.captured_alpha : facts.alpha,
            facts.workspace_bound,
            entry.no_screen_share,
            entry.public_close_only};
    }
    template <class Port, class... Styles>
    std::span<const DyingView> CWindowAnimations<Port, StyleList<Styles...>>::dying_views() const {
        m_views.clear();
        for (const auto &entry : m_entries) {
            if (entry.life != Life::DyingOwned && entry.life != Life::DyingNative) {
                continue;
            }
            m_views.push_back(dying_view(entry));
        }
        return m_views;
    }
    template <class Port, class... Styles>
    std::optional<DyingView> CWindowAnimations<Port, StyleList<Styles...>>::dying_view_for(
        std::uint64_t fadeout) const {
        if (fadeout == 0) {
            return std::nullopt;
        }
        for (const auto &entry : m_entries) {
            if (entry.fadeout == fadeout &&
                (entry.life == Life::DyingOwned || entry.life == Life::DyingNative)) {
                return dying_view(entry);
            }
        }
        return std::nullopt;
    }
}  // namespace hyprimate::animation