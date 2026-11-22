#pragma once

#include <cstdint>
#include <variant>

#include "WindowAnimationFacts.hpp"
#include "animations/construction/ConstructionBatch.hpp"
#include "animations/construction/studies/EdgeStretch.hpp"
#include "animations/construction/studies/IntactTransport.hpp"

namespace hyprimate::construction {

// Exact, opaque capture copy: use host presentation instead of flattening it again.
[[nodiscard]] bool is_capture_identity(const ConstructionBatch &batch, Box capture) noexcept;

// One mapped motion job, with no GL state, client reference, or saved image.
class MotionSite {
  public:
    enum class Kind { None, Carry, Resize };

    explicit MotionSite(std::uint64_t seed) noexcept : m_seed(seed) {}
    [[nodiscard]] static bool admits(const animation::Motion &motion) noexcept;
    [[nodiscard]] animation::Frame advance(const animation::Step &step) noexcept;
    [[nodiscard]] Kind kind() const noexcept;
    [[nodiscard]] Box body() const noexcept { return m_body; }
    [[nodiscard]] Box capture() const noexcept { return m_capture; }
    [[nodiscard]] const ConstructionBatch &batch() const noexcept { return m_batch; }

  private:
    const std::uint64_t m_seed;
    std::variant<std::monostate, studies::IntactTransport, studies::EdgeStretch> m_model;
    ConstructionBatch m_batch;
    Box m_body{}, m_capture{};
    animation::MonitorId m_monitor = 0;
    animation::WorkspaceId m_workspace = 0;
    bool m_done = false;
};

}  // namespace hyprimate::construction
