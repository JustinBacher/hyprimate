#pragma once

#include <cstdint>
#include <memory>

#include "WindowStyle.hpp"
#include "animations/construction/ConstructionCloseSite.hpp"
#include "animations/construction/ConstructionMotionSite.hpp"
#include "animations/construction/ConstructionPublication.hpp"
#include "animations/construction/ConstructionSite.hpp"
#include "config_settings_generated.hpp"

class CHyprAnimationCanvas;
class CConstructionRenderer;
struct SSharedStyleResources;

struct SConstructionStyle {
    std::unique_ptr<hyprimate::ConstructionSite> site;
    std::unique_ptr<hyprimate::construction::MotionSite> motion;
    std::unique_ptr<hyprimate::construction::CloseSite> close_site;
    hyprimate::construction::Publication publication;
    hyprimate::Box drawn_source_device{};
    double drawn_scale = 0;
    bool drawn_native = false, drawn_art_current = false;
    std::uint64_t seed = 0;
    hyprimate::animation::MonitorId monitor = 0;
    bool close_started = false;
    hyprimate::animation::WorkspaceId workspace = 0;
    bool native_seated = false;
    std::size_t peak_quads = 0;
    std::uint32_t work_width = 0, work_height = 0, work_fourcc = 0;
};

struct ConstructionParams {
    double open_seconds, close_seconds;
    hyprimate::construction::TransportVariant transport;
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
    static constexpr bool NATIVE_PROTECTED_MAPPED = true;
    static constexpr bool PUBLIC_CLOSE_ONLY = true;
    static Prepared prepare_settings(const Settings &settings);
    static State start(const Resources &, std::uint64_t seed);
    static bool open(State &, const hyprimate::animation::Place &, Resources &, hyprimate::animation::Effects &);
    static hyprimate::animation::Frame advance(State &, const hyprimate::animation::Step &, Resources &, hyprimate::animation::Effects &);
    static hyprimate::animation::CloseStart close(State &, const hyprimate::animation::Place &, Resources &);
    static void invalidate_mapped_publication(State &, Resources &) noexcept;
    static void mapped_native_publication(State &, hyprimate::Box body, hyprimate::Box capture,
        hyprimate::Box source_device, double scale, Resources &) noexcept;
    static bool accepts_close_crop(const State &, hyprimate::Box local_crop,
        hyprimate::Box source_device, double scale) noexcept;
    static hyprimate::animation::Frame initial_close_frame(State &, const hyprimate::animation::Step &, Resources &) noexcept;
    static void end(State &, Resources &, hyprimate::animation::End) noexcept;
    static bool moves(const hyprimate::animation::Motion &, const Resources &) noexcept;
    static bool needs_visible_outputs(const Resources &resources) noexcept {
        using Mode = hyprimate::construction::TransportVariant;
        return resources.prepared.transport == Mode::Crane || resources.prepared.transport == Mode::PieceByPiece;
    }
    static bool draw(State &, const hyprimate::animation::Draw<CHyprAnimationCanvas> &, Resources &);
};
