#pragma once

#include <optional>

#include "perf/GpuTimer.hpp"
#include "perf/LiveProfiler.hpp"

namespace hyprimate::workspace {
    // Propagate a static style key through native capture callbacks without changing
    // their ABI. Nested/reentrant captures restore the previous attribution.
    inline thread_local perf::Key capture_profile_key{perf::Domain::Workspace, {}, "transition"};

    class ProfileAttribution {
      public:
        explicit ProfileAttribution(perf::Key key) noexcept : m_previous(capture_profile_key) {
            capture_profile_key = key;
        }
        ~ProfileAttribution() {
            capture_profile_key = m_previous;
        }
        ProfileAttribution(const ProfileAttribution &) = delete;
        ProfileAttribution(ProfileAttribution &&) = delete;
        ProfileAttribution &operator=(ProfileAttribution &&) = delete;
        ProfileAttribution &operator=(const ProfileAttribution &) = delete;

      private:
        perf::Key m_previous;
    };

    // Workspace setup can recursively capture and prepare native render passes.
    // Count calling-thread CPU once, under the outer operation's phase.
    class ProfileCpuScope {
      public:
        ProfileCpuScope(perf::Key key, perf::Phase phase) noexcept {
            if (perf::enabled()) {
                m_active = true;
                if (s_depth++ == 0) {
                    m_scope.emplace(key, phase);
                }
            }
        }
        ~ProfileCpuScope() {
            if (m_active) {
                m_scope.reset();
                --s_depth;
            }
        }
        ProfileCpuScope(const ProfileCpuScope &) = delete;
        ProfileCpuScope &operator=(const ProfileCpuScope &) = delete;
        ProfileCpuScope(ProfileCpuScope &&) = delete;
        ProfileCpuScope &operator=(ProfileCpuScope &&) = delete;

      private:
        inline static thread_local unsigned s_depth = 0;
        bool m_active = false;
        std::optional<perf::CpuScope> m_scope;
    };

    class ProfileGpuScope {
      public:
        ProfileGpuScope() noexcept {
            if (perf::enabled() && !capture_profile_key.style.empty()) {
                m_scope.emplace(capture_profile_key);
            }
        }

      private:
        std::optional<perf::GpuScope> m_scope;
    };
}  // namespace hyprimate::workspace
