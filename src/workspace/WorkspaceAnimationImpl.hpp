#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <drm_fourcc.h>
#include <hyprland/protocols/wlr-layer-shell-unstable-v1.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/rule/layerRule/LayerRuleApplicator.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/desktop/state/FadingOutState.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/LayerState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/input/InputMethodRelay.hpp>
#include <hyprland/src/managers/input/UnifiedWorkspaceSwipeGesture.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/SceneResources.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/state/workspace/PlacementController.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/UniquePtr.hpp>

#include "gl/CompositorContext.hpp"
#include "gl/FramebufferAllocationState.hpp"
#include "gl/NativeResource.hpp"
#include "gl/RawGlBracket.hpp"
#include "perf/LiveProfiler.hpp"
#include "perf/MonitorIdentity.hpp"
#include "shader/SceneShader.hpp"
#include "workspace/WorkspaceAnimation.hpp"
#include "workspace/WorkspaceCallbackScope.hpp"
#include "workspace/WorkspaceCapture.hpp"
#include "workspace/WorkspaceCapturePlan.hpp"
#include "workspace/WorkspaceFrame.hpp"
#include "workspace/WorkspaceIdentity.hpp"
#include "workspace/WorkspaceProfiling.hpp"
#include "workspace/WorkspaceStyleList.hpp"
#include "workspace/WorkspaceTurn.hpp"

namespace workspace_animation_detail {
    using Clock = std::chrono::steady_clock;
    using TurnKind = hyprimate::workspace::CaptureKind;
    using hyprimate::workspace::Direction;
    using RenderFn = void (*)(Render::IHyprRenderer *,
        Render::CRenderContext &,
        PHLMONITOR,
        PHLWORKSPACE,
        const Time::steady_tp &,
        Render::eSceneMode,
        const Vector2D &,
        const float &);
    using DragFn = void (*)(
        Render::IHyprRenderer *, Render::CRenderContext &, PHLMONITOR, const Time::steady_tp &);
    // Borrow protected native draw methods without downcasting the renderer.
    struct PreviewDrawAccess : Render::IHyprRenderer {
        static constexpr auto BACKGROUND = &PreviewDrawAccess::renderBackground;
        static constexpr auto LAYER = &PreviewDrawAccess::renderLayer;
    };
    // Native draw callbacks can synchronously dispatch another workspace request.
    // Never capture recursively or stage a new turn from an offscreen pass.
    struct ScopedCapture {
        explicit ScopedCapture(int &depth) : m_depth(depth) {
            ++m_depth;
        }
        ~ScopedCapture() {
            --m_depth;
        }
        ScopedCapture(const ScopedCapture &) = delete;
        ScopedCapture &operator=(const ScopedCapture &) = delete;
        ScopedCapture(ScopedCapture &&) = delete;
        ScopedCapture &operator=(ScopedCapture &&) = delete;

      private:
        int &m_depth;
    };
    std::optional<std::uint32_t> numbered(const PHLWORKSPACE &workspace);
    bool safe_scene();
    enum class Coverage { Preserve, OpaqueTarget, ResetSource };
    enum class CopySource { Retained, BorrowedNative };
    std::uint32_t capture_format(const PHLMONITOR &monitor);
    std::uint64_t framebuffer_bytes(const SP<Render::IFramebuffer> &framebuffer);
    bool copy_capture(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &target,
        Coverage coverage,
        bool downsample,
        const std::optional<CBox> &crop = std::nullopt,
        CopySource ownership = CopySource::Retained);
    bool copy_opaque(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &target,
        bool opaque = true,
        bool downsample = false);
}  // namespace workspace_animation_detail

using workspace_animation_detail::capture_format;
using workspace_animation_detail::Clock;
using workspace_animation_detail::copy_capture;
using workspace_animation_detail::copy_opaque;
using workspace_animation_detail::CopySource;
using workspace_animation_detail::Coverage;
using workspace_animation_detail::Direction;
using workspace_animation_detail::DragFn;
using workspace_animation_detail::framebuffer_bytes;
using workspace_animation_detail::numbered;
using workspace_animation_detail::PreviewDrawAccess;
using workspace_animation_detail::RenderFn;
using workspace_animation_detail::safe_scene;
using workspace_animation_detail::ScopedCapture;
using workspace_animation_detail::TurnKind;

