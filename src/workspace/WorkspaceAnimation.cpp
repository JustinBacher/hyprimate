#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <drm_fourcc.h>
#include <hyprland/protocols/wlr-layer-shell-unstable-v1.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/animation/WorkspaceAnimationController.hpp>
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

#include "gl/RawGlBracket.hpp"
#include "workspace/WorkspaceAnimation.hpp"
#include "workspace/WorkspaceCapture.hpp"
#include "workspace/WorkspaceCapturePlan.hpp"
#include "workspace/WorkspaceFrame.hpp"
#include "workspace/WorkspaceIdentity.hpp"
#include "workspace/WorkspaceStyleList.hpp"
#include "workspace/WorkspaceTurn.hpp"
#include "workspace/WorkspaceCallbackScope.hpp"

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

    struct NativeHudFacts {
        bool retiring_shell = false;
    };

    NativeHudFacts native_hud_facts(const PHLMONITOR &monitor) {
        NativeHudFacts facts;
        for (const auto &fadeout : Desktop::fadingOutState()->fadeouts()) {
            facts.retiring_shell = facts.retiring_shell ||
                                   (fadeout != nullptr && fadeout->monitor().lock() == monitor &&
                                       !fadeout->done() && fadeout->alpha() > 0.F &&
                                       fadeout->source().type == Desktop::eFadeoutSource::LAYER &&
                                       fadeout->plane() != Desktop::FADEOUT_PLANE_LAYER_BACKGROUND);
        }
        return facts;
    }

    bool native_hud_safe(const PHLMONITOR &monitor) {
        return hyprimate::workspace::capture_hud_safe(native_hud_facts(monitor).retiring_shell);
    }

    hyprimate::workspace::CaptureSafetyFacts scene_facts() {
        hyprimate::workspace::CaptureSafetyFacts facts;
        facts.session_available = g_pSessionLockManager != nullptr;
        facts.locked = g_pSessionLockManager != nullptr && g_pSessionLockManager->isSessionLocked();
        facts.input_available = g_pInputManager != nullptr;
        facts.exclusive_input =
            g_pInputManager != nullptr && !g_pInputManager->m_exclusiveKeyboardLSes.empty();
        facts.input_method = g_pInputManager != nullptr &&
                             g_pInputManager->m_relay.m_inputMethod.lock() != nullptr &&
                             g_pInputManager->m_relay.getFocusedTextInput() != nullptr;
        const auto FOCUS = Desktop::focusState();
        facts.non_window_focus =
            FOCUS != nullptr && FOCUS->surface() != nullptr && FOCUS->window() == nullptr;
        // Include inactive, unmapped and fading views: retained pixels can outlive
        // native screenshare masks at their original output coordinates.
        for (const auto &window : Desktop::windowState()->windows()) {
            facts.protected_content = facts.protected_content ||
                                      window->m_ruleApplicator->noScreenShare().valueOrDefault();
        }
        for (const auto &layer : Desktop::layerState()->layers()) {
            facts.protected_content = facts.protected_content ||
                                      layer->m_ruleApplicator->noScreenShare().valueOrDefault();
        }
        for (const auto &fadeout : Desktop::fadingOutState()->fadeouts()) {
            if (fadeout == nullptr || fadeout->monitor().lock() == nullptr) {
                facts.invalid_fadeout = true;
                continue;
            }
            const auto SOURCE = fadeout->source();
            facts.protected_content = facts.protected_content || SOURCE.noScreenShare;
            const auto FRAMEBUFFER = fadeout->framebuffer();
            facts.invalid_fadeout = facts.invalid_fadeout ||
                                    (SOURCE.type != Desktop::eFadeoutSource::WINDOW &&
                                        SOURCE.type != Desktop::eFadeoutSource::LAYER) ||
                                    FRAMEBUFFER == nullptr || !FRAMEBUFFER->isAllocated() ||
                                    FRAMEBUFFER->getTexture() == nullptr ||
                                    !std::isfinite(fadeout->alpha());
        }
        return facts;
    }

    bool safe_scene() {
        return hyprimate::workspace::capture_scene_safe(scene_facts());
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
        ScopedCapture(ScopedCapture &&) = delete;
        ScopedCapture &operator=(ScopedCapture &&) = delete;
      private:
        int &m_depth;
    };

}  // namespace

