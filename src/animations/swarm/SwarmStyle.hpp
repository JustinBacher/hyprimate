#pragma once

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "WindowAnimationEntry.hpp"
#include "animations/swarm/SwarmLiveState.hpp"
#include "animations/swarm/SwarmRenderer.hpp"
#include "animations/shared/SheetResources.hpp"
#include "config_generated.hpp"

class CHyprAnimationCanvas;
struct SSharedStyleResources;

struct SSwarmPrepared {
    hyprimate::SwarmParams open;
    hyprimate::SwarmParams close;
    hyprimate::SwarmParams movement;
    hyprimate::EdgeSet open_sides;
    hyprimate::EdgeSet close_sides;
    double open_duration_ms = -1.0;
    double close_duration_ms = -1.0;
};

struct SSwarmResources :
    hyprimate::animation::ResourcesBase<SSwarmStyle, hyprimate::config::SSwarmSettings, SSwarmPrepared> {
    explicit SSwarmResources([[maybe_unused]] SSharedStyleResources &shared) {}
    SP<CSwarmSprites> sprites = makeShared<CSwarmSprites>();
    AnimationPool animations;
};
struct SSwarmModule {
    using State = SSwarmStyle;
    using Prepared = SSwarmPrepared;
    using Resources = SSwarmResources;
    using Settings = hyprimate::config::SSwarmSettings;
    static const Settings &settings(const auto &config) { return config.swarm; }
    static constexpr auto KIND = hyprimate::config::Style::Swarm;
    static constexpr bool FIXED_CLOSE_PLACEMENT = false;
    static constexpr bool DRAWS_UNDER_BLUR = false;
    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &resources, std::uint64_t seed);
    static bool open(State &state, const hyprimate::animation::Place &place,
        Resources &resources, hyprimate::animation::Effects &effects);
    static hyprimate::animation::Frame advance(State &state, const hyprimate::animation::Step &step,
        Resources &resources, hyprimate::animation::Effects &effects);
    static hyprimate::animation::CloseStart close(State &state,
        const hyprimate::animation::Place &place, Resources &resources);
    static void end(State &state, Resources &resources, hyprimate::animation::End why) noexcept;
    static bool moves(const hyprimate::animation::Motion &motion, const Resources &resources);
    template<class Canvas>
    static bool draw(State &state, const hyprimate::animation::Draw<Canvas> &draw,
        Resources &resources) {
        return draw_canvas(state, *draw.canvas, *draw.image, draw.source, draw.output,
            draw.canvas_box, draw.image_origin, draw.image_size, resources);
    }
  private:
    static bool draw_canvas(State &state, CHyprAnimationCanvas &canvas,
        const SP<Render::IFramebuffer> &image, hyprimate::Box source, hyprimate::Box output,
        hyprimate::Box canvas_box, hyprimate::Vec2 image_origin, hyprimate::Vec2 image_size,
        Resources &resources);
};
