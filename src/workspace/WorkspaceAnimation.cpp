#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <drm_fourcc.h>
#include <hyprland/protocols/wlr-layer-shell-unstable-v1.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
#include <hyprland/src/managers/input/UnifiedWorkspaceSwipeGesture.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/input/InputMethodRelay.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/state/FadingOutState.hpp>
#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/state/LayerState.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/rule/layerRule/LayerRuleApplicator.hpp>
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/SceneResources.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>
#include <hyprland/src/state/workspace/PlacementController.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/memory/UniquePtr.hpp>

#include "workspace/WorkspaceAnimation.hpp"
#include "workspace/WorkspaceCapture.hpp"
#include "workspace/WorkspaceTurn.hpp"
#include "workspace/WorkspaceFrame.hpp"
#include "workspace/WorkspaceIdentity.hpp"
#include "workspace/WorkspaceStyleList.hpp"

namespace {
    using Clock = std::chrono::steady_clock;
    using TurnKind = hyprimate::workspace::CaptureKind;
    using hyprimate::workspace::Direction;

    constexpr const char *CHANGE_NAME = "changeWorkspace";
    constexpr const char *CHANGE_SIGNATURE =
        "Monitor::CMonitor::changeWorkspace(Hyprutils::Memory::CSharedPointer<Workspace::"
        "CHLWorkspace> const&, bool, bool, bool)";
    constexpr const char *SPECIAL_NAME = "setSpecialWorkspace";
    constexpr const char *SPECIAL_SIGNATURE =
        "Monitor::CMonitor::setSpecialWorkspace(Hyprutils::Memory::CSharedPointer<Workspace::"
        "CHLWorkspace> const&, bool)";
    constexpr const char *MOVE_NAME = "moveWorkspaceToMonitor";
    constexpr const char *MOVE_SIGNATURE =
        "State::Workspace::CPlacementController::moveWorkspaceToMonitor(Hyprutils::Memory::"
        "CSharedPointer<Workspace::CHLWorkspace>, "
        "Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, bool, bool, bool) const";
    constexpr const char *SWAP_NAME = "swapActiveWorkspaces";
    constexpr const char *SWAP_SIGNATURE =
        "State::Workspace::CPlacementController::swapActiveWorkspaces(Hyprutils::Memory::"
        "CSharedPointer<Monitor::CMonitor>, Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>) "
        "const";
    constexpr const char *ANIMATE_NAME = "startAnimation";
    constexpr const char *ANIMATE_SIGNATURE =
        "Animation::Workspace::startAnimation(Hyprutils::Memory::CSharedPointer<Workspace::"
        "CHLWorkspace>, Animation::Workspace::eAnimationType, bool, bool, "
        "std::optional<std::__cxx11::basic_string<char, std::char_traits<char>, "
        "std::allocator<char> > >)";
    constexpr const char *RENDER_NAME = "renderAllClientsForWorkspace";
    constexpr const char *RENDER_SIGNATURE =
        "Render::IHyprRenderer::renderAllClientsForWorkspace(Render::CRenderContext&, "
        "Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, "
        "Hyprutils::Memory::CSharedPointer<Workspace::CHLWorkspace>, "
        "std::chrono::time_point<std::chrono::_V2::steady_clock, std::chrono::duration<long, "
        "std::ratio<1l, 1000000000l> > > const&, Render::eSceneMode, Hyprutils::Math::Vector2D "
        "const&, float const&)";
    constexpr const char *DRAG_NAME = "renderDragIcon";
    constexpr const char *DRAG_SIGNATURE =
        "Render::IHyprRenderer::renderDragIcon(Render::CRenderContext&, "
        "Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, "
        "std::chrono::time_point<std::chrono::_V2::steady_clock, std::chrono::duration<long, "
        "std::ratio<1l, 1000000000l> > > const&)";
    constexpr const char *BEGIN_RENDER_NAME = "beginRender";
    constexpr const char *BEGIN_RENDER_SIGNATURE =
        "Render::IHyprRenderer::beginRender(Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, "
        "Hyprutils::Math::CRegion&, Render::eRenderMode, "
        "Hyprutils::Memory::CSharedPointer<IHLBuffer>, "
        "Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>, bool, Render::SRenderOptions "
        "const&, std::optional<Monitor::CDamageRing::CTransaction>*)";
    constexpr const char *GESTURE_BEGIN_NAME = "CUnifiedWorkspaceSwipeGesture5begin";
    constexpr const char *GESTURE_BEGIN_SIGNATURE =
        "CUnifiedWorkspaceSwipeGesture::begin(Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>"
        ")";
    constexpr const char *GESTURE_END_NAME = "CUnifiedWorkspaceSwipeGesture3end";
    constexpr const char *GESTURE_END_SIGNATURE = "CUnifiedWorkspaceSwipeGesture::end()";
    constexpr const char *GESTURE_CANCEL_NAME = "CUnifiedWorkspaceSwipeGesture6cancel";
    constexpr const char *GESTURE_CANCEL_SIGNATURE = "CUnifiedWorkspaceSwipeGesture::cancel()";

    using ChangeFn = void (*)(Monitor::CMonitor *, const PHLWORKSPACE &, bool, bool, bool);
    using SpecialFn = void (*)(Monitor::CMonitor *, const PHLWORKSPACE &, bool);
    using MoveFn = void (*)(
        const State::Workspace::CPlacementController *, PHLWORKSPACE, PHLMONITOR, bool, bool, bool);
    using SwapFn = void (*)(const State::Workspace::CPlacementController *, PHLMONITOR, PHLMONITOR);
    using AnimateFn = void (*)(
        PHLWORKSPACE, Animation::Workspace::eAnimationType, bool, bool, std::optional<std::string>);
    using RenderFn = void (*)(Render::IHyprRenderer *,
        Render::CRenderContext &,
        PHLMONITOR,
        PHLWORKSPACE,
        const Time::steady_tp &,
        Render::eSceneMode,
        const Vector2D &,
        const float &);
    using BlurFn = SP<Render::IFramebuffer> (*)(Render::IHyprRenderer *,
        Render::CRenderContext &, SP<Render::IFramebuffer>, float, const CRegion &,
        const Render::SBlurContext &);
    constexpr const char *BLUR_SIGNATURE =
        "Render::GL::CHyprGLRenderer::blurFramebuffer(Render::CRenderContext&, "
        "Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>, float, "
        "Hyprutils::Math::CRegion const&, Render::SBlurContext const&)";
    using DragFn = void (*)(
        Render::IHyprRenderer *, Render::CRenderContext &, PHLMONITOR, const Time::steady_tp &);
    using GestureBeginFn = bool (*)(CUnifiedWorkspaceSwipeGesture *, PHLMONITOR);
    using GestureVoidFn = void (*)(CUnifiedWorkspaceSwipeGesture *);
    using WindowFn = void (*)(Render::IHyprRenderer *, Render::CRenderContext &, PHLWINDOW,
        PHLMONITOR, const Render::SWindowRenderPresentation &, const Time::steady_tp &, bool,
        Render::eRenderPassMode, bool, bool);
    constexpr const char *WINDOW_SIGNATURE =
        "Render::IHyprRenderer::renderWindow(Render::CRenderContext&, "
        "Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>, "
        "Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, "
        "Render::SWindowRenderPresentation const&, "
        "std::chrono::time_point<std::chrono::_V2::steady_clock, std::chrono::duration<long, "
        "std::ratio<1l, 1000000000l> > > const&, bool, Render::eRenderPassMode, bool, bool)";

    using LayerFn = void (*)(Render::IHyprRenderer *, Render::CRenderContext &, PHLLS,
        PHLMONITOR, const Time::steady_tp &, bool, bool, bool);
    constexpr const char *LAYER_SIGNATURE =
        "Render::IHyprRenderer::renderLayer(Render::CRenderContext&, "
        "Hyprutils::Memory::CSharedPointer<Desktop::View::CLayerSurface>, "
        "Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, "
        "std::chrono::time_point<std::chrono::_V2::steady_clock, std::chrono::duration<long, "
        "std::ratio<1l, 1000000000l> > > const&, bool, bool, bool)";

    bool native_hud_safe(const PHLMONITOR &monitor) {
        // A retiring shell snapshot can already contain its old backdrop. Keep
        // that fade on the native scene rather than moving or drawing it twice.
        for (const auto &fadeout : Desktop::fadingOutState()->fadeouts()) {
            if (fadeout && fadeout->monitor().lock() == monitor && !fadeout->done() &&
                fadeout->alpha() > 0.F && fadeout->source().type == Desktop::eFadeoutSource::LAYER &&
                fadeout->plane() != Desktop::FADEOUT_PLANE_LAYER_BACKGROUND) return false;
        }
        return true;
    }

    bool safe_scene() {
        if (!g_pSessionLockManager || g_pSessionLockManager->isSessionLocked() ||
            !g_pInputManager || !g_pInputManager->m_exclusiveKeyboardLSes.empty() ||
            (g_pInputManager->m_relay.m_inputMethod.lock() &&
                g_pInputManager->m_relay.getFocusedTextInput())) return false;
        const auto FOCUS = Desktop::focusState();
        if (FOCUS && FOCUS->surface() && !FOCUS->window()) return false;
        // Native screenshare masking occurs after scene composition at native boxes.
        // Moving any protected pixels would escape those masks. Include inactive
        // workspaces and unmapped/fading views: the departing capture can retain them.
        for (const auto &window : Desktop::windowState()->windows()) {
            if (window->m_ruleApplicator->noScreenShare().valueOrDefault()) return false;
        }
        for (const auto &layer : Desktop::layerState()->layers()) {
            if (layer->m_ruleApplicator->noScreenShare().valueOrDefault()) return false;
        }
        for (const auto &fadeout : Desktop::fadingOutState()->fadeouts()) {
            if (!fadeout || !fadeout->monitor().lock()) return false;
            const auto SOURCE = fadeout->source();
            if (SOURCE.noScreenShare || (SOURCE.type != Desktop::eFadeoutSource::WINDOW &&
                SOURCE.type != Desktop::eFadeoutSource::LAYER)) return false;
            const auto FB = fadeout->framebuffer();
            if (!FB || !FB->isAllocated() || !FB->getTexture() || !std::isfinite(fadeout->alpha())) return false;
        }
        return true;
    }

    void *exact_symbol(const std::vector<SFunctionMatch> &matches, const char *signature) {
        void *result = nullptr;
        int count = 0;
        for (const auto &match : matches) {
            if (match.demangled == signature) {
                result = match.address;
                ++count;
            }
        }
        return count == 1 ? result : nullptr;
    }


    std::optional<std::uint32_t> numbered(const PHLWORKSPACE &workspace) {
        if (!workspace) {
            return std::nullopt;
        }
        return workspace->numberedID();
    }
    std::uint32_t capture_format(const PHLMONITOR &monitor) {
        const auto FORMAT = monitor->m_drmFormat;
        return monitor->useFP16() || FORMAT == DRM_FORMAT_XRGB2101010 ||
            FORMAT == DRM_FORMAT_XBGR2101010 || FORMAT == DRM_FORMAT_ARGB2101010 ||
            FORMAT == DRM_FORMAT_ABGR2101010 ? DRM_FORMAT_ABGR16161616F : DRM_FORMAT_ABGR8888;
    }

    std::uint64_t framebuffer_bytes(const SP<Render::IFramebuffer> &framebuffer) {
        return framebuffer ? static_cast<std::uint64_t>(framebuffer->m_size.x) *
            static_cast<std::uint64_t>(framebuffer->m_size.y) *
            (framebuffer->m_drmFormat == DRM_FORMAT_ABGR8888 ? 4 : 8) : 0;
    }

    // Native draw callbacks can synchronously dispatch another workspace request.
    // Never capture recursively or stage a new turn from an offscreen pass.
    struct ScopedCapture {
        explicit ScopedCapture(int &depth) : m_depth(depth) { ++m_depth; }
        ~ScopedCapture() { --m_depth; }
        ScopedCapture(const ScopedCapture &) = delete;
        ScopedCapture &operator=(const ScopedCapture &) = delete;
      private:
        int &m_depth;
    };

}  // namespace

