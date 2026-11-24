#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "config_settings_generated.hpp"
#include "window/WindowStyle.hpp"
#include "window/shader/ShaderRenderer.hpp"

struct SSharedStyleResources;

struct SShaderStyle {
    std::unique_ptr<hyprimate::shader::WindowRenderer> renderer;
    std::uint64_t tick = 0, seed = 0;
    float elapsed = 0, duration = 0;
    float pointer_x = 0.5F, pointer_y = 0.5F;
    bool pointer_valid = false;
    bool closing = false, failed = false;
};

struct SShaderModule {
    using State = SShaderStyle;
    struct Selection {
        std::shared_ptr<hyprimate::shader::WindowProgram> program;
        float duration = 0;
        bool selected = false;
        bool operator==(const Selection &) const = default;
    };
    using Settings = std::array<Selection, 2>;
    using Prepared = Settings;
    struct Request {
        std::string path, parameters;
        double duration = 0;
        bool operator==(const Request &) const = default;
    };
    struct Resources : hyprimate::animation::ResourcesBase<State, Settings, Prepared> {
        Resources() = default;
        explicit Resources(SSharedStyleResources & /*shared*/) {}
        AnimationPool animations;
        std::array<std::optional<Request>, 2> requested;
    };

    static constexpr auto KIND = hyprimate::config::Style::Shader;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
    static constexpr bool DRAWS_UNDER_BLUR = false;
    static constexpr bool DRAWS_MAPPED_UNDER_BLUR = false;
    static constexpr bool EXCLUSIVE_OPEN = true;
    static constexpr bool PUBLIC_CLOSE_ONLY = true;
    static constexpr bool PUBLIC_MAPPED_ONLY = true;
    static constexpr bool PRESERVE_ON_RELOAD = true;

    static std::array<Request, 2> requests(const auto &config) {
        return {Request{config.open.effect, config.open.effectParameters, config.open.effectDuration},
            Request{config.close.effect, config.close.effectParameters, config.close.effectDuration}};
    }
    static std::array<bool, 2> event_overrides(const Resources &resources) noexcept {
        return {resources.prepared[0].selected, resources.prepared[1].selected};
    }
    static std::array<std::string, 2> refresh(Resources &resources,
        std::array<Request, 2> requests, bool force);
    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &resources, std::uint64_t seed);
    static bool open(State &state, const hyprimate::animation::Place &place, Resources &resources,
        hyprimate::animation::Effects &effects);
    static hyprimate::animation::CloseStart close(State &state, const hyprimate::animation::Place &place,
        Resources &resources);
    static bool bind_close_source(State &state, const hyprimate::Box &source,
        const CHyprAnimationCanvas::Image &image) noexcept;
    static hyprimate::animation::Frame initial_close_frame(State &state,
        const hyprimate::animation::Step &step, Resources &resources) noexcept;
    static hyprimate::animation::Frame advance(State &state, const hyprimate::animation::Step &step,
        Resources &resources, hyprimate::animation::Effects &effects);
    static bool draw(State &state, const hyprimate::animation::Draw<CHyprAnimationCanvas> &draw,
        Resources &resources);
    static void end(State &state, Resources &resources, hyprimate::animation::End why) noexcept;
};
