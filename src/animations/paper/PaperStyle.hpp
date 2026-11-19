#pragma once

#include <cstdint>

#include "WindowStyle.hpp"
#include "animations/paper/PaperLiveState.hpp"
#include "animations/shared/SheetMesh.hpp"
#include "config_settings_generated.hpp"

struct SSharedStyleResources;

struct SPaperPrepared {
    hyprimate::PaperParams open;
    hyprimate::PaperParams close;
};
struct SPaperResources : hyprimate::animation::ResourcesBase<SPaperStyle,
                             hyprimate::config::SPaperSettings, SPaperPrepared> {
    SPaperResources() = default;
    explicit SPaperResources([[maybe_unused]] SSharedStyleResources &shared) {}
    AnimationPool animations;
};
struct SPaperModule {
    using State = SPaperStyle;
    using Prepared = SPaperPrepared;
    using Resources = SPaperResources;
    using Settings = hyprimate::config::SPaperSettings;
    static const Settings &settings(const auto &config) { return config.paper; }
    static constexpr auto KIND = hyprimate::config::Style::Paper;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
    static constexpr bool DRAWS_UNDER_BLUR = false;

    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &resources, std::uint64_t seed);
    static bool open(State &state, const hyprimate::animation::Place &place,
        Resources &resources, hyprimate::animation::Effects &effects);
    static hyprimate::animation::Frame advance(State &state, const hyprimate::animation::Step &step,
        Resources &resources, hyprimate::animation::Effects &effects);
    static hyprimate::animation::CloseStart close(State &state, const hyprimate::animation::Place &place,
        Resources &resources);
    static void end(State &state, Resources &resources, hyprimate::animation::End why) noexcept;
    static bool moves([[maybe_unused]] const hyprimate::animation::Motion &motion,
        [[maybe_unused]] const Resources &resources) noexcept { return false; }

    template<hyprimate::animation::Vertex V>
    static hyprimate::animation::Mesh<V> mesh(State &state,
        const hyprimate::animation::MeshTarget<V> &target, [[maybe_unused]] Resources &resources) {
        return hyprimate::fill_sheet_mesh(*state.frame, target, state.crumple.shaded(),
            static_cast<float>(state.crumple.opacity()));
    }
};
