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
    float elapsed = 0, duration = 0, settle_elapsed = 0;
    float pointer_x = 0.5F, pointer_y = 0.5F;
    bool pointer_valid = false;
    hyprimate::shader::Event event = hyprimate::shader::Event::Open;
    bool failed = false;
};

struct SShaderModule {
    using State = SShaderStyle;
    struct Selection {
        std::shared_ptr<hyprimate::shader::WindowProgram> program;
        float duration = 0;
        bool selected = false;
        bool operator==(const Selection &) const = default;
    };
    static constexpr std::size_t EVENT_COUNT = 4;
    using Settings = std::array<Selection, EVENT_COUNT>;
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
        std::array<std::optional<Request>, EVENT_COUNT> requested;
    };

    static constexpr auto KIND = hyprimate::config::Style::Shader;
    static constexpr bool FIXED_CLOSE_PLACEMENT = true;
    static constexpr bool DRAWS_UNDER_BLUR = false;
    static constexpr bool DRAWS_MAPPED_UNDER_BLUR = false;
    static constexpr bool EXCLUSIVE_OPEN = true;
    static constexpr bool PUBLIC_CLOSE_ONLY = true;
    static constexpr bool PUBLIC_MAPPED_ONLY = true;
    static constexpr bool PRESERVE_ON_RELOAD = true;

    static std::array<Request, EVENT_COUNT> requests(const auto &config) {
        return {Request{config.open.effect, config.open.effectParameters, config.open.effectDuration},
            Request{config.close.effect, config.close.effectParameters, config.close.effectDuration},
            Request{config.move.shared.effect, config.move.shared.effectParameters, config.move.shared.effectDuration},
            Request{config.resize.shared.effect, config.resize.shared.effectParameters, config.resize.shared.effectDuration}};
    }
    static std::array<bool, EVENT_COUNT> event_overrides(const Resources &resources) noexcept {
        std::array<bool, EVENT_COUNT> overrides{};
        for (std::size_t i = 0; i < overrides.size(); ++i) overrides[i] = resources.prepared[i].selected;
        return overrides;
    }
    static std::array<std::string, EVENT_COUNT> refresh(Resources &resources,
        std::array<Request, EVENT_COUNT> requests, bool force);
    static bool can_begin_motion(const Resources &resources, hyprimate::animation::MotionKind kind,
        bool blurred) noexcept;
    static bool supports_open_blur(const Resources &resources) noexcept;
    static bool mapped_blur_safe(const State &state) noexcept;
    static bool close_blur_safe(const State &state) noexcept;
    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &resources, std::uint64_t seed);
    static bool open(State &state, const hyprimate::animation::Place &place, Resources &resources,
        hyprimate::animation::Effects &effects);
    static bool begin_motion(State &state, const hyprimate::animation::Place &place,
        Resources &resources, hyprimate::animation::MotionKind kind);
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