class CWorkspaceAnimation::Impl : public std::enable_shared_from_this<Impl> {
  public:
    explicit Impl(HANDLE handle) : m_handle(handle) {
        m_styles.for_each([this]<class Style>(auto &resources) {
            const auto CHANGED = [this] {
                if (m_last_settings && m_last_settings->style == Style::KIND) {
                    m_turns.reload();
                    configure(*m_last_settings);
                }
            };
            if constexpr (requires { resources.curves.install(m_handle, CHANGED); }) {
                if (!resources.curves.install(m_handle, CHANGED)) {
                    HyprlandAPI::addNotification(m_handle, "[hyprimate] Workspace style Lua curve registration unavailable",
                        CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
                }
            }
        });
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
        using Preview = hyprimate::workspace::WorkspacePreview;
        Impl *owner;
        MonitorState *entry;
        [[nodiscard]] bool buffers(bool active) const;
        [[nodiscard]] static bool scene_safe();
        [[nodiscard]] bool hud_safe() const;
        [[nodiscard]] bool capture(TurnKind kind, const hyprimate::workspace::TurnComposition<Workspace> &composition) const;
        [[nodiscard]] double duration(const hyprimate::workspace::JourneyEndpoint &from,
            const hyprimate::workspace::JourneyEndpoint &destination, double base) const;
        [[nodiscard]] double live_boundary() const;
        [[nodiscard]] double max_frame_gap() const { return owner->m_max_frame_gap; }
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
                    Style::includes_workspace(resources, candidate, source, destination);
                }) {
                    included = Style::includes_workspace(
                        resources, candidate, source, destination);
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
    };
    Composition composition(const PHLMONITOR &monitor, bool ordered = false) {
        const auto NUMBER = numbered(monitor->m_activeWorkspace);
        std::uint32_t order = 0;
        if (ordered && NUMBER) {
            for (const auto &candidate : State::Workspace::state()->workspaces()) {
                const auto NUMBER_ID = candidate->numberedID();
                if (NUMBER_ID && *NUMBER_ID < *NUMBER && candidate->type() != Workspace::eWorkspaceType::SPECIAL) {
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
        return monitor ? m_compositions.get(monitor->m_activeWorkspace, monitor->m_activeSpecialWorkspace) : 0;
    }


    bool install(std::string &error);
    void shutdown();
    void on_render_stage(const Event::SRenderStageEvent &event);
    void configure(const hyprimate::config::SWorkspaceSettings &settings);
    template<class Style> void configure_style(bool enabled, Style::Resources &resources);
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
    void inhibit_scanout(const PHLMONITOR &monitor);
    void restore_scanout(const PHLMONITOR &monitor = {});
    bool needs_native_prime(const PHLMONITOR &monitor, bool enabled) const;
    void prime_outputs() const;
    bool initialize_selected_style();

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
    const std::shared_ptr<MonitorState> *state_slot(const PHLMONITOR &monitor) const;
    MonitorState *state(const PHLMONITOR &monitor) const {
        const auto *slot = state_slot(monitor);
        return slot ? slot->get() : nullptr;
    }
    std::shared_ptr<MonitorState> ensure_state(const PHLMONITOR &monitor);
    bool ensure_buffers(MonitorState &entry, const PHLMONITOR &monitor);
    hyprimate::workspace::CaptureBufferFacts buffer_facts(const MonitorState &entry, const PHLMONITOR &monitor);
    static void release_floating_tail(MonitorState &entry, hyprimate::workspace::WorkspaceCapture &frame);
    bool capture(MonitorState &entry,
        const PHLMONITOR &monitor,
        const PHLWORKSPACE &workspace,
        hyprimate::workspace::CaptureKind kind,
        const SP<Render::IFramebuffer> &target, bool inactive = false);
    SP<Render::IFramebuffer> allocate_capture(MonitorState &entry, const PHLMONITOR &monitor,
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
    static void scan_wallpaper_surface(const SP<CWLSurfaceResource> &child, const Vector2D &offset, void *opaque);
    void capture_wallpaper_identity(MonitorState &entry, const PHLMONITOR &monitor,
        std::vector<WallpaperIdentity> &identity, bool shell = false) const;
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
    std::optional<hyprimate::config::SWorkspaceSettings> m_deferred_settings;
    void release_styles() {
        m_styles.for_each([]<class Style>(auto &resources) {
            if (resources.ready && Render::GL::g_pHyprOpenGL) {
                resources.renderer.release();
            }
            resources.ready = false;
        });
    }
    bool m_uses_wallpaper = false;
    bool m_layered = false;
    bool m_individual_windows = false;
    bool m_native_hud = false;
    bool m_uses_previews = false;
    double m_max_frame_gap = hyprimate::STALL_SECONDS;
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
    std::optional<std::string> m_invalid_curve;
    CHyprSignalListener m_wallpaper_listener;
    MonitorState *m_capturing = nullptr;
    SP<Render::IFramebuffer> m_capture_wallpaper;
    int m_capture_depth = 0;
    int m_mutation_depth = 0;
    std::vector<PHLMONITORREF> m_suppressed;
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

namespace {
    CWorkspaceAnimation::Impl *active() {
        return CWorkspaceAnimation::Impl::s_active;
    }

    template <typename Fn>
    Fn original(CFunctionHook *hook) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland exposes original hook functions as void pointers.
        return reinterpret_cast<Fn>(hook != nullptr ? hook->m_original : nullptr);
    }
    enum class Coverage { Preserve, OpaqueTarget, ResetSource };

    bool copy_capture(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &target,
        Coverage coverage,
        bool downsample,
        const std::optional<CBox> &crop = std::nullopt) {
        if (!source || !target || source == target || !source->isAllocated() ||
            !target->isAllocated() || (!crop && !downsample && source->m_size != target->m_size)) {
            return false;
        }
        auto *src = dynamic_cast<Render::GL::CGLFramebuffer *>(source.get());
        auto *dst = dynamic_cast<Render::GL::CGLFramebuffer *>(target.get());
        if (src == nullptr || dst == nullptr) {
            return false;
        }
        const auto SOURCE_ID = src->getFBID();
        const auto TARGET_ID = dst->getFBID();
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::CRawGlBracket<0> STATE{{.framebuffer = TARGET_ID}};
        glBindFramebuffer(GL_READ_FRAMEBUFFER, SOURCE_ID);
        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
            glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            return false;
        }
        const auto LEFT = crop ? static_cast<GLint>(crop->x) : 0;
        const auto BOTTOM = crop ? static_cast<GLint>(crop->y) : 0;
        const auto WIDTH = static_cast<GLint>(target->m_size.x);
        const auto HEIGHT = static_cast<GLint>(target->m_size.y);
        const auto SOURCE_WIDTH = crop ? WIDTH : static_cast<GLint>(source->m_size.x);
        const auto SOURCE_HEIGHT = crop ? HEIGHT : static_cast<GLint>(source->m_size.y);
        glBlitFramebuffer(LEFT,
            BOTTOM,
            LEFT + SOURCE_WIDTH,
            BOTTOM + SOURCE_HEIGHT,
            0,
            0,
            WIDTH,
            HEIGHT,
            GL_COLOR_BUFFER_BIT,
            downsample ? GL_LINEAR : GL_NEAREST);
        if (coverage != Coverage::Preserve) {
            if (coverage == Coverage::ResetSource) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, SOURCE_ID);
            }
            const std::array<GLfloat, 4> CLEAR{
                0.F, 0.F, 0.F, coverage == Coverage::OpaqueTarget ? 1.F : 0.F};
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
            glClearBufferfv(GL_COLOR, 0, CLEAR.data());
        }
        return true;
    }

    bool copy_opaque(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &target,
        bool opaque = true,
        bool downsample = false) {
        return copy_capture(
            source, target, opaque ? Coverage::OpaqueTarget : Coverage::Preserve, downsample);
    }

    // Borrow protected native draw methods without downcasting the renderer.
    struct PreviewDrawAccess : Render::IHyprRenderer {
        static constexpr auto BACKGROUND = &PreviewDrawAccess::renderBackground;
        static constexpr auto LAYER = &PreviewDrawAccess::renderLayer;
    };

    SP<Render::IFramebuffer> preview_target(const PHLMONITOR &monitor, int longest_side) {
        g_pHyprRenderer->glBackend()->makeEGLCurrent();
        const auto SIZE = monitor->m_transformedSize;
        const double SCALE = std::min(1.0, longest_side / std::max(SIZE.x, SIZE.y));
        auto image = g_pHyprRenderer->createFB("hyprimate workspace preview");
        if (!image) {
            return {};
        }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::CRawGlBracket<1> STATE{{}};
        if (!image->alloc(std::max(1, static_cast<int>(std::lround(SIZE.x * SCALE))),
                std::max(1, static_cast<int>(std::lround(SIZE.y * SCALE))),
                capture_format(monitor))) {
            return {};
        }
        image->setImageDescription(monitor->workBufferImageDescription());
        return image;
    }

    template<class Allowed>
    bool write_thumbnail(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &thumbnail, const Allowed &allowed) {
        if (!allowed() || !copy_opaque(source, thumbnail, true, true) || !allowed()) {
            return false;
        }
        // Only plugin-owned thumbnails receive mip levels; never modify a native client texture.
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const hyprimate::gl::CRawGlBracket<1> STATE{{}};
        glBindTexture(GL_TEXTURE_2D, thumbnail->getTexture()->m_texID);
        glGenerateMipmap(GL_TEXTURE_2D);
        return allowed();
    }

    bool write_thumbnail(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &thumbnail) {
        return write_thumbnail(source, thumbnail, safe_scene);
    }

    // Hook trampolines below must match Hyprland's original signatures exactly (ABI), so their
    // smart-pointer and optional parameters stay by value.
    // NOLINTBEGIN(performance-unnecessary-value-param)
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
            if (!RESULT) {
                manager->m_capturing->capture_failed = true;
            }
            return RESULT;
        }
        return function(renderer, ctx, source, strength, damage, context);
    }

    void hook_layer(Render::IHyprRenderer *renderer, Render::CRenderContext &ctx,
        PHLLS layer, PHLMONITOR monitor, const Time::steady_tp &now,
        bool popups, bool lockscreen, bool workspace_scene) {
        const auto *manager = active();
        const auto ORIGINAL = original<LayerFn>(manager != nullptr ? manager->m_layer_hook : nullptr);
        if (ORIGINAL == nullptr) {
            return;
        }
        // Capture only the world. Native layer drawing bakes blur RGB into the
        // target before coverage alpha is recorded; subtracting wallpaper later
        // cannot make that old blur sample the animated planetary scene.
        if (manager != nullptr && manager->m_capture_depth > 0 && layer != nullptr &&
            !hyprimate::workspace::CaptureSession{manager->capture_features()}.include_layer(
                {.background = layer->m_layer == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
                    .popup = popups})) {
            return;
        }
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
        if (manager == nullptr || !manager->m_turns.enabled() || !workspace ||
            workspace == self->m_activeWorkspace || workspace == self->m_activeSpecialWorkspace) {
            function(self, workspace, internal, no_mouse_move, no_focus);
            return;
        }
        const auto MONITOR = self->m_self.lock();
        const auto PREVIOUS = workspace->type() == Workspace::eWorkspaceType::SPECIAL
            ? workspace->m_monitor.lock() : PHLMONITOR{};
        manager->mutate({MONITOR, PREVIOUS},
            [&] { function(self, workspace, internal, no_mouse_move, no_focus); });
    }

    void hook_special(Monitor::CMonitor *self, const PHLWORKSPACE &workspace, bool no_focus) {
        auto *manager = active();
        auto function = original<SpecialFn>(manager != nullptr ? manager->m_special_hook : nullptr);
        if (function == nullptr) {
            return;
        }
        if (manager == nullptr || !manager->m_turns.enabled() || workspace == self->m_activeSpecialWorkspace) {
            function(self, workspace, no_focus);
            return;
        }
        const auto MONITOR = self->m_self.lock();
        const auto PREVIOUS = workspace ? workspace->m_monitor.lock() : PHLMONITOR{};
        manager->mutate({MONITOR, PREVIOUS}, [&] { function(self, workspace, no_focus); });
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
        if (manager != nullptr && manager->m_turns.enabled() && !SUPPRESSED && !instant &&
            manager->m_mutation_depth == 0 && manager->m_capture_depth == 0 && workspace) {
            LOG(Log::DEBUG, "[hyprimate] workspace native-animation-unintercepted workspace={} type={}",
                workspace->addressableName(), static_cast<int>(type));
        }
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
        const auto owner_pin = manager ? manager->shared_from_this() : nullptr;
        if (function == nullptr) {
            return false;
        }
        if (manager != nullptr && manager->m_turns.enabled()) {
            manager->capture_gesture(monitor);
        }
        const auto gesture_revision = manager ? manager->m_gesture_revision : 0;
        const bool BEGUN = function(gesture, monitor);
        if (!BEGUN && manager != nullptr && manager->m_gesture_revision == gesture_revision) {
            manager->clear_gesture();
        }
        return BEGUN;
    }
    // NOLINTEND(performance-unnecessary-value-param)

    void hook_gesture_end(CUnifiedWorkspaceSwipeGesture *gesture) {
        auto *manager = active();
        auto function = original<GestureVoidFn>(manager != nullptr ? manager->m_gesture_end_hook : nullptr);
        // Detach the outgoing gesture before native code can begin another one.
        if (manager != nullptr) {
            manager->clear_gesture();
        }
        if (function != nullptr) {
            function(gesture);
        }
    }

    void hook_gesture_cancel(CUnifiedWorkspaceSwipeGesture *gesture) {
        auto *manager = active();
        auto function = original<GestureVoidFn>(manager != nullptr ? manager->m_gesture_cancel_hook : nullptr);
        if (manager != nullptr) {
            manager->clear_gesture();
        }
        if (function != nullptr) {
            function(gesture);
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
            *m_captured = copy_capture(ctx.m_data.currentFB,
                m_target,
                m_reset ? Coverage::ResetSource : Coverage::Preserve,
                false,
                m_crop);
            if (!*m_captured && m_failed != nullptr) {
                *m_failed = true;
            }
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

    void read_window_capture_details(hyprimate::workspace::CaptureWindowFacts &facts,
        const PHLWINDOW &window,
        const PHLMONITOR &monitor,
        bool decorate) {
        if (facts.popup_pass) {
            if (const auto HEAD = window->popupHead(); HEAD != nullptr) {
                HEAD->breadthfirst(
                    [&](const WP<Desktop::View::CPopup> &weak, void * /*unused*/) {
                        const auto POPUP = weak.lock();
                        facts.visible_popup =
                            facts.visible_popup || (POPUP != nullptr && POPUP->mapped());
                    },
                    nullptr);
            }
            if (decorate) {
                for (const auto &decoration : window->presentation().decorations()) {
                    facts.overlay = facts.overlay ||
                                    decoration->getDecorationLayer() == DECORATION_LAYER_OVERLAY;
                }
            }
        }
        const auto BOX = window->effects().transformedExtents(window->getFullWindowBoundingBox());
        facts.bounds = {.left = (BOX.x - monitor->m_position.x) * monitor->m_scale,
            .top = (BOX.y - monitor->m_position.y) * monitor->m_scale,
            .right = (BOX.x + BOX.width - monitor->m_position.x) * monitor->m_scale,
            .bottom = (BOX.y + BOX.height - monitor->m_position.y) * monitor->m_scale};
        facts.output_width = monitor->m_transformedSize.x;
        facts.output_height = monitor->m_transformedSize.y;
    }

    void prepare_window_capture_target(CWorkspaceAnimation::Impl &manager,
        CWorkspaceAnimation::Impl::MonitorState &entry,
        const PHLMONITOR &monitor,
        SP<Render::IFramebuffer> &target,
        const Vector2D &extent) {
        const auto PLAN = hyprimate::workspace::capture_target_plan({.present = target != nullptr,
            .matching_extent = target != nullptr && target->m_size == extent});
        if (PLAN.release) {
            entry.allocated_bytes -= framebuffer_bytes(target);
            target.reset();
        }
        if (PLAN.allocate) {
            target = manager.allocate_capture(entry, monitor, extent);
        }
    }

    // Hook trampoline: must match Hyprland's original signature exactly (ABI).
    // NOLINTBEGIN(performance-unnecessary-value-param)
    void hook_window(Render::IHyprRenderer *renderer, Render::CRenderContext &ctx,
        PHLWINDOW window, PHLMONITOR monitor, const Render::SWindowRenderPresentation &presentation,
        const Time::steady_tp &now, bool decorate, Render::eRenderPassMode pass,
        bool ignore_position, bool standalone) {
        auto *manager = active();
        const auto ORIGINAL =
            original<WindowFn>(manager != nullptr ? manager->m_window_hook : nullptr);
        if (ORIGINAL == nullptr) {
            return;
        }
        auto *entry = manager != nullptr ? manager->m_capturing : nullptr;
        auto *frame = entry != nullptr ? entry->capturing_frame : nullptr;
        hyprimate::workspace::CaptureWindowFacts facts;
        facts.present = window != nullptr;
        facts.mapped = window != nullptr && window->mapped();
        facts.hidden = window != nullptr && window->isHidden();
        facts.visible = presentation.alphaVisible;
        facts.popup_pass = pass == Render::RENDER_PASS_POPUP;
        if (frame == nullptr || entry->capture_failed ||
            !entry->capture_policy.needs_window_details(facts)) {
            ORIGINAL(renderer,
                ctx,
                window,
                monitor,
                presentation,
                now,
                decorate,
                pass,
                ignore_position,
                standalone);
            return;
        }
        read_window_capture_details(facts, window, monitor, decorate);
        SP<Render::IFramebuffer> accumulated;
        std::optional<CBox> crop;
        const auto BOUNDARY = [&](const SP<Render::IFramebuffer> &target, std::optional<CBox> region = std::nullopt) {
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CWorkspaceWallpaperPass>(
                target, entry->wallpaper_captured, &entry->capture_failed, true, region));
        };
        const auto PLAN = entry->capture_policy.window(facts);
        if (PLAN.crop.has_value()) {
            crop = CBox{PLAN.crop->x, PLAN.crop->y, PLAN.crop->width, PLAN.crop->height};
            const auto SIZE = monitor->m_transformedSize;
            if (frame->floating == nullptr) {
                frame->floating =
                    std::make_shared<std::vector<hyprimate::workspace::WorkspaceFloating>>();
            }
            if (frame->floating->size() <= entry->floating_used) {
                frame->floating->emplace_back();
            }
            auto &item = (*frame->floating)[entry->floating_used++];
            const Vector2D EXTENT{crop->width, crop->height};
            prepare_window_capture_target(*manager, *entry, monitor, item.layer.backdrop, EXTENT);
            prepare_window_capture_target(
                *manager, *entry, monitor, item.layer.accumulated, EXTENT);
            if (item.layer.accumulated == nullptr || item.layer.backdrop == nullptr) {
                entry->capture_failed = true;
            } else {
                accumulated = item.layer.accumulated;
                // Retain the framebuffer, not a vector element across native callbacks.
                item.identity = (manager->m_window_identities.get(window, PHLWINDOW{}) * 4) +
                                     static_cast<std::int64_t>(pass);
                item.rect = {crop->x / SIZE.x,
                    crop->y / SIZE.y,
                    crop->width / SIZE.x,
                    crop->height / SIZE.y};
                item.order = static_cast<double>(entry->floating_used - 1);
                BOUNDARY(item.layer.backdrop, crop);
            }
        }
        ORIGINAL(renderer, ctx, window, monitor, presentation, now,
            decorate, pass, ignore_position, standalone);
        if (accumulated != nullptr) {
            BOUNDARY(accumulated, crop);
        }
    }
    // NOLINTEND(performance-unnecessary-value-param)

    // Pass analysis has already determined native optimized-blur demand when
    // draw runs. Refresh its real compositor cache here, never before the scene
    // texture and never from the preceding frame's wallpaper/HUD.
    class CWorkspaceHudBlurPass final : public IPassElement {
      public:
        using Turn = hyprimate::workspace::WorkspaceTurn<CWorkspaceAnimation::Impl::TurnPort>;
        CWorkspaceHudBlurPass(const std::shared_ptr<Turn> &turn, PHLMONITOR monitor, bool invalidate)
            : m_turn(turn), m_monitor(std::move(monitor)), m_revision(turn->revision()),
              m_invalidate(invalidate) {}
        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            // Metadata restoration may still run after cancellation, but never after shutdown.
            if (!active() || ctx.m_data.pMonitor.lock() != m_monitor) { return {}; }
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
            const auto turn = m_turn.lock();
            auto *owner = active();
            if (!owner || !turn || owner->turn(m_monitor) != turn.get() ||
                turn->revision() != m_revision || !turn->active() || !safe_scene() ||
                !native_hud_safe(m_monitor) || ctx.m_data.pMonitor.lock() != m_monitor ||
                !Turn::queue_context({.monitor = true, .capture_depth = owner->m_capture_depth,
                    .capture = ctx.m_renderingCapture, .snapshot = ctx.m_renderingSnapshot,
                    .read_only = ctx.readOnlyEffects()})) { return {}; }
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
        std::weak_ptr<Turn> m_turn;
        PHLMONITOR m_monitor;
        std::uint64_t m_revision;
        bool m_invalidate;
    };

    class CWorkspacePresentationPass final : public IPassElement {
      public:
        CWorkspacePresentationPass(std::uint64_t ticket, PHLMONITOR monitor,
            hyprimate::workspace::WorkspaceFrame frame, bool matching_wallpaper)
            : m_ticket(ticket), m_monitor(std::move(monitor)), m_frame(std::move(frame)),
              m_matching_wallpaper(matching_wallpaper) {
        }

        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            const auto owner_pin = active() ? active()->shared_from_this() : nullptr;
            using Turn = hyprimate::workspace::WorkspaceTurn<CWorkspaceAnimation::Impl::TurnPort>;
            const auto CURRENT = [&]() -> Turn * {
                auto *owner = active();
                if (!owner || owner->m_style_draw_depth != 0 || !Turn::queue_context({.monitor = ctx.m_data.pMonitor.lock() == m_monitor,
                        .capture_depth = owner->m_capture_depth, .capture = ctx.m_renderingCapture,
                        .snapshot = ctx.m_renderingSnapshot, .read_only = ctx.readOnlyEffects()})) {
                    return nullptr;
                }
                return owner->turn(m_monitor);
            };
            std::string_view style_name;
            const auto RENDER = [&](double progress, Direction direction) {
                // A preview owner can move or disappear after prepare_inventory(), even
                // if the two endpoints of this turn and the global sharing rules still match.
                const bool has_previews = m_frame.neighbors && !m_frame.neighbors->empty();
                const auto previews_current = [&] {
                    if (!has_previews) {
                        return true;
                    }
                    const auto *current = CURRENT();
                    return current && current->previews_current(active()->m_inventory_revision,
                        CWorkspaceAnimation::Impl::InventoryPort::owns);
                };
                if (!previews_current()) {
                    return false;
                }
                auto *owner = active();
                if (!owner || owner != owner_pin.get()) { return false; }
                ++owner->m_style_draw_depth;
                bool drawn = false;
                {
                    const hyprimate::workspace::CallbackExit unwind{[&]() noexcept {
                        --owner->m_style_draw_depth;
                    }};
                    try {
                        owner->m_styles.for_each([&]<class Style>(auto &resources) {
                            if (Style::KIND == owner->m_style_kind && resources.ready) {
                                style_name = Style::NAME;
                                drawn = Style::draw(resources, ctx, m_frame, progress, direction, m_matching_wallpaper);
                            }
                        });
                    } catch (const std::exception &error) {
                        LOG(Log::ERR, "[hyprimate] workspace style draw failed monitor={} style={}: {}",
                            m_monitor->m_name, style_name, error.what());
                    } catch (...) {
                        LOG(Log::ERR, "[hyprimate] workspace style draw failed monitor={} style={}: unknown exception",
                            m_monitor->m_name, style_name);
                    }
                }
                if (owner->m_style_draw_depth == 0) {
                    if (std::exchange(owner->m_release_pending, false)) { owner->release_styles(); }
                    if (auto settings = std::exchange(owner->m_deferred_settings, std::nullopt)) {
                        if (active() == owner) { owner->configure(*settings); }
                    }
                }
                return drawn && previews_current();
            };
            const auto DISPLAY = [&] {
                CTexPassElement::SRenderData data;
                data.tex = m_frame.presentation->getTexture();
                data.box = {0, 0, m_monitor->m_transformedSize.x, m_monitor->m_transformedSize.y};
                g_pHyprRenderer->draw(ctx, data, ctx.m_data.damage);
            };
            const auto result = Turn::draw_queued(m_ticket, CURRENT, RENDER, DISPLAY);
            if (result == Turn::Presentation::FirstDisplay) {
                LOG(Log::DEBUG, "[hyprimate] workspace presented monitor={} source={} destination={} style={}",
                    m_monitor->m_name, m_frame.source.identity, m_frame.destination.identity, style_name);
                if (auto *owner = active(); owner && owner->state(m_monitor)) {
                    owner->state(m_monitor)->last = Clock::now();
                }
            } else if (result == Turn::Presentation::Cancelled) {
                // The native workspace wasn't queued for this frame. Don't leave pixels from
                // an earlier swapchain image underneath the HUD while waiting for native repaint.
                g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 1)}, ctx.m_data.damage);
                if (auto *owner = active(); owner && owner->turn(m_monitor) && !owner->turn(m_monitor)->active()) {
                    owner->erase_state(m_monitor);
                }
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
        std::uint64_t m_ticket;
        PHLMONITOR m_monitor;
        hyprimate::workspace::WorkspaceFrame m_frame;
        bool m_matching_wallpaper;
    };
}  // namespace

