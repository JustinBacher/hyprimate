#pragma once

#include <memory>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "animations/smoke/SmokeLiveState.hpp"
#include "animations/shared/SheetResources.hpp"
#include "WindowStyle.hpp"
#include "config_generated.hpp"

class CSmokeRenderer;
class CHyprAnimationCanvas;
struct SSmokeResources;
bool draw_smoke(SSmokeStyle &state, CHyprAnimationCanvas &canvas, const SP<Render::IFramebuffer> &image,
    const hyprimate::Box &source, const hyprimate::Box &canvas_box, hyprimate::Vec2 image_origin,
    hyprimate::Vec2 image_size, double scale, const SSmokeResources &resources);

struct SSmokePrepared {
    hyprimate::SmokeParams movement;
    hyprimate::SmokeParams opening;
    hyprimate::SmokeParams closing;
    double open_duration = 0;
    double close_duration = 0;
};
struct SSmokeResources : hyprimate::animation::ResourcesBase<
                             SSmokeStyle, hyprimate::config::SSmokeSettings, SSmokePrepared> {
    explicit SSmokeResources(SSharedStyleResources &shared);
    std::shared_ptr<CSmokeRenderer> renderer;
    AnimationPool animations;
};
struct SSmokeModule {
    using State = SSmokeStyle;
    using Prepared = SSmokePrepared;
    using Resources = SSmokeResources;
    using Settings = hyprimate::config::SSmokeSettings;
    static const Settings &settings(const auto &config) { return config.smoke; }
    static constexpr auto KIND = hyprimate::config::Style::Smoke;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
    static constexpr bool DRAWS_UNDER_BLUR = false;

    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &resources, std::uint64_t seed);
    static bool open(State &state, const hyprimate::animation::Place &place, Resources &resources,
        hyprimate::animation::Effects &effects);
    static hyprimate::animation::Frame advance(State &state, const hyprimate::animation::Step &step,
        Resources &resources, hyprimate::animation::Effects &effects);
    static hyprimate::animation::CloseStart close(State &state, const hyprimate::animation::Place &place,
        Resources &resources);
    static void end(State &state, Resources &resources, hyprimate::animation::End why) noexcept;
    static bool moves(const hyprimate::animation::Motion &motion, const Resources &resources) noexcept;
    static void warm(Resources &resources);
    template<class Canvas> requires std::same_as<Canvas, CHyprAnimationCanvas>
    static bool draw(State &state, const hyprimate::animation::Draw<Canvas> &draw,
        Resources &resources) {
        return draw_smoke(state, *draw.canvas, *draw.image, draw.source, draw.canvas_box,
            draw.image_origin, draw.image_size, draw.scale, resources);
    }
};
