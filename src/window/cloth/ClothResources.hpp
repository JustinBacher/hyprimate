#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "config_settings_generated.hpp"
#include "window/WindowAnimationEntry.hpp"
#include "window/cloth/ClothLiveState.hpp"

struct SClothPrepared {
    hyprimate::ClothMovementParams movement;
    hyprimate::ClothParams open;
    hyprimate::ClothParams close;
};

// The pool and worker owner share the style-resource lifetime, independently of Hyprland's GL
// resources. The pool is destroyed first, while retained playback can outlive both.
struct SClothRuntimeResources : hyprimate::animation::ResourcesBase<SClothStyle,
                                    hyprimate::config::SClothSettings,
                                    SClothPrepared> {
    bool begin(hyprimate::ClothFlutter &cloth,
        hyprimate::Transition kind,
        const hyprimate::ClothParams &params,
        std::uint64_t seed,
        const hyprimate::Box &window,
        std::span<const hyprimate::Box> obstacles,
        std::optional<hyprimate::ClothCloseGeometry> geometry = std::nullopt) noexcept {
        if (!physics && hyprimate::ClothFlutter::can_begin(kind, params, window, geometry) &&
            !ensure_physics()) {
            return false;
        }
        cloth.begin(kind, params, seed, window, obstacles, geometry);
        return cloth.active();
    }

    void begin_movement(hyprimate::ClothFlutter &cloth,
        const hyprimate::ClothMovementParams &params,
        std::uint64_t seed,
        const hyprimate::Box &from,
        const hyprimate::Box &target,
        double elapsed) noexcept {
        if (!physics) {
            if (!hyprimate::ClothFlutter::can_update_movement(target, elapsed) ||
                (hyprimate::ClothFlutter::can_begin_movement(params, from) &&
                    !ensure_physics())) {
                cloth.reset();
                return;
            }
        }
        cloth.begin_movement(params, seed, from);
    }

  private:
    bool ensure_physics() noexcept {
        if (physics) {
            return true;
        }
        try {
            physics.emplace();
            return true;
        } catch (...) {
            return false;
        }
    }

    std::optional<hyprimate::ClothPhysics> physics;

  public:
    AnimationPool animations;
};