CWorkspaceAnimation::Impl *CWorkspaceAnimation::Impl::s_active = nullptr;

bool CWorkspaceAnimation::Impl::install(std::string &error) {
    shutdown();
    struct Spec {
        const char *name = nullptr;
        const char *signature = nullptr;
        const void *replacement = nullptr;
        CFunctionHook **slot = nullptr;
        bool optional = false;
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
            .replacement = reinterpret_cast<const void *>(&hook_window), .slot = &m_window_hook, .optional = true},
        Spec{.name = "renderLayer", .signature = LAYER_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): native hook trampoline.
            .replacement = reinterpret_cast<const void *>(&hook_layer), .slot = &m_layer_hook, .optional = true},
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
    s_active = this;
    for (const auto &spec : SPECS) {
        const auto *const ADDRESS =
            exact_symbol(HyprlandAPI::findFunctionsByName(m_handle, spec.name), spec.signature);
        if (ADDRESS != nullptr) {
            *spec.slot = HyprlandAPI::createFunctionHook(m_handle, ADDRESS, spec.replacement);
            if (*spec.slot != nullptr && (*spec.slot)->hook()) {
                continue;
            }
            if (*spec.slot != nullptr) {
                HyprlandAPI::removeFunctionHook(m_handle, *spec.slot);
                *spec.slot = nullptr;
            }
        }
        if (spec.optional) {
            LOG(Log::WARN, "[hyprimate] workspace capture capability unavailable: {}", spec.name);
            continue;
        }
        error = std::string("workspace hook unavailable: ") + spec.name;
        shutdown();
        return false;
    }
    m_wallpaper_listener = Event::bus()->m_events.render.stage.listen(
        [this](const Event::SRenderStageEvent &event) { on_render_stage(event); });
    const auto CHANGED = [this](const PHLWORKSPACEREF &) { ++m_inventory_revision; };
    auto &workspaces = Event::bus()->m_events.workspace;
    m_inventory_listeners = {workspaces.createdLate.listen(CHANGED), workspaces.removed.listen(CHANGED),
        workspaces.renamed.listen(CHANGED), workspaces.moveToMonitor.listen(
            [this](const PHLWORKSPACE &, const PHLMONITOR &) { ++m_inventory_revision; })};
    prime_outputs();
    return true;
}

