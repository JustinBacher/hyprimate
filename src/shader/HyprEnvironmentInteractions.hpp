#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "GlesRuntime.hpp"

class CHyprWindowPort;

namespace hyprimate::shader {
    class HyprEnvironmentInteractions {
      public:
        explicit HyprEnvironmentInteractions(::CHyprWindowPort &port);
        ~HyprEnvironmentInteractions();
        HyprEnvironmentInteractions(const HyprEnvironmentInteractions &) = delete;
        HyprEnvironmentInteractions &operator=(const HyprEnvironmentInteractions &) = delete;
        HyprEnvironmentInteractions(HyprEnvironmentInteractions &&) noexcept;
        HyprEnvironmentInteractions &operator=(HyprEnvironmentInteractions &&) noexcept;

        struct Batch {
            std::array<Interaction, MAX_INTERACTIONS> records{};
            std::array<std::uint64_t, MAX_INTERACTIONS> windows{};
            std::size_t count = 0;
            bool reset = false;
        };

        // Call only for authoritative output samples. clear() disables event tracking
        // until the next sample, so inactive effects do not inspect windows.
        Batch collect(const PHLMONITOR &monitor);
        // Run before output damage is sampled; true requires a full-output redraw.
        bool privacy_changed(const PHLMONITOR &monitor);
        void clear();
      private:
        class Impl;
        std::unique_ptr<Impl> m_impl;
    };
}  // namespace hyprimate::shader
