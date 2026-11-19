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
inline Box translated(Box box, Vec2 offset) { box.x += offset.x; box.y += offset.y; return box; }
inline Box rest_capture(const Observation &observation) {
    return {observation.goal_body.x + observation.capture.x - observation.body.x,
        observation.goal_body.y + observation.capture.y - observation.body.y,
        observation.goal_body.w + observation.capture.w - observation.body.w,
        observation.goal_body.h + observation.capture.h - observation.body.h};
}
}

template<class Port, class... Styles>
template<class Function>
bool CWindowAnimations<Port, StyleList<Styles...>>::visit(Handle handle, Function &&function) {
    bool found = false;
    ([&] {
        if (handle.style != Styles::KIND) return;
        auto &resources = m_resources.template get<Styles>();
        auto *slot = resources.animations.get({handle.index, handle.generation});
        if (slot) { function.template operator()<Styles>(*slot, resources); found = true; }
    }(), ...);
    return found;
}
template<class Port, class... Styles>
template<class Function>
bool CWindowAnimations<Port, StyleList<Styles...>>::visit(Handle handle, Function &&function) const {
    bool found = false;
    ([&] {
        if (handle.style != Styles::KIND) return;
        const auto &resources = m_resources.template get<Styles>();
        const auto *slot = resources.animations.get({handle.index, handle.generation});
        if (slot) { function.template operator()<Styles>(*slot, resources); found = true; }
    }(), ...);
    return found;
}
template<class Port, class... Styles>
template<class Function>
bool CWindowAnimations<Port, StyleList<Styles...>>::selected(Function &&function) {
    bool found = false;
    ([&] { if (m_policy.style == Styles::KIND) { function.template operator()<Styles>(m_resources.template get<Styles>()); found = true; } }(), ...);
    return found;
}
template<class Port, class... Styles>
auto CWindowAnimations<Port, StyleList<Styles...>>::tracked(WindowId id) -> Tracked * {
    const auto iter = std::find_if(m_tracked.begin(), m_tracked.end(), [id](const auto &item) { return item.id == id; });
    return iter == m_tracked.end() ? nullptr : &*iter;
}
template<class Port, class... Styles>
auto CWindowAnimations<Port, StyleList<Styles...>>::tracked(WindowId id) const -> const Tracked * {
    const auto iter = std::find_if(m_tracked.begin(), m_tracked.end(), [id](const auto &item) { return item.id == id; });
    return iter == m_tracked.end() ? nullptr : &*iter;
}
template<class Port, class... Styles>
auto CWindowAnimations<Port, StyleList<Styles...>>::record(Handle handle) -> Record * {
    const auto iter = std::find_if(m_entries.begin(), m_entries.end(), [handle](const auto &item) { return item.handle == handle; });
    return iter == m_entries.end() ? nullptr : &*iter;
}
template<class Port, class... Styles>
auto CWindowAnimations<Port, StyleList<Styles...>>::record(Handle handle) const -> const Record * {
    const auto iter = std::find_if(m_entries.begin(), m_entries.end(), [handle](const auto &item) { return item.handle == handle; });
    return iter == m_entries.end() ? nullptr : &*iter;
}
template<class Port, class... Styles>
Place CWindowAnimations<Port, StyleList<Styles...>>::place(const Observation &observation, Neighbourhood near, std::uint64_t seed) {
    return {observation.body, observation.capture, observation.goal_body, detail::rest_capture(observation),
        observation.screen, observation.work_area, near, seed, std::nullopt};
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::track(typename Port::WindowRef window) {
    const auto id = m_port.identity(window);
    if (!id || tracked(id)) return;
    m_tracked.push_back(Tracked{.window = std::move(window), .id = id, .previous = std::nullopt,
        .handle = std::nullopt, .opening_edge = std::nullopt});
}
template<class Port, class... Styles>
std::optional<Handle> CWindowAnimations<Port, StyleList<Styles...>>::acquire(Tracked &tracked_window, const Observation &observation, std::uint64_t seed) {
    std::optional<Handle> result;
    selected([&]<class Style>(auto &resources) {
        auto pool_handle = resources.animations.emplace(std::in_place, [&] { return Style::start(resources, seed); });
        Handle handle{Style::KIND, pool_handle.index, pool_handle.generation};
        try {
            m_port.acquire_visuals(handle);
            m_views.reserve(m_entries.size() + 1);
            m_tombstones.reserve(m_tombstones.size() + m_entries.size() + 1);
            m_entries.push_back(Record{.handle = handle, .origin = tracked_window.id,
                .monitor = observation.monitor, .workspace = observation.workspace});
        } catch (...) {
            m_port.release_visuals(handle);
            auto *slot = resources.animations.get(pool_handle);
            Style::end(slot->state, resources, End::Interrupted);
            resources.animations.erase(pool_handle);
            throw;
        }
        tracked_window.handle = handle;
        result = handle;
    });
    return result;
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::release_drive(Tracked &tracked_window) {
    if (!tracked_window.driven) return;
    const auto observed = m_port.observe(tracked_window.window);
    if (!observed && !m_port.window_alive(tracked_window.window)) {
        tracked_window.driven = false;
        return;
    }
    if (observed && is_usable(observed->goal_body) && !detail::same_box(observed->goal_body, tracked_window.last_warp))
        tracked_window.goal = observed->goal_body;
    m_port.warp_body(tracked_window.window, tracked_window.goal);
    tracked_window.driven = false;
    m_port.damage_window(tracked_window.window);
}
template<class Port, class... Styles>
bool CWindowAnimations<Port, StyleList<Styles...>>::apply(Tracked &tracked_window, const Observation &observation, const Effects &effects) {
    if ((effects.drive_body && effects.snap_to_goal) || (effects.drive_body && !is_usable(*effects.drive_body)) ||
        (effects.snap_to_goal && !is_usable(observation.goal_body))) return false;
    if (tracked_window.driven && !detail::same_box(observation.goal_body, tracked_window.last_warp))
        tracked_window.goal = observation.goal_body;
    if (effects.release_body) release_drive(tracked_window);
    if (effects.drive_body || effects.snap_to_goal) {
        if (!tracked_window.driven) tracked_window.goal = observation.goal_body;
        const auto box = effects.drive_body ? *effects.drive_body : observation.goal_body;
        m_port.warp_body(tracked_window.window, box);
        tracked_window.last_warp = box;
        tracked_window.driven = true;
    }
    if (effects.settle_native_open_alpha) m_port.settle_open_alpha(tracked_window.window);
    return true;
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::end(Handle handle, End why) {
    auto *entry = record(handle);
    if (!entry) return;
    const auto origin = entry->origin;
    const auto monitor = entry->monitor;
    const auto fadeout = entry->fadeout;
    const auto ownership = entry->ownership;
    const auto life = entry->life;
    if (is_usable(entry->footprint)) m_port.damage(entry->footprint);
    if (auto *window = tracked(origin); window && window->handle == handle) {
        release_drive(*window);
        m_port.detach_transformer(window->window);
        window->handle.reset();
    }
    visit(handle, [&]<class Style>(auto &slot, auto &resources) {
        Style::end(slot.state, resources, why);
        resources.animations.erase({handle.index, handle.generation});
    });
    if (life == Life::DyingOwned && why == End::Completed && fadeout && ownership == CloseStart::Owned) {
        auto facts = m_port.fadeout_facts(fadeout);
        if (facts && facts->alive) m_tombstones.push_back(fadeout);
        else m_port.suppress_native(fadeout, false);
    } else if (fadeout && ownership == CloseStart::Owned) m_port.suppress_native(fadeout, false);
    m_port.release_visuals(handle);
    m_port.recheck_scanout(monitor);
    std::erase_if(m_entries, [handle](const auto &item) { return item.handle == handle; });
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::configure(const Policy &policy) {
    const bool selection_changed = m_policy.style != policy.style;
    const bool stop_closes = !policy.enabled || policy.inert || !policy.closing || !policy.close_available;
    m_policy = policy;
    for (std::size_t index = m_entries.size(); index > 0; --index) {
        const auto item = m_entries[index - 1];
        if ((item.life == Life::Mapped && (selection_changed || !policy.enabled || policy.inert)) ||
            (item.life != Life::Mapped && stop_closes)) end(item.handle, End::Interrupted);
    }
    if (stop_closes) {
        for (auto fadeout : m_tombstones) m_port.suppress_native(fadeout, false);
        m_tombstones.clear();
    }
    ([&] {
        if constexpr (requires(typename Styles::Resources &resources, const ScreenFacts &facts) {
            Styles::Screen::tick(resources, facts);
        }) {
            const bool retained = std::any_of(m_entries.begin(), m_entries.end(), [](const auto &entry) {
                return entry.handle.style == Styles::KIND && entry.life != Life::Mapped;
            });
            auto &resources = m_resources.template get<Styles>();
            const bool screen_active = std::any_of(m_screen_frames.begin(), m_screen_frames.end(),
                [&](const auto &item) { return Styles::Screen::needs_frame(resources, item.first); });
            ScreenFacts facts{};
            facts.enabled = policy.enabled && !policy.inert && (policy.opening || policy.movement || policy.closing) &&
                (policy.style == Styles::KIND || retained || screen_active);
            Styles::Screen::tick(resources, facts);
        }
    }(), ...);
}
template<class Port, class... Styles>
template<class Style>
void CWindowAnimations<Port, StyleList<Styles...>>::update_settings(const typename Style::Settings &settings) {
    m_resources.template get<Style>().update(settings, Style::prepare_settings);
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::unmap(WindowId id) {
    auto *window = tracked(id);
    if (!window) return;
    if (window->handle) {
        auto *entry = record(*window->handle);
        if (entry && entry->life == Life::Mapped) end(entry->handle, End::Interrupted);
    }
    if (window->handle) return;
    m_port.forget_window_reference(id);
    std::erase_if(m_tracked, [id](const auto &item) { return item.id == id; });
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::destroyed(WindowId id) { unmap(id); }

template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::close(WindowId id, Clock::time_point now) {
    auto *window = tracked(id);
    if (!window) return;
    if (window->handle) {
        auto *existing = record(*window->handle);
        if (existing && existing->life != Life::Mapped) return;
    }
    auto observation = m_port.observe(window->window);
    if (!m_policy.enabled || m_policy.inert || !m_policy.closing || !m_policy.close_available ||
        !observation || !observation->mapped || observation->fullscreen ||
        !is_usable(observation->capture) || !is_usable(observation->body) ||
        !std::isfinite(observation->scale) || observation->scale <= 0) {
        if (window->handle) end(*window->handle, End::Interrupted);
        return;
    }
    auto handle = window->handle;
    if (!handle) handle = acquire(*window, *observation, 0);
    if (!handle) return;
    auto event_place = place(*observation, m_port.neighbourhood(window->window), ++m_seed);
    event_place.opening_edge = window->opening_edge;
    if (window->driven) {
        if (is_usable(observation->goal_body) && !detail::same_box(observation->goal_body, window->last_warp))
            window->goal = observation->goal_body;
        event_place.goal_body = window->goal;
        auto saved = *observation;
        saved.goal_body = window->goal;
        event_place.rest_capture = detail::rest_capture(saved);
    }
    CloseStart result = CloseStart::Declined;
    visit(*handle, [&]<class Style>(auto &slot, auto &resources) {
        result = Style::close(slot.state, event_place, resources);
    });
    if (result == CloseStart::Declined) { end(*handle, End::Interrupted); return; }
    auto *entry = record(*handle);
    if (entry->monitor != observation->monitor) entry->stall_count = 0;
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
    const auto offset = Vec2{observation->workspace_offset.x + observation->floating_offset.x - observation->screen.x,
        observation->workspace_offset.y + observation->floating_offset.y - observation->screen.y};
    const auto source = detail::translated(observation->capture, offset);
    entry->source_device = {source.x * observation->scale, source.y * observation->scale,
        source.w * observation->scale, source.h * observation->scale};
    m_port.detach_transformer(window->window);
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::bind_fadeout(WindowId id, const FadeoutFacts &fadeout, typename Port::RetainedImage image) {
    auto *window = tracked(id);
    if (!window || !window->handle) return;
    const auto handle = *window->handle;
    auto *entry = record(handle);
    if (!entry || entry->life != Life::PendingCapture) return;
    bool allows_blur = false;
    visit(handle, [&]<class Style>(auto &, auto &) { allows_blur = Style::DRAWS_UNDER_BLUR; });
    auto observation = m_port.observe(window->window);
    const auto count = std::count_if(m_entries.begin(), m_entries.end(), [](const auto &item) {
        return item.life == Life::DyingNative || item.life == Life::DyingOwned;
    });
    const bool already_bound = std::any_of(m_entries.begin(), m_entries.end(), [&](const auto &item) {
        return item.fadeout != 0 && item.fadeout == fadeout.fadeout;
    }) || std::find(m_tombstones.begin(), m_tombstones.end(), fadeout.fadeout) != m_tombstones.end();
    if (!observation || observation->monitor != entry->monitor || fadeout.monitor != entry->monitor ||
        observation->scale != entry->captured_scale || fadeout.scale != entry->captured_scale ||
        !std::isfinite(fadeout.scale) || fadeout.scale <= 0 ||
        fadeout.now < entry->pending_since ||
        std::chrono::duration<double>(fadeout.now - entry->pending_since).count() > 1.0 ||
        !fadeout.fadeout || !fadeout.alive || !fadeout.image_available || fadeout.alpha <= 0 ||
        !std::isfinite(fadeout.alpha) || !is_usable(fadeout.native_box) ||
        !std::isfinite(fadeout.image_size.x) || !std::isfinite(fadeout.image_size.y) ||
        fadeout.image_size.x <= 0 || fadeout.image_size.y <= 0 ||
        (fadeout.blurred && !allows_blur) || already_bound || count >= 16) {
        end(handle, End::Interrupted);
        return;
    }
    entry->captured_alpha = fadeout.alpha;
    try {
        m_port.bind_fadeout(handle, fadeout.fadeout, std::move(image));
    } catch (...) {
        end(handle, End::Interrupted);
        throw;
    }
    entry->fadeout = fadeout.fadeout;
    entry->fadeout_info = fadeout;
    entry->life = entry->ownership == CloseStart::Owned ? Life::DyingOwned : Life::DyingNative;
    if (entry->life == Life::DyingOwned) m_port.suppress_native(fadeout.fadeout, true);
    release_drive(*window);
    window->handle.reset();
    m_port.forget_window_reference(id);
    std::erase_if(m_tracked, [id](const auto &item) { return item.id == id; });
    m_port.schedule(entry->monitor);
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::frame(const FrameStamp &stamp) {
    auto clock = std::find_if(m_screen_frames.begin(), m_screen_frames.end(), [&](const auto &item) {
        return item.first == stamp.monitor;
    });
    const bool screen_tick = clock == m_screen_frames.end() || clock->second != stamp.frame;
    if (screen_tick) {
        if (clock == m_screen_frames.end()) m_screen_frames.emplace_back(stamp.monitor, stamp.frame);
        else clock->second = stamp.frame;
        if (auto facts = m_port.monitor_facts(stamp.monitor)) {
            facts->now = stamp.now;
            facts->frame = stamp.frame;
            ([&] {
                if constexpr (requires(typename Styles::Resources &resources, const ScreenFacts &screen) {
                    Styles::Screen::tick(resources, screen);
                }) {
                    const bool retained = std::any_of(m_entries.begin(), m_entries.end(), [](const auto &entry) {
                        return entry.handle.style == Styles::KIND && entry.life != Life::Mapped;
                    });
                    auto &resources = m_resources.template get<Styles>();
                    const bool needed_before = Styles::Screen::needs_frame(resources, stamp.monitor);
                    facts->enabled = m_policy.enabled && !m_policy.inert &&
                        (m_policy.opening || m_policy.movement || m_policy.closing) &&
                        (m_policy.style == Styles::KIND || retained ||
                            needed_before);
                    Styles::Screen::tick(resources, *facts);
                    const bool needed_after = Styles::Screen::needs_frame(resources, stamp.monitor);
                    if (needed_before || needed_after) m_port.damage(facts->screen);
                    if (needed_before != needed_after) m_port.recheck_scanout(stamp.monitor);
                    if (needed_after) m_port.schedule(stamp.monitor);
                }
            }(), ...);
        }
    }
    for (auto &window : m_tracked) {
        auto observation = m_port.observe(window.window);
        window.expired = !observation && !m_port.window_alive(window.window);
        if (!observation && !window.expired) {
            if (window.handle) {
                if (auto *entry = record(*window.handle); entry && entry->life == Life::Mapped) {
                    entry->stepped = false;
                    entry->stall_count = 0;
                }
            }
            window.previous.reset();
            continue;
        }
        bool hidden_tail = false;
        if (observation && observation->hidden && !observation->workspace_visible &&
            m_policy.workspace_movement && !observation->pinned && window.handle) {
            const auto *entry = record(*window.handle);
            hidden_tail = entry && entry->life == Life::Mapped && entry->current.tail;
        }
        if (!observation || !observation->mapped || observation->fullscreen ||
            (observation->hidden && !hidden_tail) || !is_usable(observation->body) ||
            !is_usable(observation->capture)) {
            if (window.handle) {
                auto *entry = record(*window.handle);
                if (entry && entry->life == Life::Mapped) end(entry->handle, End::Interrupted);
            }
            window.previous.reset();
            continue;
        }
        if (observation->monitor != stamp.monitor) continue;
        if (window.handle) {
            auto *entry = record(*window.handle);
            if (entry && entry->life != Life::Mapped) continue;
            if (entry && entry->monitor != stamp.monitor) {
                entry->monitor = stamp.monitor;
                entry->stepped = false;
                entry->stall_count = 0;
                window.previous = *observation;
            }
            if (entry && entry->stepped && entry->last_frame == stamp.frame) continue;
        }
        std::optional<Motion> motion;
        const bool workspace_movement = m_policy.workspace_movement && !observation->pinned;
        if (window.previous)
            motion = observe_motion(*window.previous, *observation, window.workspace_movement, workspace_movement);
        window.workspace_movement = workspace_movement;
        const bool opening = observation->animating_in && !window.opening_seen;
        if (opening) window.opening_edge.reset();
        window.opening_seen = observation->animating_in;
        bool acquired = false;
        if (!window.handle && m_policy.enabled && !m_policy.inert) {
            bool moves = false;
            if (motion && m_policy.movement) selected([&]<class Style>(auto &resources) {
                if constexpr (requires { Style::moves(*motion, resources); }) moves = Style::moves(*motion, resources);
                else moves = motion->translated;
            });
            if ((opening && m_policy.opening) || moves)
                acquired = acquire(window, *observation, ++m_seed).has_value();
        }
        if (window.handle && opening && m_policy.opening && m_policy.enabled && !m_policy.inert) {
            const auto handle = *window.handle;
            Effects effects;
            bool accepted = false;
            const auto initial = place(*observation, m_port.neighbourhood(window.window), m_seed);
            visit(handle, [&]<class Style>(auto &slot, auto &resources) {
                accepted = Style::open(slot.state, initial, resources, effects);
            });
            if (!accepted && acquired) end(handle, End::Interrupted);
            else if (accepted && !apply(window, *observation, effects)) end(handle, End::Interrupted);
            else if (accepted) {
                window.opening_edge = effects.opening_edge;
                observation = m_port.observe(window.window);
            }
        }
        if (!observation) continue;
        window.previous = *observation;
        if (!window.handle) continue;
        auto handle = *window.handle;
        auto *entry = record(handle);
        if (!entry) continue;
        double elapsed = entry->stepped ? std::chrono::duration<double>(stamp.now - entry->last_step).count() : 0;
        if (!std::isfinite(elapsed) || elapsed < 0 || elapsed > STALL_WINDOW_SECONDS) { end(handle, End::Interrupted); continue; }
        const bool stalled = elapsed > STALL_SECONDS;
        if (stalled) {
            std::size_t kept = 0;
            for (std::size_t index = 0; index < entry->stall_count; ++index)
                if (std::chrono::duration<double>(stamp.now - entry->stalls[index]).count() <= STALL_WINDOW_SECONDS)
                    entry->stalls[kept++] = entry->stalls[index];
            entry->stall_count = kept;
            entry->stalls[entry->stall_count++] = stamp.now;
            if (entry->stall_count == entry->stalls.size()) { end(handle, End::Interrupted); continue; }
        }
        entry->last_frame = stamp.frame;
        entry->last_step = stamp.now;
        entry->stepped = true;
        Step step{.elapsed = elapsed, .stalled = stalled, .body = observation->body,
            .capture = observation->capture, .goal_body = observation->goal_body,
            .rest_capture = detail::rest_capture(*observation), .screen = observation->screen,
            .work_area = observation->work_area, .motion = motion, .movement_enabled = m_policy.movement,
            .open_enabled = m_policy.opening, .held = observation->held,
            .workspace_visible = observation->workspace_visible, .workspace_moving = observation->workspace_moving};
        Effects effects;
        const bool had_tail = entry->current.tail;
        if (is_usable(entry->footprint)) m_port.damage(entry->footprint);
        visit(handle, [&]<class Style>(auto &slot, auto &resources) {
            entry->current = Style::advance(slot.state, step, resources, effects);
        });
        if (had_tail && !entry->current.tail) m_port.release_mapped_image(handle);
        if (!apply(window, *observation, effects)) { end(handle, End::Interrupted); continue; }
        auto after = m_port.observe(window.window);
        if (after) window.previous = *after;
        const auto &presented = after ? *after : *observation;
        const Vec2 offset{presented.workspace_offset.x + presented.floating_offset.x,
            presented.workspace_offset.y + presented.floating_offset.y};
        const auto capture = detail::translated(presented.capture, offset);
        if (presented.workspace_visible) {
            window.tail_capture = capture;
            window.tail_capture_valid = true;
        }
        entry->footprint = detail::translated(entry->current.bounds, {capture.x, capture.y});
        if (is_usable(entry->footprint)) m_port.damage(entry->footprint);
        if (!entry->current.running) { end(handle, End::Completed); continue; }
        m_port.ensure_transformer(window.window, handle);
        m_port.damage_window(window.window);
        for (auto monitor : m_port.monitors_overlapping(entry->footprint))
            m_port.schedule(monitor);
    }
    for (std::size_t index = m_entries.size(); index > 0; --index) {
        auto &entry = m_entries[index - 1];
        if (entry.life == Life::Mapped || entry.monitor != stamp.monitor) continue;
        const auto handle = entry.handle;
        if (entry.life == Life::PendingCapture) {
            if (std::chrono::duration<double>(stamp.now - entry.pending_since).count() > 1.0)
                end(handle, End::Interrupted);
            continue;
        }
        if (entry.stepped && entry.last_frame == stamp.frame) continue;
        if (entry.life == Life::DyingNative) {
            auto fadeout = m_port.fadeout_facts(entry.fadeout);
            if (!fadeout || !fadeout->alive) { end(handle, End::Completed); continue; }
            entry.fadeout_info = *fadeout;
        } else if (entry.fadeout) {
            auto fadeout = m_port.fadeout_facts(entry.fadeout);
            if (fadeout && fadeout->alive) entry.fadeout_info = *fadeout;
            else {
                m_port.suppress_native(entry.fadeout, false);
            }
        }
        const double elapsed = entry.stepped ? std::chrono::duration<double>(stamp.now - entry.last_step).count() : 0;
        if (!std::isfinite(elapsed) || elapsed < 0 || elapsed > STALL_WINDOW_SECONDS) { end(handle, End::Interrupted); continue; }
        const bool stalled = elapsed > STALL_SECONDS;
        if (stalled) {
            std::size_t kept = 0;
            for (std::size_t item = 0; item < entry.stall_count; ++item)
                if (std::chrono::duration<double>(stamp.now - entry.stalls[item]).count() <= STALL_WINDOW_SECONDS)
                    entry.stalls[kept++] = entry.stalls[item];
            entry.stall_count = kept;
            entry.stalls[entry.stall_count++] = stamp.now;
            if (entry.stall_count == entry.stalls.size()) { end(handle, End::Interrupted); continue; }
        }
        entry.last_frame = stamp.frame;
        entry.last_step = stamp.now;
        entry.stepped = true;
        const auto &place = entry.close_place;
        Step step{.elapsed = elapsed, .stalled = stalled, .body = place.body, .capture = place.capture,
            .goal_body = place.goal_body, .rest_capture = place.rest_capture, .screen = place.screen,
            .work_area = place.work_area, .motion = std::nullopt, .dying = true};
        if (is_usable(entry.footprint)) m_port.damage(entry.footprint);
        Effects effects;
        visit(handle, [&]<class Style>(auto &slot, auto &resources) {
            entry.current = Style::advance(slot.state, step, resources, effects);
        });
        const auto bounds = dying_view(entry).bounds_device;
        entry.footprint = {place.screen.x + bounds.x / entry.captured_scale,
            place.screen.y + bounds.y / entry.captured_scale,
            bounds.w / entry.captured_scale, bounds.h / entry.captured_scale};
        if (is_usable(entry.footprint)) m_port.damage(entry.footprint);
        if (!entry.current.running) { end(handle, End::Completed); continue; }
        m_port.schedule(stamp.monitor);
    }
    std::erase_if(m_tracked, [&](const auto &window) {
        if (!window.expired || window.handle) return false;
        m_port.forget_window_reference(window.id);
        return true;
    });
}

template<class Port, class... Styles>
bool CWindowAnimations<Port, StyleList<Styles...>>::active(Handle handle) const {
    return record(handle) && visit(handle, []<class Style>(const auto &, const auto &) {});
}
template<class Port, class... Styles>
std::optional<Frame> CWindowAnimations<Port, StyleList<Styles...>>::view(Handle handle) const {
    const auto *entry = record(handle);
    if (!entry || !active(handle)) return std::nullopt;
    return entry->current;
}
template<class Port, class... Styles>
std::optional<Handle> CWindowAnimations<Port, StyleList<Styles...>>::pending_for(WindowId id) const {
    for (const auto &entry : m_entries)
        if (entry.origin == id && entry.life == Life::PendingCapture) return entry.handle;
    return std::nullopt;
}
template<class Port, class... Styles>
bool CWindowAnimations<Port, StyleList<Styles...>>::suppresses(std::uint64_t fadeout) const {
    if (!fadeout) return false;
    if (std::find(m_tombstones.begin(), m_tombstones.end(), fadeout) != m_tombstones.end()) return true;
    return std::any_of(m_entries.begin(), m_entries.end(), [fadeout](const auto &entry) {
        return entry.fadeout == fadeout && entry.life == Life::DyingOwned;
    });
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::native_finished(std::uint64_t fadeout) {
    std::erase(m_tombstones, fadeout);
    m_port.suppress_native(fadeout, false);
    for (std::size_t index = m_entries.size(); index > 0; --index) {
        auto &entry = m_entries[index - 1];
        if (entry.fadeout == fadeout && entry.life == Life::DyingNative)
            end(entry.handle, End::Completed);
    }
}
template<class Port, class... Styles>
Box CWindowAnimations<Port, StyleList<Styles...>>::extents(Handle handle, const Box &capture) const {
    const auto state = view(handle);
    if (!state || !state->running) return {};
    return detail::translated(state->bounds, {capture.x, capture.y});
}
template<class Port, class... Styles>
bool CWindowAnimations<Port, StyleList<Styles...>>::draw(Handle handle, const Draw<Canvas> &draw_info) {
    const auto *entry = record(handle);
    if (!entry || !entry->current.running) return false;
    auto isolated_draw = draw_info;
    if (!isolated_draw.monitor) isolated_draw.monitor = entry->monitor;
    isolated_draw.window = entry->origin;
    if (!isolated_draw.frame) isolated_draw.frame = entry->last_frame;
    isolated_draw.backdrop = entry->current.backdrop
        ? m_port.backdrop(handle, draw_info.canvas.pass()) : nullptr;
    const bool real_pass = draw_info.real_pass && draw_info.canvas.pass() == PassKind::Live;
    if (entry->life == Life::Mapped && entry->current.tail && real_pass) {
        if (const auto *window = tracked(entry->origin); window && window->previous &&
            window->previous->workspace_visible) {
            const auto &observation = *window->previous;
            const Vec2 offset{observation.workspace_offset.x + observation.floating_offset.x,
                observation.workspace_offset.y + observation.floating_offset.y};
            m_port.retain_mapped_image(handle, draw_info.image, draw_info.image_origin,
                detail::translated(observation.capture, offset), isolated_draw.frame);
        }
    }
    if (!is_usable(entry->current.bounds)) return true;
    isolated_draw.real_pass = real_pass;
    bool drawn = false;
    visit(handle, [&]<class Style>(auto &slot, auto &resources) {
        if constexpr (requires { Style::warm(resources); }) Style::warm(resources);
        if constexpr (SelfDrawn<Style, Canvas>) drawn = Style::draw(slot.state, isolated_draw, resources);
        else {
            auto &cache = m_port.visual_cache(handle);
            auto &buffers = draw_info.canvas.pass() == PassKind::Live ? cache.buffers : cache.scratch_buffers;
            MeshTarget<typename Canvas::Vertex> target{draw_info.source, draw_info.output,
                draw_info.image_origin, draw_info.image_size, draw_info.scale, buffers};
            const auto mesh = Style::mesh(slot.state, target, resources);
            drawn = draw_info.canvas.submit_mesh(mesh, isolated_draw);
        }
    });
    return drawn;
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::before_window(WindowId id, Canvas &canvas) {
    auto *window = tracked(id);
    if (window && window->handle) {
        const auto *entry = record(*window->handle);
        if (entry && entry->life == Life::Mapped) {
            if (canvas.pass() == PassKind::Live) m_port.ensure_transformer(window->window, entry->handle);
            if (entry->current.backdrop && canvas.pass() != PassKind::Inert)
                m_port.capture_backdrop(entry->handle, window->window, canvas);
        }
    }
    auto observation = window ? m_port.observe(window->window) : std::nullopt;
    if (!observation) return;
    auto facts = m_port.monitor_facts(observation->monitor);
    if (!facts) return;
    ([&] {
        if constexpr (requires(typename Styles::Resources &resources, const ScreenDraw<Canvas> &draw_info) {
            Styles::Screen::under_window(resources, draw_info);
        }) {
            ScreenDraw<Canvas> info{canvas, *facts, id, canvas.pass() == PassKind::Live};
            Styles::Screen::under_window(m_resources.template get<Styles>(), info);
        }
    }(), ...);
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::after_windows(MonitorId monitor, Canvas &canvas) {
    auto facts = m_port.monitor_facts(monitor);
    if (!facts) return;
    for (auto &window : m_tracked) {
        if (!window.handle || !window.previous || window.previous->monitor != monitor ||
            window.previous->workspace_visible) continue;
        const auto *entry = record(*window.handle);
        if (!entry || !entry->current.tail) continue;
        auto retained = m_port.retained_mapped_image(entry->handle);
        if (!retained || !retained->image) continue;
        const auto &capture = window.tail_capture_valid ? window.tail_capture : retained->capture;
        const auto &bounds = entry->current.bounds;
        const Box output{(capture.x + bounds.x - facts->screen.x) * facts->scale,
            (capture.y + bounds.y - facts->screen.y) * facts->scale,
            bounds.w * facts->scale, bounds.h * facts->scale};
        const Box source{(retained->capture.x - facts->screen.x) * facts->scale,
            (retained->capture.y - facts->screen.y) * facts->scale,
            retained->capture.w * facts->scale, retained->capture.h * facts->scale};
        const TailDraw info{source, output, output, retained->image_origin, retained->image_size,
            facts->scale, 1, monitor, facts->frame};
        if (is_usable(output)) m_port.queue_tail(entry->handle, canvas, *retained->image, info);
    }
    ([&] {
        if constexpr (requires(typename Styles::Resources &resources, const ScreenDraw<Canvas> &draw_info) {
            Styles::Screen::after_windows(resources, draw_info);
        }) {
            ScreenDraw<Canvas> info{canvas, *facts, 0, canvas.pass() == PassKind::Live};
            Styles::Screen::after_windows(m_resources.template get<Styles>(), info);
        }
    }(), ...);
}
template<class Port, class... Styles>
bool CWindowAnimations<Port, StyleList<Styles...>>::needs_frame(MonitorId monitor) const {
    if (std::any_of(m_entries.begin(), m_entries.end(), [monitor](const auto &entry) {
        return entry.monitor == monitor && entry.life != Life::PendingCapture;
    })) return true;
    bool needed = false;
    ([&] {
        if constexpr (requires(const typename Styles::Resources &resources) { Styles::Screen::needs_frame(resources, monitor); })
            needed = needed || Styles::Screen::needs_frame(m_resources.template get<Styles>(), monitor);
    }(), ...);
    return needed;
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::monitor_changed(MonitorId monitor) {
    for (std::size_t index = m_entries.size(); index > 0; --index) {
        const auto &entry = m_entries[index - 1];
        if (entry.monitor == monitor && entry.life != Life::Mapped) end(entry.handle, End::Interrupted);
    }
    ([&] {
        if constexpr (requires(typename Styles::Resources &resources) { Styles::Screen::clear(resources, std::optional<MonitorId>{}); })
            Styles::Screen::clear(m_resources.template get<Styles>(), std::optional<MonitorId>{monitor});
    }(), ...);
    std::erase_if(m_screen_frames, [monitor](const auto &item) { return item.first == monitor; });
    m_port.recheck_scanout(monitor);
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::detach_transformers() {
    for (auto &window : m_tracked) if (window.handle) m_port.detach_transformer(window.window);
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::end_all() {
    while (!m_entries.empty()) end(m_entries.back().handle, End::Interrupted);
    for (auto fadeout : m_tombstones) m_port.suppress_native(fadeout, false);
    m_tombstones.clear();
    ([&] {
        if constexpr (requires(typename Styles::Resources &resources) { Styles::Screen::clear(resources, std::optional<MonitorId>{}); })
            Styles::Screen::clear(m_resources.template get<Styles>(), std::nullopt);
    }(), ...);
    m_screen_frames.clear();
}
template<class Port, class... Styles>
void CWindowAnimations<Port, StyleList<Styles...>>::reload() {
    detach_transformers();
    end_all();
    for (auto &window : m_tracked) {
        window.previous = m_port.observe(window.window);
        window.opening_seen = window.previous && window.previous->animating_in;
        window.workspace_movement = m_policy.workspace_movement && window.previous && !window.previous->pinned;
    }
}
template<class Port, class... Styles>
DyingView CWindowAnimations<Port, StyleList<Styles...>>::dying_view(const Record &entry) const {
    const auto &facts = entry.fadeout_info;
    Box render = facts.native_box;
    bool fixed = false;
    visit(entry.handle, [&]<class Style>(const auto &, const auto &) { fixed = Style::FIXED_CLOSE_PLACEMENT; });
    if (entry.life == Life::DyingOwned) {
        render = entry.source_device;
        if (!fixed) {
            render.x = facts.native_box.x + (facts.native_box.w - render.w) / 2;
            render.y = facts.native_box.y + (facts.native_box.h - render.h) / 2;
        }
    }
    const auto bounds = entry.current.bounds;
    return {entry.handle, entry.monitor, entry.workspace, entry.fadeout, entry.ownership,
        entry.source_device, render,
        {render.x + bounds.x * facts.scale, render.y + bounds.y * facts.scale,
            bounds.w * facts.scale, bounds.h * facts.scale},
        facts.image_size, facts.scale, entry.life == Life::DyingOwned ? entry.captured_alpha : facts.alpha,
        facts.workspace_bound, entry.no_screen_share};
}
template<class Port, class... Styles>
std::span<const DyingView> CWindowAnimations<Port, StyleList<Styles...>>::dying_views() const {
    m_views.clear();
    for (const auto &entry : m_entries) {
        if (entry.life != Life::DyingOwned && entry.life != Life::DyingNative) continue;
        m_views.push_back(dying_view(entry));
    }
    return m_views;
}
} // namespace hyprimate::animation