void CWorkspaceAnimation::Impl::on_render_stage(const Event::SRenderStageEvent &event) {
    if (event.stage == RENDER_POST && event.monitor && event.monitor->m_enabled &&
        event.monitor->m_output && event.monitor->m_drmFormat != DRM_FORMAT_INVALID &&
        event.monitor->m_transformedSize.x > 0 && event.monitor->m_transformedSize.y > 0) {
        std::erase_if(m_ready_monitors, [](const auto &weak) { return weak.expired(); });
        if (!ready(event.monitor)) {
            m_ready_monitors.emplace_back(event.monitor);
        }
        if (m_capture_depth == 0 && m_style_warm_pending && m_turns.enabled()) {
            m_style_warm_pending = false;
            if (!initialize_selected_style()) {
                LOG(Log::DEBUG, "[hyprimate] workspace selected-style-prewarm failed style={}",
                    static_cast<std::uint64_t>(m_style_kind));
            }
        }
        if (const auto *active_turn = turn(event.monitor); active_turn == nullptr || !active_turn->active()) {
            restore_scanout(event.monitor);
        }
    }
    if (!event.context || m_capturing == nullptr || !m_capture_wallpaper || m_capture_depth == 0) {
        return;
    }
    auto &entry = *m_capturing;
    const auto *frame = entry.capturing_frame;
    const auto BOUNDARY = [&](const SP<Render::IFramebuffer> &target) {
        Render::IHyprRenderer::addPassElement(event.context->get(),
            makeUnique<CWorkspaceWallpaperPass>(target, entry.wallpaper_captured, &entry.capture_failed));
    };
    std::optional<hyprimate::workspace::CaptureBoundary> boundary;
    if (event.stage == RENDER_POST_WALLPAPER) {
        boundary = hyprimate::workspace::CaptureBoundary::Wallpaper;
    } else if (event.stage == RENDER_PRE_WINDOWS) {
        boundary = hyprimate::workspace::CaptureBoundary::BeforeWindows;
    } else if (event.stage == RENDER_POST_WINDOWS) {
        boundary = hyprimate::workspace::CaptureBoundary::AfterWindows;
    }
    if (!boundary.has_value()) {
        return;
    }
    const auto PLAN = entry.capture_policy.boundary(*boundary);
    for (std::size_t index = 0; index < PLAN.count; ++index) {
        switch (PLAN.planes[index]) {
            case hyprimate::workspace::CapturePlane::Wallpaper:
                BOUNDARY(m_capture_wallpaper);
                break;
            case hyprimate::workspace::CapturePlane::Bottom:
                BOUNDARY(frame->bottom.accumulated);
                break;
            case hyprimate::workspace::CapturePlane::Windows:
                BOUNDARY(frame->windows.accumulated);
                break;
            case hyprimate::workspace::CapturePlane::TopBackdrop:
                BOUNDARY(frame->top.backdrop);
                break;
        }
    }
}

void CWorkspaceAnimation::Impl::shutdown() {
    if (s_active == this) { s_active = nullptr; }
    ++m_lifecycle;
    m_turns.clear();
    m_deferred_settings.reset();
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
    clear();
    m_style_warm_pending = false;
    if (Render::GL::g_pHyprOpenGL) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    }
    if (m_style_draw_depth) {
        m_release_pending = true;
    } else {
        release_styles();
    }
    m_begin_render = nullptr;
}

