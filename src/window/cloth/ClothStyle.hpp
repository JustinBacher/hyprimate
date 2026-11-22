#pragma once

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "config_generated.hpp"
#include "window/WindowAnimationEntry.hpp"
#include "window/cloth/ClothLiveState.hpp"
#include "window/shared/SheetMesh.hpp"
#include "window/shared/SheetResources.hpp"

struct SClothPrepared {
    hyprimate::ClothMovementParams movement;
    hyprimate::ClothParams open;
    hyprimate::ClothParams close;
};

struct SClothResources : hyprimate::animation::ResourcesBase<SClothStyle,
                             hyprimate::config::SClothSettings,
                             SClothPrepared> {
    explicit SClothResources(SSharedStyleResources &shared) : shade(shared.sheet_shade) {
    }
    hyprimate::ClothPhysics physics;
    CSheetShade &shade;
    AnimationPool animations;
};
struct SClothModule {
    using State = SClothStyle;
    using Prepared = SClothPrepared;
    using Resources = SClothResources;
    using Settings = hyprimate::config::SClothSettings;
    static const Settings &settings(const auto &config) {
        return config.cloth;
    }
    static constexpr auto KIND = hyprimate::config::Style::Cloth;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
    static constexpr bool DRAWS_UNDER_BLUR = false;

    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &resources, std::uint64_t seed);
    static bool open(State &state,
        const hyprimate::animation::Place &place,
        Resources &resources,
        hyprimate::animation::Effects &effects);
    static bool moves(
        const hyprimate::animation::Motion &motion, const Resources &resources) noexcept;
    static hyprimate::animation::Frame advance(State &state,
        const hyprimate::animation::Step &step,
        Resources &resources,
        hyprimate::animation::Effects &effects);
    static hyprimate::animation::CloseStart close(
        State &state, const hyprimate::animation::Place &place, Resources &resources);
    static void end(State &state, Resources &resources, hyprimate::animation::End why) noexcept;

    template <hyprimate::animation::Vertex V>
    static hyprimate::animation::Mesh<V> mesh(State &state,
        const hyprimate::animation::MeshTarget<V> &target,
        [[maybe_unused]] Resources &resources) {
        return hyprimate::fill_sheet_mesh(
            *state.frame, target, state.cloth.shaded(), static_cast<float>(state.cloth.opacity()));
    }
};
