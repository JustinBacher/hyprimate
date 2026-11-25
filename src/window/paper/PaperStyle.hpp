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
struct SPaperSimulationResources : hyprimate::animation::ResourcesBase<SPaperStyle,
                                       hyprimate::config::SPaperSettings,
                                       SPaperPrepared> {
    AnimationPool animations;
    hyprimate::PaperWorkers workers;
};
struct SPaperResources : SPaperSimulationResources {
    SPaperResources();
    explicit SPaperResources(SSharedStyleResources &);
    ~SPaperResources();

  private:
    friend struct SPaperModule;
    // Workers retain CPU runs and surfaces, never these compositor-thread GL owners.
    std::unique_ptr<CPaperRenderer> m_live_renderer, m_scratch_renderer;
};
struct SPaperModule {
    using State = SPaperStyle;
    using Prepared = SPaperPrepared;
    using Resources = SPaperResources;
    using Settings = hyprimate::config::SPaperSettings;
    using SimulationResources = SPaperSimulationResources;
    static const Settings &settings(const auto &config) {
        return config.paper;
    }
    static constexpr auto KIND = hyprimate::config::Style::Paper;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
    static constexpr bool DRAWS_UNDER_BLUR = false;

    static Prepared prepare_settings(const Settings &settings);
    static State start(const SimulationResources &resources, std::uint64_t seed);
    static bool open(State &state,
        const hyprimate::animation::Place &place,
        SimulationResources &resources,
        hyprimate::animation::Effects &effects);
    static hyprimate::animation::Frame advance(State &state,
        const hyprimate::animation::Step &step,
        SimulationResources &resources,
        hyprimate::animation::Effects &effects);
    static hyprimate::animation::CloseStart close(
        State &state, const hyprimate::animation::Place &place, SimulationResources &resources);
    static void end(
        State &state, SimulationResources &resources, hyprimate::animation::End why) noexcept;
    static bool moves(
        const hyprimate::animation::Motion &motion, const SimulationResources &) noexcept {
        return motion.translated || motion.resized;
    }

    static bool warm(Resources &resources);
    static bool draw(State &state,
        const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw,
        Resources &resources);
};
