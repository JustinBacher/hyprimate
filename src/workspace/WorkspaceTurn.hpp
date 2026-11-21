#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "animations/shared/Core.hpp"
#include "workspace/WorkspaceCapturePolicy.hpp"
#include "workspace/WorkspaceDirection.hpp"
#include "workspace/WorkspaceInventory.hpp"
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
    std::uint32_t logical_order = 0;
    std::uint64_t placement = 0;
    std::uint64_t owner = 0;
    double monitor_x = 0;
    double monitor_y = 0;
    double monitor_width = 1;
    double monitor_height = 1;
    [[nodiscard]] JourneyEndpoint endpoint() const {
        return {.identity = identity,
            .numbered = numbered,
            .special = special,
            .logical_order = logical_order,
            .placement = placement,
            .owner = owner,
            .monitor_x = monitor_x,
            .monitor_y = monitor_y,
            .monitor_width = monitor_width,
            .monitor_height = monitor_height};
    }
};

enum class HudReplay { Bottom, Blur, Top, Overlay, Popups, RestoreBlur };
struct InactiveSourceFacts {
    bool monitor = true;
    bool source = true;
    bool active_special = false;
    bool popup = false;
    bool shell_matches = true;
};
struct MutationFacts {
    bool enabled = false;
    int capture_depth = 0;
    int mutation_depth = 0;
};
struct QueueFacts {
    bool monitor = false;
    int capture_depth = 0;
    bool capture = false;
    bool snapshot = false;
    bool read_only = false;
};
struct NativePrimeFacts {
    bool enabled = false;
    bool output_configured = false;
    bool presented = false;
};
struct FloatingFacts {
    std::int64_t identity = 0;
    std::array<double, 4> rect{};
    float opacity = 1.F;
    double order = 0.0;
};
struct FloatingPresentation {
    std::size_t index = 0;
    bool stale = false;
    std::optional<std::size_t> previous = std::nullopt;
    FloatingFacts facts;
    float live_mix = 1.F;
};

