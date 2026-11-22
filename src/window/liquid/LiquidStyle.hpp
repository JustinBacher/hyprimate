#pragma once

#include "config_settings_generated.hpp"
#include "window/WindowAnimationEntry.hpp"
#include "window/WindowStyle.hpp"
#include "window/liquid/LiquidLiveState.hpp"

struct SSharedStyleResources;

struct SLiquidPrepared {
    hyprimate::LiquidParams movement;
    hyprimate::TransitionParams open;
    hyprimate::TransitionParams close;
    double movement_strength{};
    double open_strength{};
    double close_strength{};
    double emerge_drag{};
    bool close_splash{};
    hyprimate::animation::Rgba open_color;
    hyprimate::animation::Rgba close_color;
};

struct SLiquidResources : hyprimate::animation::ResourcesBase<SLiquidStyle,
                              hyprimate::config::SLiquidSettings,
                              SLiquidPrepared> {
    SLiquidResources() = default;
    explicit SLiquidResources([[maybe_unused]] SSharedStyleResources &shared) {
    }
    AnimationPool animations;
};

struct SLiquidModule {
    using State = SLiquidStyle;
    using Prepared = SLiquidPrepared;
    using Resources = SLiquidResources;
    using Settings = hyprimate::config::SLiquidSettings;
    static const Settings &settings(const auto &config) {
        return config.liquid;
    }
    static constexpr auto KIND = hyprimate::config::Style::Liquid;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
    static constexpr bool DRAWS_UNDER_BLUR = false;

    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &resources, std::uint64_t seed);
    static bool open(State &state,
        const hyprimate::animation::Place &place,
        Resources &resources,
        hyprimate::animation::Effects &effects);
    static hyprimate::animation::Frame advance(State &state,
        const hyprimate::animation::Step &step,
        Resources &resources,
        hyprimate::animation::Effects &effects);
    static hyprimate::animation::CloseStart close(
        State &state, const hyprimate::animation::Place &place, Resources &resources);
    static void end(State &state, Resources &resources, hyprimate::animation::End why) noexcept;
    static bool moves(
        const hyprimate::animation::Motion &motion, const Resources &resources) noexcept;

    template <hyprimate::animation::Vertex V>
    static hyprimate::animation::Mesh<V> mesh(
        State &state, const hyprimate::animation::MeshTarget<V> &target, Resources &resources) {
        const auto POSE = pose_for(state);
        const hyprimate::Box LOGICAL{target.source.x / target.scale,
            target.source.y / target.scale,
            target.source.w / target.scale,
            target.source.h / target.scale};
        const auto GEOMETRY = hyprimate::liquid_mesh_geometry(POSE, LOGICAL);
        const auto DEVICE = [scale = target.scale](hyprimate::Box box) {
            return hyprimate::Box{box.x * scale, box.y * scale, box.w * scale, box.h * scale};
        };
        return hyprimate::fill_liquid_mesh(POSE,
            target,
            state.grid,
            GEOMETRY.meniscus ? std::optional{DEVICE(*GEOMETRY.meniscus)} : std::nullopt,
            state.dying ? resources.prepared.close_color : resources.prepared.open_color,
            GEOMETRY.visible ? std::optional{DEVICE(*GEOMETRY.visible)} : std::nullopt);
    }

  private:
    static hyprimate::LiquidPose pose_for(const State &state);
};

static_assert(hyprimate::animation::StyleCore<SLiquidModule>);
static_assert(hyprimate::animation::MeshDrawn<SLiquidModule, hyprimate::animation::MeshVertex>);
