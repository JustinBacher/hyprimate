#pragma once

#include <algorithm>
#include <utility>

#include "WindowTracker.hpp"
#include "Profiling.hpp"
#include "WindowAnimationClock.hpp"
#include "WindowAnimationGeometry.hpp"

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
    bool CWindowAnimations<Port, StyleList<Styles...>>::selected(config::Style kind, Function &&function) {
        bool found = false;
        (
            [&] {
                if (kind == Styles::KIND) {
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
        Tracked &tracked_window, const Observation &observation, std::uint64_t seed, config::Style kind) {
        std::optional<Handle> result;
        selected(kind, [&]<class Style>(auto &resources) {
            const perf::CpuScope PROFILE{profile_key(Style::KIND), perf::Phase::Setup};
            const auto POOL_HANDLE = resources.animations.emplace(
                std::in_place, [&] { return Style::start(resources, seed); });
            const Handle ENTRY_HANDLE{Style::KIND, POOL_HANDLE.index, POOL_HANDLE.generation};
            try {
                m_port.acquire_visuals(ENTRY_HANDLE);
                m_views.reserve(m_entries.size() + 1);
                m_tombstones.reserve(m_tombstones.size() + m_entries.size() + 1);
                constexpr bool TRACKS_PUBLICATION = requires(typename Style::State &state) {
                    Style::invalidate_mapped_publication(state, resources);
                };
                constexpr bool PUBLIC_MAPPED_ONLY = [] {
                    if constexpr (requires { Style::PUBLIC_MAPPED_ONLY; }) {
                        return Style::PUBLIC_MAPPED_ONLY;
                    }
                    return false;
                }();
                constexpr bool ALLOWS_MAPPED_BLUR = [] {
                    if constexpr (requires { Style::DRAWS_MAPPED_UNDER_BLUR; }) {
                        return Style::DRAWS_MAPPED_UNDER_BLUR;
                    }
                    return true;
                }();
                constexpr bool PRESERVE_ON_RELOAD = [] {
                    if constexpr (requires { Style::PRESERVE_ON_RELOAD; }) {
                        return Style::PRESERVE_ON_RELOAD;
                    }
                    return false;
                }();
                m_entries.push_back(Record{.origin = tracked_window.id,
                    .monitor = observation.monitor,
                    .workspace = observation.workspace,
                    .last_step = {},
                    .pending_since = {},
                    .handle = ENTRY_HANDLE,
                    .tracks_mapped_publication = TRACKS_PUBLICATION,
                    .public_mapped_only = PUBLIC_MAPPED_ONLY,
                    .allows_mapped_blur = ALLOWS_MAPPED_BLUR,
                    .preserve_on_reload = PRESERVE_ON_RELOAD});
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
        const bool PUBLIC_OWNED = entry->public_close_only && LIFE == Life::DyingOwned;
        if (is_usable(entry->footprint)) {
            m_port.damage(entry->footprint);
        }
        if (PUBLIC_OWNED && (why == End::Interrupted || !is_usable(entry->footprint))) {
            const auto &screen = entry->close_place.screen;
            const auto &source = entry->source_device;
            const auto SCALE = entry->captured_scale;
            const Box NATIVE{screen.x + source.x / SCALE,
                screen.y + source.y / SCALE,
                source.w / SCALE,
                source.h / SCALE};
            if (is_usable(NATIVE)) {
                m_port.damage(NATIVE);
            }
        }
        if (auto *window = tracked(ORIGIN); window != nullptr && window->handle == handle) {
            release_drive(*window);
            m_port.detach_transformer(window->window);
            window->handle.reset();
            window->exclusive_open_goal.reset();
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
            if (facts && facts->alive &&
                (!PUBLIC_OWNED || (!facts->no_screen_share && m_port.session_unlocked()))) {
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
                    ((SELECTION_CHANGED && !ITEM.preserve_on_reload) ||
                        !policy.enabled || policy.inert ||
                        (ITEM.mapped_motion && !policy.movement) ||
                        (ITEM.preserve_on_reload && !ITEM.mapped_motion && !policy.opening))) ||
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
                    const perf::CpuScope PROFILE{profile_key(Styles::KIND), perf::Phase::Update};
                    Styles::Screen::tick(resources, facts);
                }
            }(),
            ...);
    }
    template <class Port, class... Styles>
    template <class Style>
    void CWindowAnimations<Port, StyleList<Styles...>>::update_settings(
        const Style::Settings &settings) {
        const perf::CpuScope PROFILE{profile_key(Style::KIND), perf::Phase::Setup};
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
            const perf::CpuScope PROFILE{profile_key(Style::KIND), perf::Phase::Update};
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
    bool CWindowAnimations<Port, StyleList<Styles...>>::public_mapped_source_allowed(
        const Tracked &window, const Observation &observation) const {
        return observation.window == window.id && observation.mapped && !observation.hidden &&
               observation.workspace_visible && !observation.no_screen_share &&
               m_port.tail_source_allowed(window.window, window.id, true);
    }
    template <class Port, class... Styles>
    std::optional<MotionKind> CWindowAnimations<Port, StyleList<Styles...>>::motion_kind(
        const Tracked &window, const Observation &observation, const std::optional<Motion> &motion) const {
        if (!m_policy.enabled || m_policy.inert || !m_policy.movement) return std::nullopt;
        if (observation.resize_held) return MotionKind::Resize;
        if (observation.held) return MotionKind::Move;
        if (!motion) return std::nullopt;

        // Native open interpolation is not a move/resize request. A grab,
        // workspace shift, or retarget can still take over in this very sample.
        const Box *open_goal = window.exclusive_open_goal ? &*window.exclusive_open_goal :
            window.native_open_goal && (observation.animating_in || window.opening_seen)
                ? &*window.native_open_goal : nullptr;
        if (open_goal && !motion->workspace_shifted &&
            detail::same_box(observation.goal_body, *open_goal) &&
            (observation.layout_animated || (window.previous && window.previous->layout_animated))) {
            return std::nullopt;
        }
        if (motion->resized) return MotionKind::Resize;
        if (motion->translated) return MotionKind::Move;
        return std::nullopt;
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
        reset(false);
    }
    template <class Port, class... Styles>
    void CWindowAnimations<Port, StyleList<Styles...>>::reset(bool preserve_pinned) {
        const bool HAS_PINNED = preserve_pinned &&
            std::ranges::any_of(m_entries, [](const auto &entry) { return entry.preserve_on_reload; });
        if (!HAS_PINNED) {
            m_callbacks.invalidate();
        }
        for (const auto &[monitor, frame] : m_screen_frames) {
            if (!needs_frame(monitor)) {
                continue;
            }
            if (const auto facts = m_port.monitor_facts(monitor)) {
                m_port.damage(facts->screen);
            }
            m_port.schedule(monitor);
        }
        for (std::size_t index = m_entries.size(); index > 0; --index) {
            const auto &entry = m_entries[index - 1];
            if (!preserve_pinned || !entry.preserve_on_reload) {
                end(entry.handle, End::Interrupted);
            }
        }
        // Completed owned closes still hide the native fade. A config reload
        // cannot resurrect one; only native completion or an explicit stop can.
        if (!preserve_pinned) {
            for (const auto FADEOUT : m_tombstones) {
                m_port.suppress_native(FADEOUT, false);
            }
            m_tombstones.clear();
        }
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
        reset(true);
        for (auto &window : m_tracked) {
            if (window.handle) {
                continue;
            }
            window.previous = m_port.observe(window.window);
            window.previous_sample = {};
            window.opening_seen = window.previous && window.previous->animating_in;
            window.native_open_goal = window.opening_seen ? std::optional{window.previous->goal_body} : std::nullopt;
            window.rejected_motion.reset();
            window.workspace_movement =
                m_policy.workspace_movement && window.previous && !window.previous->pinned;
        }
    }
}  // namespace hyprimate::animation