class CWorkspaceAnimation::Impl {
  public:
    explicit Impl(HANDLE handle) : m_handle(handle) {
    }
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
        Impl *owner;
        MonitorState *entry;
        [[nodiscard]] bool buffers(bool active) const;
        [[nodiscard]] bool capture(TurnKind kind, const hyprimate::workspace::TurnComposition<Workspace> &composition) const;
        [[nodiscard]] double duration(const hyprimate::workspace::JourneyEndpoint &from,
            const hyprimate::workspace::JourneyEndpoint &to, double base) const;
        [[nodiscard]] bool should_refresh(TurnKind kind, double progress) const;
        [[nodiscard]] bool refresh_inactive_source(const hyprimate::workspace::TurnComposition<Workspace> &composition) const;
        void present_native() const;
        void swap_slots() const;
        void damage() const;
    };
    struct MonitorState {
        explicit MonitorState(Impl *owner) : port{.owner = owner, .entry = this} {}
        PHLMONITORREF monitor;
        SP<Render::IFramebuffer> source;
        SP<Render::IFramebuffer> destination;
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
        bool wallpaper_seen = false;
        hyprimate::workspace::WorkspaceCapture *capturing_frame = nullptr;
        bool pre_windows_seen = false;
        bool post_windows_seen = false;
        bool capture_failed = false;
        bool floating_started = false;
        std::size_t floating_used = 0;
        struct CaptureBlend {
            std::shared_ptr<hyprimate::workspace::WorkspaceCapture> stale;
            std::shared_ptr<std::vector<hyprimate::workspace::WorkspaceFloating>> presentation;
            std::vector<std::pair<std::int64_t, std::size_t>> old_order;
            std::vector<std::uint8_t> matched;
            Clock::time_point started{};
            double start_distance = 0.0;
            double mix = 0.0;
            bool active = false;
            bool cached = false;
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
        struct PreviewJob {
            PHLWORKSPACEREF workspace;
            std::size_t index;
            double priority;
            SP<Render::IFramebuffer> paper;
            Clock::time_point started{};
        };
        std::vector<PreviewJob> preview_jobs;
        std::size_t preview_next = 0, preview_blending = 0;
        int preview_side = 0;
        std::int64_t preview_from = 0, preview_to = 0;
        std::uint64_t inventory_revision = 0;
        Clock::time_point last = Clock::now();
        TurnPort port;
    };

    using Composition = hyprimate::workspace::TurnComposition<PHLWORKSPACEREF>;
    struct Snapshot {
        PHLMONITOR monitor;
        Composition composition;
        bool ready = false;
    };
    Composition composition(const PHLMONITOR &monitor, bool ordered = false) {
        const auto NUMBER = numbered(monitor->m_activeWorkspace);
        std::uint32_t order = 0;
        if (ordered && NUMBER) {
            for (const auto &candidate : State::Workspace::state()->workspaces()) {
                const auto ID = candidate->numberedID();
                if (ID && *ID < *NUMBER && candidate->type() != Workspace::eWorkspaceType::SPECIAL) ++order;
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
        return {.identity = composition_id(monitor), .workspace = monitor->m_activeWorkspace,
            .numbered = NUMBER, .special = monitor->m_activeSpecialWorkspace != nullptr,
            .logicalOrder = order, .placement = placement};
    }
    std::int64_t composition_id(const PHLMONITOR &monitor) {
        return monitor ? m_compositions.get(monitor->m_activeWorkspace, monitor->m_activeSpecialWorkspace) : 0;
    }


    bool install(std::string &error);
    void shutdown();
    void configure(const hyprimate::config::SWorkspaceSettings &settings);
    void configure_style(bool enabled, hyprimate::config::WorkspaceStyle kind, bool wallpaper,
        double duration, std::string_view curve_name);
    void pre_checks(const PHLMONITOR &monitor);
    void pre_render(const PHLMONITOR &monitor);
    void remove_monitor(const PHLMONITOR &monitor);
    void clear();

    std::vector<Snapshot> snapshot(std::initializer_list<PHLMONITOR> monitors);
    void finish(std::vector<Snapshot> &snapshots);
    void mutate(const std::vector<PHLMONITOR> &monitors, const std::function<void()> &original);
    bool suppresses(const PHLMONITOR &monitor) const;
    void capture_gesture(const PHLMONITOR &monitor);
    void clear_gesture();
    bool queue(Render::CRenderContext &ctx, const PHLMONITOR &monitor, const Time::steady_tp &now);
    bool ready(const PHLMONITOR &monitor) const;

    static void *original(CFunctionHook *hook) {
        return hook != nullptr ? hook->m_original : nullptr;
    }
    RenderFn render_original() const {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland exposes original hook functions as void pointers.
        return reinterpret_cast<RenderFn>(original(m_render_hook));
    }
    DragFn drag_original() const {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland exposes original hook functions as void pointers.
        return reinterpret_cast<DragFn>(original(m_drag_hook));
    }

    static Impl *s_active;

    static std::uintptr_t monitor_key(const PHLMONITOR &monitor) {
        return std::bit_cast<std::uintptr_t>(monitor.get());
    }
    hyprimate::workspace::WorkspaceTurn<TurnPort> *turn(const PHLMONITOR &monitor) {
        return monitor ? m_turns.find(monitor_key(monitor)) : nullptr;
    }
    MonitorState *state(const PHLMONITOR &monitor);
    MonitorState &ensure_state(const PHLMONITOR &monitor);
    bool ensure_buffers(MonitorState &entry, const PHLMONITOR &monitor);
    bool capture(MonitorState &entry,
        const PHLMONITOR &monitor,
        const PHLWORKSPACE &workspace,
        hyprimate::workspace::CaptureKind kind,
        const SP<Render::IFramebuffer> &target, bool inactive = false);
    SP<Render::IFramebuffer> allocate_capture(MonitorState &entry, const PHLMONITOR &monitor,
        std::optional<Vector2D> size = std::nullopt);
    bool begin_refresh(MonitorState &entry, const PHLMONITOR &monitor, TurnKind kind);
    bool finish_refresh(MonitorState &entry, TurnKind kind);
    bool prepare_frame(MonitorState &entry, TurnKind kind,
        hyprimate::workspace::WorkspaceCapture &frame, double progress);
    void release_blend(MonitorState &entry, MonitorState::CaptureBlend &blend);
    void capture_wallpaper_identity(MonitorState &entry, const PHLMONITOR &monitor,
        std::vector<WallpaperIdentity> &identity, bool shell = false) const;
    void remember_wallpaper(const PHLMONITOR &, const PHLWORKSPACE &, const SP<Render::IFramebuffer> &);
    void prepare_previews(MonitorState &, const PHLMONITOR &, const Composition &, const Composition &);
    void index_previews(MonitorState &, const PHLMONITOR &, const Composition &, const Composition &);
    SP<Render::IFramebuffer> capture_preview(MonitorState &, const PHLMONITOR &, const PHLWORKSPACE &,
        const SP<Render::IFramebuffer> &paper);
    void erase_state(const PHLMONITOR &monitor);
    static void damage(const PHLMONITOR &monitor);

    HANDLE m_handle = nullptr;
    bool m_uses_wallpaper = false;
    bool m_layered = false;
    bool m_individual_windows = false;
    bool m_native_hud = false;
    bool m_uses_previews = false;
    struct ObservedWallpaper {
        PHLWORKSPACEREF workspace;
        PHLMONITORREF monitor;
        SP<Render::IFramebuffer> image;
        Vector2D size;
        wl_output_transform transform;
    };
    std::vector<ObservedWallpaper> m_observed;
    std::uint64_t m_inventory_revision = 1;
    std::array<CHyprSignalListener, 4> m_inventory_listeners;
    bool m_settings_changed = true;
    std::optional<hyprimate::config::SWorkspaceSettings> m_last_settings;
    std::optional<std::string> m_invalid_curve;
    CHyprSignalListener m_wallpaper_listener;
    MonitorState *m_capturing = nullptr;
    SP<Render::IFramebuffer> m_capture_wallpaper;
    int m_capture_depth = 0;
    int m_mutation_depth = 0;
    std::vector<PHLMONITORREF> m_suppressed;
    PHLMONITORREF m_gesture_monitor;
    std::vector<std::unique_ptr<MonitorState>> m_states;
    std::vector<PHLMONITORREF> m_ready_monitors;
    hyprimate::workspace::WorkspaceTurns<std::uintptr_t, TurnPort> m_turns;
    hyprimate::workspace::CompositionIdentities<PHLWORKSPACE, PHLWORKSPACEREF> m_compositions;
    hyprimate::workspace::CompositionPlacements m_placements;
    hyprimate::config::WorkspaceStyle m_style_kind{};
    hyprimate::animation::StyleResources<hyprimate::workspace::WorkspaceStyles> m_styles;
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

namespace {
    CWorkspaceAnimation::Impl *active() {
        return CWorkspaceAnimation::Impl::s_active;
    }

    template <typename Fn>
    Fn original(CFunctionHook *hook) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland exposes original hook functions as void pointers.
        return reinterpret_cast<Fn>(hook != nullptr ? hook->m_original : nullptr);
    }
    // Coverage alpha belongs to the captured scene, not the native blur input.
    bool copy_opaque(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &target, bool opaque = true, bool downsample = false) {
        if (!source || !target || source == target || !source->isAllocated() ||
            !target->isAllocated() || (!downsample && source->m_size != target->m_size)) {
            return false;
        }
        GLint read = 0;
        GLint draw = 0;
        std::array<GLboolean, 4> mask{};
        std::array<GLfloat, 4> clear{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear.data());
        const bool SCISSOR = glIsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): RT_GL selects the GL framebuffer backend.
            static_cast<Render::GL::CGLFramebuffer *>(source.get())->getFBID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): RT_GL selects the GL framebuffer backend.
            static_cast<Render::GL::CGLFramebuffer *>(target.get())->getFBID());
        const bool COMPLETE = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
                              glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (COMPLETE) {
            const auto WIDTH = static_cast<GLint>(source->m_size.x);
            const auto HEIGHT = static_cast<GLint>(source->m_size.y);
            glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, static_cast<GLint>(target->m_size.x),
                static_cast<GLint>(target->m_size.y), GL_COLOR_BUFFER_BIT, downsample ? GL_LINEAR : GL_NEAREST);
            if (opaque) {
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
                glClearColor(0.F, 0.F, 0.F, 1.F);
                glClear(GL_COLOR_BUFFER_BIT);
            }
        }
        glColorMask(mask[0], mask[1], mask[2], mask[3]);
        glClearColor(clear[0], clear[1], clear[2], clear[3]);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
        if (SCISSOR) {
            glEnable(GL_SCISSOR_TEST);
        }
        return COMPLETE;
    }

    // Borrow protected native draw methods without downcasting the renderer.
    struct PreviewDrawAccess : Render::IHyprRenderer {
        static constexpr auto BACKGROUND = &PreviewDrawAccess::renderBackground;
        static constexpr auto LAYER = &PreviewDrawAccess::renderLayer;
    };

    SP<Render::IFramebuffer> preview_target(const PHLMONITOR &monitor, int longest_side) {
        g_pHyprRenderer->glBackend()->makeEGLCurrent();
        const auto size = monitor->m_transformedSize;
        const double scale = std::min(1.0, longest_side / std::max(size.x, size.y));
        auto image = g_pHyprRenderer->createFB("hyprimate workspace preview");
        if (!image) return {};
        // Hyprland's FBO allocator unbinds the current unit's 2D texture.
        GLint prior = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prior);
        const bool allocated = image->alloc(std::max(1, static_cast<int>(std::lround(size.x * scale))),
            std::max(1, static_cast<int>(std::lround(size.y * scale))), capture_format(monitor));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prior));
        if (!allocated) return {};
        image->setImageDescription(monitor->workBufferImageDescription());
        return image;
    }

    bool write_thumbnail(const SP<Render::IFramebuffer> &source, const SP<Render::IFramebuffer> &thumbnail) {
        if (!copy_opaque(source, thumbnail, true, true)) return false;
        // Stars and shell stay at output resolution. Only images owned by the
        // preview cache get mip levels, once per capture, never on native client buffers.
        GLint prior = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prior);
        glBindTexture(GL_TEXTURE_2D, thumbnail->getTexture()->m_texID);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prior));
        return true;
    }

    SP<Render::IFramebuffer> hook_blur(Render::IHyprRenderer *renderer,
        Render::CRenderContext &ctx, SP<Render::IFramebuffer> source, float strength,
        const CRegion &damage, const Render::SBlurContext &context) {
        const auto *manager = active();
        auto function = original<BlurFn>(manager != nullptr ? manager->m_blur_hook : nullptr);
        if (function == nullptr) {
            return nullptr;
        }
        if (manager != nullptr && manager->m_capturing != nullptr && manager->m_capturing->wallpaper_captured &&
            source && source->m_size == manager->m_capturing->source->m_size) {
            const auto BACKDROP = manager->m_capturing->opaque_backdrop;
            if (!copy_opaque(source, BACKDROP)) {
                manager->m_capturing->wallpaper_captured = false;
                manager->m_capturing->capture_failed = true;
                return nullptr;
            }
            const auto RESULT = function(renderer, ctx, BACKDROP, strength, damage, context);
            if (!RESULT) manager->m_capturing->capture_failed = true;
            return RESULT;
        }
        return function(renderer, ctx, source, strength, damage, context);
    }

    void hook_layer(Render::IHyprRenderer *renderer, Render::CRenderContext &ctx,
        PHLLS layer, PHLMONITOR monitor, const Time::steady_tp &now,
        bool popups, bool lockscreen, bool workspace_scene) {
        const auto *manager = active();
        const auto ORIGINAL = original<LayerFn>(manager ? manager->m_layer_hook : nullptr);
        if (!ORIGINAL) return;
        // Capture only the world. Native layer drawing bakes blur RGB into the
        // target before coverage alpha is recorded; subtracting wallpaper later
        // cannot make that old blur sample the animated planetary scene.
        if (manager && manager->m_native_hud && manager->m_capture_depth > 0 && layer &&
            (popups || layer->m_layer != ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND)) return;
        // Copy ABI owners: optimized native callers may lend their argument slots.
        ORIGINAL(renderer, ctx, layer, monitor, now, popups, lockscreen, workspace_scene);
    }

    void hook_change(Monitor::CMonitor *self,
        const PHLWORKSPACE &workspace,
        bool internal,
        bool no_mouse_move,
        bool no_focus) {
        auto *manager = active();
        auto function = original<ChangeFn>(manager != nullptr ? manager->m_change_hook : nullptr);
        if (function == nullptr) {
            return;
        }
        if (manager == nullptr || !manager->m_turns.enabled()) {
            function(self, workspace, internal, no_mouse_move, no_focus);
            return;
        }
        const auto MONITOR = self->m_self.lock();
        manager->mutate({MONITOR}, [&] { function(self, workspace, internal, no_mouse_move, no_focus); });
    }

    void hook_special(Monitor::CMonitor *self, const PHLWORKSPACE &workspace, bool no_focus) {
        auto *manager = active();
        auto function = original<SpecialFn>(manager != nullptr ? manager->m_special_hook : nullptr);
        if (function == nullptr) {
            return;
        }
        if (manager == nullptr || !manager->m_turns.enabled()) {
            function(self, workspace, no_focus);
            return;
        }
        const auto MONITOR = self->m_self.lock();
        manager->mutate({MONITOR}, [&] { function(self, workspace, no_focus); });
    }

    void hook_move(const State::Workspace::CPlacementController *self,
        PHLWORKSPACE workspace,
        PHLMONITOR monitor,
        bool no_warp,
        bool carry_focus,
        bool replace) {
        auto *manager = active();
        auto function = original<MoveFn>(manager != nullptr ? manager->m_move_hook : nullptr);
        if (function == nullptr) {
            return;
        }
        if (manager == nullptr || !manager->m_turns.enabled()) {
            function(self, workspace, monitor, no_warp, carry_focus, replace);
            return;
        }
        const auto OLD_MONITOR = workspace ? workspace->m_monitor.lock() : PHLMONITOR{};
        manager->mutate({OLD_MONITOR, monitor},
            [&] { function(self, workspace, monitor, no_warp, carry_focus, replace); });
    }

    void hook_swap(
        const State::Workspace::CPlacementController *self, PHLMONITOR first, PHLMONITOR second) {
        auto *manager = active();
        auto function = original<SwapFn>(manager != nullptr ? manager->m_swap_hook : nullptr);
        if (function == nullptr) {
            return;
        }
        if (manager == nullptr || !manager->m_turns.enabled()) {
            function(self, first, second);
            return;
        }
        manager->mutate({first, second}, [&] { function(self, first, second); });
    }

    void hook_animate(PHLWORKSPACE workspace,
        Animation::Workspace::eAnimationType type,
        bool left,
        bool instant,
        std::optional<std::string> style) {
        const auto *manager = active();
        auto function = original<AnimateFn>(manager != nullptr ? manager->m_animate_hook : nullptr);
        if (function == nullptr) {
            return;
        }
        // Still let Hyprland establish its incoming/outgoing workspace state and callbacks.
        // Suppressing the call entirely leaves previously hidden workspaces transparent or
        // offset; instant completion avoids a competing native animation.
        const bool SUPPRESSED =
            manager != nullptr && manager->suppresses(workspace ? workspace->m_monitor.lock() : PHLMONITOR{});
        function(workspace, type, left, instant || SUPPRESSED, style);
    }

    void hook_render(Render::IHyprRenderer *renderer,
        Render::CRenderContext &ctx,
        PHLMONITOR monitor,
        PHLWORKSPACE workspace,
        const Time::steady_tp &now,
        Render::eSceneMode mode,
        const Vector2D &translate,
        const float &scale) {
        auto *manager = active();
        auto function = manager != nullptr ? manager->render_original() : nullptr;
        if (function == nullptr) {
            return;
        }
        if (manager != nullptr && mode == Render::eSceneMode::MONITOR && manager->queue(ctx, monitor, now)) {
            if (auto drag = manager->drag_original(); drag != nullptr) {
                drag(renderer, ctx, monitor, now);
            }
            return;
        }
        function(renderer, ctx, monitor, workspace, now, mode, translate, scale);
    }

    void hook_drag(Render::IHyprRenderer *renderer,
        Render::CRenderContext &ctx,
        PHLMONITOR monitor,
        const Time::steady_tp &now) {
        const auto *manager = active();
        auto function = manager != nullptr ? manager->drag_original() : nullptr;
        if (function == nullptr || (manager != nullptr && manager->m_capture_depth > 0)) {
            return;
        }
        function(renderer, ctx, monitor, now);
    }

    bool hook_gesture_begin(CUnifiedWorkspaceSwipeGesture *gesture, PHLMONITOR monitor) {
        auto *manager = active();
        auto function = original<GestureBeginFn>(manager != nullptr ? manager->m_gesture_begin_hook : nullptr);
        if (function == nullptr) {
            return false;
        }
        if (manager != nullptr && manager->m_turns.enabled()) {
            manager->capture_gesture(monitor);
        }
        const bool BEGUN = function(gesture, monitor);
        if (!BEGUN && manager != nullptr) {
            manager->clear_gesture();
        }
        return BEGUN;
    }

    void hook_gesture_end(CUnifiedWorkspaceSwipeGesture *gesture) {
        auto *manager = active();
        auto function = original<GestureVoidFn>(manager != nullptr ? manager->m_gesture_end_hook : nullptr);
        if (function != nullptr) {
            function(gesture);
        }
        if (manager != nullptr) {
            manager->clear_gesture();
        }
    }

    void hook_gesture_cancel(CUnifiedWorkspaceSwipeGesture *gesture) {
        auto *manager = active();
        auto function = original<GestureVoidFn>(manager != nullptr ? manager->m_gesture_cancel_hook : nullptr);
        if (function != nullptr) {
            function(gesture);
        }
        if (manager != nullptr) {
            manager->clear_gesture();
        }
    }

    // Preserve native backdrop RGB for blur and glass effects, while recording foreground
    // coverage in alpha. The final shader can remove the wallpaper contribution exactly.
    class CWorkspaceWallpaperPass final : public IPassElement {
      public:
        CWorkspaceWallpaperPass(SP<Render::IFramebuffer> target, bool &captured,
            bool *failed = nullptr, bool reset = true, std::optional<CBox> crop = std::nullopt)
            : m_target(std::move(target)), m_captured(&captured), m_failed(failed), m_reset(reset), m_crop(crop) {}

        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            const auto SOURCE = ctx.m_data.currentFB;
            if (!SOURCE || !m_target || SOURCE == m_target ||
                (!m_crop && SOURCE->m_size != m_target->m_size)) {
                if (m_failed) *m_failed = true;
                return {};
            }
            GLint read = 0;
            GLint draw = 0;
            std::array<GLboolean, 4> mask{};
            std::array<GLfloat, 4> clear{};
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
            glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
            glGetFloatv(GL_COLOR_CLEAR_VALUE, clear.data());
            const bool SCISSOR = glIsEnabled(GL_SCISSOR_TEST) != GL_FALSE;
            glDisable(GL_SCISSOR_TEST);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): RT_GL selects the GL framebuffer backend.
            const auto SOURCE_ID = static_cast<Render::GL::CGLFramebuffer *>(SOURCE.get())->getFBID();
            glBindFramebuffer(GL_READ_FRAMEBUFFER, SOURCE_ID);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): RT_GL selects the GL framebuffer backend.
                static_cast<Render::GL::CGLFramebuffer *>(m_target.get())->getFBID());
            const bool COMPLETE = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
                glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            if (COMPLETE) {
                const auto WIDTH = static_cast<GLint>(m_target->m_size.x);
                const auto HEIGHT = static_cast<GLint>(m_target->m_size.y);
                const auto X = m_crop ? static_cast<GLint>(m_crop->x) : 0;
                const auto Y = m_crop ? static_cast<GLint>(m_crop->y) : 0;
                glBlitFramebuffer(X, Y, X + WIDTH, Y + HEIGHT, 0, 0, WIDTH, HEIGHT,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
                if (m_reset) {
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, SOURCE_ID);
                    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
                    glClearColor(0.F, 0.F, 0.F, 0.F);
                    glClear(GL_COLOR_BUFFER_BIT);
                }
            }
            glColorMask(mask[0], mask[1], mask[2], mask[3]);
            glClearColor(clear[0], clear[1], clear[2], clear[3]);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
            if (SCISSOR) {
                glEnable(GL_SCISSOR_TEST);
            }
            *m_captured = COMPLETE;
            if (!COMPLETE && m_failed) *m_failed = true;
            return {};
        }
        bool needsLiveBlur(Render::CRenderContext & /*ctx*/) override { return false; }
        bool needsPrecomputeBlur(Render::CRenderContext & /*ctx*/) override { return false; }
        bool undiscardable(Render::CRenderContext & /*ctx*/) override { return true; }
        bool disableSimplification(Render::CRenderContext & /*ctx*/) override { return true; }
        bool requiresFullDamage(Render::CRenderContext & /*ctx*/) override { return true; }
        const char *passName() override { return "hyprimate-workspace-wallpaper"; }
        ePassElementType type() override { return EK_CUSTOM; }

      private:
        SP<Render::IFramebuffer> m_target;
        bool *m_captured;
        bool *m_failed;
        bool m_reset;
        std::optional<CBox> m_crop;
    };

    void hook_window(Render::IHyprRenderer *renderer, Render::CRenderContext &ctx,
        PHLWINDOW window, PHLMONITOR monitor, const Render::SWindowRenderPresentation &presentation,
        const Time::steady_tp &now, bool decorate, Render::eRenderPassMode pass,
        bool ignore_position, bool standalone) {
        auto *manager = active();
        const auto ORIGINAL = original<WindowFn>(manager ? manager->m_window_hook : nullptr);
        if (!ORIGINAL) return;
        auto *entry = manager ? manager->m_capturing : nullptr;
        auto *frame = entry ? entry->capturing_frame : nullptr;
        bool INDIVIDUAL = frame && frame->layered && manager->m_individual_windows && window &&
            window->mapped() && !window->isHidden() && presentation.alphaVisible;
        if (INDIVIDUAL && pass == Render::RENDER_PASS_POPUP) {
            bool visible_popup = false;
            if (const auto head = window->popupHead(); head) head->breadthfirst([&](WP<Desktop::View::CPopup> weak, void *) {
                const auto popup = weak.lock();
                visible_popup = visible_popup || (popup && popup->mapped());
            }, nullptr);
            bool overlay = false;
            if (decorate) for (const auto &decoration : window->presentation().decorations()) {
                overlay = overlay || decoration->getDecorationLayer() == DECORATION_LAYER_OVERLAY;
            }
            INDIVIDUAL = visible_popup || overlay;
        }
        hyprimate::workspace::WorkspaceFloating *floating = nullptr;
        std::optional<CBox> crop;
        const auto BOUNDARY = [&](const SP<Render::IFramebuffer> &target, std::optional<CBox> region = std::nullopt) {
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CWorkspaceWallpaperPass>(
                target, entry->wallpaper_captured, &entry->capture_failed, true, region));
        };
        if (INDIVIDUAL && !entry->capture_failed) {
            if (!entry->pre_windows_seen || entry->post_windows_seen) {
                entry->capture_failed = true;
            } else {
                const auto BOX = window->effects().transformedExtents(window->getFullWindowBoundingBox());
                const auto SIZE = monitor->m_transformedSize;
                const double X = std::clamp(std::floor((BOX.x - monitor->m_position.x) * monitor->m_scale), 0.0, SIZE.x);
                const double Y = std::clamp(std::floor((BOX.y - monitor->m_position.y) * monitor->m_scale), 0.0, SIZE.y);
                const double RIGHT = std::clamp(std::ceil((BOX.x + BOX.width - monitor->m_position.x) * monitor->m_scale), 0.0, SIZE.x);
                const double BOTTOM = std::clamp(std::ceil((BOX.y + BOX.height - monitor->m_position.y) * monitor->m_scale), 0.0, SIZE.y);
                if (RIGHT > X && BOTTOM > Y) {
                    crop = CBox{X, Y, RIGHT - X, BOTTOM - Y};
                    if (!entry->floating_started) {
                        if (frame->windows.accumulated != frame->bottom.accumulated)
                            BOUNDARY(frame->windows.accumulated);
                        entry->floating_started = true;
                    }
                    if (!frame->floating) frame->floating = std::make_shared<std::vector<hyprimate::workspace::WorkspaceFloating>>();
                    if (frame->floating->size() <= entry->floating_used) frame->floating->emplace_back();
                    auto &item = (*frame->floating)[entry->floating_used++];
                    const Vector2D EXTENT{crop->width, crop->height};
                    const auto PREPARE = [&](SP<Render::IFramebuffer> &target) {
                        if (target && target->m_size != EXTENT) {
                            entry->allocated_bytes -= framebuffer_bytes(target);
                            target.reset();
                        }
                        if (!target) target = manager->allocate_capture(*entry, monitor, EXTENT);
                    };
                    PREPARE(item.layer.backdrop);
                    PREPARE(item.layer.accumulated);
                    if (!item.layer.accumulated || !item.layer.backdrop) {
                        entry->capture_failed = true;
                    } else {
                        floating = &item;
                        // Body and popup plates share one window, but never share temporal pixels.
                        floating->identity = manager->m_window_identities.get(window, PHLWINDOW{}) * 4 +
                            static_cast<std::int64_t>(pass);
                        floating->rect = {X / SIZE.x, Y / SIZE.y, crop->width / SIZE.x, crop->height / SIZE.y};
                        floating->order = static_cast<double>(entry->floating_used - 1);
                        BOUNDARY(floating->layer.backdrop, crop);
                    }
                }
            }
        }
        ORIGINAL(renderer, ctx, window, monitor, presentation, now,
            decorate, pass, ignore_position, standalone);
        if (floating) BOUNDARY(floating->layer.accumulated, crop);
    }

    // Pass analysis has already determined native optimized-blur demand when
    // draw runs. Refresh its real compositor cache here, never before the scene
    // texture and never from the preceding frame's wallpaper/HUD.
    class CWorkspaceHudBlurPass final : public IPassElement {
      public:
        explicit CWorkspaceHudBlurPass(bool invalidate = false) : m_invalidate(invalidate) {}
        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            const auto RESOURCES = ctx.sceneResources();
            if (m_invalidate) {
                // completePreBlur clears dirty/queued. Invalidate after every
                // HUD reader, so the first ordinary scene regenerates wallpaper
                // blur rather than reusing planetary pixels.
                if (RESOURCES) {
                    RESOURCES->setBlurDirty(true);
                    RESOURCES->setBlurQueued(true);
                } else {
                    ctx.m_data.pMonitor->m_blurFBDirty = true;
                    ctx.m_data.pMonitor->m_blurFBShouldRender = true;
                }
                return {};
            }
            const bool QUEUED = RESOURCES ? RESOURCES->blurQueued() : ctx.m_data.pMonitor->m_blurFBShouldRender;
            if (QUEUED) {
                const CRegion DAMAGE{CBox{{}, ctx.m_data.pMonitor->m_transformedSize}};
                g_pHyprRenderer->preBlurForCurrentMonitor(ctx, DAMAGE);
            }
            return {};
        }
        bool needsLiveBlur(Render::CRenderContext & /*ctx*/) override { return false; }
        bool needsPrecomputeBlur(Render::CRenderContext & /*ctx*/) override { return false; }
        bool undiscardable(Render::CRenderContext & /*ctx*/) override { return true; }
        bool disableSimplification(Render::CRenderContext & /*ctx*/) override { return true; }
        bool requiresFullDamage(Render::CRenderContext & /*ctx*/) override { return true; }
        const char *passName() override { return "hyprimate-workspace-native-hud-blur"; }
        ePassElementType type() override { return EK_CUSTOM; }

      private:
        bool m_invalidate;
    };

    class CWorkspacePresentationPass final : public IPassElement {
      public:
        CWorkspacePresentationPass(CWorkspaceAnimation::Impl *owner, PHLMONITOR monitor,
            hyprimate::workspace::WorkspaceFrame frame, bool matching_wallpaper)
            : m_owner(owner), m_monitor(std::move(monitor)), m_frame(std::move(frame)),
              m_matching_wallpaper(matching_wallpaper) {
        }

        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            auto *turn = m_owner != nullptr ? m_owner->turn(m_monitor) : nullptr;
            if (turn == nullptr) {
                copy_opaque(m_frame.destination.composite, m_frame.presentation);
                return {};
            }
            turn->draw([&](double progress, Direction direction, bool matching) {
                bool drawn = false;
                m_owner->m_styles.for_each([&]<class Style>(auto &resources) {
                    if (Style::KIND == m_owner->m_style_kind && resources.ready) {
                        drawn = Style::draw(resources, ctx, m_frame, progress, direction, matching);
                    }
                });
                return drawn;
            }, m_matching_wallpaper);
            if (!turn->active()) {
                m_owner->erase_state(m_monitor);
            }
            return {};
        }
        bool needsLiveBlur(Render::CRenderContext & /*ctx*/) override {
            return false;
        }
        bool needsPrecomputeBlur(Render::CRenderContext & /*ctx*/) override {
            return false;
        }
        bool undiscardable(Render::CRenderContext & /*ctx*/) override {
            return true;
        }
        const char *passName() override {
            return "hyprimate-workspace-presentation";
        }
        ePassElementType type() override {
            return EK_CUSTOM;
        }

      private:
        CWorkspaceAnimation::Impl *m_owner;
        PHLMONITOR m_monitor;
        hyprimate::workspace::WorkspaceFrame m_frame;
        bool m_matching_wallpaper;
    };
}  // namespace