class CWorkspaceAnimation::Impl : public std::enable_shared_from_this<Impl> {
  public:
    explicit Impl(HANDLE handle);
    std::function<void(const PHLMONITOR &)> prepare_capture;
    ~Impl() {
        shutdown();
    }
    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;

    using WallpaperIdentity = hyprimate::workspace::WallpaperIdentity<SP<Render::ITexture>>;
    struct WallpaperResource {
        WP<CWLSurfaceResource> surface;
        std::uint64_t generation = 0;
        std::optional<CBox> geometry;
        float alpha = 1.F;
        std::size_t plane = 0;
        CHyprSignalListener commit;
    };

    struct MonitorState;
    struct TurnPort {
        using Workspace = PHLWORKSPACEREF;
        using Preview = hyprimate::workspace::WorkspacePreview;
        Impl *owner;
        MonitorState *entry;
        [[nodiscard]] bool buffers(bool active) const;
        [[nodiscard]] static bool scene_safe();
        [[nodiscard]] bool hud_safe() const;
        [[nodiscard]] bool capture(TurnKind kind,
            const hyprimate::workspace::TurnComposition<Workspace> &composition) const;
        [[nodiscard]] double duration(const hyprimate::workspace::JourneyEndpoint &from,
            const hyprimate::workspace::JourneyEndpoint &destination,
            double base) const;
        [[nodiscard]] std::optional<std::pair<double, hyprimate::workspace::Transition::Curve>>
        next_pair(const hyprimate::workspace::JourneyEndpoint &from,
            const hyprimate::workspace::JourneyEndpoint &destination) const;
        [[nodiscard]] hyprimate::workspace::WorkspaceTravel travel() const {
            return owner->m_travel;
        }
        [[nodiscard]] double live_boundary() const;
        [[nodiscard]] bool reuse_captures() const;
        [[nodiscard]] double max_frame_gap() const {
            return owner->m_max_frame_gap;
        }
        [[nodiscard]] bool layered() const {
            return owner->m_layered;
        }
        [[nodiscard]] bool native_hud() const {
            return owner->m_native_hud;
        }
        [[nodiscard]] static double now() {
            return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
        }
        [[nodiscard]] bool clone_capture(TurnKind kind) const;
        void release_blend(TurnKind kind) const;
        [[nodiscard]] hyprimate::workspace::InactiveSourceFacts inactive_source_facts(
            const hyprimate::workspace::TurnComposition<Workspace> &composition) const;
        [[nodiscard]] bool capture_inactive_source(
            const hyprimate::workspace::TurnComposition<Workspace> &composition) const;
        void swap_slots() const;
        void damage() const;
        void diagnostic(std::string_view reason) const;
        void landed() const;
    };
    struct MonitorState {
        explicit MonitorState(Impl *owner) : port{.owner = owner, .entry = this} {
        }
        void latch_effect() {
            effect_program = port.owner->m_effect_program;
            base_duration = port.owner->m_turns.duration();
            curve = port.owner->m_turns.curve();
            effect.reset();
            effect_generation = 0;
            effect_declined = false;
        }
        PHLMONITORREF monitor;
        SP<Render::IFramebuffer> source;
        SP<Render::IFramebuffer> destination;
        // One simulation per presented endpoint pair, including reversals.
        std::shared_ptr<hyprimate::shader::SceneProgram> effect_program;
        bool effect_declined = false;
        double base_duration = 0.0;
        hyprimate::workspace::Transition::Curve curve;
        std::unique_ptr<hyprimate::shader::SceneShader> effect;
        std::uint64_t effect_generation = 0;
        std::uint64_t effect_seed = 0;
        SP<Render::IFramebuffer> presentation;
        SP<Render::IFramebuffer> source_wallpaper;
        SP<Render::IFramebuffer> destination_wallpaper;
        SP<Render::IFramebuffer> opaque_backdrop;
        std::vector<WallpaperIdentity> source_wallpaper_identity;
        std::vector<WallpaperIdentity> destination_wallpaper_identity;
        std::vector<std::unique_ptr<WallpaperResource>> wallpaper_resources;
        bool wallpaper_captured = false;
        hyprimate::workspace::WorkspaceCapture source_frame;
        hyprimate::workspace::WorkspaceCapture destination_frame;
        hyprimate::workspace::WorkspaceCapture *capturing_frame = nullptr;
        hyprimate::workspace::CaptureSession capture_policy;
        bool capture_failed = false;
        std::size_t floating_used = 0;
        struct CaptureBlend {
            std::shared_ptr<hyprimate::workspace::WorkspaceCapture> stale;
            std::shared_ptr<std::vector<hyprimate::workspace::WorkspaceFloating>> presentation;
        };
        CaptureBlend source_blend;
        CaptureBlend destination_blend;
        std::vector<WallpaperIdentity> source_shell_identity;
        std::vector<WallpaperIdentity> destination_shell_identity;
        std::uint64_t allocated_bytes = 0;
        Vector2D logical_size;
        std::vector<WallpaperIdentity> shell_probe_identity;
        double scale = 0;
        wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
        uint32_t output_format = 0;
        SP<Render::CSceneResources> source_resources;
        std::shared_ptr<std::vector<hyprimate::workspace::WorkspacePreview>> neighbors;
        // Provenance for opt-in accounting only. History wallpaper images are
        // shared across turns and must not be charged to this animation.
        std::vector<WP<Render::IFramebuffer>> profile_previews;
        std::vector<GLuint> profile_attachments;
        Clock::time_point last = Clock::now();
        TurnPort port;
    };
    struct InventoryPort {
        using Fact =
            hyprimate::workspace::InventoryWorkspace<PHLWORKSPACEREF, SP<Render::IFramebuffer>>;
        Impl *owner;
        MonitorState *entry;
        PHLMONITOR monitor;
        hyprimate::workspace::WorkspaceTurn<TurnPort> *turn_identity = nullptr;
        std::uint64_t turn_revision = 0;
        std::uint64_t inventory_revision = 0;
        std::int64_t composition_identity = 0;
        mutable bool revoked = false;
        [[nodiscard]] static bool owns(const PHLWORKSPACEREF &workspace, std::uint64_t identity) {
            const auto WORKSPACE = workspace.lock();
            const auto MONITOR = WORKSPACE ? WORKSPACE->m_monitor.lock() : nullptr;
            return MONITOR && MONITOR->m_enabled && MONITOR->m_output &&
                   static_cast<std::uint64_t>(MONITOR->m_id) == identity;
        }
        [[nodiscard]] bool allowed() const;
        [[nodiscard]] std::uint64_t revision() const {
            return owner->m_inventory_revision;
        }
        [[nodiscard]] static double now() {
            return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
        }
        [[nodiscard]] static std::size_t workspace_count() {
            return static_cast<std::size_t>(
                std::ranges::distance(State::Workspace::state()->workspaces()));
        }
        [[nodiscard]] std::vector<Fact> workspaces() const;
        [[nodiscard]] bool includes(const hyprimate::workspace::JourneyEndpoint &candidate,
            const hyprimate::workspace::JourneyEndpoint &source,
            const hyprimate::workspace::JourneyEndpoint &destination) const {
            bool included = false;
            owner->m_styles.for_each([&]<class Style>(const auto &resources) {
                if (Style::KIND != owner->m_style_kind) {
                    return;
                }
                if constexpr (requires {
                                  Style::includes_workspace(
                                      resources, candidate, source, destination);
                              }) {
                    included = Style::includes_workspace(resources, candidate, source, destination);
                }
            });
            return included;
        }
        [[nodiscard]] hyprimate::workspace::PreviewCaptureFacts capture_facts(
            const PHLWORKSPACEREF &workspace) const;
        [[nodiscard]] hyprimate::workspace::CompositionPlacements &placements() const {
            return owner->m_placements;
        }
        [[nodiscard]] SP<Render::IFramebuffer> local_wallpaper(int side) const;
        [[nodiscard]] SP<Render::IFramebuffer> capture(const PHLWORKSPACEREF &workspace,
            const SP<Render::IFramebuffer> &wallpaper,
            hyprimate::workspace::PreviewCapturePlan plan) const;
        void publish(
            const std::shared_ptr<std::vector<hyprimate::workspace::WorkspacePreview>> &previews)
            const {
            // prepare() has authorized the retained vector after its last callback.
            entry->neighbors = previews;
        }
    };
    [[nodiscard]] hyprimate::workspace::CaptureFeatures capture_features() const {
        return {.wallpaper = m_uses_wallpaper,
            .layered = m_layered,
            .individual = m_individual_windows,
            .native_hud = m_native_hud};
    }

