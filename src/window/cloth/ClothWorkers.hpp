#pragma once

#include <atomic>
#include <memory>
#include <mutex>

#include "window/cloth/ClothFlutter.hpp"

namespace hyprimate::cloth_detail {
    struct Lease;
    struct SimInput;

    // Admission holds the lifecycle lock through the caller's run publication and state update.
    class WorkerAdmission {
      public:
        WorkerAdmission();
        [[nodiscard]] bool available() const noexcept;
        [[nodiscard]] std::shared_ptr<Lease> launch_transition(
            const std::shared_ptr<ClothFlutter::Run> &run, SimInput &&input) const;
        [[nodiscard]] std::shared_ptr<Lease> launch_movement(
            const std::shared_ptr<ClothFlutter::Run> &run, double phase) const;

      private:
        std::scoped_lock<std::mutex> m_lock;
    };

    // Workers share this test pause flag and threshold.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    extern std::atomic<bool> s_pause;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    extern std::atomic<int> s_pause_after;
}  // namespace hyprimate::cloth_detail