CWorkspaceAnimation::Impl *CWorkspaceAnimation::Impl::s_active = nullptr;

bool CWorkspaceAnimation::Impl::install(std::string &error) {
    shutdown();
    struct Spec {
        const char *name;
        const char *signature;
        const void *replacement;
        CFunctionHook **slot;
    };
    const std::array SPECS{
        Spec{.name = CHANGE_NAME, .signature = CHANGE_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_change), .slot = &m_change_hook},
        Spec{.name = SPECIAL_NAME, .signature = SPECIAL_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_special), .slot = &m_special_hook},
        Spec{.name = MOVE_NAME, .signature = MOVE_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_move), .slot = &m_move_hook},
        Spec{.name = SWAP_NAME, .signature = SWAP_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_swap), .slot = &m_swap_hook},
        Spec{.name = ANIMATE_NAME, .signature = ANIMATE_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_animate), .slot = &m_animate_hook},
        Spec{.name = RENDER_NAME, .signature = RENDER_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_render), .slot = &m_render_hook},
        Spec{.name = DRAG_NAME, .signature = DRAG_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_drag), .slot = &m_drag_hook},
        Spec{.name = "blurFramebuffer", .signature = BLUR_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_blur), .slot = &m_blur_hook},
        Spec{.name = "renderWindow", .signature = WINDOW_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): native hook trampoline.
            .replacement = reinterpret_cast<const void *>(&hook_window), .slot = &m_window_hook},
        Spec{.name = "renderLayer", .signature = LAYER_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): native hook trampoline.
            .replacement = reinterpret_cast<const void *>(&hook_layer), .slot = &m_layer_hook},
        Spec{.name = GESTURE_BEGIN_NAME, .signature = GESTURE_BEGIN_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_gesture_begin), .slot = &m_gesture_begin_hook},
        Spec{.name = GESTURE_END_NAME, .signature = GESTURE_END_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_gesture_end), .slot = &m_gesture_end_hook},
        Spec{.name = GESTURE_CANCEL_NAME, .signature = GESTURE_CANCEL_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_gesture_cancel), .slot = &m_gesture_cancel_hook},
    };
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland exposes symbol addresses as void pointers.
    m_begin_render = reinterpret_cast<hyprimate::workspace::BeginRender>(exact_symbol(
        HyprlandAPI::findFunctionsByName(m_handle, BEGIN_RENDER_NAME), BEGIN_RENDER_SIGNATURE));
    if (m_begin_render == nullptr) {
        error = "workspace beginRender symbol not found by exact signature";
        return false;
    }
    for (const auto &spec : SPECS) {
        const auto *const ADDRESS =
            exact_symbol(HyprlandAPI::findFunctionsByName(m_handle, spec.name), spec.signature);
        if (ADDRESS == nullptr) {
            error = std::string("workspace symbol not found: ") + spec.name;
            shutdown();
            return false;
        }
        *spec.slot = HyprlandAPI::createFunctionHook(m_handle, ADDRESS, spec.replacement);
        if (*spec.slot == nullptr) {
            error = std::string("workspace hook creation failed: ") + spec.name;
            shutdown();
            return false;
        }
    }
    s_active = this;
    for (const auto &spec : SPECS) {
        if (!(*spec.slot)->hook()) {
            error = std::string("workspace hook install failed: ") + spec.name;
            shutdown();
            return false;
        }
    }
    m_wallpaper_listener = Event::bus()->m_events.render.stage.listen(
        [this](const Event::SRenderStageEvent &event) {
            if (event.stage == RENDER_POST && event.monitor && event.monitor->m_enabled &&
                event.monitor->m_output && event.monitor->m_drmFormat != DRM_FORMAT_INVALID &&
                event.monitor->m_transformedSize.x > 0 && event.monitor->m_transformedSize.y > 0) {
                std::erase_if(m_ready_monitors, [](const auto &weak) { return weak.expired(); });
                if (!ready(event.monitor)) m_ready_monitors.emplace_back(event.monitor);
            }
            if (!event.context || m_capturing == nullptr || !m_capture_wallpaper || m_capture_depth == 0) return;
            auto &entry = *m_capturing;
            auto *frame = entry.capturing_frame;
            const auto BOUNDARY = [&](const SP<Render::IFramebuffer> &target) {
                Render::IHyprRenderer::addPassElement(event.context->get(),
                    makeUnique<CWorkspaceWallpaperPass>(target, entry.wallpaper_captured, &entry.capture_failed));
            };
            if (event.stage == RENDER_POST_WALLPAPER) {
                BOUNDARY(m_capture_wallpaper);
                entry.wallpaper_seen = true;
            } else if (frame && frame->layered && event.stage == RENDER_PRE_WINDOWS) {
                if (!entry.pre_windows_seen) {
                    BOUNDARY(frame->bottom.accumulated);
                    entry.pre_windows_seen = true;
                } else if (m_individual_windows) {
                    // Special dim/blur between individual window batches cannot be
                    // assigned to one window plate without changing native stacking.
                    entry.capture_failed = true;
                }
            } else if (frame && frame->layered && event.stage == RENDER_POST_WINDOWS) {
                if (!entry.floating_started && frame->windows.accumulated != frame->bottom.accumulated)
                    BOUNDARY(frame->windows.accumulated);
                if (frame->top.backdrop != frame->windows.accumulated) BOUNDARY(frame->top.backdrop);
                entry.post_windows_seen = true;
            }
        });
    auto changed = [this](PHLWORKSPACEREF) { ++m_inventory_revision; };
    auto &workspaces = Event::bus()->m_events.workspace;
    m_inventory_listeners = {workspaces.createdLate.listen(changed), workspaces.removed.listen(changed),
        workspaces.renamed.listen(changed), workspaces.moveToMonitor.listen(
            [this](PHLWORKSPACE, PHLMONITOR) { ++m_inventory_revision; })};
    return true;
}

