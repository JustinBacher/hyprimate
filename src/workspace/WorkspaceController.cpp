#include <cmath>

#include "animations/shared/Core.hpp"
#include "workspace/WorkspaceController.hpp"

namespace hyprimate::workspace {

    WorkspaceController::Style WorkspaceController::selectStyle(
        bool enabled, std::string_view name) {
        return enabled && name == "rotation" ? Style::Rotation : Style::None;
    }

    WorkspaceController::Start WorkspaceController::begin(Monitor monitor,
        Workspace source,
        Workspace destination,
        Direction direction,
        Style style,
        double duration,
        Transition::Curve curve) {
        if (!monitor || style == Style::None || !std::isfinite(duration) || duration <= 0.0) {
            cancel(monitor);
            return Start::Native;
        }
        auto [it, inserted] = m_states.try_emplace(monitor);
        auto &state = it->second;
        if (inserted) {
            if (source == destination) {
                m_states.erase(it);
                return Start::Native;
            }
            state.source = source;
            state.destination = destination;
            state.style = style;
            state.duration = duration;
            state.curve = curve;
            state.transition.begin(direction, duration, state.curve);
            return Start::Capture;
        }
        if (source == destination) return Start::Retarget;
        state.duration = duration;
        state.curve = curve;
        if (!state.presented) {
            if (destination == state.source) {
                m_states.erase(it);
                return Start::Native;
            }
            state.destination = destination;
            state.transition.begin(direction, duration, state.curve);
            return Start::RetainSource;
        }
        if (destination == state.source || destination == state.destination) {
            state.pending.reset();
            state.transition.retarget(destination == state.destination, duration);
            return Start::Retarget;
        }
        state.pending = destination;
        state.pendingDirection = direction;
        return Start::Pending;
    }

    void WorkspaceController::presented(Monitor monitor) {
        const auto it = m_states.find(monitor);
        if (it != m_states.end()) {
            it->second.presented = true;
        }
    }

    WorkspaceController::Advance WorkspaceController::advance(Monitor monitor, double elapsed) {
        const auto it = m_states.find(monitor);
        if (it == m_states.end()) return Advance::None;
        auto &state = it->second;
        state.transition.advance(elapsed);
        if (state.transition.active()) return Advance::None;
        if (!state.pending || !std::isfinite(elapsed) || elapsed < 0.0 ||
            elapsed > STALL_SECONDS) {
            m_states.erase(it);
            return Advance::Finished;
        }
        const bool fromDestination = state.transition.targetDestination();
        state.source = fromDestination ? state.destination : state.source;
        state.destination = *state.pending;
        state.pending.reset();
        state.presented = false;
        state.transition.begin(state.pendingDirection, state.duration, state.curve);
        return fromDestination ? Advance::NextDestination : Advance::NextSource;
    }

    void WorkspaceController::cancel(Monitor monitor) {
        m_states.erase(monitor);
    }

    void WorkspaceController::clear() {
        m_states.clear();
    }

    const WorkspaceController::State *WorkspaceController::state(Monitor monitor) const {
        const auto it = m_states.find(monitor);
        return it == m_states.end() ? nullptr : &it->second;
    }

}  // namespace hyprimate::workspace
