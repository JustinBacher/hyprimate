#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "animations/shared/Core.hpp"
#include "workspace/WorkspaceCapturePolicy.hpp"
#include "workspace/WorkspaceDirection.hpp"
#include "workspace/WorkspaceJourney.hpp"

namespace hyprimate::workspace {
template<class Texture> struct WallpaperIdentity {
    Texture texture;
    std::uint64_t generation = 0;
    bool operator==(const WallpaperIdentity &) const = default;
};

template<class Texture>
bool wallpaper_matches(std::span<const WallpaperIdentity<Texture>> source,
    std::span<const WallpaperIdentity<Texture>> destination) {
    return std::ranges::equal(source, destination);
}


template<class Workspace> struct TurnComposition {
    std::int64_t identity = 0;
    Workspace workspace{};
    std::optional<std::uint32_t> numbered;
    bool special = false;
    std::uint32_t logicalOrder = 0;
    std::uint64_t placement = 0;
    [[nodiscard]] JourneyEndpoint endpoint() const { return {identity, numbered, special, logicalOrder, placement}; }
};

template<class Port> class WorkspaceTurn {
  public:
    using Composition = TurnComposition<typename Port::Workspace>;
    explicit WorkspaceTurn(Port &port) : m_port(&port) {}

    bool stage(const Composition &current) {
        if (!m_port->buffers(m_active)) {
            cancel();
            return false;
        }
        if (m_active) {
            if (!m_presented) {
                return true;
            }
            bool captured = true;
            if (current.identity == m_source.identity && m_port->should_refresh(CaptureKind::SourceIsolated, progress())) {
                captured = m_port->capture(CaptureKind::SourceIsolated, current);
            } else if (current.identity == m_destination.identity && m_port->should_refresh(CaptureKind::DestinationLive, progress())) {
                captured = m_port->capture(CaptureKind::DestinationLive, current);
                m_destination_ready = captured;
            }
            if (!captured) {
                cancel();
            }
            return captured;
        }
        if (m_staged && m_staged_identity == current.identity) {
            return true;
        }
        m_staged = m_port->capture(CaptureKind::SourceIsolated, current);
        m_staged_identity = current.identity;
        return m_staged;
    }

    void release_gesture() {
        if (!m_active) {
            m_staged = false;
        }
    }

    void begin(const Composition &before, const Composition &after, double base_duration,
        Transition::Curve curve) {
        if (before.identity == after.identity) {
            if (!m_active) {
                m_staged = false;
            }
            return;
        }
        if (!m_active && !m_staged) {
            cancel();
            return;
        }
        const Composition *from = &before;
        if (m_active && !m_presented) {
            from = &m_source;
        } else if (m_active && after.identity != m_source.identity &&
                   after.identity != m_destination.identity) {
            from = m_transition.target_destination() ? &m_destination : &m_source;
        }
        const bool RETARGET = m_active && m_presented &&
            (after.identity == m_source.identity || after.identity == m_destination.identity);
        // A reversal traverses the original pair, not the last (possibly queued) native workspace.
        const double duration = RETARGET ? m_port->duration(m_source.endpoint(), m_destination.endpoint(), base_duration)
                                         : m_port->duration(from->endpoint(), after.endpoint(), base_duration);
        if (!std::isfinite(duration) || duration <= 0.0) {
            cancel();
            return;
        }
        const auto DIRECTION = direction_between(from->numbered, after.numbered, from->special, after.special);
        m_duration = duration;
        m_curve = std::move(curve);
        if (!m_active) {
            m_source = before;
            m_destination = after;
            m_active = m_transition.begin(DIRECTION, duration, m_curve);
            m_destination_ready = false;
        } else if (!m_presented) {
            if (after.identity == m_source.identity) {
                cancel();
                return;
            }
            m_destination = after;
            m_pending.reset();
            m_transition.begin(DIRECTION, duration, m_curve);
            m_destination_ready = false;
        } else if (after.identity == m_source.identity || after.identity == m_destination.identity) {
            m_pending.reset();
            m_transition.retarget(after.identity == m_destination.identity, duration);
        } else {
            m_pending = after;
            m_pending_direction = DIRECTION;
        }
        m_staged = false;
        m_port->damage();
    }

    void frame(const Composition &current, double elapsed) {
        if (!m_active) {
            return;
        }
        if (!m_port->buffers(true)) {
            cancel();
            return;
        }
        if (m_presented) {
            m_transition.advance(elapsed);
            if (!m_transition.active()) {
                if (!m_pending || !std::isfinite(elapsed) || elapsed < 0.0 || elapsed > STALL_SECONDS) {
                    cancel();
                    return;
                }
                if (m_transition.target_destination()) {
                    m_port->swap_slots();
                    m_source = m_destination;
                }
                m_destination = *m_pending;
                m_pending.reset();
                m_presented = false;
                m_destination_ready = false;
                m_transition.begin(m_pending_direction, m_duration, m_curve);
            }
        }
        bool captured = true;
        const bool LIVE_SOURCE = m_port->should_refresh(CaptureKind::SourceIsolated, progress());
        if (current.identity == m_source.identity && LIVE_SOURCE) {
            captured = m_port->capture(CaptureKind::SourceIsolated, current);
        } else if (current.identity == m_destination.identity) {
            if (m_presented && LIVE_SOURCE) {
                captured = m_port->refresh_inactive_source(m_source);
            }
            if (captured && (!m_destination_ready || m_port->should_refresh(CaptureKind::DestinationLive, progress()))) {
                captured = m_port->capture(CaptureKind::DestinationLive, current);
                m_destination_ready = captured;
            }
        }
        if (!captured || !m_destination_ready) {
            cancel();
            return;
        }
        m_port->damage();
    }

    template<class Draw> bool draw(Draw &&draw_style, bool matching_wallpaper) {
        if (!drawable()) {
            m_port->present_native();
            return false;
        }
        const bool SUCCESS = std::forward<Draw>(draw_style)(progress(), direction(), matching_wallpaper);
        presented(SUCCESS);
        return SUCCESS;
    }

    void presented(bool success) {
        if (!m_active) {
            return;
        }
        if (!success) {
            m_port->present_native();
            cancel();
            return;
        }
        m_presented = true;
        m_port->damage();
    }
    void cancel() {
        const bool WAS_ACTIVE = m_active;
        m_active = m_presented = m_destination_ready = m_staged = false;
        m_pending.reset();
        m_transition.cancel();
        if (WAS_ACTIVE) {
            m_port->damage();
        }
    }
    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] bool drawable() const { return m_active && m_destination_ready && m_transition.active(); }
    [[nodiscard]] double progress() const { return m_transition.progress(); }
    [[nodiscard]] Direction direction() const { return m_transition.direction(); }
    [[nodiscard]] const Composition &source() const { return m_source; }
    [[nodiscard]] const Composition &destination() const { return m_destination; }
    [[nodiscard]] bool presented_once() const { return m_presented; }

