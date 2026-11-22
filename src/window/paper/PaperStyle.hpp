#pragma once

#include <cstdint>
#include <memory>

#include "config_settings_generated.hpp"
#include "window/WindowStyle.hpp"
#include "window/paper/PaperLiveState.hpp"

struct SSharedStyleResources;
class CHyprAnimationCanvas;
class CPaperRenderer;

struct SPaperPrepared {
    hyprimate::PaperParams open;
    hyprimate::PaperParams close;
    hyprimate::PaperInteractionParams interaction;
    float shading = 0.7F;
};
struct SPaperResources : hyprimate::animation::ResourcesBase<SPaperStyle,
                             hyprimate::config::SPaperSettings,
                             SPaperPrepared> {
    SPaperResources() = default;
    explicit SPaperResources([[maybe_unused]] SSharedStyleResources &shared) {
    }
    AnimationPool animations;
    hyprimate::PaperWorkers workers;
    std::shared_ptr<CPaperRenderer> renderer, scratch_renderer;
};
struct SPaperModule {
    using State = SPaperStyle;
    using Prepared = SPaperPrepared;
    using Resources = SPaperResources;
    using Settings = hyprimate::config::SPaperSettings;
    static const Settings &settings(const auto &config) {
        return config.paper;
    }
    static constexpr auto KIND = hyprimate::config::Style::Paper;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
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
    static bool moves(const hyprimate::animation::Motion &motion, const Resources &) noexcept {
        return motion.translated || motion.resized;
    }

    static bool warm(Resources &resources);
    static bool draw(State &state,
        const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw,
        Resources &resources);
};
