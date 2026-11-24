#pragma once

#include <concepts>
#include <type_traits>

#include "shared/WindowStyleList.hpp"
#include "workspace/cube/CubeStyle.hpp"
#include "workspace/planet_hop/PlanetHopStyle.hpp"
#include "workspace/rotation/RotationStyle.hpp"

namespace hyprimate::workspace {
    template <class Style>
    concept WorkspaceStyle = requires(Style::Resources &resources,
        const config::SWorkspaceSettings &settings,
        Render::CRenderContext &context,
        const WorkspaceFrame &frame,
        const JourneyEndpoint &endpoint,
        double progress,
        Direction direction,
        bool matching) {
        typename Style::Resources;
        { Style::KIND } -> std::convertible_to<config::WorkspaceStyle>;
        { Style::USES_WALLPAPER } -> std::convertible_to<bool>;
        { Style::LAYERED } -> std::convertible_to<bool>;
        { Style::duration(resources, endpoint, endpoint) } -> std::same_as<double>;
        { Style::travel(resources) } -> std::same_as<WorkspaceTravel>;
        { Style::live_boundary(resources) } -> std::same_as<double>;
        { Style::needs_individual_windows(resources) } -> std::same_as<bool>;
        { Style::uses_native_hud(resources) } -> std::same_as<bool>;
        { Style::uses_workspace_previews(resources) } -> std::same_as<bool>;
        { Style::options(settings) == Style::options(settings) } -> std::same_as<bool>;
        { Style::prepare(resources, settings) } -> std::same_as<void>;
        { resources.settings.prepared.duration } -> std::convertible_to<double>;
        { resources.settings.prepared.curve } -> std::convertible_to<std::string_view>;
        { resources.renderer.initialize() } -> std::same_as<bool>;
        { resources.renderer.release() } -> std::same_as<void>;
        {
            Style::draw(resources, context, frame, progress, direction, matching)
        } -> std::same_as<bool>;
    };

    template <class... Styles>
    struct UniqueResources : std::true_type {};
    template <class First, class... Rest>
    struct UniqueResources<First, Rest...>
        : std::bool_constant<
              (!std::is_same_v<typename First::Resources, typename Rest::Resources> && ...) &&
              UniqueResources<Rest...>::value> {};

    template <class List>
    struct WorkspaceStyleCoverage;
    template <class... Styles>
    struct WorkspaceStyleCoverage<animation::StyleList<Styles...>> {
        static consteval bool valid() {
            if constexpr (!(WorkspaceStyle<Styles> && ...)) {
                return false;
            }
            constexpr std::array<config::WorkspaceStyle, sizeof...(Styles)> KINDS{Styles::KIND...};
            if (sizeof...(Styles) != config::WORKSPACE_STYLES.size()) {
                return false;
            }
            for (const auto EXPECTED : config::WORKSPACE_STYLES) {
                unsigned count = 0;
                for (const auto ACTUAL : KINDS) {
                    count += ACTUAL == EXPECTED;
                }
                if (count != 1) {
                    return false;
                }
            }
            return UniqueResources<Styles...>::value;
        }
    };

    using WorkspaceStyles = animation::StyleList<RotationStyle, PlanetHopStyle, CubeStyle>;
    static_assert(WorkspaceStyleCoverage<WorkspaceStyles>::valid());
}  // namespace hyprimate::workspace