    using Composition = hyprimate::workspace::TurnComposition<PHLWORKSPACEREF>;
    struct Snapshot {
        PHLMONITOR monitor;
        Composition composition;
        bool ready = false;
        std::shared_ptr<MonitorState> state;
        std::shared_ptr<hyprimate::workspace::WorkspaceTurn<TurnPort>> turn;
        std::uint64_t revision = 0;
        std::uint64_t lifecycle = 0;
        std::optional<Direction> native_direction = std::nullopt;
    };
    Composition composition(const PHLMONITOR &monitor, bool ordered = false) {
        const auto NUMBER = numbered(monitor->m_activeWorkspace);
        std::uint32_t order = 0;
        if (ordered && NUMBER) {
            for (const auto &candidate : State::Workspace::state()->workspaces()) {
                const auto NUMBER_ID = candidate->numberedID();
                if (NUMBER_ID && *NUMBER_ID < *NUMBER &&
                    candidate->type() != Workspace::eWorkspaceType::SPECIAL) {
                    ++order;
                }
            }
        }
        std::uint64_t placement = 0;
        if (ordered && monitor->m_activeWorkspace) {
            if (NUMBER && !monitor->m_activeSpecialWorkspace) {
                placement = m_placements.get(NUMBER, {}, std::nullopt);
            } else {
                const auto NORMAL = monitor->m_activeWorkspace->addressableName();
                if (monitor->m_activeSpecialWorkspace) {
                    const auto SPECIAL = monitor->m_activeSpecialWorkspace->addressableName();
                    placement = m_placements.get(std::nullopt, {}, std::string_view{SPECIAL});
                } else {
                    placement = m_placements.get(NUMBER, NORMAL, std::nullopt);
                }
            }
        }
        return {.identity = composition_id(monitor),
            .workspace = monitor->m_activeWorkspace,
            .numbered = NUMBER,
            .special = monitor->m_activeSpecialWorkspace != nullptr,
            .logical_order = order,
            .placement = placement,
            .owner = static_cast<std::uint64_t>(monitor->m_id),
            .monitor_x = monitor->m_position.x,
            .monitor_y = monitor->m_position.y,
            .monitor_width = monitor->m_size.x,
            .monitor_height = monitor->m_size.y};
    }
    std::int64_t composition_id(const PHLMONITOR &monitor) {
        return monitor ? m_compositions.get(
                             monitor->m_activeWorkspace, monitor->m_activeSpecialWorkspace)
                       : 0;
    }

