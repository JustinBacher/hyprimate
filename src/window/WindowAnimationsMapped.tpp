#pragma once

#include <chrono>
#include <optional>

namespace hyprimate::animation {
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
    bool CWindowAnimations<Port, StyleList<Styles...>>::begin_mapped(
        Tracked &window, const Observation &observation, const std::optional<Motion> &motion,
        std::optional<MotionKind> active_motion) {
        const bool OPENING = observation.animating_in && !window.opening_seen;
        if (OPENING) {
            window.opening_edge.reset();
            window.native_open_goal = observation.goal_body;
        }
        if (!observation.animating_in) window.native_open_goal.reset();
        window.opening_seen = observation.animating_in;
        bool open_override = OPENING && m_policy.opening && m_policy.enabled && !m_policy.inert &&
                             m_policy.open_style.has_value();
        const auto OPEN_KIND = m_policy.open_style.value_or(m_policy.style);
        const auto mapped_allowed = [&](config::Style kind, const Record *entry,
                                        std::optional<MotionKind> starting_motion = {}) {
            bool allowed = true;
            selected(kind, [&]<class Style>(auto &resources) {
                if constexpr (requires { Style::PUBLIC_MAPPED_ONLY; }) {
                    if (Style::PUBLIC_MAPPED_ONLY) {
                        allowed = public_mapped_source_allowed(window, observation);
                    }
                }
                if constexpr (requires { Style::NATIVE_PROTECTED_MAPPED; }) {
                    allowed &= !Style::NATIVE_PROTECTED_MAPPED || !observation.no_screen_share;
                }
                if (allowed && entry) {
                    allowed = entry->allows_mapped_blur || !m_port.mapped_blur_required(window.window);
                } else if (allowed && starting_motion) {
                    if constexpr (requires { Style::can_begin_motion(resources, *starting_motion, false); }) {
                        allowed = Style::can_begin_motion(resources, *starting_motion,
                            m_port.mapped_blur_required(window.window));
                    } else {
                        allowed = false;
                    }
                } else if constexpr (requires { Style::DRAWS_MAPPED_UNDER_BLUR; }) {
                    if (allowed && !Style::DRAWS_MAPPED_UNDER_BLUR &&
                        m_port.mapped_blur_required(window.window)) {
                        if constexpr (requires { Style::supports_open_blur(resources); }) {
                            allowed = Style::supports_open_blur(resources);
                        } else {
                            allowed = false;
                        }
                    }
                }
            });
            return allowed;
        };
        const auto base_moves = [&] {
            bool moves = false;
            if (motion && m_policy.movement && m_policy.enabled && !m_policy.inert) {
                selected(m_policy.style, [&]<class Style>(auto &resources) {
                    if constexpr (requires { Style::moves(*motion, resources); }) {
                        moves = Style::moves(*motion, resources);
                    } else {
                        moves = motion->translated;
                    }
                });
            }
            return moves;
        };
        const auto independent_motion = [&](const Box &opening_goal) {
            // Ignore native pop-in/slide interpolation toward the original layout
            // goal, not an actual drag, workspace slide or layout retarget.
            return motion && (observation.held || observation.resize_held ||
                motion->workspace_shifted || !detail::same_box(observation.goal_body, opening_goal) ||
                (!observation.layout_animated &&
                    !(window.previous && window.previous->layout_animated)));
        };
        const auto *current = window.handle ? record(*window.handle) : nullptr;
        auto motion_style = active_motion == MotionKind::Resize ? m_policy.resize_style :
            active_motion == MotionKind::Move ? m_policy.move_style : std::nullopt;
        if (active_motion && current && current->mapped_motion == active_motion) {
            motion_style = current->handle.style;  // Keep the acquired generation through reload.
        }
        if (window.rejected_motion != active_motion) window.rejected_motion.reset();
        open_override = open_override && mapped_allowed(OPEN_KIND, nullptr);
        bool exclusive_open = false;
        if (open_override) {
            selected(OPEN_KIND, [&]<class Style>(auto &) {
                if constexpr (requires { Style::EXCLUSIVE_OPEN; }) {
                    exclusive_open = Style::EXCLUSIVE_OPEN;
                }
            });
            if (exclusive_open && (motion_style ||
                (window.previous && independent_motion(window.previous->goal_body) && base_moves()))) {
                open_override = false;
            }
        }
        bool reread = false;
        if (current) {
            const bool MOTION_HANDOFF = active_motion &&
                ((current->mapped_motion && current->mapped_motion != active_motion) ||
                    (motion_style && current->mapped_motion != active_motion));
            if (!mapped_allowed(current->handle.style, current) || MOTION_HANDOFF ||
                (open_override && (current->handle.style != OPEN_KIND || current->mapped_motion)) ||
                (window.exclusive_open_goal && independent_motion(*window.exclusive_open_goal) &&
                    base_moves())) {
                // Effects own one raster and timeline at a time. A handoff consumes
                // this same geometry sample rather than chaining transformers.
                reread = window.driven;
                end(current->handle, End::Interrupted);
            }
        }
        const auto KIND = open_override ? OPEN_KIND : m_policy.style;
        const bool TRY_OPEN = OPENING && m_policy.opening && m_policy.enabled && !m_policy.inert &&
                              (open_override || (!motion_style && !m_policy.open_style &&
                                  mapped_allowed(KIND, nullptr)));
        bool acquired = false;
        if (!window.handle && TRY_OPEN) {
            acquired = acquire(window, observation, ++m_seed, KIND).has_value();
        }
        if (window.handle && TRY_OPEN) {
            const auto CURRENT_HANDLE = *window.handle;
            Effects effects;
            bool accepted = false;
            const auto INITIAL = detail::place(
                observation, m_port.neighbourhood(window.window), acquired ? m_seed : ++m_seed);
            visit(CURRENT_HANDLE, [&]<class Style>(auto &slot, auto &resources) {
                const perf::CpuScope PROFILE{profile_key(Style::KIND, "open"), perf::Phase::Setup};
                accepted = Style::open(slot.state, INITIAL, resources, effects);
                if constexpr (requires { Style::mapped_blur_safe(slot.state); }) {
                    if (accepted) record(CURRENT_HANDLE)->allows_mapped_blur = Style::mapped_blur_safe(slot.state);
                }
            });
            if ((!accepted && acquired) || (accepted && !apply(window, observation, effects))) {
                end(CURRENT_HANDLE, End::Interrupted);
            } else if (accepted) {
                if (exclusive_open) {
                    window.exclusive_open_goal = observation.goal_body;
                }
                window.opening_edge = effects.opening_edge;
                return true;
            }
        }
        if (!window.handle && motion_style) {
            if (window.rejected_motion || !mapped_allowed(*motion_style, nullptr, active_motion)) {
                return reread;  // A selected but unavailable event presents natively.
            }
            if (const auto handle = acquire(window, observation, ++m_seed, *motion_style)) {
                bool accepted = false;
                visit(*handle, [&]<class Style>(auto &slot, auto &resources) {
                    const auto MOTION = *active_motion;
                    const perf::CpuScope PROFILE{profile_key(Style::KIND,
                        MOTION == MotionKind::Move ? "move" : "resize"), perf::Phase::Setup};
                    if constexpr (requires { Style::begin_motion(slot.state, Place{}, resources, MOTION); }) {
                        accepted = Style::begin_motion(slot.state,
                            detail::place(observation, m_port.neighbourhood(window.window), m_seed), resources, MOTION);
                        if (accepted) {
                            auto *entry = record(*handle);
                            entry->mapped_motion = MOTION;
                            if constexpr (requires { Style::mapped_blur_safe(slot.state); }) {
                                entry->allows_mapped_blur = Style::mapped_blur_safe(slot.state);
                            }
                        }
                    }
                });
                if (!accepted) {
                    window.rejected_motion = active_motion;
                    end(*handle, End::Interrupted);
                }
            }
        } else if (!window.handle && base_moves() && mapped_allowed(m_policy.style, nullptr)) {
            acquire(window, observation, ++m_seed, m_policy.style);
        }
        return reread;
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
            if (entry.mapped_motion && entry.current.interrupted) {
                window.rejected_motion = entry.mapped_motion;
            }
            end(entry.handle, entry.current.interrupted ? End::Interrupted : End::Completed);
            return;
        }
        if (entry.current.native_presentation) {
            m_port.detach_transformer(window.window);
        } else {
            m_port.ensure_transformer(window.window, entry.handle);
        }
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
        const auto ACTIVE_MOTION = motion_kind(window, *observation, motion);
        window.previous_sample = stamp.now;
        window.workspace_movement = WORKSPACE_MOVEMENT;
        if (begin_mapped(window, *observation, motion, ACTIVE_MOTION)) {
            observation = m_port.observe(window.window);
        }
        if (!observation) {
            return;
        }
        auto *entry = window.handle ? record(*window.handle) : nullptr;
        const auto PREVIOUS_BODY = entry && entry->mapped_motion && window.previous
            ? std::optional{detail::presented_body(*window.previous)} : std::nullopt;
        window.previous = *observation;
        if (!entry) return;
        const auto CURRENT_HANDLE = entry->handle;
        const auto ELAPSED = detail::step_clock(entry->stepped, entry->last_step,
            entry->last_frame, entry->stall_count, entry->stalls, stamp);
        if (!ELAPSED) {
            end(CURRENT_HANDLE, End::Interrupted);
            return;
        }
        // Hyprland's warp replaces the native goal with the last presented box.
        // Keep the layout target until the compositor supplies a different goal.
        const auto &GOAL = window.driven && detail::same_box(observation->goal_body, window.last_warp)
                               ? window.goal
                               : observation->goal_body;
        Step STEP{.elapsed = *ELAPSED,
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
            .window = window.id,
            .resize_held = observation->resize_held,
            .scale = observation->scale,
            .fullscreen = observation->fullscreen};
        if (entry->mapped_motion) {
            const auto CURRENT_BODY = detail::presented_body(*observation);
            const bool ACTIVE = entry->mapped_motion == ACTIVE_MOTION;
            const auto INTERVAL = motion ? motion->sample_seconds : 0;
            STEP.motion_geometry = MotionGeometry{CURRENT_BODY, PREVIOUS_BODY.value_or(CURRENT_BODY),
                INTERVAL > 0 && INTERVAL <= STALL_SECONDS ? INTERVAL : 0, ACTIVE};
        }
        Effects effects;
        const bool HAD_TAIL = entry->current.tail;
        if (is_usable(entry->footprint)) {
            m_port.damage(entry->footprint);
        }
        visit(CURRENT_HANDLE, [&]<class Style>(auto &slot, auto &resources) {
            if constexpr (requires {
                              Style::needs_visible_outputs(resources);
                              m_port.visible_outputs(Vec2{});
                          }) {
                if (STEP.movement_enabled && Style::needs_visible_outputs(resources)) {
                    const Vec2 OFFSET{
                        observation->workspace_offset.x + observation->floating_offset.x,
                        observation->workspace_offset.y + observation->floating_offset.y};
                    STEP.visible_output_owner = detail::translated(STEP.screen, {-OFFSET.x, -OFFSET.y});
                    STEP.visible_outputs = m_port.visible_outputs(OFFSET);
                }
            }
            const perf::CpuScope PROFILE{profile_key(Style::KIND), perf::Phase::Update};
            if (perf::enabled()) {
                perf::resource_sample(profile_key(Style::KIND), sizeof(slot.state), 0);
            }
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
}  // namespace hyprimate::animation