void CWorkspaceAnimation::Impl::shutdown() {
    m_wallpaper_listener.reset();
    m_inventory_listeners = {};
    m_observed.clear();
    m_ready_monitors.clear();
    const std::array HOOKS{m_gesture_cancel_hook,
        m_gesture_end_hook,
        m_gesture_begin_hook,
        m_drag_hook,
        m_blur_hook,
        m_window_hook,
        m_layer_hook,
        m_render_hook,
        m_animate_hook,
        m_swap_hook,
        m_move_hook,
        m_special_hook,
        m_change_hook};
    for (auto *hook : HOOKS) {
        if (hook != nullptr) {
            HyprlandAPI::removeFunctionHook(m_handle, hook);
        }
    }
    m_change_hook = m_special_hook = m_move_hook = m_swap_hook = m_animate_hook = nullptr;
    m_render_hook = m_drag_hook = m_gesture_begin_hook = m_gesture_end_hook = m_gesture_cancel_hook =
        nullptr;
    m_blur_hook = m_window_hook = m_layer_hook = nullptr;
    s_active = nullptr;
    clear();
    if (Render::GL::g_pHyprOpenGL) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    }
    m_styles.for_each([]<class Style>(auto &resources) {
        if (resources.ready && Render::GL::g_pHyprOpenGL) {
            resources.renderer.release();
        }
        resources.ready = false;
    });
    m_begin_render = nullptr;
}

