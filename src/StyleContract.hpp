#pragma once

#include <concepts>
#include <functional>
#include <span>
#include <type_traits>

// All close overloads must be visible before this definition.
#include "AnimationCloseRenderer.hpp"
#include "CloseStyles.hpp"
#include "LiveRender.hpp"
#include "LiveStyle.hpp"

template <class T>
concept StyleModule = requires(typename T::State &state, const typename T::State &constant,
    const typename T::CloseData &data, CPluginRuntime &runtime, const CPluginRuntime &const_runtime,
    typename T::Tracked &tracked, CAnimationTransformer &transformer,
    const CAnimationTransformer *transformer_ptr, const PHLWINDOW &window,
    hyprimate::WindowSnapshot &snapshot, const hyprimate::TrackDecision &decision,
    const typename T::Inputs &inputs, const hyprimate::config::SConfig &config,
    CAnimationCloseRenderer::SPending &pending, CAnimationCloseRenderer::SClosing &record,
    const std::function<hyprimate::EdgeInput()> &edges,
    const std::function<std::span<const hyprimate::Box>()> &obstacles,
    const std::function<std::uint64_t()> &seed,
    Render::CRenderContext &ctx, const Render::SWindowTransformBuffer &input,
    const Render::SWindowTransformContext &context, const SLiveRenderGeometry &geometry,
    const CBox &box, const PHLMONITOR &monitor, const SP<Render::ITexture> &texture,
    const SP<Desktop::IFadeout> &fadeout, CRegion damage,
    typename T::Resources &resources, const typename T::Resources &const_resources) {
    typename T::Module;
    typename T::Resources;
    { T::KIND } -> std::convertible_to<hyprimate::config::Style>;
    { T::FIXED_CLOSE_PLACEMENT } -> std::convertible_to<bool>;
    { T::inputs(const_runtime) } -> std::same_as<typename T::Inputs>;
    { T::initial(inputs, std::uint64_t{}) } -> std::same_as<typename T::State>;
    { T::prepare(runtime, resources, false) } -> std::same_as<void>;
    { T::movement_requested(decision) } noexcept -> std::same_as<bool>;
    { T::strength(const_runtime) } -> std::same_as<double>;
    { T::usable(const_runtime, const_resources) } -> std::same_as<bool>;
    { T::open(runtime, tracked, window, resources) } -> std::same_as<bool>;
    { T::movement(runtime, transformer, window, decision, 0.0, false, false) } -> std::same_as<void>;
    { T::snapshot(transformer_ptr, snapshot) } -> std::same_as<void>;
    { T::cancelOpening(transformer) } -> std::same_as<void>;
    { T::cancelMovement(transformer) } -> std::same_as<void>;
    { T::available(constant) } noexcept -> std::same_as<bool>;
    { T::deforming(constant) } noexcept -> std::same_as<bool>;
    { T::builtFor(constant, inputs) } noexcept -> std::same_as<bool>;
    { T::step(runtime, transformer, 0.0, hyprimate::Vec2{}, false) } -> std::same_as<void>;
    { live_extents(constant, box) } -> std::same_as<CBox>;
    { render_live(state, ctx, input, context, geometry, resources) } -> std::same_as<Render::SWindowTransformBuffer>;
    { close_style::close_enabled(data, config) } -> std::same_as<bool>;
    { close_style::stage(state, pending, window, false, config, edges, obstacles, seed) } -> std::same_as<bool>;
    { close_style::valid(constant, pending, false) } -> std::same_as<bool>;
    { close_style::begin(state, pending, false) } -> std::same_as<void>;
    { close_style::running(constant) } -> std::same_as<bool>;
    { close_style::finished(constant) } -> std::same_as<bool>;
    { close_style::needs_owned(constant) } -> std::same_as<bool>;
    { close_style::advance(state, record, 0.0, false) } -> std::same_as<void>;
    { close_style::settled(constant) } -> std::same_as<bool>;
    { close_style::fill(state, record) } -> std::same_as<void>;
    { close_style::footprint(constant, record) } -> std::same_as<CBox>;
    { close_style::render(state, record, ctx, monitor, texture, resources) } -> std::same_as<void>;
    { close_style::render_native(state, record, ctx, fadeout, texture, damage) } -> std::same_as<bool>;
};
