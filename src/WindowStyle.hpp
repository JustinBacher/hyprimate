#pragma once

#include <concepts>
#include <cstdint>

#include "WindowAnimationEntry.hpp"

namespace hyprimate::animation {
template<class S> concept StyleCore = requires(
    S::State &state, S::Resources &resources,
    const S::Resources &constant_resources, const S::Settings &settings,
    const Place &place, const Step &step, Effects &effects, End why) {
    typename S::State;
    typename S::Settings;
    typename S::Prepared;
    requires std::derived_from<typename S::Resources,
        ResourcesBase<typename S::State, typename S::Settings, typename S::Prepared>>;
    requires std::movable<typename S::State>;
    requires std::same_as<decltype(resources.animations), CSlotPool<Slot<typename S::State>>>;
    { S::KIND } -> std::convertible_to<config::Style>;
    { S::FIXED_CLOSE_PLACEMENT } -> std::convertible_to<bool>;
    { S::DRAWS_UNDER_BLUR } -> std::convertible_to<bool>;
    { S::prepare_settings(settings) } -> std::same_as<typename S::Prepared>;
    { S::start(constant_resources, std::uint64_t{}) } -> std::same_as<typename S::State>;
    { S::open(state, place, resources, effects) } -> std::same_as<bool>;
    { S::advance(state, step, resources, effects) } -> std::same_as<Frame>;
    { S::close(state, place, resources) } -> std::same_as<CloseStart>;
    { S::end(state, resources, why) } noexcept -> std::same_as<void>;
};
template<class S, class V> concept MeshDrawn = Vertex<V> && requires(
    S::State &state, const MeshTarget<V> &target, S::Resources &resources) {
    { S::mesh(state, target, resources) } -> std::same_as<Mesh<V>>;
};
template<class S, class Canvas> concept SelfDrawn = requires(
    S::State &state, const Draw<Canvas> &draw, S::Resources &resources) {
    { S::draw(state, draw, resources) } -> std::same_as<bool>;
};
template<class S, class Canvas> concept ScreenContract = requires(
    S::Resources &resources, const S::Resources &constant_resources,
    const ScreenFacts &facts, const ScreenDraw<Canvas> &draw, MonitorId monitor) {
    { S::Screen::tick(resources, facts) } -> std::same_as<void>;
    { S::Screen::needs_frame(constant_resources, monitor) } -> std::same_as<bool>;
    { S::Screen::under_window(resources, draw) } -> std::same_as<void>;
    { S::Screen::after_windows(resources, draw) } -> std::same_as<void>;
    { S::Screen::clear(resources, std::optional<MonitorId>{}) } -> std::same_as<void>;
};
template<class S, class Canvas> concept WindowStyle = StyleCore<S> &&
    (MeshDrawn<S, typename Canvas::Vertex> != SelfDrawn<S, Canvas>) &&
    (!requires { typename S::Screen; } || ScreenContract<S, Canvas>);
} // namespace hyprimate::animation