    bool install(std::string &error);
    void shutdown();
    void on_render_stage(const Event::SRenderStageEvent &event);
    void configure(const hyprimate::config::SWorkspaceSettings &settings, bool force);
    template <class Style>
    void configure_style(bool enabled, Style::Resources &resources);
    bool request_enabled();
    void pre_checks(const PHLMONITOR &monitor);
    void pre_render(const PHLMONITOR &monitor);
    void remove_monitor(const PHLMONITOR &monitor);
    void clear();

    std::vector<Snapshot> snapshot(std::initializer_list<PHLMONITOR> monitors);
    void finish(std::vector<Snapshot> &snapshots);
    void mutate(const std::vector<PHLMONITOR> &monitors, const std::function<void()> &original);
    Snapshot *staged_mutation(const PHLMONITOR &monitor);
    bool burn_window_planes_allowed(const PHLMONITOR &monitor, bool special_request = false) const;
    bool special_workspace_in_native_scene() const;
    void decline_burn_transition(std::initializer_list<PHLMONITOR> monitors);
    bool transition_destination_allowed(
        const PHLMONITOR &monitor, const PHLWORKSPACE &normal, const PHLWORKSPACE &special);
    bool transition_destination_allowed(const PHLMONITOR &monitor, std::int64_t identity);
    void cancel_transition(const PHLMONITOR &monitor);
    void decline_all_burn_transitions();
    bool latched_shader_turn(const std::shared_ptr<MonitorState> &entry);
    bool has_latched_shader_turn();
    void expire_unlatched_turns();
    void capture_gesture(const PHLMONITOR &monitor);
    void clear_gesture();
    bool queue(Render::CRenderContext &ctx, const PHLMONITOR &monitor, const Time::steady_tp &now);
    bool ready(const PHLMONITOR &monitor) const;
    void inhibit_scanout(const PHLMONITOR &monitor);
    void restore_scanout(const PHLMONITOR &monitor = {});
    bool needs_native_prime(const PHLMONITOR &monitor, bool enabled) const;
    void prime_outputs() const;
    bool initialize_selected_style();

