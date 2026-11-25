#pragma once

#include "gl/EglContext.hpp"

namespace hyprimate::gl {
    enum class ContextOwnerState { current, unavailable, replaced };

    namespace detail {
        // `current` is a validated compositor snapshot, or empty when none is available.
        // Backend identity is checked separately because EGL can recycle handle values.
        [[nodiscard]] inline ContextOwnerState classify_context_owner(
            EglContextId owner, EglContextId current, bool same_backend) noexcept {
            if (!current.valid()) return ContextOwnerState::unavailable;
            if (!owner.valid() || (same_backend && owner == current))
                return ContextOwnerState::current;
            return ContextOwnerState::replaced;
        }
    }  // namespace detail
}  // namespace hyprimate::gl
