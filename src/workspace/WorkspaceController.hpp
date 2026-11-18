#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "workspace/WorkspaceTransition.hpp"

namespace hyprimate::workspace {

    // This model has no compositor objects: the bridge owns the corresponding buffers and
    // removes a monitor when its native resources disappear.
    class WorkspaceController {
      public:
        using Monitor = std::uintptr_t;
        using Workspace = std::int64_t;
        enum class Style { None, Rotation };
        enum class Start { Native, Capture, RetainSource, Retarget, Pending };
        enum class Advance { None, Finished, NextSource, NextDestination };

        struct State {
            Workspace source = 0;
            std::optional<Workspace> pending;
            Direction pendingDirection = Direction::Forward;
            double duration = 0.0;
            Transition::Curve curve;
            Workspace destination = 0;
            Style style = Style::None;
            bool presented = false;
            Transition transition;
        };

        static Style selectStyle(bool enabled, std::string_view name);
        Start begin(Monitor monitor,
            Workspace source,
            Workspace destination,
            Direction direction,
            Style style,
            double duration,
            Transition::Curve curve = [](double t) { return t * t * (3.0 - 2.0 * t); });
        void presented(Monitor monitor);
        Advance advance(Monitor monitor, double elapsed);
        void cancel(Monitor monitor);
        void clear();
        [[nodiscard]] const State *state(Monitor monitor) const;

      private:
        std::unordered_map<Monitor, State> m_states;
    };

}  // namespace hyprimate::workspace
