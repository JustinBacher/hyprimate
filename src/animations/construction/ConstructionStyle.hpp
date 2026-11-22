#pragma once

#include <cstdint>
#include <memory>

#include "WindowStyle.hpp"
#include "animations/construction/ConstructionSite.hpp"
#include "config_settings_generated.hpp"

class CHyprAnimationCanvas;
class CConstructionRenderer;
struct SSharedStyleResources;

struct SConstructionStyle {
    std::unique_ptr<hyprimate::ConstructionSite> site;
    std::uint64_t seed = 0;
    hyprimate::animation::MonitorId monitor = 0;
    hyprimate::animation::WorkspaceId workspace = 0;
    bool drew_identity = false;
    std::size_t peak_quads = 0;
    std::uint32_t work_width = 0, work_height = 0, work_fourcc = 0;
};

struct ConstructionParams {
    double open_seconds;
};

struct SConstructionResources : hyprimate::animation::ResourcesBase<SConstructionStyle,
                                    hyprimate::config::SConstructionSettings, ConstructionParams> {
    SConstructionResources() = default;
    explicit SConstructionResources(SSharedStyleResources &) {}
    AnimationPool animations;
    std::shared_ptr<CConstructionRenderer> renderer;
};

struct SConstructionModule {
    using State = SConstructionStyle;
    using Settings = hyprimate::config::SConstructionSettings;
    using Prepared = ConstructionParams;
    using Resources = SConstructionResources;
    static const Settings &settings(const auto &config) { return config.construction; }
    static constexpr auto KIND = hyprimate::config::Style::Construction;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
    static constexpr bool DRAWS_UNDER_BLUR = false;
    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &, std::uint64_t seed);
    static bool open(State &, const hyprimate::animation::Place &, Resources &, hyprimate::animation::Effects &);
    static hyprimate::animation::Frame advance(State &, const hyprimate::animation::Step &, Resources &, hyprimate::animation::Effects &);
    static hyprimate::animation::CloseStart close(State &, const hyprimate::animation::Place &, Resources &);
    static void end(State &, Resources &, hyprimate::animation::End) noexcept;
    static bool moves(const hyprimate::animation::Motion &, const Resources &) noexcept;
    static bool draw(State &, const hyprimate::animation::Draw<CHyprAnimationCanvas> &, Resources &);
};
