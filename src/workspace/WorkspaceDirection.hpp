#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "workspace/WorkspaceTransition.hpp"

namespace hyprimate::workspace {
    // startAnimation's `left` means the incoming workspace starts to the right.
    // A slide style's optional direction overrides that sign; vertical slides
    // still use the material's horizontal travel axis.
    inline Direction animation_direction(bool left, std::string_view style) {
        const auto SEPARATOR = style.find_first_of(" \t");
        if (SEPARATOR != std::string_view::npos) {
            const auto START = style.find_first_not_of(" \t", SEPARATOR);
            if (START != std::string_view::npos) {
                const auto WORD = style.substr(START, style.find_first_of(" \t", START) - START);
                if (WORD == "left" || WORD == "top") {
                    return Direction::Backward;
                }
                if (WORD == "right" || WORD == "bottom") {
                    return Direction::Forward;
                }
            }
        }
        return left ? Direction::Forward : Direction::Backward;
    }

    inline Direction direction_between(std::optional<std::uint32_t> source,
        std::optional<std::uint32_t> destination,
        bool source_special,
        bool destination_special) {
        if (source_special != destination_special) {
            return destination_special ? Direction::Forward : Direction::Backward;
        }
        return source && destination && *destination < *source ? Direction::Backward
                                                               : Direction::Forward;
    }
}  // namespace hyprimate::workspace
