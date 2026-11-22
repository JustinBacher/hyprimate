#pragma once

#include <algorithm>
#include <cmath>
#include <utility>

#include "WindowTracker.hpp"

namespace hyprimate::animation {
    namespace detail {
        inline bool same_box(const Box &lhs, const Box &rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
        }
        inline Box translated(Box box, Vec2 offset) {
            box.x += offset.x;
            box.y += offset.y;
            return box;
        }
        inline Box rest_capture(const Observation &observation, const Box &goal) {
            return {.x = goal.x + observation.capture.x - observation.body.x,
                .y = goal.y + observation.capture.y - observation.body.y,
                .w = goal.w + observation.capture.w - observation.body.w,
                .h = goal.h + observation.capture.h - observation.body.h};
        }
    }  // namespace detail

    template <class Port, class... Styles>
    template <class Function>
    bool CWindowAnimations<Port, StyleList<Styles...>>::visit(Handle handle, Function &&function) {
        bool found = false;
        (
            [&] {
                if (handle.style != Styles::KIND) {
                    return;
                }
                auto &resources = m_resources.template get<Styles>();
                auto *slot = resources.animations.get({handle.index, handle.generation});
                if (slot != nullptr) {
                    std::forward<Function>(function).template operator()<Styles>(*slot, resources);
                    found = true;
                }
            }(),
            ...);
        return found;
    }
    template <class Port, class... Styles>
    template <class Function>
    bool CWindowAnimations<Port, StyleList<Styles...>>::visit(
        Handle handle, Function &&function) const {
        bool found = false;
        (
            [&] {
                if (handle.style != Styles::KIND) {
                    return;
                }
                const auto &resources = m_resources.template get<Styles>();
                const auto *slot = resources.animations.get({handle.index, handle.generation});
                if (slot != nullptr) {
                    std::forward<Function>(function).template operator()<Styles>(*slot, resources);
                    found = true;
                }
            }(),
            ...);
        return found;
    }
    template <class Port, class... Styles>
    template <class Function>
    bool CWindowAnimations<Port, StyleList<Styles...>>::selected(Function &&function) {
        bool found = false;
        (
            [&] {
                if (m_policy.style == Styles::KIND) {
                    std::forward<Function>(function).template operator()<Styles>(
                        m_resources.template get<Styles>());
                    found = true;
                }
            }(),
            ...);
        return found;
    }
    template <class Port, class... Styles>
    auto CWindowAnimations<Port, StyleList<Styles...>>::tracked(WindowId window_id) -> Tracked * {
        const auto ITER = std::find_if(m_tracked.begin(),
            m_tracked.end(),
            [window_id](const auto &item) { return item.id == window_id; });
        return ITER == m_tracked.end() ? nullptr : &*ITER;
    }
    template <class Port, class... Styles>
    auto CWindowAnimations<Port, StyleList<Styles...>>::tracked(WindowId window_id) const
        -> const Tracked * {
        const auto ITER = std::find_if(m_tracked.begin(),
            m_tracked.end(),
            [window_id](const auto &item) { return item.id == window_id; });
        return ITER == m_tracked.end() ? nullptr : &*ITER;
    }
    template <class Port, class... Styles>
    auto CWindowAnimations<Port, StyleList<Styles...>>::record(Handle handle) -> Record * {
        const auto ITER = std::find_if(m_entries.begin(),
            m_entries.end(),
            [handle](const auto &item) { return item.handle == handle; });
        return ITER == m_entries.end() ? nullptr : &*ITER;
    }
    template <class Port, class... Styles>
    auto CWindowAnimations<Port, StyleList<Styles...>>::record(Handle handle) const
        -> const Record * {
        const auto ITER = std::find_if(m_entries.begin(),
            m_entries.end(),
            [handle](const auto &item) { return item.handle == handle; });
        return ITER == m_entries.end() ? nullptr : &*ITER;
    }
    template <class Port, class... Styles>
    Place CWindowAnimations<Port, StyleList<Styles...>>::place(
        const Observation &observation, Neighbourhood near, std::uint64_t seed) {
        if (near.cursor) {
            near.cursor->x -= observation.workspace_offset.x + observation.floating_offset.x;
            near.cursor->y -= observation.workspace_offset.y + observation.floating_offset.y;
        }
        return {.body = observation.body,
            .capture = observation.capture,
            .goal_body = observation.goal_body,
            .rest_capture = detail::rest_capture(observation, observation.goal_body),
            .screen = observation.screen,
            .work_area = observation.work_area,
            .near = near,
            .seed = seed,
            .opening_edge = std::nullopt,
            .monitor = observation.monitor,
            .workspace = observation.workspace,
            .pinned = observation.pinned};
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::track(Port::WindowRef window) {
        const auto WINDOW_ID = m_port.identity(window);
        if (WINDOW_ID == 0 || tracked(WINDOW_ID) != nullptr) {
            return;
        }
        m_tracked.push_back(Tracked{.window = std::move(window),
            .id = WINDOW_ID,
            .previous = std::nullopt,
            .handle = std::nullopt,
            .opening_edge = std::nullopt});
    }
    template <class Port, class... Styles>
    std::optional<Handle> CWindowAnimations<Port, StyleList<Styles...>>::acquire(
        Tracked &tracked_window, const Observation &observation, std::uint64_t seed) {
        std::optional<Handle> result;
        selected([&]<class Style>(auto &resources) {
            const auto POOL_HANDLE = resources.animations.emplace(
                std::in_place, [&] { return Style::start(resources, seed); });
            const Handle ENTRY_HANDLE{Style::KIND, POOL_HANDLE.index, POOL_HANDLE.generation};
            try {
                m_port.acquire_visuals(ENTRY_HANDLE);
                m_views.reserve(m_entries.size() + 1);
                m_tombstones.reserve(m_tombstones.size() + m_entries.size() + 1);
                m_entries.push_back(Record{.origin = tracked_window.id,
                    .monitor = observation.monitor,
                    .workspace = observation.workspace,
                    .last_step = {},
                    .pending_since = {},
                    .handle = ENTRY_HANDLE});
            } catch (...) {
                m_port.release_visuals(ENTRY_HANDLE);
                auto *slot = resources.animations.get(POOL_HANDLE);
                Style::end(slot->state, resources, End::Interrupted);
                resources.animations.erase(POOL_HANDLE);
                throw;
            }
            tracked_window.handle = ENTRY_HANDLE;
            result = ENTRY_HANDLE;
        });
        return result;
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::release_drive(Tracked &tracked_window) {
        if (!tracked_window.driven) {
            return;
        }
        const auto OBSERVED = m_port.observe(tracked_window.window);
        if (!OBSERVED && !m_port.window_alive(tracked_window.window)) {
            tracked_window.driven = false;
            return;
        }
        if (OBSERVED && is_usable(OBSERVED->goal_body) &&
            !detail::same_box(OBSERVED->goal_body, tracked_window.last_warp)) {
            tracked_window.goal = OBSERVED->goal_body;
        }
        m_port.warp_body(tracked_window.window, tracked_window.goal);
        tracked_window.driven = false;
        m_port.damage_window(tracked_window.window);
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::apply(
        Tracked &tracked_window, const Observation &observation, const Effects &effects) {
        if ((effects.drive_body && effects.snap_to_goal) ||
            (effects.drive_body && !is_usable(*effects.drive_body)) ||
            (effects.snap_to_goal && !is_usable(observation.goal_body))) {
            return false;
        }
        if (tracked_window.driven &&
            !detail::same_box(observation.goal_body, tracked_window.last_warp)) {
            tracked_window.goal = observation.goal_body;
        }
        if (effects.release_body) {
            release_drive(tracked_window);
        }
        if (effects.drive_body || effects.snap_to_goal) {
            if (!tracked_window.driven) {
                tracked_window.goal = observation.goal_body;
            }
            const auto BOX = effects.drive_body ? *effects.drive_body : observation.goal_body;
            m_port.warp_body(tracked_window.window, BOX);
            tracked_window.last_warp = BOX;
            tracked_window.driven = true;
        }
        if (effects.settle_native_open_alpha) {
            m_port.settle_open_alpha(tracked_window.window);
        }
        return true;
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::end(Handle handle, End why) {
        const auto *entry = record(handle);
        if (entry == nullptr) {
            return;
        }
        const auto ORIGIN = entry->origin;
        const auto MONITOR = entry->monitor;
        const auto FADEOUT = entry->fadeout;
        const auto OWNERSHIP = entry->ownership;
        const auto LIFE = entry->life;
        if (is_usable(entry->footprint)) {
            m_port.damage(entry->footprint);
        }
        if (auto *window = tracked(ORIGIN); window != nullptr && window->handle == handle) {
            release_drive(*window);
            m_port.detach_transformer(window->window);
            window->handle.reset();
        }
        InFlightDraw *deferred = nullptr;
        for (auto *flight = m_draws; flight != nullptr; flight = flight->previous) {
            if (flight->handle == handle) {
                deferred = flight;
            }
        }
        if (deferred != nullptr) {
            deferred->deferred = why;
        } else {
            visit(handle, [&]<class Style>(auto &slot, auto &resources) {
                Style::end(slot.state, resources, why);
                resources.animations.erase({handle.index, handle.generation});
            });
        }
        if (LIFE == Life::DyingOwned && why == End::Completed && FADEOUT != 0 &&
            OWNERSHIP == CloseStart::Owned) {
            auto facts = m_port.fadeout_facts(FADEOUT);
            if (facts && facts->alive) {
                m_tombstones.push_back(FADEOUT);
            } else {
                m_port.suppress_native(FADEOUT, false);
            }
        } else if (FADEOUT != 0 && OWNERSHIP == CloseStart::Owned) {
            m_port.suppress_native(FADEOUT, false);
        }
        if (deferred == nullptr) {
            m_port.release_visuals(handle);
        }
        m_port.recheck_scanout(MONITOR);
        std::erase_if(m_entries, [handle](const auto &item) { return item.handle == handle; });
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::finish_draw(InFlightDraw &flight) {
        m_draws = flight.previous;
        if (!flight.deferred) {
            return;
        }
        visit(flight.handle, [&]<class Style>(auto &slot, auto &resources) {
            Style::end(slot.state, resources, *flight.deferred);
            resources.animations.erase({flight.handle.index, flight.handle.generation});
        });
        m_port.release_visuals(flight.handle);
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::configure(const Policy &policy) {
        const bool SELECTION_CHANGED = m_policy.style != policy.style;
        const bool STOP_CLOSES =
            !policy.enabled || policy.inert || !policy.closing || !policy.close_available;
        m_policy = policy;
        for (std::size_t index = m_entries.size(); index > 0; --index) {
            const auto ITEM = m_entries[index - 1];
            if ((ITEM.life == Life::Mapped &&
                    (SELECTION_CHANGED || !policy.enabled || policy.inert)) ||
                (ITEM.life != Life::Mapped && STOP_CLOSES)) {
                end(ITEM.handle, End::Interrupted);
            }
        }
        if (STOP_CLOSES) {
            for (const auto FADEOUT : m_tombstones) {
                m_port.suppress_native(FADEOUT, false);
            }
            m_tombstones.clear();
        }
        (
            [&] {
                if constexpr (requires(Styles::Resources &resources, const ScreenFacts &facts) {
                                  Styles::Screen::tick(resources, facts);
                              }) {
                    const bool RETAINED =
                        std::any_of(m_entries.begin(), m_entries.end(), [](const auto &entry) {
                            return entry.handle.style == Styles::KIND && entry.life != Life::Mapped;
                        });
                    auto &resources = m_resources.template get<Styles>();
                    const bool SCREEN_ACTIVE = std::any_of(
                        m_screen_frames.begin(), m_screen_frames.end(), [&](const auto &item) {
                            return Styles::Screen::needs_frame(resources, item.first);
                        });
                    ScreenFacts facts{};
                    facts.enabled = policy.enabled && !policy.inert &&
                                    (policy.opening || policy.movement || policy.closing) &&
                                    (policy.style == Styles::KIND || RETAINED || SCREEN_ACTIVE);
                    Styles::Screen::tick(resources, facts);
                }
            }(),
            ...);
    }
    template <class Port, class... Styles>
    template <class Style>
    void CWindowAnimations<Port, StyleList<Styles...>>::update_settings(
        const Style::Settings &settings) {
        m_resources.template get<Style>().update(settings, Style::prepare_settings);
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::unmap(WindowId window_id) {
        auto *window = tracked(window_id);
        if (window == nullptr) {
            return;
        }
        if (window->handle) {
            const auto *entry = record(*window->handle);
            if (entry != nullptr && entry->life == Life::Mapped) {
                end(entry->handle, End::Interrupted);
            }
        }
        if (window->handle) {
            return;
        }
        m_port.forget_window_reference(window_id);
        std::erase_if(m_tracked, [window_id](const auto &item) { return item.id == window_id; });
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::destroyed(WindowId window_id) {
        unmap(window_id);
    }

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
        auto handle = window->handle;
        if (!handle) {
            handle = acquire(*window, *observation, 0);
        }
        if (!handle) {
            return;
        }
        auto event_place = place(*observation, m_port.neighbourhood(window->window), ++m_seed);
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
        visit(*handle, [&]<class Style>(auto &slot, auto &resources) {
            result = Style::close(slot.state, event_place, resources);
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
        entry->captured_scale = observation->scale;
        const auto OFFSET = Vec2{observation->workspace_offset.x + observation->floating_offset.x -
                                     observation->screen.x,
            observation->workspace_offset.y + observation->floating_offset.y -
                observation->screen.y};
        const auto SOURCE = detail::translated(observation->capture, OFFSET);
        entry->source_device = {SOURCE.x * observation->scale,
            SOURCE.y * observation->scale,
            SOURCE.w * observation->scale,
            SOURCE.h * observation->scale};
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
        bool allows_blur = false;
        visit(CURRENT_HANDLE,
            [&]<class Style>(auto &, auto &) { allows_blur = Style::DRAWS_UNDER_BLUR; });
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
            fadeout.monitor != entry->monitor || observation->scale != entry->captured_scale ||
            fadeout.scale != entry->captured_scale || !std::isfinite(fadeout.scale) ||
            fadeout.scale <= 0 || fadeout.now < entry->pending_since ||
            std::chrono::duration<double>(fadeout.now - entry->pending_since).count() > 1.0 ||
            fadeout.fadeout == 0 || !fadeout.alive || !fadeout.image_available ||
            fadeout.alpha <= 0 || !std::isfinite(fadeout.alpha) || !is_usable(fadeout.native_box) ||
            !std::isfinite(fadeout.image_size.x) || !std::isfinite(fadeout.image_size.y) ||
            fadeout.image_size.x <= 0 || fadeout.image_size.y <= 0 ||
            (fadeout.blurred && !allows_blur) || ALREADY_BOUND || COUNT >= 16) {
            end(CURRENT_HANDLE, End::Interrupted);
            return;
        }
        entry->captured_alpha = fadeout.alpha;
        try {
            m_port.bind_fadeout(CURRENT_HANDLE, fadeout.fadeout, std::move(image));
        } catch (...) {
            end(CURRENT_HANDLE, End::Interrupted);
            throw;
        }
        entry->fadeout = fadeout.fadeout;
        entry->fadeout_info = fadeout;
        entry->life = entry->ownership == CloseStart::Owned ? Life::DyingOwned : Life::DyingNative;
        if (entry->life == Life::DyingOwned) {
            m_port.suppress_native(fadeout.fadeout, true);
        }
        release_drive(*window);
        window->handle.reset();
        m_port.forget_window_reference(window_id);
        std::erase_if(m_tracked, [window_id](const auto &item) { return item.id == window_id; });
        m_port.schedule(entry->monitor);
    }
    template <class Port, class... Styles>
    template <class Style>
    void CWindowAnimations<Port, StyleList<Styles...>>::tick_screen(
        ScreenFacts &facts, MonitorId monitor) {
        if constexpr (requires(Style::Resources &resources, const ScreenFacts &screen) {
                          Style::Screen::tick(resources, screen);
                      }) {
            const bool RETAINED =
                std::any_of(m_entries.begin(), m_entries.end(), [](const auto &entry) {
                    return entry.handle.style == Style::KIND && entry.life != Life::Mapped;
                });
            auto &resources = m_resources.template get<Style>();
            const bool NEEDED_BEFORE = Style::Screen::needs_frame(resources, monitor);
            facts.enabled = m_policy.enabled && !m_policy.inert &&
                            (m_policy.opening || m_policy.movement || m_policy.closing) &&
                            (m_policy.style == Style::KIND || RETAINED || NEEDED_BEFORE);
            Style::Screen::tick(resources, facts);
            const bool NEEDED_AFTER = Style::Screen::needs_frame(resources, monitor);
            if (NEEDED_BEFORE || NEEDED_AFTER) {
                m_port.damage(facts.screen);
            }
            if (NEEDED_BEFORE != NEEDED_AFTER) {
                m_port.recheck_scanout(monitor);
            }
            if (NEEDED_AFTER) {
                m_port.schedule(monitor);
            }
        }
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::tick_screens(const FrameStamp &stamp) {
        const auto CLOCK = std::find_if(m_screen_frames.begin(),
            m_screen_frames.end(),
            [&](const auto &item) { return item.first == stamp.monitor; });
        if (CLOCK != m_screen_frames.end() && CLOCK->second == stamp.frame) {
            return;
        }
        if (CLOCK == m_screen_frames.end()) {
            m_screen_frames.emplace_back(stamp.monitor, stamp.frame);
        } else {
            CLOCK->second = stamp.frame;
        }
        if (auto facts = m_port.monitor_facts(stamp.monitor)) {
            facts->now = stamp.now;
            facts->frame = stamp.frame;
            (tick_screen<Styles>(*facts, stamp.monitor), ...);
        }
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::usable_observation(
        Tracked &window, const std::optional<Observation> &observation) {
        window.expired = !observation && !m_port.window_alive(window.window);
        if (!observation && !window.expired) {
            if (window.handle) {
                if (auto *entry = record(*window.handle);
                    entry != nullptr && entry->life == Life::Mapped) {
                    entry->stepped = false;
                    entry->stall_count = 0;
                }
            }
            window.previous.reset();
            return false;
        }
        bool hidden_tail = false;
        if (observation && observation->hidden && !observation->workspace_visible &&
            m_policy.workspace_movement && !observation->pinned && window.handle) {
            const auto *entry = record(*window.handle);
            hidden_tail = entry != nullptr && entry->life == Life::Mapped && entry->current.tail;
        }
        if (!observation || !observation->mapped || observation->fullscreen ||
            (observation->hidden && !hidden_tail) || !is_usable(observation->body) ||
            !is_usable(observation->capture)) {
            if (window.handle) {
                const auto *entry = record(*window.handle);
                if (entry != nullptr && entry->life == Life::Mapped) {
                    end(entry->handle, End::Interrupted);
                }
            }
            window.previous.reset();
            return false;
        }
        return true;
    }
    template <class Port, class... Styles>
    std::optional<Observation> CWindowAnimations<Port, StyleList<Styles...>>::sample_window(
        Tracked &window, const FrameStamp &stamp) {
        auto observation = m_port.observe(window.window);
        if (!usable_observation(window, observation) || !observation) {
            return std::nullopt;
        }
        if (observation->monitor != stamp.monitor) {
            return std::nullopt;
        }
        if (window.handle) {
            auto *entry = record(*window.handle);
            if (entry != nullptr && entry->life != Life::Mapped) {
                return std::nullopt;
            }
            if (entry != nullptr && entry->monitor != stamp.monitor) {
                entry->monitor = stamp.monitor;
                entry->stepped = false;
                entry->stall_count = 0;
                window.previous = *observation;
            }
            if (entry != nullptr && entry->stepped && entry->last_frame == stamp.frame) {
                return std::nullopt;
            }
        }
        return observation;
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::begin_mapped(
        Tracked &window, const Observation &observation, const std::optional<Motion> &motion) {
        const bool OPENING = observation.animating_in && !window.opening_seen;
        if (OPENING) {
            window.opening_edge.reset();
        }
        window.opening_seen = observation.animating_in;
        bool acquired = false;
        if (!window.handle && m_policy.enabled && !m_policy.inert) {
            bool moves = false;
            if (motion && m_policy.movement) {
                selected([&]<class Style>(auto &resources) {
                    if constexpr (requires { Style::moves(*motion, resources); }) {
                        moves = Style::moves(*motion, resources);
                    } else {
                        moves = motion->translated;
                    }
                });
            }
            if ((OPENING && m_policy.opening) || moves) {
                acquired = acquire(window, observation, ++m_seed).has_value();
            }
        }
        if (window.handle && OPENING && m_policy.opening && m_policy.enabled && !m_policy.inert) {
            const auto CURRENT_HANDLE = *window.handle;
            Effects effects;
            bool accepted = false;
            const auto INITIAL = place(
                observation, m_port.neighbourhood(window.window), acquired ? m_seed : ++m_seed);
            visit(CURRENT_HANDLE, [&]<class Style>(auto &slot, auto &resources) {
                accepted = Style::open(slot.state, INITIAL, resources, effects);
            });
            if ((!accepted && acquired) || (accepted && !apply(window, observation, effects))) {
                end(CURRENT_HANDLE, End::Interrupted);
            } else if (accepted) {
                window.opening_edge = effects.opening_edge;
                return true;
            }
        }
        return false;
    }
    template <class Port, class... Styles>
    std::optional<double> CWindowAnimations<Port, StyleList<Styles...>>::step_clock(
        Record &entry, const FrameStamp &stamp) {
        const double ELAPSED =
            entry.stepped ? std::chrono::duration<double>(stamp.now - entry.last_step).count() : 0;
        if (!std::isfinite(ELAPSED) || ELAPSED < 0 || ELAPSED > STALL_WINDOW_SECONDS) {
            return std::nullopt;
        }
        if (ELAPSED > STALL_SECONDS) {
            std::size_t kept = 0;
            for (std::size_t index = 0; index < entry.stall_count; ++index) {
                if (std::chrono::duration<double>(stamp.now - entry.stalls[index]).count() <=
                    STALL_WINDOW_SECONDS) {
                    entry.stalls[kept++] = entry.stalls[index];
                }
            }
            entry.stall_count = kept;
            entry.stalls[entry.stall_count++] = stamp.now;
            if (entry.stall_count == entry.stalls.size()) {
                return std::nullopt;
            }
        }
        entry.last_frame = stamp.frame;
        entry.last_step = stamp.now;
        entry.stepped = true;
        return ELAPSED;
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::present_mapped(
        Tracked &window, Record &entry, const Observation &observation) {
        // Effects may change body and capture; presentation and the next baseline use the reread.
        auto after = m_port.observe(window.window);
        if (after) {
            window.previous = *after;
        }
        const auto &presented = after ? *after : observation;
        const Vec2 OFFSET{presented.workspace_offset.x + presented.floating_offset.x,
            presented.workspace_offset.y + presented.floating_offset.y};
        const auto CAPTURE = detail::translated(presented.capture, OFFSET);
        if (presented.workspace_visible) {
            window.tail_capture = CAPTURE;
            window.tail_capture_valid = true;
        }
        entry.footprint = detail::translated(entry.current.bounds, {CAPTURE.x, CAPTURE.y});
        if (is_usable(entry.footprint)) {
            m_port.damage(entry.footprint);
        }
        if (!entry.current.running) {
            end(entry.handle, entry.current.interrupted ? End::Interrupted : End::Completed);
            return;
        }
        m_port.ensure_transformer(window.window, entry.handle);
        m_port.damage_window(window.window);
        for (const auto MONITOR : m_port.monitors_overlapping(entry.footprint)) {
            m_port.schedule(MONITOR);
        }
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::frame_mapped(
        Tracked &window, const FrameStamp &stamp) {
        auto observation = sample_window(window, stamp);
        if (!observation) {
            return;
        }
        std::optional<Motion> motion;
        const bool WORKSPACE_MOVEMENT = m_policy.workspace_movement && !observation->pinned;
        if (window.previous) {
            const bool SWEEP = ([&] {
                if constexpr (requires { Styles::SWEEPS_LARGE_MOVES; }) {
                    return Styles::SWEEPS_LARGE_MOVES && m_policy.style == Styles::KIND &&
                           m_policy.movement;
                }
                return false;
            }() || ...);
            motion = observe_motion(*window.previous,
                *observation,
                window.workspace_movement,
                WORKSPACE_MOVEMENT,
                SWEEP);
            if (motion && window.previous_sample != Clock::time_point{}) {
                motion->sample_seconds =
                    std::chrono::duration<double>(stamp.now - window.previous_sample).count();
            }
        }
        window.previous_sample = stamp.now;
        window.workspace_movement = WORKSPACE_MOVEMENT;
        if (begin_mapped(window, *observation, motion)) {
            observation = m_port.observe(window.window);
        }
        if (!observation) {
            return;
        }
        window.previous = *observation;
        if (!window.handle) {
            return;
        }
        const auto CURRENT_HANDLE = *window.handle;
        auto *entry = record(CURRENT_HANDLE);
        if (entry == nullptr) {
            return;
        }
        const auto ELAPSED = step_clock(*entry, stamp);
        if (!ELAPSED) {
            end(CURRENT_HANDLE, End::Interrupted);
            return;
        }
        // Hyprland's warp replaces the native goal with the last presented box.
        // Keep the layout target until the compositor supplies a different goal.
        const auto &GOAL = window.driven && detail::same_box(observation->goal_body, window.last_warp)
                               ? window.goal
                               : observation->goal_body;
        const Step STEP{.elapsed = *ELAPSED,
            .stalled = *ELAPSED > STALL_SECONDS,
            .body = observation->body,
            .capture = observation->capture,
            .goal_body = GOAL,
            .rest_capture = detail::rest_capture(*observation, GOAL),
            .screen = observation->screen,
            .work_area = observation->work_area,
            .motion = motion,
            .movement_enabled = m_policy.movement,
            .open_enabled = m_policy.opening,
            .held = observation->held,
            .workspace_visible = observation->workspace_visible,
            .workspace_moving = observation->workspace_moving,
            .monitor = observation->monitor,
            .workspace = observation->workspace,
            .pinned = observation->pinned,
            .window = window.id};
        Effects effects;
        const bool HAD_TAIL = entry->current.tail;
        if (is_usable(entry->footprint)) {
            m_port.damage(entry->footprint);
        }
        visit(CURRENT_HANDLE, [&]<class Style>(auto &slot, auto &resources) {
            entry->current = Style::advance(slot.state, STEP, resources, effects);
        });
        if (HAD_TAIL && !entry->current.tail) {
            m_port.release_mapped_image(CURRENT_HANDLE);
        }
        if (!apply(window, *observation, effects)) {
            end(CURRENT_HANDLE, End::Interrupted);
            return;
        }
        present_mapped(window, *entry, *observation);
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::refresh_fadeout(Record &entry) {
        if (entry.life == Life::DyingNative) {
            auto fadeout = m_port.fadeout_facts(entry.fadeout);
            if (!fadeout || !fadeout->alive) {
                return false;
            }
            entry.fadeout_info = *fadeout;
        } else if (entry.fadeout != 0) {
            auto fadeout = m_port.fadeout_facts(entry.fadeout);
            if (fadeout && fadeout->alive) {
                entry.fadeout_info = *fadeout;
            } else {
                m_port.suppress_native(entry.fadeout, false);
            }
        }
        return true;
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
        if (!refresh_fadeout(entry)) {
            end(CURRENT_HANDLE, End::Completed);
            return;
        }
        const auto ELAPSED = step_clock(entry, stamp);
        if (!ELAPSED) {
            end(CURRENT_HANDLE, End::Interrupted);
            return;
        }
        const auto &place = entry.close_place;
        const Step STEP{.elapsed = *ELAPSED,
            .stalled = *ELAPSED > STALL_SECONDS,
            .body = place.body,
            .capture = place.capture,
            .goal_body = place.goal_body,
            .rest_capture = place.rest_capture,
            .screen = place.screen,
            .work_area = place.work_area,
            .motion = std::nullopt,
            .dying = true,
            .monitor = entry.monitor,
            .workspace = place.workspace,
            .pinned = place.pinned};
        if (is_usable(entry.footprint)) {
            m_port.damage(entry.footprint);
        }
        Effects effects;
        visit(CURRENT_HANDLE, [&]<class Style>(auto &slot, auto &resources) {
            entry.current = Style::advance(slot.state, STEP, resources, effects);
        });
        const auto BOUNDS = dying_view(entry).bounds_device;
        entry.footprint = {place.screen.x + (BOUNDS.x / entry.captured_scale),
            place.screen.y + (BOUNDS.y / entry.captured_scale),
            BOUNDS.w / entry.captured_scale,
            BOUNDS.h / entry.captured_scale};
        if (is_usable(entry.footprint)) {
            m_port.damage(entry.footprint);
        }
        if (!entry.current.running) {
            end(CURRENT_HANDLE, entry.current.interrupted ? End::Interrupted : End::Completed);
            return;
        }
        m_port.schedule(stamp.monitor);
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::frame(const FrameStamp &stamp) {
        tick_screens(stamp);
        for (auto &window : m_tracked) {
            frame_mapped(window, stamp);
        }
        // Releases erase records: retain the reverse traversal and never reuse a released
        // reference.
        for (std::size_t index = m_entries.size(); index > 0; --index) {
            frame_dying(m_entries[index - 1], stamp);
        }
        std::erase_if(m_tracked, [&](const auto &window) {
            if (!window.expired || window.handle) {
                return false;
            }
            m_port.forget_window_reference(window.id);
            return true;
        });
    }

    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::active(Handle handle) const {
        return record(handle) != nullptr &&
               visit(handle, []<class Style>(const auto &, const auto &) {});
    }
    template <class Port, class... Styles>
    std::optional<Frame> CWindowAnimations<Port, StyleList<Styles...>>::view(Handle handle) const {
        const auto *entry = record(handle);
        if (entry == nullptr || !active(handle)) {
            return std::nullopt;
        }
        return entry->current;
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
    Box CWindowAnimations<Port, StyleList<Styles...>>::extents(
        Handle handle, const Box &capture) const {
        const auto STATE = view(handle);
        if (!STATE || !STATE->running) {
            return {};
        }
        return detail::translated(STATE->bounds, {capture.x, capture.y});
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::tail_allowed(
        Handle handle, const Canvas &canvas, bool retained_no_screen_share) const {
        const auto *entry = record(handle);
        if (!m_policy.enabled || m_policy.inert || entry == nullptr ||
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
        if (entry == nullptr || !entry->current.running) {
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
        isolated_draw.backdrop =
            entry->current.backdrop ? m_port.backdrop(handle, draw_info.canvas->pass()) : nullptr;
        const bool REAL_PASS = draw_info.real_pass && draw_info.canvas->pass() == PassKind::Live;
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
                        entry->retained_no_screen_share = observation->no_screen_share;
                    }
                    // Retained framebuffers are shared, not immutable pixel copies.
                    // A later private draw can update storage retained by an earlier frame.
                    entry->retained_no_screen_share |= observation->no_screen_share;
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
        isolated_draw.real_pass = REAL_PASS;
        bool drawn = false;
        InFlightDraw flight{*this, handle};
        visit(handle, [&]<class Style>(auto &slot, auto &resources) {
            if constexpr (requires { Style::warm(resources); }) {
                Style::warm(resources);
            }
            if (!CALLBACK.valid() || record(handle) == nullptr) {
                return;
            }
            if constexpr (SelfDrawn<Style, Canvas>) {
                drawn = Style::draw(slot.state, isolated_draw, resources);
            } else {
                auto &cache = m_port.visual_cache(handle);
                auto &buffers = draw_info.canvas->pass() == PassKind::Live ? cache.buffers
                                                                           : cache.scratch_buffers;
                const MeshTarget<typename Canvas::Vertex> TARGET{draw_info.source,
                    draw_info.output,
                    draw_info.image_origin,
                    draw_info.image_size,
                    draw_info.scale,
                    &buffers};
                const auto MESH = Style::mesh(slot.state, TARGET, resources);
                drawn = draw_info.canvas->submit_mesh(MESH, isolated_draw);
            }
        });
        // Style/canvas callbacks can erase or grow m_entries, including ending this handle.
        // Never consult the borrowed pre-draw entry after returning from them.
        if (!CALLBACK.valid()) {
            return false;
        }
        const auto *current = record(handle);
        if (current == nullptr) {
            return false;
        }
        if (!drawn && REAL_PASS && current->life == Life::DyingOwned) {
            end(handle, End::Interrupted);
        }
        return drawn;
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::before_window(
        WindowId window_id, Canvas &canvas) {
        auto *window = tracked(window_id);
        if (window != nullptr && window->handle) {
            const auto *entry = record(*window->handle);
            if (entry != nullptr && entry->life == Life::Mapped) {
                if (canvas.pass() == PassKind::Live) {
                    m_port.ensure_transformer(window->window, entry->handle);
                }
                if (entry->current.backdrop && canvas.pass() != PassKind::Inert) {
                    m_port.capture_backdrop(entry->handle, window->window, canvas);
                }
            }
        }
        auto observation = window != nullptr ? m_port.observe(window->window) : std::nullopt;
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
                    Styles::Screen::under_window(m_resources.template get<Styles>(), INFO);
                }
            }(),
            ...);
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::before_windows(
        MonitorId monitor, Canvas &canvas) {
        if (canvas.pass() != PassKind::Live) {
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
                    Styles::Screen::before_windows(m_resources.template get<Styles>(), INFO);
                }
            }(),
            ...);
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::after_windows(
        MonitorId monitor, Canvas &canvas) {
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
                    Styles::Screen::after_windows(m_resources.template get<Styles>(), INFO);
                }
            }(),
            ...);
    }
    template <class Port, class... Styles>
    bool CWindowAnimations<Port, StyleList<Styles...>>::needs_frame(MonitorId monitor) const {
        if (std::any_of(m_entries.begin(), m_entries.end(), [monitor](const auto &entry) {
                return entry.monitor == monitor && entry.life != Life::PendingCapture;
            })) {
            return true;
        }
        bool needed = false;
        (
            [&] {
                if constexpr (requires(const Styles::Resources &resources) {
                                  Styles::Screen::needs_frame(resources, monitor);
                              }) {
                    needed = needed || Styles::Screen::needs_frame(
                                           m_resources.template get<Styles>(), monitor);
                }
            }(),
            ...);
        return needed;
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::monitor_changed(MonitorId monitor) {
        if (needs_frame(monitor)) {
            if (const auto facts = m_port.monitor_facts(monitor)) {
                m_port.damage(facts->screen);
            }
            m_port.schedule(monitor);
        }
        for (std::size_t index = m_entries.size(); index > 0; --index) {
            const auto &entry = m_entries[index - 1];
            if (entry.monitor == monitor && entry.life != Life::Mapped) {
                end(entry.handle, End::Interrupted);
            }
        }
        (
            [&] {
                if constexpr (requires(Styles::Resources &resources) {
                                  Styles::Screen::clear(resources, std::optional<MonitorId>{});
                              }) {
                    Styles::Screen::clear(
                        m_resources.template get<Styles>(), std::optional<MonitorId>{monitor});
                }
            }(),
            ...);
        std::erase_if(
            m_screen_frames, [monitor](const auto &item) { return item.first == monitor; });
        m_port.recheck_scanout(monitor);
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::detach_transformers() {
        for (const auto &window : m_tracked) {
            if (window.handle) {
                m_port.detach_transformer(window.window);
            }
        }
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::end_all() {
        m_callbacks.invalidate();
        for (const auto &[monitor, frame] : m_screen_frames) {
            if (!needs_frame(monitor)) {
                continue;
            }
            if (const auto facts = m_port.monitor_facts(monitor)) {
                m_port.damage(facts->screen);
            }
            m_port.schedule(monitor);
        }
        while (!m_entries.empty()) {
            end(m_entries.back().handle, End::Interrupted);
        }
        for (const auto FADEOUT : m_tombstones) {
            m_port.suppress_native(FADEOUT, false);
        }
        m_tombstones.clear();
        (
            [&] {
                if constexpr (requires(Styles::Resources &resources) {
                                  Styles::Screen::clear(resources, std::optional<MonitorId>{});
                              }) {
                    Styles::Screen::clear(m_resources.template get<Styles>(), std::nullopt);
                }
            }(),
            ...);
        m_screen_frames.clear();
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::reload() {
        detach_transformers();
        end_all();
        for (auto &window : m_tracked) {
            window.previous = m_port.observe(window.window);
            window.previous_sample = {};
            window.opening_seen = window.previous && window.previous->animating_in;
            window.workspace_movement =
                m_policy.workspace_movement && window.previous && !window.previous->pinned;
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
            entry.no_screen_share};
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