    static void *original(CFunctionHook *hook) {
        return hook != nullptr ? hook->m_original : nullptr;
    }
    RenderFn render_original() const {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland exposes original
        // hook functions as void pointers.
        return reinterpret_cast<RenderFn>(original(m_render_hook));
    }
    DragFn drag_original() const {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland exposes original
        // hook functions as void pointers.
        return reinterpret_cast<DragFn>(original(m_drag_hook));
    }

    static Impl *s_active;

    static std::uintptr_t monitor_key(const PHLMONITOR &monitor) {
        return std::bit_cast<std::uintptr_t>(monitor.get());
    }
    [[nodiscard]] hyprimate::perf::Key profile_key() const noexcept {
        hyprimate::perf::Key key{hyprimate::perf::Domain::Workspace, {}, "transition"};
        m_styles.for_each([&]<class Style>(const auto &) {
            if (Style::KIND == m_style_kind) {
                key.style = Style::NAME;
            }
        });
        return key;
    }
    hyprimate::workspace::WorkspaceTurn<TurnPort> *turn(const PHLMONITOR &monitor) {
        return monitor ? m_turns.find(monitor_key(monitor)) : nullptr;
    }
    const std::shared_ptr<MonitorState> *state_slot(const PHLMONITOR &monitor) const;
    MonitorState *state(const PHLMONITOR &monitor) const {
        const auto *slot = state_slot(monitor);
        return slot ? slot->get() : nullptr;
    }
    std::shared_ptr<MonitorState> ensure_state(const PHLMONITOR &monitor);
    bool ensure_buffers(MonitorState &entry, const PHLMONITOR &monitor);
    hyprimate::workspace::CaptureBufferFacts buffer_facts(
        const MonitorState &entry, const PHLMONITOR &monitor);
    static void release_floating_tail(
        MonitorState &entry, hyprimate::workspace::WorkspaceCapture &frame);
    bool capture(MonitorState &entry,
        const PHLMONITOR &monitor,
        const PHLWORKSPACE &workspace,
        hyprimate::workspace::CaptureKind kind,
        const SP<Render::IFramebuffer> &target,
        bool inactive = false);
    SP<Render::IFramebuffer> allocate_capture(MonitorState &entry,
        const PHLMONITOR &monitor,
        std::optional<Vector2D> size = std::nullopt);
    bool begin_refresh(MonitorState &entry, const PHLMONITOR &monitor, TurnKind kind);
    bool prepare_frame(MonitorState &entry,
        TurnKind kind,
        hyprimate::workspace::WorkspaceCapture &frame,
        double mix);
    static void release_blend(MonitorState &entry, MonitorState::CaptureBlend &blend);
    struct IdentityScan {
        MonitorState *state;
        std::vector<WallpaperIdentity> *identity;
        Vector2D position;
        float alpha = 1.F;
        std::size_t plane = 0;
    };
    static void scan_wallpaper_surface(
        const SP<CWLSurfaceResource> &child, const Vector2D &offset, void *opaque);
    void capture_wallpaper_identity(MonitorState &entry,
        const PHLMONITOR &monitor,
        std::vector<WallpaperIdentity> &identity,
        bool shell = false) const;
    void remember_wallpaper(const PHLMONITOR &monitor,
        const PHLWORKSPACE &workspace,
        const SP<Render::IFramebuffer> &source);
    SP<Render::IFramebuffer> capture_preview(const InventoryPort &port,
        const PHLWORKSPACE &workspace,
        const SP<Render::IFramebuffer> &paper,
        hyprimate::workspace::PreviewCapturePlan plan);
    void erase_state(const PHLMONITOR &monitor);
    static void damage(const PHLMONITOR &monitor);