void CWorkspaceAnimation::Impl::configure(const hyprimate::config::SWorkspaceSettings &requested) {
    const auto keep_alive = shared_from_this();
    const auto settings = requested;
    auto lifecycle = ++m_lifecycle;
    if (m_style_draw_depth) {
        m_turns.clear();
        m_deferred_settings = settings;
        return;
    }
    if (m_capture_depth) { m_turns.clear(); }
    if (!m_last_settings || *m_last_settings != settings) {
        m_settings_changed = true;
        m_last_settings = settings;
    }
    m_styles.for_each([&]<class Style>(auto &resources) {
        if (settings.style != Style::KIND) {
            return;
        }
        const auto PREVIOUS = resources.settings.last;
        Style::prepare(resources, settings);
        if (m_lifecycle != lifecycle) { return; }
        if (PREVIOUS != resources.settings.last || m_style_kind != Style::KIND || !settings.enabled) {
            clear();
            if (m_lifecycle != lifecycle + 1) { return; }
            lifecycle = m_lifecycle;
        }
        m_layered = Style::LAYERED;
        m_max_frame_gap = hyprimate::STALL_SECONDS;
        if constexpr (requires { Style::max_frame_gap(resources); }) {
            m_max_frame_gap = std::max(m_max_frame_gap, Style::max_frame_gap(resources));
        }
        m_individual_windows = Style::needs_individual_windows(resources);
        m_native_hud = Style::uses_native_hud(resources);
        m_uses_previews = settings.enabled && Style::uses_workspace_previews(resources);
        if (!m_uses_previews) {
            m_observed.clear();
        }
        const bool SUPPORTED = (!m_individual_windows || m_window_hook) && (!m_native_hud || m_layer_hook);
        if (settings.enabled && !SUPPORTED && m_settings_changed) {
            HyprlandAPI::addNotification(m_handle, "[hyprimate] Selected workspace options require an unavailable native capture hook",
                CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
        }
        if (m_lifecycle != lifecycle) { return; }
        configure_style<Style>(settings.enabled && SUPPORTED, resources);
        if (m_lifecycle != lifecycle) { return; }
        m_style_warm_pending = settings.enabled && SUPPORTED && !resources.ready;
    });
    if (m_lifecycle == lifecycle && settings.enabled) {
        prime_outputs();
    }
}

template<class Style>
void CWorkspaceAnimation::Impl::configure_style(bool enabled, Style::Resources &resources) {
    const auto lifecycle = m_lifecycle;
    if (m_style_kind != Style::KIND) {
        m_invalid_curve.reset();
    }
    m_style_kind = Style::KIND;
    m_uses_wallpaper = Style::USES_WALLPAPER;
    const auto &prepared = resources.settings.prepared;
    const auto RESOLVE = [&](std::string_view requested_curve) -> hyprimate::workspace::Transition::Curve {
        auto &manager = Animation::mgr();
        if (!manager) {
            return {};
        }
        const std::string NAME(requested_curve);
        if constexpr (requires { resources.curves.resolve(NAME, resources.settings.prepared.duration); }) {
            if (auto curve = resources.curves.resolve(NAME, resources.settings.prepared.duration)) {
                if (m_lifecycle != lifecycle) { return {}; }
                m_invalid_curve.reset();
                return curve;
            }
        }
        const bool KNOWN = manager->bezierExists(NAME);
        if (!KNOWN && (!m_invalid_curve || *m_invalid_curve != NAME)) {
            HyprlandAPI::addNotification(m_handle,
                "[hyprimate] Unknown workspace curve '" + NAME +
                    (m_turns.curve() ? "'; retaining the last valid curve." : "'; using the native default curve."),
                CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
            if (m_lifecycle != lifecycle) { return {}; }
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
    m_turns.configure(enabled, static_cast<std::uint64_t>(Style::KIND), prepared.duration, prepared.curve,
        m_settings_changed, RESOLVE);
    if (m_lifecycle != lifecycle) { return; }
    m_settings_changed = false;
}

const std::shared_ptr<CWorkspaceAnimation::Impl::MonitorState> *CWorkspaceAnimation::Impl::state_slot(
    const PHLMONITOR &monitor) const {
    const auto FOUND = std::ranges::find_if(
        m_states, [&](const auto &entry) { return entry->monitor.lock() == monitor; });
    return FOUND == m_states.end() ? nullptr : &*FOUND;
}

std::shared_ptr<CWorkspaceAnimation::Impl::MonitorState> CWorkspaceAnimation::Impl::ensure_state(
    const PHLMONITOR &monitor) {
    if (const auto *existing = state_slot(monitor)) {
        return *existing;
    }
    auto created = std::make_shared<MonitorState>(this);
    created->monitor = monitor;
    return m_states.emplace_back(std::move(created));
}

SP<Render::IFramebuffer> CWorkspaceAnimation::Impl::allocate_capture(
    MonitorState &entry, const PHLMONITOR &monitor, std::optional<Vector2D> size) {
    if (!g_pHyprRenderer || !ready(monitor)) {
        return nullptr;
    }
    const auto SIZE = size.value_or(monitor->m_transformedSize);
    const auto FORMAT = capture_format(monitor);
    const auto BYTES = static_cast<std::uint64_t>(SIZE.x) * static_cast<std::uint64_t>(SIZE.y) *
        (FORMAT == DRM_FORMAT_ABGR8888 ? 4 : 8);
    std::uint64_t total = 0;
    for (const auto &state : m_states) {
        total += state->allocated_bytes;
    }
    if (!hyprimate::workspace::capture_allocation_allowed({.bytes = BYTES,
            .monitor_bytes = entry.allocated_bytes,
            .total_bytes = total,
            .wide_color = FORMAT != DRM_FORMAT_ABGR8888})) {
        LOG(Log::DEBUG, "[hyprimate] workspace fallback reason=capture-budget monitor={} requested={} monitor_bytes={} total_bytes={}",
            monitor->m_name, BYTES, entry.allocated_bytes, total);
        return nullptr;
    }
    auto framebuffer = g_pHyprRenderer->createFB("hyprimate workspace capture");
    if (!framebuffer || !framebuffer->alloc(static_cast<int>(SIZE.x), static_cast<int>(SIZE.y), FORMAT)) {
        entry.port.diagnostic("capture-allocation");
        return nullptr;
    }
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

hyprimate::workspace::CaptureBufferFacts CWorkspaceAnimation::Impl::buffer_facts(
    const MonitorState &entry, const PHLMONITOR &monitor) {
    hyprimate::workspace::CaptureBufferFacts facts;
    facts.native_ready = monitor->m_activeWorkspace != nullptr &&
                         g_pHyprRenderer->type() == Render::IHyprRenderer::RT_GL &&
                         g_pHyprRenderer->glBackend() != nullptr;
    facts.compatible =
        entry.source == nullptr ||
        (entry.logical_size == monitor->m_size && entry.scale == monitor->m_scale &&
            entry.transform == monitor->m_transform &&
            entry.output_format == monitor->m_drmFormat &&
            entry.source->m_drmFormat == capture_format(monitor) &&
            entry.source->imageDescription() == monitor->workBufferImageDescription());
    facts.scene_safe = TurnPort::scene_safe();
    facts.hud_safe = entry.port.hud_safe();
    const auto *const ACTIVE_TURN = turn(monitor);
    facts.active = ACTIVE_TURN != nullptr && ACTIVE_TURN->active();
    const auto USABLE = [&](const SP<Render::IFramebuffer> &framebuffer) {
        return framebuffer != nullptr && framebuffer->isAllocated() &&
               framebuffer->m_size == monitor->m_transformedSize &&
               framebuffer->getTexture() != nullptr;
    };
    facts.base = {USABLE(entry.source),
        USABLE(entry.destination),
        USABLE(entry.presentation),
        USABLE(entry.source_wallpaper),
        USABLE(entry.destination_wallpaper),
        USABLE(entry.opaque_backdrop)};
    facts.planes = {USABLE(entry.source_frame.bottom.accumulated),
        USABLE(entry.source_frame.windows.accumulated),
        USABLE(entry.source_frame.top.backdrop),
        USABLE(entry.destination_frame.bottom.accumulated),
        USABLE(entry.destination_frame.windows.accumulated),
        USABLE(entry.destination_frame.top.backdrop)};
    return facts;
}

bool CWorkspaceAnimation::Impl::ensure_buffers(MonitorState &entry, const PHLMONITOR &monitor) {
    // resources() is lazy and fatal on an unconfigured output. A completed native
    // POST frame, not resources(), is the readiness proof during connect/disconnect.
    if (g_pHyprRenderer == nullptr || !ready(monitor)) {
        return false;
    }
    const auto FACTS = buffer_facts(entry, monitor);
    const auto PLAN = hyprimate::workspace::capture_buffer_plan(FACTS, capture_features());
    if (PLAN == hyprimate::workspace::CaptureBufferAction::Reject) {
        LOG(Log::DEBUG, "[hyprimate] workspace fallback reason=buffer-policy monitor={} native_ready={} compatible={} scene_safe={} hud_safe={} active={}",
            monitor->m_name, FACTS.native_ready, FACTS.compatible, FACTS.scene_safe, FACTS.hud_safe, FACTS.active);
        return false;
    }
    g_pHyprRenderer->glBackend()->makeEGLCurrent();
    if (PLAN == hyprimate::workspace::CaptureBufferAction::Allocate) {
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
        const auto LAYOUT = hyprimate::workspace::capture_plane_layout(capture_features());
        const auto PREPARE = [&](hyprimate::workspace::WorkspaceCapture &frame,
            const SP<Render::IFramebuffer> &composite, const SP<Render::IFramebuffer> &wallpaper) {
            frame.composite = composite;
            frame.wallpaper = wallpaper;
            frame.layered = LAYOUT.layered;
            if (LAYOUT.layered) {
                frame.bottom = {.accumulated = ALLOCATE(), .backdrop = wallpaper};
                frame.windows = {
                    .accumulated = LAYOUT.separate_windows ? ALLOCATE() : frame.bottom.accumulated,
                    .backdrop = frame.bottom.accumulated};
                frame.top = {.accumulated = composite,
                    .backdrop =
                        LAYOUT.separate_top_backdrop ? ALLOCATE() : frame.windows.accumulated};
            }
        };
        PREPARE(entry.source_frame, entry.source, entry.source_wallpaper);
        PREPARE(entry.destination_frame, entry.destination, entry.destination_wallpaper);
    }
    if (hyprimate::workspace::capture_buffer_plan(buffer_facts(entry, monitor), capture_features()) !=
        hyprimate::workspace::CaptureBufferAction::Reuse) {
        return false;
    }
    if (!entry.source_resources) {
        const auto BLUR = g_pHyprRenderer->createFB("hyprimate workspace source blur");
        entry.source_resources = makeShared<Render::CSceneResources>(BLUR,
            Render::SSceneBufferDescription{.format = entry.source->m_drmFormat,
                .imageDescription = entry.source->imageDescription()});
    }
    const bool READY = initialize_selected_style();
    if (!READY) {
        entry.port.diagnostic("style-initialize");
    }
    return READY;
}

bool CWorkspaceAnimation::Impl::initialize_selected_style() {
    if (!g_pHyprRenderer || g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL) {
        return false;
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

void CWorkspaceAnimation::Impl::scan_wallpaper_surface(
    const SP<CWLSurfaceResource> &child, const Vector2D &offset, void *opaque) {
    const auto &args = *static_cast<IdentityScan *>(opaque);
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
        tracked.alpha != args.alpha || tracked.plane != args.plane)) {
        ++tracked.generation;
    }
    tracked.geometry = GEOMETRY;
    tracked.alpha = args.alpha;
    tracked.plane = args.plane;
    args.identity->push_back({.texture = child->m_current.texture, .generation = tracked.generation});
}

void CWorkspaceAnimation::Impl::capture_wallpaper_identity(MonitorState &entry,
    const PHLMONITOR &monitor, std::vector<WallpaperIdentity> &identity, bool shell) const {
    identity.clear();
    std::erase_if(entry.wallpaper_resources, [](const auto &item) { return item->surface.expired(); });
    IdentityScan scan{.state = &entry, .identity = &identity, .position = {}};
    if (!m_uses_wallpaper) {
        return;
    }
    for (std::size_t layer = 0; layer < monitor->m_layerSurfaceLayers.size(); ++layer) {
        if (layer != ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND && (!shell || m_native_hud)) {
            continue;
        }
        for (const auto &weak : monitor->m_layerSurfaceLayers[layer]) {
            const auto LAYER = weak.lock();
            const auto SURFACE = LAYER && LAYER->mapped() ? LAYER->resource() : nullptr;
            if (!SURFACE) {
                continue;
            }
            scan.position = LAYER->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
            scan.alpha = LAYER->alpha()[Desktop::View::LS_ALPHA_FADE]->value();
            scan.plane = layer;
            SURFACE->breadthfirst(scan_wallpaper_surface, &scan);
        }
    }
}

void CWorkspaceAnimation::Impl::release_floating_tail(MonitorState &entry, hyprimate::workspace::WorkspaceCapture &frame) {
    if (!frame.floating || frame.floating->size() <= entry.floating_used) {
        return;
    }
    const auto RELEASE = [&](const SP<Render::IFramebuffer> &target) {
        if (target) {
            entry.allocated_bytes -= framebuffer_bytes(target);
        }
    };
    for (std::size_t index = entry.floating_used; index < frame.floating->size(); ++index) {
        RELEASE((*frame.floating)[index].layer.accumulated);
        RELEASE((*frame.floating)[index].layer.backdrop);
    }
    frame.floating->resize(entry.floating_used);
}

bool CWorkspaceAnimation::Impl::capture(MonitorState &entry,
    const PHLMONITOR &monitor,
    const PHLWORKSPACE &workspace,
    hyprimate::workspace::CaptureKind kind,
    const SP<Render::IFramebuffer> &target, bool inactive) {
    if (!safe_scene() || !workspace || !ensure_buffers(entry, monitor)) {
        return false;
    }
    entry.capture_policy = hyprimate::workspace::CaptureSession{capture_features()};
    for (const auto &fadeout : Desktop::fadingOutState()->fadeouts()) {
        if (fadeout->monitor().lock() != monitor || fadeout->done() || fadeout->alpha() <= 0.F) {
            continue;
        }
        const auto SOURCE = fadeout->source();
        if (SOURCE.type != Desktop::eFadeoutSource::WINDOW) {
            continue;
        }
        const bool SELECTED = inactive
            ? Render::sceneSelectsFadeout(Render::eSceneMode::WORKSPACE_WITH_SHELL, SOURCE.type,
                SOURCE.workspace.lock() == workspace, SOURCE.pinned)
            : (!fadeout->workspace().lock() || fadeout->workspace().lock() == workspace);
        // Native window fadeouts already contain output-global dim/blur and may
        // span several per-window planes. Only this affected individual scene
        // abstains; composite and aggregate layered scenes keep native ordering.
        if (!entry.capture_policy.accepts_fadeout(SELECTED)) {
            entry.port.diagnostic("individual-fadeout");
            return false;
        }
    }
    const auto RESOURCES = kind == hyprimate::workspace::CaptureKind::SourceIsolated
                               ? entry.source_resources
                               : monitor->resources()->sceneResources();
    struct DrawData {
        Impl *owner;
        PHLMONITOR monitor;
        PHLWORKSPACE workspace;
        bool inactive;
        std::uint64_t lifecycle;
        std::uint64_t gesture_revision;
        hyprimate::workspace::WorkspaceTurn<TurnPort> *turn;
        std::uint64_t revision;
        std::int64_t composition;
        [[nodiscard]] bool allowed() const {
            auto *current = owner->turn(monitor);
            return active() == owner && owner->m_lifecycle == lifecycle && current == turn &&
                owner->m_gesture_revision == gesture_revision &&
                current && current->revision() == revision && safe_scene() &&
                (!owner->m_native_hud || native_hud_safe(monitor)) &&
                owner->composition_id(monitor) == composition;
        }
    } data{.owner = this, .monitor = monitor, .workspace = workspace, .inactive = inactive,
        .lifecycle = m_lifecycle, .gesture_revision = m_gesture_revision, .turn = turn(monitor),
        .revision = turn(monitor) ? turn(monitor)->revision() : 0,
        .composition = composition_id(monitor)};
    const auto DRAW = [](void *opaque, Render::CRenderContext &ctx) {
        const auto &args = *static_cast<DrawData *>(opaque);
        const auto ORIGINAL = args.owner->render_original();
        if (!args.allowed() || ORIGINAL == nullptr || !g_pHyprRenderer) {
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
    const hyprimate::workspace::ScopedRestore capturing_scope{m_capturing};
    const hyprimate::workspace::ScopedRestore wallpaper_scope{m_capture_wallpaper};
    const hyprimate::workspace::ScopedRestore frame_scope{entry.capturing_frame};
    entry.capture_failed = false;
    entry.floating_used = 0;
    entry.capturing_frame = &frame;
    entry.wallpaper_captured = false;
    m_capturing = m_uses_wallpaper ? &entry : nullptr;
    m_capture_wallpaper.reset();
    if (m_uses_wallpaper) {
        m_capture_wallpaper = target == entry.source ? entry.source_wallpaper : entry.destination_wallpaper;
    }
    const ScopedCapture CAPTURE_SCOPE{m_capture_depth};
    const bool CAPTURED = hyprimate::workspace::capture_workspace(
        *g_pHyprRenderer, m_begin_render, monitor, target, RESOURCES, kind, DRAW, &data,
        [](void *opaque) { return static_cast<DrawData *>(opaque)->allowed(); });
    // Capture markers are restored by scope guards, also on exceptions.
    release_floating_tail(entry, frame);
    const bool COMPLETE = entry.capture_policy.complete({.native_captured = CAPTURED && !entry.capture_failed,
        .wallpaper_captured = entry.wallpaper_captured});
    if (!COMPLETE) {
        LOG(Log::DEBUG, "[hyprimate] workspace fallback reason=capture-incomplete monitor={} kind={} native={} pass_failed={} wallpaper={}",
            monitor->m_name, static_cast<int>(kind), CAPTURED, entry.capture_failed, entry.wallpaper_captured);
    }
    return COMPLETE;
}

void CWorkspaceAnimation::Impl::release_blend(MonitorState &entry, MonitorState::CaptureBlend &blend) {
    if (blend.stale) {
        const auto &old = *blend.stale;
        const auto RELEASE = [&](const SP<Render::IFramebuffer> &target) {
            if (target) {
                entry.allocated_bytes -= framebuffer_bytes(target);
            }
        };
        RELEASE(old.composite);
        RELEASE(old.wallpaper);
        RELEASE(old.bottom.accumulated);
        if (old.windows.accumulated != old.bottom.accumulated) {
            RELEASE(old.windows.accumulated);
        }
        if (old.top.backdrop != old.windows.accumulated) {
            RELEASE(old.top.backdrop);
        }
        if (old.floating) {
            for (const auto &window : *old.floating) {
                RELEASE(window.layer.accumulated);
                RELEASE(window.layer.backdrop);
            }
        }
    }
    blend.presentation.reset();
    blend.stale.reset();
}

bool CWorkspaceAnimation::Impl::begin_refresh(MonitorState &entry, const PHLMONITOR &monitor, TurnKind kind) {
    auto &blend = kind == TurnKind::SourceIsolated ? entry.source_blend : entry.destination_blend;
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
            !CLONE(live.bottom.accumulated, old.bottom.accumulated)) {
            return false;
        }
        old.bottom.backdrop = old.wallpaper;
        old.windows.backdrop = old.bottom.accumulated;
        old.top.accumulated = old.composite;
        if (live.windows.accumulated == live.bottom.accumulated) {
            old.windows.accumulated = old.bottom.accumulated;
        } else if (!CLONE(live.windows.accumulated, old.windows.accumulated)) {
            return false;
        }
        if (live.top.backdrop == live.windows.accumulated) {
            old.top.backdrop = old.windows.accumulated;
        } else if (!CLONE(live.top.backdrop, old.top.backdrop)) {
            return false;
        }
        if (live.floating) {
            old.floating = std::make_shared<std::vector<hyprimate::workspace::WorkspaceFloating>>();
            old.floating->reserve(live.floating->size());
            for (const auto &window : *live.floating) {
                old.floating->emplace_back();
                auto &item = old.floating->back();
                item.identity = window.identity;
                item.rect = window.rect;
                item.order = window.order;
                if (!CLONE(window.layer.accumulated, item.layer.accumulated) ||
                    !CLONE(window.layer.backdrop, item.layer.backdrop)) {
                    return false;
                }
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool CWorkspaceAnimation::Impl::prepare_frame(
    MonitorState &entry, TurnKind kind, hyprimate::workspace::WorkspaceCapture &frame, double mix) {
    auto &blend = kind == TurnKind::SourceIsolated ? entry.source_blend : entry.destination_blend;
    if (!blend.stale) {
        return true;
    }
    frame.previous = blend.stale;  // One bounded immutable generation.
    frame.live_mix = mix;
    struct PresentationPort {
        using Floating = hyprimate::workspace::WorkspaceFloating;
        using Facts = hyprimate::workspace::FloatingFacts;
        MonitorState::CaptureBlend *blend;
        hyprimate::workspace::WorkspaceCapture *frame;
        const std::vector<Floating> *live;
        [[nodiscard]] std::size_t old_count() const {
            return blend->stale->floating ? blend->stale->floating->size() : 0;
        }
        [[nodiscard]] std::size_t live_count() const {
            return live != nullptr ? live->size() : 0;
        }
        [[nodiscard]] static Facts facts(const Floating &window) {
            return {.identity = window.identity,
                .rect = {window.rect.x, window.rect.y, window.rect.width, window.rect.height},
                .opacity = window.opacity,
                .order = window.order};
        }
        [[nodiscard]] Facts old_window(std::size_t index) const {
            return facts((*blend->stale->floating)[index]);
        }
        [[nodiscard]] Facts live_window(std::size_t index) const {
            return facts((*live)[index]);
        }
        void begin(std::size_t count) const {
            if (count == 0) {
                return;
            }
            if (!blend->presentation) {
                blend->presentation = std::make_shared<std::vector<Floating>>();
            }
            blend->presentation->clear();
            blend->presentation->reserve(count);
            frame->floating = blend->presentation;
        }
        void emit(const hyprimate::workspace::FloatingPresentation &command) const {
            blend->presentation->push_back(
                command.stale ? (*blend->stale->floating)[command.index] : (*live)[command.index]);
            auto &item = blend->presentation->back();
            item.opacity = command.facts.opacity;
            const auto &rect = command.facts.rect;
            item.rect = {rect[0], rect[1], rect[2], rect[3]};
            item.order = command.facts.order;
            if (command.previous.has_value()) {
                item.previous_layer = (*blend->stale->floating)[*command.previous].layer;
                item.live_mix = command.live_mix;
            }
        }
    } port{.blend = &blend, .frame = &frame, .live = frame.floating.get()};
    auto *active_turn = turn(entry.monitor.lock());
    return active_turn != nullptr && active_turn->present_floating(kind, port, mix);
}

bool CWorkspaceAnimation::Impl::TurnPort::scene_safe() {
    return hyprimate::workspace::capture_scene_safe(scene_facts());
}
bool CWorkspaceAnimation::Impl::TurnPort::hud_safe() const {
    return native_hud_safe(entry->monitor.lock());
}
bool CWorkspaceAnimation::Impl::TurnPort::buffers(bool /*active*/) const {
    return owner->ensure_buffers(*entry, entry->monitor.lock());
}
bool CWorkspaceAnimation::Impl::TurnPort::capture(
    TurnKind kind, const hyprimate::workspace::TurnComposition<Workspace> &composition) const {
    const auto MONITOR = entry->monitor.lock();
    const auto TARGET = kind == TurnKind::SourceIsolated ? entry->source : entry->destination;
    auto &frame = kind == TurnKind::SourceIsolated ? entry->source_frame : entry->destination_frame;
    frame.identity = composition.identity;
    frame.endpoint = composition.endpoint();
    return owner->capture(*entry, MONITOR, composition.workspace.lock(), kind, TARGET);
}
double CWorkspaceAnimation::Impl::TurnPort::duration(
    const hyprimate::workspace::JourneyEndpoint &from,
    const hyprimate::workspace::JourneyEndpoint &destination, double base) const {
    double duration = base;
    owner->m_styles.for_each([&]<class Style>(auto &resources) {
        if (Style::KIND == owner->m_style_kind) {
            duration = Style::duration(resources, from, destination);
        }
    });
    return duration;
}

double CWorkspaceAnimation::Impl::TurnPort::live_boundary() const {
    double boundary = 1.0;
    owner->m_styles.for_each([&]<class Style>(auto &resources) {
        if (Style::KIND == owner->m_style_kind) {
            boundary = Style::live_boundary(resources);
        }
    });
    return boundary;
}

hyprimate::workspace::InactiveSourceFacts
CWorkspaceAnimation::Impl::TurnPort::inactive_source_facts(
    const hyprimate::workspace::TurnComposition<Workspace> &composition) const {
    const auto MONITOR = entry->monitor.lock();
    hyprimate::workspace::InactiveSourceFacts facts{.monitor = MONITOR != nullptr,
        .source = !composition.workspace.expired(),
        .active_special = MONITOR && MONITOR->m_activeSpecialWorkspace != nullptr};
    for (const auto &layer : Desktop::layerState()->layers()) {
        if (layer->m_monitor.lock() != MONITOR) {
            continue;
        }
        if (const auto HEAD = layer->popupHead(); HEAD) {
            HEAD->breadthfirst(
                [&](const WP<Desktop::View::CPopup> &weak, void *) {
                    const auto ITEM = weak.lock();
                    facts.popup = facts.popup || (ITEM && ITEM->mapped());
                },
                nullptr);
        }
    }
    if (MONITOR) {
        auto &shell = entry->shell_probe_identity;
        owner->capture_wallpaper_identity(*entry, MONITOR, shell, true);
        facts.shell_matches = hyprimate::workspace::wallpaper_matches<SP<Render::ITexture>>(
            shell, entry->source_shell_identity);
    }
    return facts;
}

bool CWorkspaceAnimation::Impl::TurnPort::capture_inactive_source(
    const hyprimate::workspace::TurnComposition<Workspace> &composition) const {
    return owner->capture(*entry,
        entry->monitor.lock(),
        composition.workspace.lock(),
        TurnKind::SourceIsolated,
        entry->source,
        true);
}

bool CWorkspaceAnimation::Impl::TurnPort::clone_capture(TurnKind kind) const {
    return owner->begin_refresh(*entry, entry->monitor.lock(), kind);
}

void CWorkspaceAnimation::Impl::TurnPort::release_blend(TurnKind kind) const {
    Impl::release_blend(
        *entry, kind == TurnKind::SourceIsolated ? entry->source_blend : entry->destination_blend);
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

void CWorkspaceAnimation::Impl::TurnPort::landed() const {
    const auto MONITOR = entry->monitor.lock();
    LOG(Log::DEBUG, "[hyprimate] workspace landed monitor={}",
        MONITOR ? MONITOR->m_name : "<removed>");
}

void CWorkspaceAnimation::Impl::TurnPort::diagnostic(std::string_view reason) const {
    const auto MONITOR = entry->monitor.lock();
    LOG(Log::DEBUG, "[hyprimate] workspace fallback reason={} monitor={} style={}",
        reason, MONITOR ? MONITOR->m_name : "<removed>", static_cast<std::uint64_t>(owner->m_style_kind));
    if (reason.contains("scene") || reason == "stage-buffers") {
        const auto FACTS = scene_facts();
        LOG(Log::DEBUG, "[hyprimate] workspace safety session={} locked={} input={} exclusive={} ime={} non_window_focus={} protected={} invalid_fadeout={}",
            FACTS.session_available, FACTS.locked, FACTS.input_available, FACTS.exclusive_input,
            FACTS.input_method, FACTS.non_window_focus, FACTS.protected_content, FACTS.invalid_fadeout);
    }
}

std::vector<CWorkspaceAnimation::Impl::Snapshot> CWorkspaceAnimation::Impl::snapshot(
    std::initializer_list<PHLMONITOR> monitors) {
    const auto keep_alive = shared_from_this();
    const auto lifecycle = m_lifecycle;
    std::vector<Snapshot> result;
    result.reserve(monitors.size());
    for (const auto &monitor : monitors) {
        if (std::ranges::any_of(result, [&](const auto &item) { return item.monitor == monitor; })) {
            continue;
        }
        if (!ready(monitor) || !monitor->m_activeWorkspace) {
            if (monitor) {
                LOG(Log::DEBUG, "[hyprimate] workspace fallback reason=output-not-ready monitor={}", monitor->m_name);
            }
            continue;
        }
        const auto entry = ensure_state(monitor);
        const auto staged = m_turns.get(monitor_key(monitor), entry->port);
        Snapshot item{.monitor = monitor, .composition = composition(monitor, true), .ready = false,
            .state = entry, .turn = staged, .revision = staged->revision() + 1,
            .lifecycle = lifecycle};
        item.ready = staged->stage(item.composition);
        if (m_lifecycle != lifecycle || turn(monitor) != staged.get() ||
            staged->revision() != item.revision) {
            continue;
        }
        item.ready = item.ready && composition_id(monitor) == item.composition.identity;
        if (!item.ready) {
            erase_state(monitor);
        } else if (m_uses_previews && !staged->active() && safe_scene()) {
            remember_wallpaper(monitor, monitor->m_activeSpecialWorkspace ? monitor->m_activeSpecialWorkspace :
                monitor->m_activeWorkspace, entry->source_wallpaper);
        }
        result.push_back(std::move(item));
    }
    return result;
}

void CWorkspaceAnimation::Impl::finish(std::vector<Snapshot> &snapshots) {
    const auto keep_alive = shared_from_this();
    for (const auto &item : snapshots) {
        if (!item.ready || !item.monitor) {
            continue;
        }
        const auto entry = item.state;
        const auto active_turn = item.turn;
        const auto current = [&] {
            return item.lifecycle == m_lifecycle && turn(item.monitor) == active_turn.get() &&
                active_turn && active_turn->revision() == item.revision;
        };
        if (!entry || !current()) {
            continue;
        }
        const auto AFTER = composition(item.monitor, true);
        if (item.composition.identity != AFTER.identity) {
            // Internal switches and placement swaps bypass startAnimation entirely.
            // A previously hidden incoming workspace can still have zero alpha or
            // a native slide offset. Establish its native endpoint before capture.
            if (auto animate = ::original<AnimateFn>(m_animate_hook)) {
                const auto NORMALIZE = [&](const PHLWORKSPACE &workspace) {
                    if (!workspace ||
                        (workspace->m_alpha->value() == 1.F && workspace->m_alpha->goal() == 1.F &&
                            workspace->m_renderOffset->value() == Vector2D{} &&
                            workspace->m_renderOffset->goal() == Vector2D{})) {
                        return;
                    }
                    animate(workspace, Animation::Workspace::ANIMATION_TYPE_IN, true, true, std::nullopt);
                };
                NORMALIZE(item.monitor->m_activeWorkspace);
                if (!current() || composition_id(item.monitor) != AFTER.identity) { continue; }
                NORMALIZE(item.monitor->m_activeSpecialWorkspace);
                if (!current() || composition_id(item.monitor) != AFTER.identity) { continue; }
            }
        }
        active_turn->begin(item.composition, AFTER, m_turns.duration(), m_turns.curve());
        if (turn(item.monitor) != active_turn.get() || item.lifecycle != m_lifecycle ||
            active_turn->revision() != item.revision + 1) { continue; }
        if (item.composition.identity != AFTER.identity) {
            LOG(Log::DEBUG, "[hyprimate] workspace request monitor={} before={} after={} accepted={} presented={}",
                item.monitor->m_name, item.composition.identity, AFTER.identity,
                active_turn->active(), active_turn->presented_once());
        }
        if (!active_turn->active()) {
            erase_state(item.monitor);
        }
    }
}

void CWorkspaceAnimation::Impl::mutate(
    const std::vector<PHLMONITOR> &monitors, const std::function<void()> &original) {
    const auto keep_alive = shared_from_this();
    if (!hyprimate::workspace::WorkspaceTurn<TurnPort>::intercept_mutation(
            {.enabled = m_turns.enabled(),
                .capture_depth = m_capture_depth,
                .mutation_depth = m_mutation_depth})) {
        original();
        return;
    }
    std::vector<Snapshot> before;
    before.reserve(monitors.size());
    for (const auto &monitor : monitors) {
        if (std::ranges::any_of(before, [&](const auto &item) { return item.monitor == monitor; })) {
            continue;
        }
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
            owner->m_suppressed.clear();
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
    const auto keep_alive = shared_from_this();
    const auto gesture_revision = ++m_gesture_revision;
    if (m_capture_depth != 0 || !m_turns.enabled() || !monitor) {
        return;
    }
    auto captured = snapshot({monitor});
    if (gesture_revision == m_gesture_revision && !captured.empty() && captured.front().ready) {
        m_gesture_monitor = monitor;
    }
}

void CWorkspaceAnimation::Impl::clear_gesture() {
    ++m_gesture_revision;
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

bool CWorkspaceAnimation::Impl::needs_native_prime(const PHLMONITOR &monitor, bool enabled) const {
    return hyprimate::workspace::WorkspaceTurn<TurnPort>::prime_native({
        .enabled = enabled,
        .output_configured = monitor && monitor->m_enabled && monitor->m_output &&
            monitor->m_drmFormat != DRM_FORMAT_INVALID &&
            monitor->m_transformedSize.x > 0 && monitor->m_transformedSize.y > 0,
        .presented = ready(monitor)});
}

void CWorkspaceAnimation::Impl::prime_outputs() const {
    bool warm = m_style_warm_pending;
    for (const auto &monitor : State::monitorState()->monitors()) {
        // Shader preparation needs a native POST even on an already primed
        // output. One output suffices for the selected renderer.
        if (needs_native_prime(monitor, true) || (warm && ready(monitor))) {
            damage(monitor);
            warm = false;
        }
    }
}

void CWorkspaceAnimation::Impl::inhibit_scanout(const PHLMONITOR &monitor) {
    if (!monitor || monitor->m_solitaryClient.expired()) {
        return;
    }
    if (std::ranges::none_of(m_scanout_inhibited,
            [&](const auto &weak) { return weak.lock() == monitor; })) {
        m_scanout_inhibited.emplace_back(monitor);
    }
    monitor->m_solitaryClient.reset();
}

void CWorkspaceAnimation::Impl::restore_scanout(const PHLMONITOR &monitor) {
    auto inhibited = std::exchange(m_scanout_inhibited, {});
    for (const auto &weak : inhibited) {
        const auto candidate = weak.lock();
        if (!candidate) { continue; }
        if (monitor && candidate != monitor) {
            m_scanout_inhibited.push_back(weak);
        } else {
            candidate->recheckSolitary();
        }
    }
}

void CWorkspaceAnimation::Impl::pre_checks(const PHLMONITOR &monitor) {
    if (needs_native_prime(monitor, m_turns.enabled()) ||
        (m_style_warm_pending && m_turns.enabled() && ready(monitor))) {
        // A scanout frame returns before RENDER_POST. Force one ordinary native
        // frame, not a capture, to establish safe render-resource readiness.
        inhibit_scanout(monitor);
    }
    if (const auto *active_turn = turn(monitor); active_turn != nullptr && active_turn->active()) {
        inhibit_scanout(monitor);
    }
}

void CWorkspaceAnimation::Impl::pre_render(const PHLMONITOR &monitor) {
    const auto keep_alive = shared_from_this();
    if (!monitor || m_capture_depth != 0) {
        return;
    }
    const auto *slot = state_slot(monitor);
    if (!slot) {
        return;
    }
    // Native capture can synchronously remove the monitor or replace its turn. Keep
    // their storage alive, but do not grant the old frame authority over a replacement.
    const auto entry = *slot;
    const auto active_turn = m_turns.pin(monitor_key(monitor));
    if (!active_turn || !active_turn->active()) {
        return;
    }
    const auto NOW = Clock::now();
    const auto ELAPSED = std::chrono::duration<double>(NOW - entry->last).count();
    entry->last = NOW;
    const auto current = composition(monitor);
    const auto revision = active_turn->revision() + 1;
    active_turn->frame(current, ELAPSED);
    if (turn(monitor) != active_turn.get() || active_turn->revision() != revision) {
        return;
    }
    if (m_uses_previews && active_turn->drawable()) {
        InventoryPort port{.owner = this, .entry = entry.get(), .monitor = monitor,
            .turn_identity = active_turn.get(), .turn_revision = active_turn->revision(),
            .inventory_revision = m_inventory_revision, .composition_identity = current.identity};
        active_turn->prepare_inventory(port);
        if (turn(monitor) != active_turn.get() || active_turn->revision() != port.turn_revision) {
            return;
        }
        if (composition_id(monitor) != current.identity) {
            active_turn->cancel("preview-composition-changed");
        }
    }
    if (!active_turn->active()) {
        erase_state(monitor);
    }
}

bool CWorkspaceAnimation::Impl::queue(
    Render::CRenderContext &ctx, const PHLMONITOR &monitor, const Time::steady_tp &now) {
    if (m_style_draw_depth != 0 || !hyprimate::workspace::WorkspaceTurn<TurnPort>::queue_context(
            {.monitor = monitor != nullptr,
                .capture_depth = m_capture_depth,
                .capture = ctx.m_renderingCapture,
                .snapshot = ctx.m_renderingSnapshot,
                .read_only = ctx.readOnlyEffects()})) {
        return false;
    }
    const auto keep_alive = shared_from_this();
    const auto *slot = state_slot(monitor);
    const auto entry = slot ? *slot : nullptr;
    const auto active_turn = m_turns.pin(monitor_key(monitor));
    if (entry == nullptr || active_turn == nullptr) {
        return false;
    }
    entry->source_frame.endpoint = active_turn->source().endpoint();
    entry->destination_frame.endpoint = active_turn->destination().endpoint();
    hyprimate::workspace::WorkspaceFrame frame{
        .source = entry->source_frame, .destination = entry->destination_frame,
        .presentation = entry->presentation, .neighbors = entry->neighbors, .native_hud = m_native_hud};
    const bool RESOURCES_READY = entry->source && entry->destination && entry->presentation;
    const auto revision = active_turn->revision() + 1;
    const auto ticket = active_turn->queue(RESOURCES_READY, [&] {
            return prepare_frame(*entry,
                       TurnKind::SourceIsolated,
                       frame.source,
                       active_turn->blend_mix(TurnKind::SourceIsolated)) &&
                   prepare_frame(*entry,
                       TurnKind::DestinationLive,
                       frame.destination,
                       active_turn->blend_mix(TurnKind::DestinationLive));
    });
    if (turn(monitor) != active_turn.get() || active_turn->revision() != revision) { return false; }
    if (!ticket) {
        if (!active_turn->active()) {
            erase_state(monitor);
        }
        return false;
    }
    Render::IHyprRenderer::addPassElement(ctx,
        makeUnique<CWorkspacePresentationPass>(ticket,
            monitor,
            std::move(frame),
            hyprimate::workspace::wallpaper_matches<SP<Render::ITexture>>(
                entry->source_wallpaper_identity, entry->destination_wallpaper_identity)));
    active_turn->replay_hud([&](hyprimate::workspace::HudReplay step) {
        using hyprimate::workspace::HudReplay;
        const auto revision = active_turn->revision();
        const auto REPLAY = [&](const PHLLSREF &weak, bool popup) {
            if (turn(monitor) != active_turn.get() || active_turn->revision() != revision ||
                !safe_scene() || (m_native_hud && !native_hud_safe(monitor))) { return; }
            if (const auto SURFACE = weak.lock()) {
                ((*g_pHyprRenderer).*PreviewDrawAccess::LAYER)(
                    ctx, SURFACE, monitor, now, popup, false, false);
            }
        };
        if (step == HudReplay::Blur || step == HudReplay::RestoreBlur) {
            Render::IHyprRenderer::addPassElement(
                ctx, makeUnique<CWorkspaceHudBlurPass>(active_turn, monitor, step == HudReplay::RestoreBlur));
        } else if (step == HudReplay::Popups) {
            const auto layers_snapshot = monitor->m_layerSurfaceLayers;
            for (const auto &layers : layers_snapshot) {
                for (const auto &weak : layers) {
                    REPLAY(weak, true);
                }
            }
        } else {
            auto layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
            if (step == HudReplay::Bottom) {
                layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
            } else if (step == HudReplay::Top) {
                layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
            }
            const auto layers_snapshot = monitor->m_layerSurfaceLayers[layer];
            for (const auto &weak : layers_snapshot) {
                REPLAY(weak, false);
            }
        }
    });
    return turn(monitor) == active_turn.get() && active_turn->revision() == revision &&
        active_turn->active() && safe_scene() && (!m_native_hud || native_hud_safe(monitor));
}


void CWorkspaceAnimation::Impl::damage(const PHLMONITOR &monitor) {
    if (!monitor) {
        return;
    }
    monitor->addDamage(CBox{0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y});
    monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
}

void CWorkspaceAnimation::Impl::erase_state(const PHLMONITOR &monitor) {
    const auto *slot = state_slot(monitor);
    const auto entry = slot ? *slot : nullptr;
    std::erase_if(m_states, [&](const auto &candidate) { return candidate == entry; });
    if (monitor) {
        m_turns.remove(monitor_key(monitor));
    }
    restore_scanout(monitor);
}

void CWorkspaceAnimation::Impl::remember_wallpaper(const PHLMONITOR &monitor, const PHLWORKSPACE &workspace,
    const SP<Render::IFramebuffer> &source) {
    if (!workspace || !source) {
        return;
    }
    using Observation = hyprimate::workspace::WallpaperObservation<
        PHLWORKSPACEREF, PHLMONITORREF, WallpaperSignature>;
    const Observation OBSERVATION{.workspace = workspace,
        .monitor = monitor,
        .signature = {.allocation = {.size = monitor->m_transformedSize,
                          .format = source->m_drmFormat,
                          .description = source->imageDescription()},
            .transform = monitor->m_transform}};
    const auto COPY = [&](SP<Render::IFramebuffer> image) {
        if (!image) {
            image = preview_target(monitor, 256);
        }
        return write_thumbnail(source, image) ? image : nullptr;
    };
    hyprimate::workspace::WorkspaceTurn<TurnPort>::observe_wallpaper(m_observed, OBSERVATION, COPY);
}

std::vector<CWorkspaceAnimation::Impl::InventoryPort::Fact>
CWorkspaceAnimation::Impl::InventoryPort::workspaces() const {
    std::vector<Fact> facts;
    facts.reserve(workspace_count());
    for (const auto &weak : State::Workspace::state()->workspaces()) {
        const auto WORKSPACE = weak.lock();
        if (!WORKSPACE) {
            continue;
        }
        const auto MONITOR = WORKSPACE->m_monitor.lock();
        const bool SPECIAL = WORKSPACE->type() == Workspace::eWorkspaceType::SPECIAL;
        Fact fact{.workspace = WORKSPACE,
            .endpoint = {.identity = SPECIAL ? owner->m_compositions.get({}, WORKSPACE)
                                             : owner->m_compositions.get(WORKSPACE, {}),
                .numbered = numbered(WORKSPACE),
                .special = SPECIAL,
                .owner = MONITOR ? static_cast<std::uint64_t>(MONITOR->m_id) : UINT64_MAX,
                .monitor_x = MONITOR ? MONITOR->m_position.x : 0,
                .monitor_y = MONITOR ? MONITOR->m_position.y : 0,
                .monitor_width = MONITOR ? MONITOR->m_size.x : 1,
                .monitor_height = MONITOR ? MONITOR->m_size.y : 1},
            .name = WORKSPACE->addressableName(),
            .wallpaper = {},
            .owner_ready = owner->ready(MONITOR),
            .local = MONITOR == monitor,
            .has_windows = WORKSPACE->getWindowCount() != 0,
            .has_retiring = !Desktop::fadingOutState()->fadeouts().empty()};
        if (MONITOR) {
            const WallpaperSignature SIGNATURE{
                .allocation = {.size = MONITOR->m_transformedSize,
                    .format = capture_format(MONITOR),
                    .description = MONITOR->workBufferImageDescription()},
                .transform = MONITOR->m_transform};
            fact.wallpaper = owner->m_observed.find(
                PHLWORKSPACEREF{WORKSPACE}, PHLMONITORREF{MONITOR}, SIGNATURE);
        }
        facts.push_back(std::move(fact));
    }
    return facts;
}

bool CWorkspaceAnimation::Impl::InventoryPort::allowed() const {
    if (revoked || Impl::s_active != owner || !safe_scene() || !owner->ready(monitor) ||
        owner->state(monitor) != entry || owner->m_inventory_revision != inventory_revision) {
        revoked = true;
        return false;
    }
    const auto *current = owner->turn(monitor);
    const bool ALLOWED = current && current == turn_identity && current->drawable() &&
                         current->revision() == turn_revision &&
                         owner->composition_id(monitor) == composition_identity;
    revoked = !ALLOWED;
    return ALLOWED;
}

SP<Render::IFramebuffer> CWorkspaceAnimation::Impl::InventoryPort::local_wallpaper(int side) const {
    if (!allowed()) {
        return {};
    }
    const ScopedCapture CAPTURE_SCOPE{owner->m_capture_depth};
    const auto WALLPAPER = entry->destination_frame.wallpaper;
    const auto IMAGE = preview_target(monitor, side);
    return write_thumbnail(WALLPAPER, IMAGE, [&] { return allowed(); }) ? IMAGE : nullptr;
}

hyprimate::workspace::PreviewCaptureFacts CWorkspaceAnimation::Impl::InventoryPort::capture_facts(
    const PHLWORKSPACEREF &workspace) const {
    const auto WORKSPACE = workspace.lock();
    const auto MONITOR = WORKSPACE ? WORKSPACE->m_monitor.lock() : nullptr;
    return {.ready = owner->ready(MONITOR),
        .local = MONITOR == monitor,
        .pixels = MONITOR ? MONITOR->m_transformedSize.x * MONITOR->m_transformedSize.y : 0.0};
}

SP<Render::IFramebuffer> CWorkspaceAnimation::Impl::InventoryPort::capture(
    const PHLWORKSPACEREF &workspace,
    const SP<Render::IFramebuffer> &wallpaper,
    hyprimate::workspace::PreviewCapturePlan plan) const {
    const auto WORKSPACE = workspace.lock();
    return WORKSPACE ? owner->capture_preview(*this, WORKSPACE, wallpaper, plan) : nullptr;
}

SP<Render::IFramebuffer> CWorkspaceAnimation::Impl::capture_preview(const InventoryPort &port,
    const PHLWORKSPACE &workspace,
    const SP<Render::IFramebuffer> &paper,
    hyprimate::workspace::PreviewCapturePlan plan) {
    struct DrawData {
        RenderFn original;
        PHLMONITOR monitor;
        PHLWORKSPACE workspace;
        SP<Render::IFramebuffer> wallpaper;
        bool native_background = false;
        const InventoryPort *port = nullptr;
        bool remote = false;
        [[nodiscard]] bool allowed() const {
            if (!port->allowed() || workspace->m_monitor.lock() != monitor ||
                !port->owner->ready(monitor) || remote != (monitor != port->monitor)) {
                port->revoked = true;
                return false;
            }
            return true;
        }
    } data{.original = render_original(),
        .monitor = workspace->m_monitor.lock(),
        .workspace = workspace,
        .wallpaper = paper,
        .native_background = plan.native_background,
        .port = &port,
        .remote = plan.remote};
    const auto ALLOWED = [&] { return data.allowed(); };
    if (!ALLOWED()) {
        return {};
    }
    const auto OWNER = data.monitor;
    const ScopedCapture CAPTURE_SCOPE{m_capture_depth};
    // The presentation target is scratch only before the ordinary output pass.
    auto scratch = port.entry->presentation;
    auto resources = port.entry->source_resources;
    if (plan.remote) {
        const auto EXTENT = OWNER->m_transformedSize;
        scratch = preview_target(OWNER, static_cast<int>(std::max(EXTENT.x, EXTENT.y)));
        if (!scratch) {
            return {};
        }
        resources = makeShared<Render::CSceneResources>(g_pHyprRenderer->createFB("hyprimate workspace preview blur"),
            Render::SSceneBufferDescription{.format = scratch->m_drmFormat, .imageDescription = scratch->imageDescription()});
    }
    if (!scratch || !resources) {
        return {};
    }
    const auto DRAW = [](void *context, Render::CRenderContext &ctx) {
        const auto &args = *static_cast<DrawData *>(context);
        if (!args.allowed()) {
            return;
        }
        auto &renderer = *g_pHyprRenderer;
        const auto NOW = Time::steadyNow();
        if (!args.native_background) {
            CTexPassElement::SRenderData layer;
            layer.tex = args.wallpaper->getTexture();
            layer.box = {0, 0, args.monitor->m_transformedSize.x, args.monitor->m_transformedSize.y};
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CTexPassElement>(std::move(layer)));
        } else {
            // Wallpaper clients expose a current background, not workspace-specific images.
            (renderer.*PreviewDrawAccess::BACKGROUND)(ctx, args.monitor);
            if (!args.allowed()) {
                return;
            }
            // Native render callbacks may remove layers while building the pass.
            const auto LAYERS =
                args.monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND];
            for (const auto &layer : LAYERS) {
                if (const auto SURFACE = layer.lock()) {
                    (renderer.*PreviewDrawAccess::LAYER)(
                        ctx, SURFACE, args.monitor, NOW, false, false, true);
                    if (!args.allowed()) {
                        return;
                    }
                }
            }
        }
        args.original(&renderer,
            ctx,
            args.monitor,
            args.workspace,
            NOW,
            Render::eSceneMode::WORKSPACE_WINDOWS,
            {},
            1.F);
    };
    const auto IMAGE = preview_target(OWNER, plan.side);
    return IMAGE && data.original && ALLOWED() &&
                   hyprimate::workspace::capture_workspace(*g_pHyprRenderer,
                       m_begin_render,
                       OWNER,
                       scratch,
                       resources,
                       TurnKind::SourceIsolated,
                       DRAW,
                       &data,
                       [](void *context) { return static_cast<DrawData *>(context)->allowed(); }) &&
                   write_thumbnail(scratch, IMAGE, ALLOWED)
               ? IMAGE
               : nullptr;
}

void CWorkspaceAnimation::Impl::remove_monitor(const PHLMONITOR &monitor) {
    if (!monitor) {
        return;
    }
    ++m_inventory_revision; // Other monitors can hold previews from this output.
    std::erase_if(m_ready_monitors, [&](const auto &weak) { return weak.expired() || weak.lock() == monitor; });
    m_observed.remove_monitor(PHLMONITORREF{monitor});
    erase_state(monitor);
}

void CWorkspaceAnimation::Impl::clear() {
    m_turns.reload();
    ++m_lifecycle;
    ++m_gesture_revision;
    const auto states = std::exchange(m_states, {});
    m_suppressed.clear();
    m_gesture_monitor.reset();
    m_turns.clear();
    for (const auto &entry : states) {
        if (const auto MONITOR = entry->monitor.lock()) {
            damage(MONITOR);
        }
    }
    restore_scanout();
}

CWorkspaceAnimation::CWorkspaceAnimation(HANDLE handle) : m_impl(std::make_shared<Impl>(handle)) {
}
CWorkspaceAnimation::~CWorkspaceAnimation() { m_impl->shutdown(); }
bool CWorkspaceAnimation::install(std::string &error) {
    const auto impl = m_impl;
    return impl->install(error);
}
void CWorkspaceAnimation::shutdown() {
    const auto impl = m_impl;
    impl->shutdown();
}
void CWorkspaceAnimation::configure(const hyprimate::config::SWorkspaceSettings &settings) {
    const auto impl = m_impl;
    impl->configure(settings);
}
void CWorkspaceAnimation::on_render_pre_checks(const PHLMONITOR &monitor) {
    const auto impl = m_impl;
    const auto output = monitor;
    impl->pre_checks(output);
}
void CWorkspaceAnimation::on_render_pre(const PHLMONITOR &monitor) {
    const auto impl = m_impl;
    const auto output = monitor;
    impl->pre_render(output);
}
void CWorkspaceAnimation::remove_monitor(const PHLMONITOR &monitor) {
    const auto impl = m_impl;
    const auto output = monitor;
    impl->remove_monitor(output);
}
void CWorkspaceAnimation::clear() {
    const auto impl = m_impl;
    impl->clear();
}
