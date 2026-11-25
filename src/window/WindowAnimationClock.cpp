#include <cmath>

#include "WindowAnimationClock.hpp"

namespace hyprimate::animation::detail {
    std::optional<double> step_clock(bool &stepped, Clock::time_point &last_step,
        FrameId &last_frame, std::size_t &stall_count,
        std::array<Clock::time_point, STALL_LIMIT> &stalls, const FrameStamp &stamp) {
        const double ELAPSED =
            stepped ? std::chrono::duration<double>(stamp.now - last_step).count() : 0;
        if (!std::isfinite(ELAPSED) || ELAPSED < 0 || ELAPSED > STALL_WINDOW_SECONDS) {
            return std::nullopt;
        }
        if (ELAPSED > STALL_SECONDS) {
            std::size_t kept = 0;
            for (std::size_t index = 0; index < stall_count; ++index) {
                if (std::chrono::duration<double>(stamp.now - stalls[index]).count() <=
                    STALL_WINDOW_SECONDS) {
                    stalls[kept++] = stalls[index];
                }
            }
            stall_count = kept;
            stalls[stall_count++] = stamp.now;
            if (stall_count == stalls.size()) {
                return std::nullopt;
            }
        }
        last_frame = stamp.frame;
        last_step = stamp.now;
        stepped = true;
        return ELAPSED;
    }
}  // namespace hyprimate::animation::detail
