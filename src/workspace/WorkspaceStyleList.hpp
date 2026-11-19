#pragma once

#include <concepts>
#include <type_traits>

#include "WindowStyleList.hpp"
#include "workspace/rotation/RotationStyle.hpp"

namespace hyprimate::workspace {
template<class Style>
concept WorkspaceStyle = requires(typename Style::Resources &resources,
    const config::SWorkspaceSettings &settings, Render::CRenderContext &context,
    const SP<Render::IFramebuffer> &buffer, double progress, Direction direction, bool matching) {
    typename Style::Resources;
    { Style::kind } -> std::convertible_to<config::WorkspaceStyle>;
    { Style::usesWallpaper } -> std::convertible_to<bool>;
    { Style::prepare(resources, settings) } -> std::same_as<void>;
    { resources.settings.prepared.duration } -> std::convertible_to<double>;
    { resources.settings.prepared.curve } -> std::convertible_to<std::string_view>;
    { resources.renderer.initialize() } -> std::same_as<bool>;
    { resources.renderer.release() } -> std::same_as<void>;
    { Style::draw(resources, context, buffer, buffer, buffer, buffer, buffer, progress, direction, matching) }
        -> std::same_as<bool>;
};

template<class... Styles> struct UniqueResources : std::true_type {};
template<class First, class... Rest>
struct UniqueResources<First, Rest...>
    : std::bool_constant<(!std::is_same_v<typename First::Resources, typename Rest::Resources> && ...) &&
                         UniqueResources<Rest...>::value> {};

template<class List> struct WorkspaceStyleCoverage;
template<class... Styles>
struct WorkspaceStyleCoverage<animation::StyleList<Styles...>> {
    static consteval bool valid() {
        if constexpr (!(WorkspaceStyle<Styles> && ...)) return false;
        constexpr std::array<config::WorkspaceStyle, sizeof...(Styles)> kinds{Styles::kind...};
        if (sizeof...(Styles) != config::WORKSPACE_STYLES.size()) return false;
        for (const auto expected : config::WORKSPACE_STYLES) {
            unsigned count = 0;
            for (const auto actual : kinds) count += actual == expected;
            if (count != 1) return false;
        }
        return UniqueResources<Styles...>::value;
    }
};

using WorkspaceStyles = animation::StyleList<RotationStyle>;
static_assert(WorkspaceStyleCoverage<WorkspaceStyles>::valid());
}