void CWorkspaceAnimation::Impl::configure(const hyprimate::config::SWorkspaceSettings &settings) {
    if (!m_last_settings || *m_last_settings != settings) {
        m_settings_changed = true;
        m_last_settings = settings;
    }
    m_styles.for_each([&]<class Style>(auto &resources) {
        if (settings.style != Style::KIND) {
            return;
        }
        const auto previous = resources.settings.last;
        Style::prepare(resources, settings);
        if (previous != resources.settings.last || m_style_kind != Style::KIND || !settings.enabled) clear();
        m_layered = Style::LAYERED;
        m_individual_windows = Style::needs_individual_windows(resources);
        m_native_hud = Style::uses_native_hud(resources);
        m_uses_previews = settings.enabled && Style::uses_workspace_previews(resources);
        if (!m_uses_previews) m_observed.clear();
        const auto &prepared = resources.settings.prepared;
        configure_style(settings.enabled, Style::KIND, Style::USES_WALLPAPER, prepared.duration, prepared.curve);
    });
}

void CWorkspaceAnimation::Impl::configure_style(bool enabled,
    hyprimate::config::WorkspaceStyle kind, bool wallpaper, double duration, std::string_view curve_name) {
    m_style_kind = kind;
    m_uses_wallpaper = wallpaper;
    const auto RESOLVE = [this](std::string_view requested_curve) -> hyprimate::workspace::Transition::Curve {
        auto &manager = Animation::mgr();
        if (!manager) {
            return {};
        }
        const std::string NAME(requested_curve);
        const bool KNOWN = manager->bezierExists(NAME);
        if (!KNOWN && (!m_invalid_curve || *m_invalid_curve != NAME)) {
            HyprlandAPI::addNotification(m_handle,
                "[hyprimate] Unknown workspace curve '" + NAME +
                    "'; retaining the last valid curve.",
                CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
            m_invalid_curve = NAME;
        }
        if (!KNOWN && m_turns.curve()) {
            return m_turns.curve();
        }
        std::string selected = NAME;
        if (!KNOWN) {
            selected = manager->bezierExists("default") ? "default" : "linear";
        }
        const auto CURVE = manager->getBezier(selected);
        if (!CURVE) {
            return {};
        }
        if (KNOWN) {
            m_invalid_curve.reset();
        }
        return [CURVE](double progress) {
            return static_cast<double>(CURVE->getYForPoint(static_cast<float>(progress)));
        };
    };
    m_turns.configure(enabled, static_cast<std::uint64_t>(kind), duration, curve_name,
        m_settings_changed, RESOLVE);
    m_settings_changed = false;
}

CWorkspaceAnimation::Impl::MonitorState *CWorkspaceAnimation::Impl::state(
    const PHLMONITOR &monitor) {
    const auto FOUND = std::ranges::find_if(
        m_states, [&](const auto &entry) { return entry->monitor.lock() == monitor; });
    return FOUND == m_states.end() ? nullptr : FOUND->get();
}

CWorkspaceAnimation::Impl::MonitorState &CWorkspaceAnimation::Impl::ensure_state(
    const PHLMONITOR &monitor) {
    if (auto *existing = state(monitor); existing != nullptr) {
        return *existing;
    }
    auto created = std::make_unique<MonitorState>(this);
    created->monitor = monitor;
    m_states.push_back(std::move(created));
    return *m_states.back();
}

SP<Render::IFramebuffer> CWorkspaceAnimation::Impl::allocate_capture(
    MonitorState &entry, const PHLMONITOR &monitor, std::optional<Vector2D> size) {
    if (!g_pHyprRenderer || !ready(monitor)) return nullptr;
    const auto SIZE = size.value_or(monitor->m_transformedSize);
    const auto FORMAT = capture_format(monitor);
    const auto BYTES = static_cast<std::uint64_t>(SIZE.x) * static_cast<std::uint64_t>(SIZE.y) *
        (FORMAT == DRM_FORMAT_ABGR8888 ? 4 : 8);
    const std::uint64_t MONITOR_LIMIT = (FORMAT == DRM_FORMAT_ABGR8888 ? 512ULL : 1024ULL) * 1024 * 1024;
    // Retained targets only; Hyprland's isolated scene/blur caches remain compositor-owned.
    constexpr std::uint64_t GLOBAL_LIMIT = 2048ULL * 1024 * 1024;
    std::uint64_t total = 0;
    for (const auto &state : m_states) total += state->allocated_bytes;
    if (BYTES == 0 || BYTES > MONITOR_LIMIT || entry.allocated_bytes > MONITOR_LIMIT - BYTES ||
        total > GLOBAL_LIMIT - BYTES) return nullptr;
    auto framebuffer = g_pHyprRenderer->createFB("hyprimate workspace capture");
    if (!framebuffer || !framebuffer->alloc(static_cast<int>(SIZE.x), static_cast<int>(SIZE.y),
            FORMAT)) return nullptr;
    framebuffer->setImageDescription(monitor->workBufferImageDescription());
    entry.allocated_bytes += BYTES;
    return framebuffer;
}

bool CWorkspaceAnimation::Impl::ready(const PHLMONITOR &monitor) const {
    return monitor && monitor->m_enabled && monitor->m_output &&
        monitor->m_drmFormat != DRM_FORMAT_INVALID &&
        monitor->m_transformedSize.x > 0 && monitor->m_transformedSize.y > 0 &&
        std::ranges::any_of(m_ready_monitors, [&](const auto &weak) { return weak.lock() == monitor; });
}

bool CWorkspaceAnimation::Impl::ensure_buffers(MonitorState &entry, const PHLMONITOR &monitor) {
    // resources() is lazy and fatal on an unconfigured output. A completed native
    // POST frame, not resources(), is the readiness proof during connect/disconnect.
    if (!g_pHyprRenderer || !ready(monitor) || !monitor->m_activeWorkspace || !safe_scene() ||
        (m_native_hud && !native_hud_safe(monitor)) ||
        g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL || !g_pHyprRenderer->glBackend()) {
        return false;
    }
    g_pHyprRenderer->glBackend()->makeEGLCurrent();
    const auto SIZE = monitor->m_transformedSize;
    if (entry.source && (entry.logical_size != monitor->m_size ||
        entry.scale != monitor->m_scale || entry.transform != monitor->m_transform ||
        entry.output_format != monitor->m_drmFormat || entry.source->m_drmFormat != capture_format(monitor) ||
        entry.source->imageDescription() != monitor->workBufferImageDescription())) return false;
    const auto USABLE = [&](const SP<Render::IFramebuffer> &framebuffer) {
        return framebuffer && framebuffer->isAllocated() && framebuffer->m_size == SIZE && framebuffer->getTexture();
    };
    if (!USABLE(entry.source) || !USABLE(entry.destination) || !USABLE(entry.presentation) ||
        (m_uses_wallpaper && (!USABLE(entry.source_wallpaper) || !USABLE(entry.destination_wallpaper) ||
            !USABLE(entry.opaque_backdrop)))) {
        // Never replace a frozen pair during a turn. SDR retains native 8-bit
        // precision; HDR/10-bit uses floating-point RGB with independent coverage.
        if (const auto *active_turn = turn(monitor); active_turn != nullptr && active_turn->active()) {
            return false;
        }
        const auto ALLOCATE = [&] { return allocate_capture(entry, monitor); };
        entry.source = ALLOCATE();
        entry.destination = ALLOCATE();
        entry.presentation = ALLOCATE();
        if (m_uses_wallpaper) {
            entry.source_wallpaper = ALLOCATE();
            entry.destination_wallpaper = ALLOCATE();
            entry.opaque_backdrop = ALLOCATE();
        }
        entry.source_resources.reset();
        entry.logical_size = monitor->m_size;
        entry.scale = monitor->m_scale;
        entry.transform = monitor->m_transform;
        entry.output_format = monitor->m_drmFormat;
        const auto PREPARE = [&](hyprimate::workspace::WorkspaceCapture &frame,
            const SP<Render::IFramebuffer> &composite, const SP<Render::IFramebuffer> &wallpaper) {
            frame.composite = composite;
            frame.wallpaper = wallpaper;
            frame.layered = m_layered;
            if (m_layered) {
                frame.bottom = {.accumulated = ALLOCATE(), .backdrop = wallpaper};
                frame.windows = {.accumulated = m_individual_windows ? frame.bottom.accumulated : ALLOCATE(),
                    .backdrop = frame.bottom.accumulated};
                frame.top = {.accumulated = composite,
                    .backdrop = m_individual_windows ? ALLOCATE() : frame.windows.accumulated};
            }
        };
        PREPARE(entry.source_frame, entry.source, entry.source_wallpaper);
        PREPARE(entry.destination_frame, entry.destination, entry.destination_wallpaper);
    }
    if (!USABLE(entry.source) || !USABLE(entry.destination) || !USABLE(entry.presentation) ||
        (m_uses_wallpaper && (!USABLE(entry.source_wallpaper) || !USABLE(entry.destination_wallpaper) ||
            !USABLE(entry.opaque_backdrop)))) {
        return false;
    }
    if (m_layered && (!USABLE(entry.source_frame.bottom.accumulated) ||
        !USABLE(entry.source_frame.windows.accumulated) || !USABLE(entry.source_frame.top.backdrop) ||
        !USABLE(entry.destination_frame.bottom.accumulated) ||
        !USABLE(entry.destination_frame.windows.accumulated) ||
        !USABLE(entry.destination_frame.top.backdrop))) {
        return false;
    }
    if (!entry.source_resources) {
        const auto BLUR = g_pHyprRenderer->createFB("hyprimate workspace source blur");
        entry.source_resources = makeShared<Render::CSceneResources>(BLUR,
            Render::SSceneBufferDescription{.format = entry.source->m_drmFormat,
                .imageDescription = entry.source->imageDescription()});
    }
    bool ready = false;
    m_styles.for_each([&]<class Style>(auto &resources) {
        if (Style::KIND != m_style_kind) {
            return;
        }
        if (!resources.ready) {
            const auto BACKEND = g_pHyprRenderer->glBackend();
            if (!BACKEND) {
                return;
            }
            BACKEND->makeEGLCurrent();
            resources.ready = resources.renderer.initialize();
        }
        ready = resources.ready;
    });
    return ready;
}

void CWorkspaceAnimation::Impl::capture_wallpaper_identity(MonitorState &entry,
    const PHLMONITOR &monitor, std::vector<WallpaperIdentity> &identity, bool shell) const {
    identity.clear();
    std::erase_if(entry.wallpaper_resources, [](const auto &item) { return item->surface.expired(); });
    struct IdentityData {
        MonitorState *state;
        std::vector<WallpaperIdentity> *identity;
        Vector2D position;
        float alpha = 1.F;
        std::size_t plane = 0;
    } identity_data{.state = &entry, .identity = &identity, .position = {}};
    if (!m_uses_wallpaper) {
        return;
    }
    for (std::size_t layer = 0; layer < monitor->m_layerSurfaceLayers.size(); ++layer) {
      if (!shell && layer != ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) continue;
      if (m_native_hud && layer != ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) continue;
      for (const auto &weak : monitor->m_layerSurfaceLayers[layer]) {
        const auto LAYER = weak.lock();
        const auto SURFACE = LAYER && LAYER->mapped() ? LAYER->resource() : nullptr;
        if (!SURFACE) {
            continue;
        }
        identity_data.position = LAYER->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        identity_data.alpha = LAYER->alpha()[Desktop::View::LS_ALPHA_FADE]->value();
        identity_data.plane = layer;
        SURFACE->breadthfirst([](SP<CWLSurfaceResource> child, const Vector2D &offset, void *opaque) {
            const auto &args = *static_cast<IdentityData *>(opaque);
            if (!child->m_current.texture) {
                return;
            }
            auto found = std::ranges::find_if(args.state->wallpaper_resources,
                [&](const auto &item) { return item->surface.lock() == child; });
            if (found == args.state->wallpaper_resources.end()) {
                auto resource = std::make_unique<WallpaperResource>();
                resource->surface = child;
                auto *tracked = resource.get();
                resource->commit = child->m_events.commit.listen([tracked] {
                    const auto SURFACE = tracked->surface.lock();
                    if (!SURFACE) {
                        return;
                    }
                    const auto &bits = SURFACE->m_current.updated.bits;
                    // SHM surfaces reuse their texture object; identity must include content
                    // commits, but not frame callbacks or input-only state changes.
                    if (bits.buffer || bits.damage || bits.viewport || bits.transform ||
                        bits.scale || bits.offset || bits.alphaModifier) {
                        ++tracked->generation;
                    }
                });
                args.state->wallpaper_resources.push_back(std::move(resource));
                found = std::prev(args.state->wallpaper_resources.end());
            }
            auto &tracked = **found;
            const CBox GEOMETRY{args.position + offset, child->m_current.size};
            if (tracked.geometry && (tracked.geometry->x != GEOMETRY.x || tracked.geometry->y != GEOMETRY.y ||
                tracked.geometry->width != GEOMETRY.width || tracked.geometry->height != GEOMETRY.height ||
                tracked.alpha != args.alpha || tracked.plane != args.plane)) ++tracked.generation;
            tracked.geometry = GEOMETRY;
            tracked.alpha = args.alpha;
            tracked.plane = args.plane;
            args.identity->push_back({.texture = child->m_current.texture, .generation = (*found)->generation});
        }, &identity_data);
    }
    }
}

bool CWorkspaceAnimation::Impl::capture(MonitorState &entry,
    const PHLMONITOR &monitor,
    const PHLWORKSPACE &workspace,
    hyprimate::workspace::CaptureKind kind,
    const SP<Render::IFramebuffer> &target, bool inactive) {
    if (!safe_scene() || !workspace || !ensure_buffers(entry, monitor)) {
        return false;
    }
    if (m_individual_windows) for (const auto &fadeout : Desktop::fadingOutState()->fadeouts()) {
        if (fadeout->monitor().lock() != monitor || fadeout->done() || fadeout->alpha() <= 0.F) continue;
        const auto SOURCE = fadeout->source();
        if (SOURCE.type != Desktop::eFadeoutSource::WINDOW) continue;
        const bool SELECTED = inactive
            ? Render::sceneSelectsFadeout(Render::eSceneMode::WORKSPACE_WITH_SHELL, SOURCE.type,
                SOURCE.workspace.lock() == workspace, SOURCE.pinned)
            : (!fadeout->workspace().lock() || fadeout->workspace().lock() == workspace);
        // Native window fadeouts already contain output-global dim/blur and may
        // span several per-window planes. Only this affected individual scene
        // abstains; composite and aggregate layered scenes keep native ordering.
        if (SELECTED) return false;
    }
    const auto RESOURCES = kind == hyprimate::workspace::CaptureKind::SourceIsolated
                               ? entry.source_resources
                               : monitor->resources()->sceneResources();
    struct DrawData {
        Impl *owner;
        PHLMONITOR monitor;
        PHLWORKSPACE workspace;
        bool inactive;
    } data{.owner = this, .monitor = monitor, .workspace = workspace, .inactive = inactive};
    const auto DRAW = [](void *opaque, Render::CRenderContext &ctx) {
        const auto &args = *static_cast<DrawData *>(opaque);
        const auto ORIGINAL = args.owner->render_original();
        if (ORIGINAL == nullptr || !g_pHyprRenderer) {
            return;
        }
        ORIGINAL(g_pHyprRenderer.get(),
            ctx,
            args.monitor,
            args.workspace,
            Time::steadyNow(),
            args.inactive ? Render::eSceneMode::WORKSPACE_WITH_SHELL : Render::eSceneMode::MONITOR,
            {},
            1.F);
    };
    auto &identity = target == entry.source ? entry.source_wallpaper_identity
                                            : entry.destination_wallpaper_identity;
    capture_wallpaper_identity(entry, monitor, identity);
    auto &frame = target == entry.source ? entry.source_frame : entry.destination_frame;
    auto &shell_identity = target == entry.source ? entry.source_shell_identity : entry.destination_shell_identity;
    capture_wallpaper_identity(entry, monitor, shell_identity, true);
    entry.pre_windows_seen = entry.post_windows_seen = entry.capture_failed = entry.floating_started = false;
    entry.wallpaper_seen = false;
    entry.floating_used = 0;
    entry.capturing_frame = &frame;
    entry.wallpaper_captured = false;
    m_capturing = m_uses_wallpaper ? &entry : nullptr;
    m_capture_wallpaper.reset();
    if (m_uses_wallpaper) {
        m_capture_wallpaper = target == entry.source ? entry.source_wallpaper : entry.destination_wallpaper;
    }
    const ScopedCapture capture_scope{m_capture_depth};
    const bool CAPTURED = hyprimate::workspace::capture_workspace(
        *g_pHyprRenderer, m_begin_render, monitor, target, RESOURCES, kind, DRAW, &data);
    m_capturing = nullptr;
    m_capture_wallpaper.reset();
    entry.capturing_frame = nullptr;
    if (frame.floating && frame.floating->size() > entry.floating_used) {
        for (std::size_t i = entry.floating_used; i < frame.floating->size(); ++i) {
            const auto RELEASE = [&](const SP<Render::IFramebuffer> &target) {
                if (target) entry.allocated_bytes -= framebuffer_bytes(target);
            };
            RELEASE((*frame.floating)[i].layer.accumulated);
            RELEASE((*frame.floating)[i].layer.backdrop);
        }
        frame.floating->resize(entry.floating_used);
    }
    return CAPTURED && !entry.capture_failed && (!m_uses_wallpaper ||
        (entry.wallpaper_seen && entry.wallpaper_captured)) &&
        (!m_layered || (entry.pre_windows_seen && entry.post_windows_seen));
}

void CWorkspaceAnimation::Impl::release_blend(MonitorState &entry, MonitorState::CaptureBlend &blend) {
    if (blend.stale) {
        const auto &old = *blend.stale;
        const auto RELEASE = [&](const SP<Render::IFramebuffer> &target) {
            if (target) entry.allocated_bytes -= framebuffer_bytes(target);
        };
        RELEASE(old.composite);
        RELEASE(old.wallpaper);
        RELEASE(old.bottom.accumulated);
        if (old.windows.accumulated != old.bottom.accumulated) RELEASE(old.windows.accumulated);
        if (old.top.backdrop != old.windows.accumulated) RELEASE(old.top.backdrop);
        if (old.floating) for (const auto &window : *old.floating) {
            RELEASE(window.layer.accumulated);
            RELEASE(window.layer.backdrop);
        }
    }
    blend.presentation.reset();
    blend.stale.reset();
    blend.old_order.clear();
    blend.matched.clear();
    blend.active = false;
    blend.mix = 1.0;
}

bool CWorkspaceAnimation::Impl::begin_refresh(MonitorState &entry, const PHLMONITOR &monitor, TurnKind kind) {
    auto &blend = kind == TurnKind::SourceIsolated ? entry.source_blend : entry.destination_blend;
    const auto *active_turn = turn(monitor);
    if (!m_layered || !active_turn || !active_turn->presented_once() || !blend.cached || blend.active) return true;
    g_pHyprRenderer->glBackend()->makeEGLCurrent();
    const auto &live = kind == TurnKind::SourceIsolated ? entry.source_frame : entry.destination_frame;
    const auto CLONE = [&](const SP<Render::IFramebuffer> &input, SP<Render::IFramebuffer> &output) {
        output = input ? allocate_capture(entry, monitor, input->m_size) : nullptr;
        return input && output && copy_opaque(input, output, false);
    };
    try {
        blend.stale = std::make_shared<hyprimate::workspace::WorkspaceCapture>();
        auto &old = *blend.stale;
        old.identity = live.identity;
        old.endpoint = live.endpoint;
        old.layered = live.layered;
        if (!CLONE(live.composite, old.composite) || !CLONE(live.wallpaper, old.wallpaper) ||
            !CLONE(live.bottom.accumulated, old.bottom.accumulated)) return false;
        old.bottom.backdrop = old.wallpaper;
        old.windows.backdrop = old.bottom.accumulated;
        old.top.accumulated = old.composite;
        if (live.windows.accumulated == live.bottom.accumulated) old.windows.accumulated = old.bottom.accumulated;
        else if (!CLONE(live.windows.accumulated, old.windows.accumulated)) return false;
        if (live.top.backdrop == live.windows.accumulated) old.top.backdrop = old.windows.accumulated;
        else if (!CLONE(live.top.backdrop, old.top.backdrop)) return false;
        if (live.floating) {
            old.floating = std::make_shared<std::vector<hyprimate::workspace::WorkspaceFloating>>();
            old.floating->reserve(live.floating->size());
            blend.old_order.reserve(live.floating->size());
            for (const auto &window : *live.floating) {
                blend.old_order.emplace_back(window.identity, old.floating->size());
                old.floating->emplace_back();
                auto &item = old.floating->back();
                item.identity = window.identity;
                item.rect = window.rect;
                item.order = window.order;
                if (!CLONE(window.layer.accumulated, item.layer.accumulated) ||
                    !CLONE(window.layer.backdrop, item.layer.backdrop)) return false;
            }
            std::ranges::sort(blend.old_order, {}, [](const auto &item) { return item.first; });
            blend.matched.resize(old.floating->size());
        }
    } catch (...) {
        return false;
    }
    const double PROGRESS = active_turn->progress();
    blend.start_distance = std::max(1e-6, std::min(PROGRESS, 1.0 - PROGRESS));
    blend.started = Clock::now();
    blend.mix = 0.0;
    blend.active = true;
    return true;
}

bool CWorkspaceAnimation::Impl::finish_refresh(MonitorState &entry, TurnKind kind) {
    (kind == TurnKind::SourceIsolated ? entry.source_blend : entry.destination_blend).cached = false;
    return true;
}

bool CWorkspaceAnimation::Impl::prepare_frame(MonitorState &entry, TurnKind kind,
    hyprimate::workspace::WorkspaceCapture &frame, double progress) {
    auto &blend = kind == TurnKind::SourceIsolated ? entry.source_blend : entry.destination_blend;
    if (!blend.active || !blend.stale) return true;
    const double TIME = std::chrono::duration<double>(Clock::now() - blend.started).count() / .08;
    const double DISTANCE = std::max(0.0, std::min(progress, 1.0 - progress));
    // A monotonic content handoff, accelerated as either native endpoint approaches.
    // Even a 100 ms journey with a narrow live boundary must arrive fully live.
    blend.mix = std::clamp(std::max({blend.mix, TIME, 1.0 - DISTANCE / blend.start_distance}), 0.0, 1.0);
    if (blend.mix >= 1.0) {
        release_blend(entry, blend);
        return true;
    }
    frame.previous = blend.stale; // Clones never have a previous pointer: one bounded generation.
    frame.live_mix = blend.mix;
    const auto &old = *blend.stale;
    const auto COUNT = [](const auto &capture) { return capture.floating ? capture.floating->size() : 0; };
    if (COUNT(old) == 0 && COUNT(frame) == 0) return true;
    try {
        if (!blend.presentation) blend.presentation = std::make_shared<std::vector<hyprimate::workspace::WorkspaceFloating>>();
        auto &view = *blend.presentation;
        view.clear();
        view.reserve(COUNT(old) + COUNT(frame));
        std::ranges::fill(blend.matched, 0);
        if (frame.floating) for (const auto &window : *frame.floating) {
            const auto FOUND = std::ranges::lower_bound(blend.old_order, window.identity, {},
                [](const auto &item) { return item.first; });
            view.push_back(window);
            auto &item = view.back();
            if (FOUND == blend.old_order.end() || FOUND->first != window.identity) {
                item.opacity *= static_cast<float>(blend.mix);
            } else {
                const auto INDEX = FOUND->second;
                blend.matched[INDEX] = 1;
                const auto &previous = (*old.floating)[INDEX];
                item.previous_layer = previous.layer;
                item.live_mix = static_cast<float>(blend.mix);
                item.rect = {std::lerp(previous.rect.x, window.rect.x, blend.mix),
                    std::lerp(previous.rect.y, window.rect.y, blend.mix),
                    std::lerp(previous.rect.width, window.rect.width, blend.mix),
                    std::lerp(previous.rect.height, window.rect.height, blend.mix)};
                item.order = std::lerp(previous.order, window.order, blend.mix);
            }
        }
        if (old.floating) for (std::size_t i = 0; i < old.floating->size(); ++i) {
            if (blend.matched[i]) continue;
            view.push_back((*old.floating)[i]);
            view.back().opacity *= static_cast<float>(1.0 - blend.mix);
        }
        std::ranges::sort(view, {}, [](const auto &item) { return item.order; });
        frame.floating = blend.presentation;
    } catch (...) {
        return false;
    }
    return true;
}

bool CWorkspaceAnimation::Impl::TurnPort::buffers(bool /*active*/) const {
    return owner->ensure_buffers(*entry, entry->monitor.lock());
}
bool CWorkspaceAnimation::Impl::TurnPort::capture(
    TurnKind kind, const hyprimate::workspace::TurnComposition<Workspace> &composition) const {
    const auto MONITOR = entry->monitor.lock();
    const auto TARGET = kind == TurnKind::SourceIsolated ? entry->source : entry->destination;
    if (!owner->begin_refresh(*entry, MONITOR, kind)) return false;
    auto &frame = kind == TurnKind::SourceIsolated ? entry->source_frame : entry->destination_frame;
    frame.identity = composition.identity;
    frame.endpoint = composition.endpoint();
    return owner->capture(*entry, MONITOR, composition.workspace.lock(), kind, TARGET) &&
        owner->finish_refresh(*entry, kind);
}
double CWorkspaceAnimation::Impl::TurnPort::duration(
    const hyprimate::workspace::JourneyEndpoint &from,
    const hyprimate::workspace::JourneyEndpoint &to, double base) const {
    double duration = base;
    owner->m_styles.for_each([&]<class Style>(auto &resources) {
        if (Style::KIND == owner->m_style_kind) duration = Style::duration(resources, from, to);
    });
    return duration;
}

bool CWorkspaceAnimation::Impl::TurnPort::should_refresh(TurnKind kind, double progress) const {
    double boundary = 1.0;
    owner->m_styles.for_each([&]<class Style>(auto &resources) {
        if (Style::KIND == owner->m_style_kind) boundary = Style::live_boundary(resources);
    });
    boundary = std::clamp(boundary, 0.0, 1.0);
    // Workspace-aware shell clients may repaint several frames after dispatch.
    // Keep both endpoint captures current through takeoff, then cache during distance.
    const bool REFRESH = progress <= boundary ||
        (kind == TurnKind::DestinationLive && progress >= 1.0 - boundary);
    if (!REFRESH) (kind == TurnKind::SourceIsolated ? entry->source_blend : entry->destination_blend).cached = true;
    return REFRESH;
}

bool CWorkspaceAnimation::Impl::TurnPort::refresh_inactive_source(
    const hyprimate::workspace::TurnComposition<Workspace> &composition) const {
    if (!owner->m_layered) return true;
    const auto MONITOR = entry->monitor.lock();
    const auto SOURCE = composition.workspace.lock();
    if (!MONITOR || !safe_scene()) return false;
    if (!SOURCE || composition.special || MONITOR->m_activeSpecialWorkspace) return true;
    // Interactive shell popups have their own scene selection/lifecycle and are
    // not children of the layer's tracked wl_surface commit tree.
    if (!owner->m_native_hud) for (const auto &layer : Desktop::layerState()->layers()) {
        if (layer->m_monitor.lock() != MONITOR) continue;
        bool popup = false;
        if (const auto head = layer->popupHead(); head) head->breadthfirst(
            [&](WP<Desktop::View::CPopup> weak, void *) {
                const auto item = weak.lock();
                popup = popup || (item && item->mapped());
            }, nullptr);
        if (popup) return true;
    }
    auto &shell = entry->shell_probe_identity;
    owner->capture_wallpaper_identity(*entry, MONITOR, shell, true);
    // WORKSPACE_WITH_SHELL uses today's monitor shell. Never transplant a changed
    // panel/wallpaper into the frozen departing endpoint.
    if (!hyprimate::workspace::wallpaper_matches<SP<Render::ITexture>>(
            shell, entry->source_shell_identity)) return true;
    if (!owner->begin_refresh(*entry, MONITOR, TurnKind::SourceIsolated)) return false;
    return owner->capture(*entry, MONITOR, SOURCE, TurnKind::SourceIsolated, entry->source, true) &&
        owner->finish_refresh(*entry, TurnKind::SourceIsolated);
}

void CWorkspaceAnimation::Impl::TurnPort::present_native() const {
    copy_opaque(entry->destination, entry->presentation);
}
void CWorkspaceAnimation::Impl::TurnPort::swap_slots() const {
    std::swap(entry->source, entry->destination);
    std::swap(entry->source_wallpaper, entry->destination_wallpaper);
    std::swap(entry->source_wallpaper_identity, entry->destination_wallpaper_identity);
    std::swap(entry->source_frame, entry->destination_frame);
    std::swap(entry->source_shell_identity, entry->destination_shell_identity);
    std::swap(entry->source_blend, entry->destination_blend);
}
void CWorkspaceAnimation::Impl::TurnPort::damage() const {
    Impl::damage(entry->monitor.lock());
}

std::vector<CWorkspaceAnimation::Impl::Snapshot> CWorkspaceAnimation::Impl::snapshot(
    std::initializer_list<PHLMONITOR> monitors) {
    std::vector<Snapshot> result;
    result.reserve(monitors.size());
    for (const auto &monitor : monitors) {
        if (!ready(monitor) || !monitor->m_activeWorkspace ||
            std::ranges::any_of(result, [&](const auto &item) { return item.monitor == monitor; })) {
            continue;
        }
        auto &entry = ensure_state(monitor);
        Snapshot item{.monitor = monitor, .composition = composition(monitor, true)};
        item.ready = m_turns.get(monitor_key(monitor), entry.port).stage(item.composition);
        if (!item.ready) {
            erase_state(monitor);
        } else if (m_uses_previews && !turn(monitor)->active()) {
            remember_wallpaper(monitor, monitor->m_activeSpecialWorkspace ? monitor->m_activeSpecialWorkspace :
                monitor->m_activeWorkspace, entry.source_wallpaper);
        }
        result.push_back(std::move(item));
    }
    return result;
}

void CWorkspaceAnimation::Impl::finish(std::vector<Snapshot> &snapshots) {
    for (const auto &item : snapshots) {
        if (!item.ready || !item.monitor) {
            continue;
        }
        auto *entry = state(item.monitor);
        auto *active_turn = turn(item.monitor);
        if (entry == nullptr || active_turn == nullptr) {
            continue;
        }
        auto before = item.composition;
        auto after = composition(item.monitor, true);
        // Removing/creating the previous workspace during the mutation can collapse
        // two distinct numbered endpoints onto the same rank. Preserve one logical hop.
        if (before.numbered && after.numbered && before.numbered != after.numbered &&
            before.logicalOrder == after.logicalOrder) {
            if (*before.numbered < *after.numbered) ++after.logicalOrder;
            else ++before.logicalOrder;
        }
        active_turn->begin(before, after, m_turns.duration(), m_turns.curve());
        entry->last = Clock::now();
        if (!active_turn->active()) {
            erase_state(item.monitor);
        }
    }
}

void CWorkspaceAnimation::Impl::mutate(
    const std::vector<PHLMONITOR> &monitors, const std::function<void()> &original) {
    if (!m_turns.enabled() || m_capture_depth > 0 || m_mutation_depth > 0) {
        original();
        return;
    }
    std::vector<Snapshot> before;
    before.reserve(monitors.size());
    for (const auto &monitor : monitors) {
        auto one = snapshot({monitor});
        if (!one.empty()) {
            before.push_back(std::move(one.front()));
        }
    }
    m_suppressed.clear();
    for (const auto &item : before) {
        if (item.ready) {
            m_suppressed.emplace_back(item.monitor);
        }
    }
    struct MutationDepth {
        Impl *owner;
        explicit MutationDepth(Impl &value) : owner(&value) {
            ++owner->m_mutation_depth;
        }
        MutationDepth(const MutationDepth &) = delete;
        MutationDepth &operator=(const MutationDepth &) = delete;
        MutationDepth(MutationDepth &&) = delete;
        MutationDepth &operator=(MutationDepth &&) = delete;
        ~MutationDepth() {
            --owner->m_mutation_depth;
        }
    };
    const MutationDepth DEPTH{*this};
    original();
    m_suppressed.clear();
    finish(before);
}

bool CWorkspaceAnimation::Impl::suppresses(const PHLMONITOR &monitor) const {
    return monitor && std::ranges::any_of(m_suppressed,
                          [&](const auto &candidate) { return candidate.lock() == monitor; });
}

void CWorkspaceAnimation::Impl::capture_gesture(const PHLMONITOR &monitor) {
    if (!m_turns.enabled() || !monitor) {
        return;
    }
    auto captured = snapshot({monitor});
    if (!captured.empty() && captured.front().ready) {
        m_gesture_monitor = monitor;
    }
}

void CWorkspaceAnimation::Impl::clear_gesture() {
    const auto MONITOR = m_gesture_monitor.lock();
    m_gesture_monitor.reset();
    if (!MONITOR) {
        return;
    }
    if (auto *active_turn = turn(MONITOR); active_turn != nullptr) {
        active_turn->release_gesture();
        if (!active_turn->active()) {
            erase_state(MONITOR);
        }
    }
}

void CWorkspaceAnimation::Impl::pre_checks(const PHLMONITOR &monitor) {
    if (const auto *active_turn = turn(monitor); active_turn != nullptr && active_turn->active()) {
        monitor->m_solitaryClient.reset();
    }
}

void CWorkspaceAnimation::Impl::pre_render(const PHLMONITOR &monitor) {
    if (!monitor) {
        return;
    }
    auto *entry = state(monitor);
    auto *active_turn = turn(monitor);
    if (entry == nullptr || active_turn == nullptr || !active_turn->active()) {
        return;
    }
    if (!safe_scene() || (m_native_hud && !native_hud_safe(monitor))) {
        erase_state(monitor);
        return;
    }
    const auto NOW = Clock::now();
    const auto ELAPSED = std::chrono::duration<double>(NOW - entry->last).count();
    entry->last = NOW;
    active_turn->frame(composition(monitor), ELAPSED);
    if (m_uses_previews && active_turn->drawable()) {
        prepare_previews(*entry, monitor, active_turn->source(), active_turn->destination());
    }
    if (!active_turn->active()) {
        erase_state(monitor);
    }
}

bool CWorkspaceAnimation::Impl::queue(
    Render::CRenderContext &ctx, const PHLMONITOR &monitor, const Time::steady_tp &now) {
    if (m_capture_depth > 0 || !monitor || ctx.m_renderingCapture || ctx.m_renderingSnapshot || ctx.readOnlyEffects()) {
        return false;
    }
    auto *entry = state(monitor);
    const auto *active_turn = turn(monitor);
    if (entry == nullptr || active_turn == nullptr || !active_turn->drawable() || !entry->source || !entry->destination ||
        !entry->presentation) {
        return false;
    }
    if (!safe_scene() || (m_native_hud && !native_hud_safe(monitor))) {
        erase_state(monitor);
        return false;
    }
    entry->source_frame.endpoint = active_turn->source().endpoint();
    entry->destination_frame.endpoint = active_turn->destination().endpoint();
    hyprimate::workspace::WorkspaceFrame frame{
        .source = entry->source_frame, .destination = entry->destination_frame,
        .presentation = entry->presentation, .neighbors = entry->neighbors, .native_hud = m_native_hud};
    if (!prepare_frame(*entry, TurnKind::SourceIsolated, frame.source, active_turn->progress()) ||
        !prepare_frame(*entry, TurnKind::DestinationLive, frame.destination, active_turn->progress())) {
        erase_state(monitor);
        return false;
    }
    Render::IHyprRenderer::addPassElement(ctx,
        makeUnique<CWorkspacePresentationPass>(this,
            monitor,
            std::move(frame),
            hyprimate::workspace::wallpaper_matches<SP<Render::ITexture>>(
                entry->source_wallpaper_identity, entry->destination_wallpaper_identity)));
    CTexPassElement::SRenderData data;
    data.tex = entry->presentation->getTexture();
    data.box = {0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
    data.a = 1.F;
    data.damage = CRegion{data.box};
    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CTexPassElement>(std::move(data)));
    if (m_native_hud) {
        // Intentionally lift screen-aligned shell above the planetary scene.
        // Preserve native body ordering (bottom/top/overlay), then native popup
        // ordering across all layer lists. Background bodies remain in worlds.
        const auto replay = [&](const PHLLSREF &weak, bool popup = false) {
            if (const auto surface = weak.lock())
                ((*g_pHyprRenderer).*PreviewDrawAccess::LAYER)(ctx, surface, monitor, now, popup, false, false);
        };
        for (const auto &weak : monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM])
            replay(weak);
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CWorkspaceHudBlurPass>());
        for (const auto layer : {ZWLR_LAYER_SHELL_V1_LAYER_TOP, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY})
            for (const auto &weak : monitor->m_layerSurfaceLayers[layer])
                replay(weak);
        for (const auto &layers : monitor->m_layerSurfaceLayers)
            for (const auto &weak : layers)
                replay(weak, true);
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CWorkspaceHudBlurPass>(true));
    }
    return true;
}