  private:
    Port *m_port;
    Composition m_source{};
    Composition m_destination{};
    std::optional<Composition> m_pending;
    Direction m_pending_direction = Direction::Forward;
    Transition m_transition;
    Transition::Curve m_curve;
    double m_duration = 0.0;
    std::int64_t m_staged_identity = 0;
    bool m_active = false;
    bool m_presented = false;
    bool m_destination_ready = false;
    bool m_staged = false;
};

template<class Monitor, class Port> class WorkspaceTurns {
  public:
    using Turn = WorkspaceTurn<Port>;
    Turn &get(Monitor monitor, Port &port) {
        return m_turns.try_emplace(monitor, port).first->second;
    }
    Turn *find(Monitor monitor) {
        const auto FOUND = m_turns.find(monitor);
        return FOUND == m_turns.end() ? nullptr : &FOUND->second;
    }
    void remove(Monitor monitor) {
        if (auto *turn = find(monitor)) {
            turn->cancel();
            m_turns.erase(monitor);
        }
    }
    void clear() {
        for (auto &item : m_turns) {
            item.second.cancel();
        }
        m_turns.clear();
    }
    template<class Resolve>
    void configure(bool enabled, std::uint64_t style, double duration, std::string_view curve_name,
        bool settings_changed, Resolve &&resolve) {
        const bool VALID = enabled && std::isfinite(duration) && duration > 0.0;
        if (m_enabled && (!VALID || style != m_style)) {
            clear();
        }
        if (settings_changed || m_enabled != VALID || m_style != style || m_duration != duration ||
            m_curve_name != curve_name) {
            m_curve_dirty = true;
        }
        m_enabled = VALID;
        m_style = style;
        m_duration = duration;
        if (m_curve_name != curve_name) {
            m_curve_name = curve_name;
        }
        if (m_curve_dirty || !m_curve) {
            auto curve = std::forward<Resolve>(resolve)(m_curve_name);
            if (curve) {
                m_curve = std::move(curve);
                m_curve_dirty = false;
            }
        }
    }
    void reload() { m_curve_dirty = true; }
    [[nodiscard]] bool enabled() const { return m_enabled && static_cast<bool>(m_curve); }
    [[nodiscard]] double duration() const { return m_duration; }
    [[nodiscard]] const Transition::Curve &curve() const { return m_curve; }

  private:
    std::unordered_map<Monitor, Turn> m_turns;
    std::uint64_t m_style = 0;
    double m_duration = 0.0;
    std::string m_curve_name;
    Transition::Curve m_curve;
    bool m_enabled = false;
    bool m_curve_dirty = true;
};

}  // namespace hyprimate::workspace