template<class Port> class WorkspaceTurn {
  public:
    using Composition = TurnComposition<typename Port::Workspace>;
    explicit WorkspaceTurn(Port &port) : m_port(&port) {}
    [[nodiscard]] static bool intercept_mutation(const MutationFacts &facts) {
        return facts.enabled && facts.capture_depth <= 0 && facts.mutation_depth <= 0;
    }
    [[nodiscard]] static bool queue_context(const QueueFacts &facts) {
        return facts.monitor && facts.capture_depth <= 0 && !facts.capture && !facts.snapshot &&
               !facts.read_only;
    }
    [[nodiscard]] static bool prime_native(const NativePrimeFacts &facts) {
        return facts.enabled && facts.output_configured && !facts.presented;
    }

    bool stage(const Composition &current) {
        invalidate_frame();
        if (!m_port->buffers(m_active)) {
            cancel("stage-buffers");
            return false;
        }
        if (m_active) {
            if (!m_presented) {
                return true;
            }
            bool captured = true;
            if (current.identity == m_source.identity &&
                should_refresh(CaptureKind::SourceIsolated)) {
                captured = capture(CaptureKind::SourceIsolated, current);
            } else if (current.identity == m_destination.identity &&
                       should_refresh(CaptureKind::DestinationLive)) {
                captured = capture(CaptureKind::DestinationLive, current);
                m_destination_ready = captured;
            }
            if (!captured) {
                cancel("stage-capture");
            }
            return captured;
        }
        if (m_staged && m_staged_identity == current.identity) {
            return true;
        }
        m_staged = capture(CaptureKind::SourceIsolated, current);
        m_staged_identity = current.identity;
        return m_staged;
    }

    void release_gesture() {
        if (!m_active) {
            m_staged = false;
        }
    }

    void begin(
        Composition before, Composition after, double base_duration, Transition::Curve curve) {
        invalidate_frame();
        preserve_logical_hop(before, after);
        if (before.identity == after.identity) {
            if (!m_active) {
                m_staged = false;
            }
            return;
        }
        if (!m_active && !m_staged) {
            cancel("unstaged-request");
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
        const double DURATION =
            RETARGET
                ? m_port->duration(m_source.endpoint(), m_destination.endpoint(), base_duration)
                : m_port->duration(from->endpoint(), after.endpoint(), base_duration);
        if (!std::isfinite(DURATION) || DURATION <= 0.0) {
            cancel("invalid-duration");
            return;
        }
        const auto DIRECTION = direction_between(from->numbered, after.numbered, from->special, after.special);
        m_duration = DURATION;
        m_curve = std::move(curve);
        if (!m_active) {
            m_source = before;
            m_destination = after;
            m_active = m_transition.begin(DIRECTION, DURATION, m_curve);
            m_destination_ready = false;
        } else if (!m_presented) {
            if (after.identity == m_source.identity) {
                cancel();
                return;
            }
            m_destination = after;
            m_pending.reset();
            m_transition.begin(DIRECTION, DURATION, m_curve);
            m_destination_ready = false;
        } else if (after.identity == m_source.identity || after.identity == m_destination.identity) {
            m_pending.reset();
            const bool DESTINATION = after.identity == m_destination.identity;
            if (DESTINATION != m_transition.target_destination()) {
                m_transition.retarget(DESTINATION, DURATION);
            }
        } else {
            m_pending = after;
            m_pending_direction = DIRECTION;
        }
        m_staged = false;
        m_port->damage();
    }

    void frame(const Composition &current, double elapsed) {
        invalidate_frame();
        if (!m_active) {
            return;
        }
        if (!m_port->scene_safe()) {
            cancel("scene-unsafe");
            return;
        }
        if (m_port->native_hud() && !m_port->hud_safe()) {
            cancel("hud-unsafe");
            return;
        }
        if (!m_port->buffers(true)) {
            cancel("frame-buffers");
            return;
        }
        if (m_presented) {
            if (!std::isfinite(elapsed) || elapsed < 0.0 || elapsed > m_port->max_frame_gap()) {
                cancel("clock-stall");
                return;
            }
            m_transition.advance(elapsed);
            if (!m_transition.active()) {
                m_port->landed();
                if (!m_pending) {
                    cancel();
                    return;
                }
                if (m_transition.target_destination()) {
                    m_port->swap_slots();
                    std::swap(m_source_blend, m_destination_blend);
                    m_source = m_destination;
                }
                reset_blend(CaptureKind::SourceIsolated);
                reset_blend(CaptureKind::DestinationLive);
                m_destination = *m_pending;
                m_pending.reset();
                m_presented = false;
                m_destination_ready = false;
                m_transition.begin(m_pending_direction, m_duration, m_curve);
            }
        }
        bool captured = true;
        const bool LIVE_SOURCE = should_refresh(CaptureKind::SourceIsolated);
        if (current.identity == m_source.identity && LIVE_SOURCE) {
            captured = capture(CaptureKind::SourceIsolated, current);
        } else if (current.identity == m_destination.identity) {
            if (m_presented && LIVE_SOURCE) {
                captured = refresh_inactive_source();
            }
            if (captured &&
                (!m_destination_ready || should_refresh(CaptureKind::DestinationLive))) {
                captured = capture(CaptureKind::DestinationLive, current);
                m_destination_ready = captured;
            }
        }
        if (!captured || !m_destination_ready) {
            cancel(captured ? "destination-not-ready" : "frame-capture");
            return;
        }
        m_port->damage();
    }

    enum class Presentation { Skipped, Cancelled, Displayed, FirstDisplay };
    // The style writes offscreen. Only the final display callback may expose those pixels.
    // Renderer calls can synchronously replace the turn; don't keep a turn reference across one.
    template<class Resolve, class Render, class Display>
    static Presentation draw_queued(std::uint64_t ticket, Resolve resolve, Render render, Display display) {
        auto result = Presentation::Skipped;
        const auto CURRENT = [&]() -> WorkspaceTurn * {
            auto *turn = resolve();
            return ticket && turn && turn->m_queued == ticket && turn->drawable() ? turn : nullptr;
        };
        const auto ALLOWED = [&]() -> WorkspaceTurn * {
            auto *turn = CURRENT();
            if (turn && (!turn->m_port->scene_safe() ||
                            (turn->m_port->native_hud() && !turn->m_port->hud_safe()))) {
                turn->cancel("draw-unsafe");
                result = Presentation::Cancelled;
                return nullptr;
            }
            return turn;
        };
        auto *turn = ALLOWED();
        if (!turn) {
            return result;
        }
        const bool RENDERED = render(turn->progress(), turn->direction());
        turn = ALLOWED();
        if (!turn) {
            return result;
        }
        if (!RENDERED) {
            turn->presented(false);
            return Presentation::Cancelled;
        }
        display();
        if ((turn = CURRENT())) {
            turn->m_queued = 0;
            result = turn->presented_once() ? Presentation::Displayed : Presentation::FirstDisplay;
            turn->presented(true);
        }
        return result;
    }
    template <class History, class Observation, class Copy>
    static void observe_wallpaper(History &history, const Observation &observation, Copy &&copy) {
        history.remember(observation.workspace,
            observation.monitor,
            observation.signature,
            std::forward<Copy>(copy));
    }

    template <class InventoryPort>
    void prepare_inventory(InventoryPort &port) {
        if (drawable()) {
            m_inventory.prepare(port, m_source.endpoint(), m_destination.endpoint());
        }
    }

    void presented(bool success) {
        if (!m_active) {
            return;
        }
        if (!success) {
            cancel("style-draw");
            return;
        }
        m_presented = true;
        m_port->damage();
    }
    void cancel(std::string_view reason = {}) {
        invalidate_frame();
        if (!reason.empty()) {
            m_port->diagnostic(reason);
        }
        reset_blend(CaptureKind::SourceIsolated);
        reset_blend(CaptureKind::DestinationLive);
        const bool WAS_ACTIVE = m_active;
        m_active = m_presented = m_destination_ready = m_staged = false;
        m_pending.reset();
        m_transition.cancel();
        if (WAS_ACTIVE) {
            m_port->damage();
        }
    }

    template <class Prepare>
    std::uint64_t queue(bool resources_ready, Prepare &&prepare) {
        m_queued = 0;
        if (!drawable()) {
            return 0;
        }
        if (!resources_ready) {
            cancel("queue-resources");
            return 0;
        }
        if (!m_port->scene_safe()) {
            cancel("queue-scene-unsafe");
            return 0;
        }
        if (m_port->native_hud() && !m_port->hud_safe()) {
            cancel("queue-hud-unsafe");
            return 0;
        }
        if (!std::forward<Prepare>(prepare)()) {
            cancel("queue-prepare");
            return 0;
        }
        // Issued on the compositor thread; a removed/recreated turn cannot reuse a live ticket.
        m_queued = ++s_next_ticket;
        return m_queued;
    }
    template <class Replay>
    void replay_hud(const Replay &replay) const {
        if (!m_port->native_hud()) {
            return;
        }
        for (const auto STEP : {HudReplay::Bottom,
                 HudReplay::Blur,
                 HudReplay::Top,
                 HudReplay::Overlay,
                 HudReplay::Popups,
                 HudReplay::RestoreBlur}) {
            replay(STEP);
        }
    }
    [[nodiscard]] double blend_mix(CaptureKind kind) {
        auto &blend = blend_state(kind);
        if (!blend.active) {
            return 1.0;
        }
        const double TIME = (m_port->now() - blend.started) / .08;
        const double DISTANCE = std::max(0.0, std::min(progress(), 1.0 - progress()));
        blend.mix = std::clamp(
            std::max({blend.mix, TIME, 1.0 - (DISTANCE / blend.start_distance)}), 0.0, 1.0);
        if (blend.mix >= 1.0) {
            m_port->release_blend(kind);
            blend.active = false;
        }
        return blend.mix;
    }

    template <class PresentationPort>
    bool present_floating(CaptureKind kind, PresentationPort &port, double mix) {
        auto &blend = blend_state(kind);
        try {
            index_floating(blend, port);
            blend.commands.clear();
            blend.commands.reserve(port.old_count() + port.live_count());
            std::ranges::fill(blend.matched, 0);
            for (std::size_t index = 0; index < port.live_count(); ++index) {
                auto facts = port.live_window(index);
                const auto FOUND = std::ranges::lower_bound(
                    blend.old_order, facts.identity, {}, [](const auto &item) {
                        return item.first;
                    });
                FloatingPresentation command{.index = index, .facts = facts};
                if (FOUND == blend.old_order.end() || FOUND->first != facts.identity) {
                    command.facts.opacity *= static_cast<float>(mix);
                } else {
                    const auto PREVIOUS = FOUND->second;
                    blend.matched[PREVIOUS] = 1;
                    const auto OLD = port.old_window(PREVIOUS);
                    command.previous = PREVIOUS;
                    command.live_mix = static_cast<float>(mix);
                    for (std::size_t axis = 0; axis < facts.rect.size(); ++axis) {
                        command.facts.rect[axis] = std::lerp(OLD.rect[axis], facts.rect[axis], mix);
                    }
                    command.facts.order = std::lerp(OLD.order, facts.order, mix);
                }
                blend.commands.push_back(command);
            }
            append_retired_floating(blend, port, mix);
            std::ranges::sort(
                blend.commands, {}, [](const auto &command) { return command.facts.order; });
            port.begin(blend.commands.size());
            for (const auto &command : blend.commands) {
                port.emit(command);
            }
        } catch (...) {
            return false;
        }
        return true;
    }
    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] std::uint64_t revision() const { return m_revision; }
    [[nodiscard]] bool drawable() const { return m_active && m_destination_ready && m_transition.active(); }
    template<class Owns>
    [[nodiscard]] bool previews_current(std::uint64_t revision, const Owns &owns) const {
        return m_inventory.current(revision, owns);
    }
    [[nodiscard]] double progress() const { return m_transition.progress(); }
    [[nodiscard]] Direction direction() const { return m_transition.direction(); }
    [[nodiscard]] const Composition &source() const { return m_source; }
    [[nodiscard]] const Composition &destination() const { return m_destination; }
    [[nodiscard]] bool presented_once() const { return m_presented; }

  private:
    void invalidate_frame() {
        ++m_revision;
        m_queued = 0;
    }
    template <class PresentationPort>
    static void index_floating(auto &blend, const PresentationPort &port) {
        if (blend.indexed) {
            return;
        }
        blend.old_order.clear();
        blend.old_order.reserve(port.old_count());
        for (std::size_t index = 0; index < port.old_count(); ++index) {
            blend.old_order.emplace_back(port.old_window(index).identity, index);
        }
        std::ranges::sort(blend.old_order, {}, [](const auto &item) { return item.first; });
        blend.matched.resize(port.old_count());
        blend.indexed = true;
    }
    template <class PresentationPort>
    static void append_retired_floating(auto &blend, const PresentationPort &port, double mix) {
        for (std::size_t index = 0; index < port.old_count(); ++index) {
            if (blend.matched[index] != 0) {
                continue;
            }
            auto facts = port.old_window(index);
            facts.opacity *= static_cast<float>(1.0 - mix);
            blend.commands.push_back({.index = index, .stale = true, .facts = facts});
        }
    }
    static void preserve_logical_hop(Composition &before, Composition &after) {
        if (before.numbered && after.numbered && before.numbered != after.numbered &&
            before.logical_order == after.logical_order) {
            if (*before.numbered < *after.numbered) {
                ++after.logical_order;
            } else {
                ++before.logical_order;
            }
        }
    }
    WorkspaceInventory<typename Port::Workspace, typename Port::Preview> m_inventory;
    struct Blend {
        double started = 0.0;
        double start_distance = 0.0;
        double mix = 0.0;
        bool active = false;
        bool cached = false;
        bool indexed = false;
        std::vector<std::pair<std::int64_t, std::size_t>> old_order;
        std::vector<std::uint8_t> matched;
        std::vector<FloatingPresentation> commands;
    };
    Blend &blend_state(CaptureKind kind) {
        return kind == CaptureKind::SourceIsolated ? m_source_blend : m_destination_blend;
    }
    void reset_blend(CaptureKind kind) {
        m_port->release_blend(kind);
        auto &blend = blend_state(kind);
        blend.started = blend.start_distance = blend.mix = 0.0;
        blend.active = blend.cached = blend.indexed = false;
        blend.old_order.clear();
        blend.matched.clear();
        blend.commands.clear();
    }
    bool should_refresh(CaptureKind kind) {
        const double BOUNDARY = std::clamp(m_port->live_boundary(), 0.0, 1.0);
        const bool REFRESH = progress() <= BOUNDARY ||
                             (kind == CaptureKind::DestinationLive && progress() >= 1.0 - BOUNDARY);
        if (!REFRESH) {
            blend_state(kind).cached = true;
        }
        return REFRESH;
    }
    bool begin_refresh(CaptureKind kind) {
        auto &blend = blend_state(kind);
        if (!m_port->layered() || !m_presented || !blend.cached || blend.active) {
            return true;
        }
        if (!m_port->clone_capture(kind)) {
            return false;
        }
        blend.start_distance = std::max(1e-6, std::min(progress(), 1.0 - progress()));
        blend.started = m_port->now();
        blend.mix = 0.0;
        blend.active = true;
        blend.indexed = false;
        return true;
    }
    bool capture(CaptureKind kind, const Composition &composition) {
        if (!begin_refresh(kind)) {
            m_port->diagnostic("capture-clone");
            return false;
        }
        if (!m_port->capture(kind, composition)) {
            m_port->diagnostic(kind == CaptureKind::SourceIsolated ? "capture-source" : "capture-destination");
            return false;
        }
        blend_state(kind).cached = false;
        return true;
    }
    bool refresh_inactive_source() {
        if (!m_port->layered()) {
            return true;
        }
        const auto FACTS = m_port->inactive_source_facts(m_source);
        if (!FACTS.monitor || !m_port->scene_safe()) {
            return false;
        }
        if (!FACTS.source || m_source.special || FACTS.active_special ||
            (!m_port->native_hud() && FACTS.popup) || !FACTS.shell_matches) {
            return true;
        }
        if (!begin_refresh(CaptureKind::SourceIsolated) ||
            !m_port->capture_inactive_source(m_source)) {
            return false;
        }
        m_source_blend.cached = false;
        return true;
    }
    Blend m_source_blend;
    Blend m_destination_blend;
    Port *m_port;
    inline static std::uint64_t s_next_ticket = 0;
    std::uint64_t m_queued = 0;
    std::uint64_t m_revision = 0;
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
    std::shared_ptr<Turn> get(Monitor monitor, Port &port) {
        if (auto existing = pin(monitor)) {
            return existing;
        }
        return m_turns.emplace(monitor, std::make_shared<Turn>(port)).first->second;
    }
    Turn *find(Monitor monitor) {
        const auto FOUND = m_turns.find(monitor);
        return FOUND == m_turns.end() ? nullptr : FOUND->second.get();
    }
    // A renderer callback may remove the registry entry while a capture is on the stack.
    [[nodiscard]] std::shared_ptr<Turn> pin(Monitor monitor) const {
        const auto found = m_turns.find(monitor);
        return found == m_turns.end() ? nullptr : found->second;
    }
    void remove(Monitor monitor) {
        if (auto *turn = find(monitor)) {
            turn->cancel();
            m_turns.erase(monitor);
        }
    }
    void clear() {
        for (auto &item : m_turns) {
            item.second->cancel();
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
        if (style != m_style) {
            m_curve = {};
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
    std::unordered_map<Monitor, std::shared_ptr<Turn>> m_turns;
    std::uint64_t m_style = 0;
    double m_duration = 0.0;
    std::string m_curve_name;
    Transition::Curve m_curve;
    bool m_enabled = false;
    bool m_curve_dirty = true;
};

}  // namespace hyprimate::workspace