void CWorkspaceAnimation::Impl::damage(const PHLMONITOR &monitor) {
    if (!monitor) {
        return;
    }
    monitor->addDamage(CBox{0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y});
    monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
}

void CWorkspaceAnimation::Impl::erase_state(const PHLMONITOR &monitor) {
    if (monitor) {
        m_turns.remove(monitor_key(monitor));
    }
    std::erase_if(m_states, [&](const auto &entry) { return entry->monitor.lock() == monitor; });
}

void CWorkspaceAnimation::Impl::remember_wallpaper(const PHLMONITOR &monitor, const PHLWORKSPACE &workspace,
    const SP<Render::IFramebuffer> &source) {
    if (!workspace || !source) return;
    std::erase_if(m_observed, [](const auto &item) { return item.workspace.expired() || item.monitor.expired(); });
    auto found = std::ranges::find_if(m_observed, [&](const auto &item) { return item.workspace.lock() == workspace; });
    // Keep a bounded LRU of actual wallpaper history, never private client pixels.
    SP<Render::IFramebuffer> image;
    if (found != m_observed.end()) {
        if (found->size == monitor->m_transformedSize && found->image->m_drmFormat == source->m_drmFormat &&
            found->image->imageDescription() == source->imageDescription()) image = std::move(found->image);
        m_observed.erase(found);
    } else if (m_observed.size() >= 64) m_observed.erase(m_observed.begin());
    if (!image) image = preview_target(monitor, 256);
    if (write_thumbnail(source, image))
        m_observed.push_back({workspace, monitor, image, monitor->m_transformedSize, monitor->m_transform});
}