    HANDLE m_handle = nullptr;
    std::uint64_t m_lifecycle = 0;
    std::uint64_t m_gesture_revision = 0;
    int m_style_draw_depth = 0;
    bool m_release_pending = false;
    std::optional<std::pair<hyprimate::config::SWorkspaceSettings, bool>> m_deferred_settings;
    void release_styles();
    bool m_uses_wallpaper = false;
    bool m_native_direction = false;
    bool m_layered = false;
    bool m_window_planes = false;
    bool m_individual_windows = false;
    bool m_native_hud = false;
    bool m_uses_previews = false;
    double m_max_frame_gap = hyprimate::STALL_SECONDS;
    hyprimate::workspace::WorkspaceTravel m_travel = nullptr;
    struct WallpaperAllocation {
        Vector2D size;
        std::uint32_t format = 0;
        std::remove_cvref_t<decltype(std::declval<Render::IFramebuffer>().imageDescription())>
            description;
        bool operator==(const WallpaperAllocation &) const = default;
    };
    struct WallpaperSignature {
        WallpaperAllocation allocation;
        wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
        bool operator==(const WallpaperSignature &) const = default;
    };
    hyprimate::workspace::WallpaperHistory<PHLWORKSPACEREF,
        PHLMONITORREF,
        SP<Render::IFramebuffer>,
        WallpaperSignature>
        m_observed;
    std::uint64_t m_inventory_revision = 1;
    std::array<CHyprSignalListener, 4> m_inventory_listeners;
    bool m_settings_changed = true;
    std::optional<hyprimate::config::SWorkspaceSettings> m_last_settings;
    std::shared_ptr<hyprimate::shader::SceneProgram> m_effect_program;
    double m_effect_duration = 0.0;
    std::pair<std::string, std::string> m_effect_request;
    bool m_admit_turns = false;
    std::optional<std::string> m_invalid_curve;
    CHyprSignalListener m_wallpaper_listener;
    MonitorState *m_capturing = nullptr;
    SP<Render::IFramebuffer> m_capture_wallpaper;
    int m_capture_depth = 0;
    int m_mutation_depth = 0;
    std::span<Snapshot> m_mutation_snapshots;
    PHLMONITORREF m_gesture_monitor;
    std::vector<std::shared_ptr<MonitorState>> m_states;
    std::vector<PHLMONITORREF> m_ready_monitors;
    std::vector<PHLMONITORREF> m_scanout_inhibited;
    hyprimate::workspace::WorkspaceTurns<std::uintptr_t, TurnPort> m_turns;
    hyprimate::workspace::CompositionIdentities<PHLWORKSPACE, PHLWORKSPACEREF> m_compositions;
    hyprimate::workspace::CompositionPlacements m_placements;
    hyprimate::config::WorkspaceStyle m_style_kind{};
    hyprimate::animation::StyleResources<hyprimate::workspace::WorkspaceStyles> m_styles;
    bool m_style_warm_pending = false;
    hyprimate::workspace::CompositionIdentities<PHLWINDOW, PHLWINDOWREF> m_window_identities;
    hyprimate::workspace::BeginRender m_begin_render = nullptr;

    CFunctionHook *m_change_hook = nullptr;
    // Native optimized callers may lend their shared-pointer argument storage.
    // Hook trampolines must copy incoming owners, never move from ABI arguments.
    CFunctionHook *m_special_hook = nullptr;
    CFunctionHook *m_move_hook = nullptr;
    CFunctionHook *m_swap_hook = nullptr;
    CFunctionHook *m_animate_hook = nullptr;
    CFunctionHook *m_render_hook = nullptr;
    CFunctionHook *m_drag_hook = nullptr;
    CFunctionHook *m_blur_hook = nullptr;
    CFunctionHook *m_window_hook = nullptr;
    CFunctionHook *m_layer_hook = nullptr;
    CFunctionHook *m_gesture_begin_hook = nullptr;
    CFunctionHook *m_gesture_end_hook = nullptr;
    CFunctionHook *m_gesture_cancel_hook = nullptr;
};

namespace workspace_animation_detail {
    void sample_resources(
        hyprimate::perf::Key key, CWorkspaceAnimation::Impl::MonitorState &entry) noexcept;
}
using workspace_animation_detail::sample_resources;