void CWorkspaceAnimation::Impl::prepare_previews(MonitorState &entry, const PHLMONITOR &monitor,
    const Composition &source, const Composition &destination) {
    if (!entry.neighbors || entry.preview_from != source.identity || entry.preview_to != destination.identity ||
        entry.inventory_revision != m_inventory_revision)
        index_previews(entry, monitor, source, destination);

    const auto now = Clock::now();
    // Keep work bounded even on cold starts. A newly captured client fades in
    // over its own wallpaper, rather than popping in when the scheduler gets to it.
    if (entry.preview_next < entry.preview_jobs.size()) {
        auto &job = entry.preview_jobs[entry.preview_next++];
        const auto ws = job.workspace.lock();
        auto &preview = (*entry.neighbors)[job.index];
        if (const auto image = ws ? capture_preview(entry, monitor, ws, job.paper) : nullptr) {
            preview.previous_image = std::move(preview.image);
            preview.image = image;
            preview.live_mix = 0.F;
            job.started = now;
        }
    }
    for (auto i = entry.preview_blending; i < entry.preview_next; ++i) {
        const auto &job = entry.preview_jobs[i];
        auto &preview = (*entry.neighbors)[job.index];
        const float x = std::clamp(std::chrono::duration<float>(now - job.started).count() / .08F, 0.F, 1.F);
        preview.live_mix = x * x * (3.F - 2.F * x);
        if (x == 1.F) {
            preview.previous_image.reset();
            if (entry.preview_blending == i) ++entry.preview_blending;
        }
    }
}

void CWorkspaceAnimation::Impl::index_previews(MonitorState &entry, const PHLMONITOR &monitor,
    const Composition &source, const Composition &destination) {
    auto inventory = State::Workspace::state()->workspaces();
    const auto count = static_cast<std::size_t>(std::ranges::distance(inventory));
    auto neighbors = std::make_shared<std::vector<hyprimate::workspace::WorkspacePreview>>();
    neighbors->reserve(count);
    entry.preview_jobs.clear();
    entry.preview_jobs.reserve(count);
    entry.preview_next = entry.preview_blending = 0;
    // Bound all per-journey thumbnails together to 2M texels (16 MiB at FP16).
    // Reserve room for mip levels (4/3 base size) and one local fallback wallpaper.
    constexpr double BASE_TEXELS = 2'097'152 * .75 - 256 * 256;
    entry.preview_side = std::clamp(static_cast<int>(std::sqrt(BASE_TEXELS / std::max<std::size_t>(1, count))), 1, 384);
    SP<Render::IFramebuffer> shared_paper;
    bool sampled_local_paper = false;
    for (const auto &weak : inventory) {
        const auto ws = weak.lock();
        if (!ws) continue;
        const auto owner = ws->m_monitor.lock();
        const bool special = ws->type() == Workspace::eWorkspaceType::SPECIAL;
        const auto number = numbered(ws);
        const auto placement = number && !special ? m_placements.get(number, {}, std::nullopt) :
            special ? m_placements.get(std::nullopt, {}, ws->addressableName()) :
                      m_placements.get(std::nullopt, ws->addressableName(), std::nullopt);
        if (placement == source.placement || placement == destination.placement) continue;
        const auto identity = special ? m_compositions.get({}, ws) : m_compositions.get(ws, {});
        auto &preview = neighbors->emplace_back(hyprimate::workspace::WorkspacePreview{
            .endpoint = {.identity = identity, .numbered = number, .special = special, .placement = placement},
            .image = {}, .previous_image = {}});
        if (!ready(owner)) continue;
        const auto remembered = std::ranges::find_if(m_observed, [&](const auto &item) {
            return item.workspace.lock() == ws && item.monitor.lock() == owner && item.size == owner->m_transformedSize &&
                item.transform == owner->m_transform && item.image->m_drmFormat == capture_format(owner) &&
                item.image->imageDescription() == owner->workBufferImageDescription();
        });
        const auto paper = remembered == m_observed.end() ? SP<Render::IFramebuffer>{} : remembered->image;
        if (!paper && owner == monitor && !sampled_local_paper) {
            sampled_local_paper = true;
            auto image = preview_target(monitor, 256);
            if (write_thumbnail(entry.destination_frame.wallpaper, image)) shared_paper = std::move(image);
        }
        preview.image = paper ? paper : owner == monitor ? shared_paper : nullptr;
        // WORKSPACE_WINDOWS selects only clients owned by this workspace.
        // Without clients or retiring snapshots the wallpaper *is* its preview.
        if (preview.image && ws->getWindowCount() == 0 && Desktop::fadingOutState()->fadeouts().empty()) continue;
        // Prioritize numbered worlds along the route ahead of worlds beyond
        // either end. Placement breaks ties independently of inventory order.
        double priority = 1e12;
        if (number && source.numbered && destination.numbered) {
            const auto [lo, hi] = std::minmax(*source.numbered, *destination.numbered);
            priority = std::abs(static_cast<double>(*number) - *source.numbered);
            if (*number < lo || *number > hi) priority += 1e10;
        }
        entry.preview_jobs.push_back({ws, neighbors->size() - 1, priority, paper});
    }
    std::ranges::sort(entry.preview_jobs, [&](const auto &a, const auto &b) {
        return a.priority != b.priority ? a.priority < b.priority :
            (*neighbors)[a.index].endpoint.placement < (*neighbors)[b.index].endpoint.placement;
    });
    entry.neighbors = std::move(neighbors);
    entry.preview_from = source.identity;
    entry.preview_to = destination.identity;
    entry.inventory_revision = m_inventory_revision;
}

SP<Render::IFramebuffer> CWorkspaceAnimation::Impl::capture_preview(MonitorState &entry, const PHLMONITOR &monitor,
    const PHLWORKSPACE &ws, const SP<Render::IFramebuffer> &paper) {
    const auto owner = ws->m_monitor.lock();
    if (!ready(owner)) return {};
    // The presentation target is scratch only before the ordinary output pass.
    auto scratch = entry.presentation;
    auto resources = entry.source_resources;
    if (owner != monitor) {
        const auto extent = owner->m_transformedSize;
        if (extent.x * extent.y > 16777216.) return {};
        scratch = preview_target(owner, static_cast<int>(std::max(extent.x, extent.y)));
        if (!scratch) return {};
        resources = makeShared<Render::CSceneResources>(g_pHyprRenderer->createFB("hyprimate workspace preview blur"),
            Render::SSceneBufferDescription{.format = scratch->m_drmFormat, .imageDescription = scratch->imageDescription()});
    }
    if (!scratch || !resources) return {};
    struct DrawData {
        RenderFn original;
        PHLMONITOR monitor;
        PHLWORKSPACE workspace;
        SP<Render::IFramebuffer> wallpaper;
    } data{render_original(), owner, ws, paper};
    const auto draw = [](void *context, Render::CRenderContext &ctx) {
        auto &args = *static_cast<DrawData *>(context);
        auto &renderer = *g_pHyprRenderer;
        const auto now = Time::steadyNow();
        if (args.wallpaper) {
            CTexPassElement::SRenderData layer;
            layer.tex = args.wallpaper->getTexture();
            layer.box = {0, 0, args.monitor->m_transformedSize.x, args.monitor->m_transformedSize.y};
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CTexPassElement>(std::move(layer)));
        } else {
            // A wallpaper client exposes its current surface per monitor, not a
            // database of workspace-specific images. Use that real background
            // until the workspace has been observed, never another window image.
            (renderer.*PreviewDrawAccess::BACKGROUND)(ctx, args.monitor);
            for (const auto &layer : args.monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND])
                if (const auto surface = layer.lock())
                    (renderer.*PreviewDrawAccess::LAYER)(ctx, surface, args.monitor, now, false, false, true);
        }
        args.original(&renderer, ctx, args.monitor, args.workspace, now,
            Render::eSceneMode::WORKSPACE_WINDOWS, {}, 1.F);
    };
    auto image = preview_target(owner, entry.preview_side);
    const ScopedCapture capture_scope{m_capture_depth};
    return image && hyprimate::workspace::capture_workspace(*g_pHyprRenderer, m_begin_render, owner, scratch,
        resources, TurnKind::SourceIsolated, draw, &data) && write_thumbnail(scratch, image) ? image : nullptr;
}

void CWorkspaceAnimation::Impl::remove_monitor(const PHLMONITOR &monitor) {
    if (!monitor) {
        return;
    }
    std::erase_if(m_ready_monitors, [&](const auto &weak) { return weak.expired() || weak.lock() == monitor; });
    std::erase_if(m_observed, [&](const auto &item) { return item.monitor.expired() || item.monitor.lock() == monitor; });
    erase_state(monitor);
}

void CWorkspaceAnimation::Impl::clear() {
    m_turns.reload();
    m_turns.clear();
    for (const auto &entry : m_states) {
        if (const auto MONITOR = entry->monitor.lock()) {
            damage(MONITOR);
        }
    }
    m_states.clear();
    m_suppressed.clear();
    m_gesture_monitor.reset();
}

CWorkspaceAnimation::CWorkspaceAnimation(HANDLE handle) : m_impl(std::make_unique<Impl>(handle)) {
}
CWorkspaceAnimation::~CWorkspaceAnimation() = default;
bool CWorkspaceAnimation::install(std::string &error) {
    return m_impl->install(error);
}
void CWorkspaceAnimation::shutdown() {
    m_impl->shutdown();
}
void CWorkspaceAnimation::configure(const hyprimate::config::SWorkspaceSettings &settings) {
    m_impl->configure(settings);
}
void CWorkspaceAnimation::on_render_pre_checks(const PHLMONITOR &monitor) {
    m_impl->pre_checks(monitor);
}
void CWorkspaceAnimation::on_render_pre(const PHLMONITOR &monitor) {
    m_impl->pre_render(monitor);
}
void CWorkspaceAnimation::remove_monitor(const PHLMONITOR &monitor) {
    m_impl->remove_monitor(monitor);
}
void CWorkspaceAnimation::clear() {
    m_impl->clear();
}
