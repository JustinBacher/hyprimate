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
#include "workspace/WorkspaceStyleList.hpp"
#include "workspace/WorkspaceProfiling.hpp"
#include "workspace/WorkspaceTurn.hpp"
#include "workspace/WorkspaceAnimationImpl.hpp"

namespace {

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
    using BlurFn = SP<Render::IFramebuffer> (*)(Render::IHyprRenderer *,
        Render::CRenderContext &,
        SP<Render::IFramebuffer>,
        float,
        const CRegion &,
        const Render::SBlurContext &);
    constexpr const char *BLUR_SIGNATURE =
        "Render::GL::CHyprGLRenderer::blurFramebuffer(Render::CRenderContext&, "
        "Hyprutils::Memory::CSharedPointer<Render::IFramebuffer>, float, "
        "Hyprutils::Math::CRegion const&, Render::SBlurContext const&)";
    using GestureBeginFn = bool (*)(CUnifiedWorkspaceSwipeGesture *, PHLMONITOR);
    using GestureVoidFn = void (*)(CUnifiedWorkspaceSwipeGesture *);
    using WindowFn = void (*)(Render::IHyprRenderer *,
        Render::CRenderContext &,
        PHLWINDOW,
        PHLMONITOR,
        const Render::SWindowRenderPresentation &,
        const Time::steady_tp &,
        bool,
        Render::eRenderPassMode,
        bool,
        bool);
    constexpr const char *WINDOW_SIGNATURE =
        "Render::IHyprRenderer::renderWindow(Render::CRenderContext&, "
        "Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>, "
        "Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>, "
        "Render::SWindowRenderPresentation const&, "
        "std::chrono::time_point<std::chrono::_V2::steady_clock, std::chrono::duration<long, "
        "std::ratio<1l, 1000000000l> > > const&, bool, Render::eRenderPassMode, bool, bool)";

    using LayerFn = void (*)(Render::IHyprRenderer *,
        Render::CRenderContext &,
        PHLLS,
        PHLMONITOR,
        const Time::steady_tp &,
        bool,
        bool,
        bool);
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



}  // namespace

std::optional<std::uint32_t> workspace_animation_detail::numbered(const PHLWORKSPACE &workspace) {
    if (!workspace) {
        return std::nullopt;
    }
    return workspace->numberedID();
}

bool workspace_animation_detail::safe_scene() {
    return hyprimate::workspace::capture_scene_safe(scene_facts());
}


namespace {
    CWorkspaceAnimation::Impl *active() {
        return CWorkspaceAnimation::Impl::s_active;
    }



    template <typename Fn>
    Fn original(CFunctionHook *hook) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland exposes original
        // hook functions as void pointers.
        return reinterpret_cast<Fn>(hook != nullptr ? hook->m_original : nullptr);
    }

    // Capture boundaries reset only alpha to record foreground coverage for
    // built-in styles. RGB already contains the complete scene, including each
    // endpoint's own wallpaper. Expose that RGB as opaque to both raw and helper
    // shader samplers without copying pixels or discarding fallback coverage.
    class OpaqueEndpointSamples {
      public:
        explicit OpaqueEndpointSamples(std::array<GLuint, 2> textures) : m_textures(textures) {
            with_bound_textures([&](std::size_t index) {
                glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, &m_alpha[index]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
            });
        }
        ~OpaqueEndpointSamples() {
            with_bound_textures([&](std::size_t index) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, m_alpha[index]);
            });
        }
        OpaqueEndpointSamples(const OpaqueEndpointSamples &) = delete;
        OpaqueEndpointSamples &operator=(const OpaqueEndpointSamples &) = delete;

      private:
        void with_bound_textures(const auto &operation) const {
            GLint previous = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
            for (std::size_t index = 0; index < m_textures.size(); ++index) {
                glBindTexture(GL_TEXTURE_2D, m_textures[index]);
                operation(index);
            }
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
        }

        std::array<GLuint, 2> m_textures;
        std::array<GLint, 2> m_alpha{};
    };




    // Hook trampolines below must match Hyprland's original signatures exactly (ABI), so their
    // smart-pointer and optional parameters stay by value.
    // NOLINTBEGIN(performance-unnecessary-value-param)
    SP<Render::IFramebuffer> hook_blur(Render::IHyprRenderer *renderer,
        Render::CRenderContext &ctx,
        SP<Render::IFramebuffer> source,
        float strength,
        const CRegion &damage,
        const Render::SBlurContext &context) {
        const auto *manager = active();
        auto function = original<BlurFn>(manager != nullptr ? manager->m_blur_hook : nullptr);
        if (function == nullptr) {
            return nullptr;
        }
        if (manager != nullptr && manager->m_capturing != nullptr &&
            manager->m_capturing->wallpaper_captured && source &&
            source->m_size == manager->m_capturing->source->m_size) {
            const auto BACKDROP = manager->m_capturing->opaque_backdrop;
            if (!copy_capture(source,
                    BACKDROP,
                    Coverage::OpaqueTarget,
                    false,
                    std::nullopt,
                    CopySource::BorrowedNative)) {
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

    void hook_layer(Render::IHyprRenderer *renderer,
        Render::CRenderContext &ctx,
        PHLLS layer,
        PHLMONITOR monitor,
        const Time::steady_tp &now,
        bool popups,
        bool lockscreen,
        bool workspace_scene) {
        const auto *manager = active();
        const auto ORIGINAL =
            original<LayerFn>(manager != nullptr ? manager->m_layer_hook : nullptr);
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
        const auto MONITOR = self->m_self.lock();
        const auto PREVIOUS = workspace && workspace->type() == Workspace::eWorkspaceType::SPECIAL
                                  ? workspace->m_monitor.lock()
                                  : PHLMONITOR{};
        const bool SPECIAL_TARGET =
            workspace && workspace->type() == Workspace::eWorkspaceType::SPECIAL;
        if (manager != nullptr && manager->m_window_planes &&
            (!manager->burn_window_planes_allowed(MONITOR, SPECIAL_TARGET) ||
                (PREVIOUS && !manager->burn_window_planes_allowed(PREVIOUS, SPECIAL_TARGET)))) {
            if (SPECIAL_TARGET || manager->special_workspace_in_native_scene()) {
                manager->decline_all_burn_transitions();
            } else {
                manager->decline_burn_transition({MONITOR, PREVIOUS});
            }
            function(self, workspace, internal, no_mouse_move, no_focus);
            return;
        }
        if (manager != nullptr && workspace) {
            const auto &NORMAL =
                SPECIAL_TARGET ? self->m_activeWorkspace : workspace;
            const auto &SPECIAL =
                SPECIAL_TARGET ? workspace : self->m_activeSpecialWorkspace;
            if (!manager->transition_destination_allowed(MONITOR, NORMAL, SPECIAL)) {
                manager->cancel_transition(MONITOR);
                function(self, workspace, internal, no_mouse_move, no_focus);
                return;
            }
        }
        if (manager == nullptr || !manager->request_enabled() || !workspace ||
            workspace == self->m_activeWorkspace || workspace == self->m_activeSpecialWorkspace) {
            function(self, workspace, internal, no_mouse_move, no_focus);
            return;
        }
        manager->mutate({MONITOR, PREVIOUS},
            [&] { function(self, workspace, internal, no_mouse_move, no_focus); });
    }

    void hook_special(Monitor::CMonitor *self, const PHLWORKSPACE &workspace, bool no_focus) {
        auto *manager = active();
        auto function = original<SpecialFn>(manager != nullptr ? manager->m_special_hook : nullptr);
        if (function == nullptr) {
            return;
        }
        const auto MONITOR = self->m_self.lock();
        const auto PREVIOUS = workspace ? workspace->m_monitor.lock() : PHLMONITOR{};
        if (manager != nullptr && manager->m_window_planes &&
            (!manager->burn_window_planes_allowed(MONITOR, workspace != nullptr) ||
                (PREVIOUS && !manager->burn_window_planes_allowed(PREVIOUS, workspace != nullptr)))) {
            if (workspace || manager->special_workspace_in_native_scene()) {
                manager->decline_all_burn_transitions();
            } else {
                manager->decline_burn_transition({MONITOR, PREVIOUS});
            }
            function(self, workspace, no_focus);
            return;
        }
        if (manager != nullptr &&
            !manager->transition_destination_allowed(MONITOR, self->m_activeWorkspace, workspace)) {
            manager->cancel_transition(MONITOR);
            function(self, workspace, no_focus);
            return;
        }
        if (manager == nullptr || !manager->request_enabled() ||
            workspace == self->m_activeSpecialWorkspace) {
            function(self, workspace, no_focus);
            return;
        }
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
        const auto OLD_MONITOR = workspace ? workspace->m_monitor.lock() : PHLMONITOR{};
        const bool SPECIAL_TARGET =
            workspace && workspace->type() == Workspace::eWorkspaceType::SPECIAL;
        if (manager != nullptr && manager->m_window_planes &&
            (!manager->burn_window_planes_allowed(OLD_MONITOR, SPECIAL_TARGET) ||
                !manager->burn_window_planes_allowed(monitor, SPECIAL_TARGET))) {
            if (SPECIAL_TARGET || manager->special_workspace_in_native_scene()) {
                manager->decline_all_burn_transitions();
            } else {
                manager->decline_burn_transition({OLD_MONITOR, monitor});
            }
            function(self, workspace, monitor, no_warp, carry_focus, replace);
            return;
        }
        if (manager != nullptr && !manager->m_admit_turns) {
            const auto *OLD_TURN = manager->turn(OLD_MONITOR);
            const auto *NEW_TURN = manager->turn(monitor);
            if ((OLD_TURN && OLD_TURN->active()) || (NEW_TURN && NEW_TURN->active())) {
                manager->decline_burn_transition({OLD_MONITOR, monitor});
                function(self, workspace, monitor, no_warp, carry_focus, replace);
                return;
            }
        }
        if (manager == nullptr || !manager->request_enabled()) {
            function(self, workspace, monitor, no_warp, carry_focus, replace);
            return;
        }
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
        if (manager != nullptr && manager->m_window_planes &&
            (!manager->burn_window_planes_allowed(first) ||
                !manager->burn_window_planes_allowed(second))) {
            if (manager->special_workspace_in_native_scene()) {
                manager->decline_all_burn_transitions();
            } else {
                manager->decline_burn_transition({first, second});
            }
            function(self, first, second);
            return;
        }
        if (manager != nullptr && !manager->m_admit_turns) {
            const auto *FIRST_TURN = manager->turn(first);
            const auto *SECOND_TURN = manager->turn(second);
            if ((FIRST_TURN && FIRST_TURN->active()) || (SECOND_TURN && SECOND_TURN->active())) {
                manager->decline_burn_transition({first, second});
                function(self, first, second);
                return;
            }
        }
        if (manager == nullptr || !manager->request_enabled()) {
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
        auto *manager = active();
        auto function = original<AnimateFn>(manager != nullptr ? manager->m_animate_hook : nullptr);
        if (function == nullptr) {
            return;
        }
        // Still let Hyprland establish its incoming/outgoing workspace state and callbacks.
        // Suppressing the call entirely leaves previously hidden workspaces transparent or
        // offset; instant completion avoids a competing native animation.
        const auto MONITOR = workspace ? workspace->m_monitor.lock() : PHLMONITOR{};
        auto *staged = manager != nullptr && workspace
                           ? manager->staged_mutation(MONITOR)
                           : nullptr;
        if (manager != nullptr && manager->m_window_planes &&
            !manager->burn_window_planes_allowed(
                MONITOR, workspace && workspace->type() == Workspace::eWorkspaceType::SPECIAL)) {
            if ((workspace && workspace->type() == Workspace::eWorkspaceType::SPECIAL) ||
                manager->special_workspace_in_native_scene()) {
                manager->decline_all_burn_transitions();
            } else {
                manager->decline_burn_transition({MONITOR});
            }
            staged = nullptr;
        }
        const bool INCOMING = type == Animation::Workspace::ANIMATION_TYPE_IN;
        if (staged != nullptr && !instant && manager->m_native_direction &&
            (INCOMING || !staged->native_direction)) {
            std::string_view effective_style;
            if (style) {
                effective_style = *style;
            } else if (const auto &TREE = Config::animationTree()) {
                const bool SPECIAL = workspace->type() == Workspace::eWorkspaceType::SPECIAL;
                const char *leaf = INCOMING ? "workspacesIn" : "workspacesOut";
                if (SPECIAL) {
                    leaf = INCOMING ? "specialWorkspaceIn" : "specialWorkspaceOut";
                }
                const auto NODE = TREE->getAnimationPropertyConfig(leaf);
                const auto VALUES = NODE ? NODE->pValues.lock() : nullptr;
                if (VALUES) {
                    effective_style = VALUES->internalStyle;
                }
            }
            staged->native_direction =
                hyprimate::workspace::animation_direction(left, effective_style);
        }
        if (staged != nullptr) {
            staged = manager->staged_mutation(MONITOR);
        }
        if (manager != nullptr && MONITOR &&
            !manager->transition_destination_allowed(MONITOR, manager->composition_id(MONITOR))) {
            manager->cancel_transition(MONITOR);
            staged = nullptr;
        }
        const bool SUPPRESSED = staged != nullptr;
        if (manager != nullptr && manager->m_turns.enabled() && !SUPPRESSED && !instant &&
            manager->m_mutation_depth == 0 && manager->m_capture_depth == 0 && workspace) {
            LOG(Log::DEBUG,
                "[hyprimate] workspace native-animation-unintercepted workspace={} type={}",
                workspace->addressableName(),
                static_cast<int>(type));
        }
        function(workspace, type, left, instant || SUPPRESSED, std::move(style));
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
        if (manager != nullptr && mode == Render::eSceneMode::MONITOR &&
            manager->queue(ctx, monitor, now)) {
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
        auto function =
            original<GestureBeginFn>(manager != nullptr ? manager->m_gesture_begin_hook : nullptr);
        const auto owner_pin = manager ? manager->shared_from_this() : nullptr;
        if (function == nullptr) {
            return false;
        }
        if (manager != nullptr && manager->request_enabled()) {
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
        auto function =
            original<GestureVoidFn>(manager != nullptr ? manager->m_gesture_end_hook : nullptr);
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
        auto function =
            original<GestureVoidFn>(manager != nullptr ? manager->m_gesture_cancel_hook : nullptr);
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
        CWorkspaceWallpaperPass(SP<Render::IFramebuffer> target,
            bool &captured,
            bool *failed = nullptr,
            bool reset = true,
            std::optional<CBox> crop = std::nullopt)
            : m_target(std::move(target)),
              m_captured(&captured),
              m_failed(failed),
              m_reset(reset),
              m_crop(crop) {
        }

        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            *m_captured = copy_capture(ctx.m_data.currentFB,
                m_target,
                m_reset ? Coverage::ResetSource : Coverage::Preserve,
                false,
                m_crop,
                CopySource::BorrowedNative);
            if (!*m_captured && m_failed != nullptr) {
                *m_failed = true;
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
        bool disableSimplification(Render::CRenderContext & /*ctx*/) override {
            return true;
        }
        bool requiresFullDamage(Render::CRenderContext & /*ctx*/) override {
            return true;
        }
        const char *passName() override {
            return "hyprimate-workspace-wallpaper";
        }
        ePassElementType type() override {
            return EK_CUSTOM;
        }

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
    void hook_window(Render::IHyprRenderer *renderer,
        Render::CRenderContext &ctx,
        PHLWINDOW window,
        PHLMONITOR monitor,
        const Render::SWindowRenderPresentation &presentation,
        const Time::steady_tp &now,
        bool decorate,
        Render::eRenderPassMode pass,
        bool ignore_position,
        bool standalone) {
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
        const auto BOUNDARY = [&](const SP<Render::IFramebuffer> &target,
                                  std::optional<CBox> region = std::nullopt) {
            Render::IHyprRenderer::addPassElement(ctx,
                makeUnique<CWorkspaceWallpaperPass>(
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
        CWorkspaceHudBlurPass(
            const std::shared_ptr<Turn> &turn, PHLMONITOR monitor, bool invalidate)
            : m_turn(turn),
              m_monitor(std::move(monitor)),
              m_revision(turn->revision()),
              m_invalidate(invalidate) {
        }
        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            // Metadata restoration may still run after cancellation, but never after shutdown.
            if (!active() || ctx.m_data.pMonitor.lock() != m_monitor) {
                return {};
            }
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
                !Turn::queue_context({.monitor = true,
                    .capture_depth = owner->m_capture_depth,
                    .capture = ctx.m_renderingCapture,
                    .snapshot = ctx.m_renderingSnapshot,
                    .read_only = ctx.readOnlyEffects()})) {
                return {};
            }
            const bool QUEUED =
                RESOURCES ? RESOURCES->blurQueued() : ctx.m_data.pMonitor->m_blurFBShouldRender;
            if (QUEUED) {
                const CRegion DAMAGE{CBox{{}, ctx.m_data.pMonitor->m_transformedSize}};
                g_pHyprRenderer->preBlurForCurrentMonitor(ctx, DAMAGE);
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
        bool disableSimplification(Render::CRenderContext & /*ctx*/) override {
            return true;
        }
        bool requiresFullDamage(Render::CRenderContext & /*ctx*/) override {
            return true;
        }
        const char *passName() override {
            return "hyprimate-workspace-native-hud-blur";
        }
        ePassElementType type() override {
            return EK_CUSTOM;
        }

      private:
        std::weak_ptr<Turn> m_turn;
        PHLMONITOR m_monitor;
        std::uint64_t m_revision;
        bool m_invalidate;
    };

    class CWorkspacePresentationPass final : public IPassElement {
      public:
        CWorkspacePresentationPass(std::uint64_t ticket,
            PHLMONITOR monitor,
            hyprimate::workspace::WorkspaceFrame frame,
            bool matching_wallpaper,
            hyprimate::perf::Key key)
            : m_ticket(ticket),
              m_monitor(std::move(monitor)),
              m_frame(std::move(frame)),
              m_matching_wallpaper(matching_wallpaper),
              m_key(key) {
        }

        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            const auto owner_pin = active() ? active()->shared_from_this() : nullptr;
            using Turn = hyprimate::workspace::WorkspaceTurn<CWorkspaceAnimation::Impl::TurnPort>;
            const auto CURRENT = [&]() -> Turn * {
                auto *owner = active();
                if (!owner || owner->m_style_draw_depth != 0 ||
                    !Turn::queue_context({.monitor = ctx.m_data.pMonitor.lock() == m_monitor,
                        .capture_depth = owner->m_capture_depth,
                        .capture = ctx.m_renderingCapture,
                        .snapshot = ctx.m_renderingSnapshot,
                        .read_only = ctx.readOnlyEffects()})) {
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
                if (!owner || owner != owner_pin.get()) {
                    return false;
                }
                ++owner->m_style_draw_depth;
                bool drawn = false;
                {
                    const hyprimate::workspace::CallbackExit unwind{
                        [&]() noexcept { --owner->m_style_draw_depth; }};
                    try {
                        owner->m_styles.for_each([&]<class Style>(auto &resources) {
                            if (Style::KIND == owner->m_style_kind && resources.ready) {
                                style_name = Style::NAME;
                                const hyprimate::perf::Key KEY{
                                    hyprimate::perf::Domain::Workspace, Style::NAME, "transition"};
                                const hyprimate::workspace::ProfileCpuScope CPU{
                                    KEY, hyprimate::perf::Phase::Draw};
                                const hyprimate::workspace::ProfileAttribution ATTRIBUTION{KEY};
                                const hyprimate::perf::GpuScope GPU{KEY};
                                drawn = Style::draw(resources,
                                    ctx,
                                    m_frame,
                                    progress,
                                    direction,
                                    m_matching_wallpaper);
                            }
                        });
                    } catch (const std::exception &error) {
                        LOG(Log::ERR,
                            "[hyprimate] workspace style draw failed monitor={} style={}: {}",
                            m_monitor->m_name,
                            style_name,
                            error.what());
                    } catch (...) {
                        LOG(Log::ERR,
                            "[hyprimate] workspace style draw failed monitor={} style={}: unknown "
                            "exception",
                            m_monitor->m_name,
                            style_name);
                    }
                }
                if (owner->m_style_draw_depth == 0) {
                    if (std::exchange(owner->m_release_pending, false)) {
                        owner->release_styles();
                    }
                    if (auto pending = std::exchange(owner->m_deferred_settings, std::nullopt)) {
                        if (active() == owner) {
                            owner->configure(pending->first, pending->second);
                        }
                    }
                }
                return drawn && previews_current();
            };
            const auto DISPLAY = [&] {
                if (!hyprimate::gl::native_current(m_frame.presentation)) {
                    return false;
                }
                const hyprimate::workspace::ProfileCpuScope CPU{
                    m_key, hyprimate::perf::Phase::Draw};
                const hyprimate::perf::GpuScope GPU{m_key};
                CTexPassElement::SRenderData data;
                data.tex = m_frame.presentation->getTexture();
                data.box = {0, 0, m_monitor->m_transformedSize.x, m_monitor->m_transformedSize.y};
                g_pHyprRenderer->draw(ctx, data, ctx.m_data.damage);
                return true;
            };
            const auto *slot = owner_pin ? owner_pin->state_slot(m_monitor) : nullptr;
            const auto entry = slot && (*slot)->effect_program && !(*slot)->effect_declined
                                   ? *slot : nullptr;
            const auto EFFECT = [&](const Turn::PresentationSample &sample) {
                // An unusable alternate presentation resumes the native capture cadence.
                entry->effect_declined = true;
                if (owner_pin->state(m_monitor) != entry.get() ||
                    m_frame.source.identity != m_frame.source.endpoint.identity ||
                    m_frame.destination.identity != m_frame.destination.endpoint.identity ||
                    !m_frame.source.composite || !m_frame.destination.composite ||
                    m_frame.source.composite != entry->source ||
                    m_frame.destination.composite != entry->destination ||
                    m_frame.source.composite == m_frame.destination.composite ||
                    !m_frame.source.shader_readable || !m_frame.destination.shader_readable ||
                    !hyprimate::gl::native_current(m_frame.source.composite) ||
                    !hyprimate::gl::native_current(m_frame.destination.composite)) {
                    return false;
                }
                const auto &source = m_frame.source.composite;
                const auto &destination = m_frame.destination.composite;
                const auto SRC = source->getTexture();
                const auto DST = destination->getTexture();
                if (!SRC || !DST || SRC->m_type != Render::TEXTURE_RGBA ||
                    DST->m_type != Render::TEXTURE_RGBA || !SRC->m_texID || !DST->m_texID ||
                    SRC->m_texID == DST->m_texID) {
                    return false;
                }
                if (entry->effect_generation != sample.generation) {
                    entry->effect_generation = sample.generation;
                    entry->effect_seed = m_ticket;
                    entry->effect =
                        std::make_unique<hyprimate::shader::SceneShader>(entry->effect_program);
                }
                if (!entry->effect) {
                    return false;
                }
                const hyprimate::workspace::ProfileCpuScope CPU{
                    EFFECT_PROFILE, hyprimate::perf::Phase::Draw};
                const hyprimate::workspace::ProfileAttribution ATTRIBUTION{EFFECT_PROFILE};
                const hyprimate::perf::GpuScope GPU{EFFECT_PROFILE};
                auto frame = hyprimate::shader::Frame{
                    .tick = sample.tick,
                    .seed = entry->effect_seed,
                    .event = hyprimate::shader::Event::Workspace,
                    .progress = static_cast<float>(sample.progress),
                    .elapsed = static_cast<float>(sample.elapsed),
                    .delta = static_cast<float>(sample.delta),
                    .duration = static_cast<float>(sample.duration),
                    .width = static_cast<float>(m_monitor->m_size.x),
                    .height = static_cast<float>(m_monitor->m_size.y),
                    .scale = static_cast<float>(m_monitor->m_scale)};
                frame.direction = sample.direction == Direction::Forward ? 1.F : -1.F;
                entry->effect->advance(frame);
                std::string error;
                // Only retained, policy-authorized full-workspace captures are
                // inputs. The active context is the output, never a sampler.
                bool drawn = false;
                {
                    const OpaqueEndpointSamples ENDPOINTS{{SRC->m_texID, DST->m_texID}};
                    drawn = entry->effect->draw(ctx, source, destination, error);
                }
                if (!drawn) {
                    entry->effect.reset();
                    LOG(Log::DEBUG,
                        "[hyprimate] workspace shader declined monitor={}: {}",
                        m_monitor->m_name,
                        error);
                    return false;
                }
                entry->effect_declined = false;
                style_name = "shader";
                return true;
            };
            const auto result = entry
                                    ? Turn::draw_queued(m_ticket, CURRENT, RENDER, DISPLAY, EFFECT)
                                    : Turn::draw_queued(m_ticket, CURRENT, RENDER, DISPLAY);
            if (result == Turn::Presentation::Displayed ||
                result == Turn::Presentation::FirstDisplay) {
                if (hyprimate::perf::enabled()) {
                    const auto MONITOR = hyprimate::perf::monitor_identity(m_monitor);
                    if (MONITOR != 0) {
                        hyprimate::perf::touch(
                            style_name == "shader" ? EFFECT_PROFILE : m_key, MONITOR);
                    }
                }
            }
            if (result == Turn::Presentation::FirstDisplay) {
                LOG(Log::DEBUG,
                    "[hyprimate] workspace presented monitor={} source={} destination={} style={}",
                    m_monitor->m_name,
                    m_frame.source.identity,
                    m_frame.destination.identity,
                    style_name);
                if (auto *owner = active(); owner && owner->state(m_monitor)) {
                    owner->state(m_monitor)->last = Clock::now();
                }
            } else if (result == Turn::Presentation::Cancelled) {
                // The native workspace wasn't queued for this frame. Don't leave pixels from
                // an earlier swapchain image underneath the HUD while waiting for native repaint.
                g_pHyprRenderer->draw(
                    ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 1)}, ctx.m_data.damage);
                if (auto *owner = active();
                    owner && owner->turn(m_monitor) && !owner->turn(m_monitor)->active()) {
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
        static constexpr hyprimate::perf::Key EFFECT_PROFILE{
            hyprimate::perf::Domain::Workspace, "shader", "transition"};
        std::uint64_t m_ticket;
        PHLMONITOR m_monitor;
        hyprimate::workspace::WorkspaceFrame m_frame;
        bool m_matching_wallpaper;
        hyprimate::perf::Key m_key;
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
        Spec{.name = CHANGE_NAME,
            .signature = CHANGE_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_change),
            .slot = &m_change_hook},
        Spec{.name = SPECIAL_NAME,
            .signature = SPECIAL_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_special),
            .slot = &m_special_hook},
        Spec{.name = MOVE_NAME,
            .signature = MOVE_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_move),
            .slot = &m_move_hook},
        Spec{.name = SWAP_NAME,
            .signature = SWAP_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_swap),
            .slot = &m_swap_hook},
        Spec{.name = ANIMATE_NAME,
            .signature = ANIMATE_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_animate),
            .slot = &m_animate_hook},
        Spec{.name = RENDER_NAME,
            .signature = RENDER_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_render),
            .slot = &m_render_hook},
        Spec{.name = DRAG_NAME,
            .signature = DRAG_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_drag),
            .slot = &m_drag_hook},
        Spec{.name = "blurFramebuffer",
            .signature = BLUR_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_blur),
            .slot = &m_blur_hook},
        Spec{.name = "renderWindow",
            .signature = WINDOW_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): native hook trampoline.
            .replacement = reinterpret_cast<const void *>(&hook_window),
            .slot = &m_window_hook,
            .optional = true},
        Spec{.name = "renderLayer",
            .signature = LAYER_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): native hook trampoline.
            .replacement = reinterpret_cast<const void *>(&hook_layer),
            .slot = &m_layer_hook,
            .optional = true},
        Spec{.name = GESTURE_BEGIN_NAME,
            .signature = GESTURE_BEGIN_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_gesture_begin),
            .slot = &m_gesture_begin_hook},
        Spec{.name = GESTURE_END_NAME,
            .signature = GESTURE_END_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_gesture_end),
            .slot = &m_gesture_end_hook},
        Spec{.name = GESTURE_CANCEL_NAME,
            .signature = GESTURE_CANCEL_SIGNATURE,
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland's hook API
            // accepts trampoline addresses as void pointers.
            .replacement = reinterpret_cast<const void *>(&hook_gesture_cancel),
            .slot = &m_gesture_cancel_hook},
    };
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Hyprland exposes symbol
    // addresses as void pointers.
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
    m_inventory_listeners = {workspaces.createdLate.listen(CHANGED),
        workspaces.removed.listen(CHANGED),
        workspaces.renamed.listen(CHANGED),
        workspaces.moveToMonitor.listen(
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
                LOG(Log::DEBUG,
                    "[hyprimate] workspace selected-style-prewarm failed style={}",
                    static_cast<std::uint64_t>(m_style_kind));
            }
        }
        if (const auto *active_turn = turn(event.monitor);
            active_turn == nullptr || !active_turn->active()) {
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
            makeUnique<CWorkspaceWallpaperPass>(
                target, entry.wallpaper_captured, &entry.capture_failed));
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
    if (s_active == this) {
        s_active = nullptr;
    }
    ++m_lifecycle;
    m_turns.clear();
    m_effect_program.reset();
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
    m_render_hook = m_drag_hook = m_gesture_begin_hook = m_gesture_end_hook =
        m_gesture_cancel_hook = nullptr;
    m_blur_hook = m_window_hook = m_layer_hook = nullptr;
    clear();
    m_style_warm_pending = false;
    if (m_style_draw_depth) {
        m_release_pending = true;
    } else {
        release_styles();
    }
    m_begin_render = nullptr;
}

bool CWorkspaceAnimation::Impl::request_enabled() {
    if (m_native_direction && m_last_settings && m_last_settings->enabled && m_capture_depth == 0 &&
        m_mutation_depth == 0 && m_style_draw_depth == 0) {
        // Native animation settings can change without a plugin config callback.
        // Refresh once at request admission, never in the output-frame loop.
        const auto KEEP_ALIVE = shared_from_this();
        m_styles.for_each([&]<class Style>(auto &resources) {
            if constexpr (requires { Style::refresh_timing(resources); }) {
                if (Style::KIND == m_style_kind && Style::refresh_timing(resources)) {
                    m_settings_changed = true;
                    configure(*m_last_settings, false);
                }
            }
        });
        return active() == this && m_turns.enabled();
    }
    return m_turns.enabled();
}

bool CWorkspaceAnimation::Impl::latched_shader_turn(
    const std::shared_ptr<MonitorState> &entry) {
    if (!entry) {
        return false;
    }
    const auto MONITOR = entry->monitor.lock();
    const auto *ACTIVE_TURN = turn(MONITOR);
    return hyprimate::workspace::preserve_latched_shader_pair({.active =
            ACTIVE_TURN && ACTIVE_TURN->active(),
        .has_program = entry->effect_program != nullptr,
        .usable = !entry->effect_declined});
}

bool CWorkspaceAnimation::Impl::has_latched_shader_turn() {
    return std::ranges::any_of(
        m_states, [&](const auto &entry) { return latched_shader_turn(entry); });
}

void CWorkspaceAnimation::Impl::expire_unlatched_turns() {
    for (std::size_t index = 0; index < m_states.size();) {
        const auto ENTRY = m_states[index];
        if (latched_shader_turn(ENTRY)) {
            ++index;
            continue;
        }
        const auto MONITOR = ENTRY->monitor.lock();
        if (MONITOR) {
            erase_state(MONITOR);
            damage(MONITOR);
        } else {
            m_states.erase(m_states.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
}

void CWorkspaceAnimation::Impl::configure(
    const hyprimate::config::SWorkspaceSettings &requested, bool force) {
    const auto keep_alive = shared_from_this();
    if (!force && !m_settings_changed && m_last_settings == requested && !m_deferred_settings) {
        return;
    }
    const auto settings = requested;
    const auto *previous = m_last_settings ? &*m_last_settings : nullptr;
    bool native_changed = !previous || previous->enabled != settings.enabled ||
                          previous->style != settings.style;
    if (!native_changed) {
        m_styles.for_each([&]<class Style>(auto & /*resources*/) {
            if (settings.style == Style::KIND) {
                native_changed = Style::options(*previous) != Style::options(settings);
            }
        });
    }
    if (m_style_draw_depth) {
        const bool DEFERRED_FORCE = m_deferred_settings && m_deferred_settings->second;
        m_deferred_settings.emplace(settings, force || DEFERRED_FORCE);
        return;
    }
    auto lifecycle = m_lifecycle;
    if (native_changed) {
        lifecycle = ++m_lifecycle;
        if (m_capture_depth) {
            m_turns.clear();
        }
    }
    const double EFFECT_DURATION = settings.effectDuration / 1000.0;
    const bool SAME_EFFECT = previous && previous->enabled &&
                             previous->effectDuration > 0.0 &&
                             previous->effect == settings.effect &&
                             previous->effectParameters == settings.effectParameters;
    const bool MATCHES_PREPARED = m_effect_program &&
                                  m_effect_request.first == settings.effect &&
                                  m_effect_request.second == settings.effectParameters;
    if (!settings.enabled || settings.effect.empty() || !std::isfinite(EFFECT_DURATION) ||
        EFFECT_DURATION <= 0.0) {
        m_effect_program.reset();
        m_effect_duration = 0.0;
        m_effect_request = {};
    } else if (!force && MATCHES_PREPARED) {
        m_effect_duration = EFFECT_DURATION;
    } else if (force || !SAME_EFFECT) {
        std::string error;
        auto effect = hyprimate::shader::prepare_scene_program(
            hyprimate::shader::Event::Workspace, settings.effect, settings.effectParameters, error);
        if (m_lifecycle != lifecycle) {
            return;
        }
        if (effect) {
            m_effect_program = std::move(effect);
            m_effect_duration = EFFECT_DURATION;
            m_effect_request = {settings.effect, settings.effectParameters};
        } else {
            LOG(Log::WARN, "[hyprimate] workspace shader unavailable: {}", error);
        }
    }
    m_settings_changed = m_settings_changed || native_changed || force;
    m_last_settings = settings;
    m_styles.for_each([&]<class Style>(auto &resources) {
        if (settings.style != Style::KIND) {
            return;
        }
        const hyprimate::workspace::ProfileCpuScope CPU{
            {hyprimate::perf::Domain::Workspace, Style::NAME, "transition"},
            hyprimate::perf::Phase::Setup};
        const auto PREVIOUS = resources.settings.last;
        Style::prepare(resources, settings);
        if (m_lifecycle != lifecycle) {
            return;
        }
        const bool ENABLED = hyprimate::workspace::configured_transition_available(
            settings.enabled,
            m_effect_program != nullptr && m_effect_duration > 0.0,
            resources.settings.prepared.duration);
        const bool PRESERVE_LATCHED_SHADER =
            settings.enabled && !ENABLED &&
            (!Style::needs_individual_windows(resources) || m_window_hook) &&
            (!Style::uses_native_hud(resources) || m_layer_hook) && has_latched_shader_turn();
        if (PREVIOUS != resources.settings.last || m_style_kind != Style::KIND ||
            !settings.enabled || (m_turns.enabled() && !ENABLED && !PRESERVE_LATCHED_SHADER)) {
            clear();
            if (m_lifecycle != lifecycle + 1) {
                return;
            }
            lifecycle = m_lifecycle;
        } else if (PRESERVE_LATCHED_SHADER) {
            // Shader-only entries retain their latched program/timing; all others can
            // no longer complete under the native configuration and must fall back.
            expire_unlatched_turns();
        }
        m_travel = Style::travel(resources);
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
        const bool SUPPORTED =
            (!m_individual_windows || m_window_hook) && (!m_native_hud || m_layer_hook);
        if (ENABLED && !SUPPORTED && m_settings_changed) {
            HyprlandAPI::addNotification(m_handle,
                "[hyprimate] Selected workspace options require an unavailable native capture hook",
                CHyprColor(1.F, 0.35F, 0.2F, 1.F),
                10000.F);
        }
        if (m_lifecycle != lifecycle) {
            return;
        }
        m_admit_turns = ENABLED && SUPPORTED;
        // A shader-only setup can become disabled for new turns while a
        // previously admitted pair still needs its registry and capture policy.
        configure_style<Style>(settings.enabled && SUPPORTED, resources);
        if (m_lifecycle != lifecycle) {
            return;
        }
        m_style_warm_pending = m_admit_turns && m_turns.enabled() && !resources.ready;
    });
    if (m_lifecycle == lifecycle && m_admit_turns && m_turns.enabled()) {
        prime_outputs();
    }
}

template <class Style>
void CWorkspaceAnimation::Impl::configure_style(bool enabled, Style::Resources &resources) {
    const auto lifecycle = m_lifecycle;
    if (m_style_kind != Style::KIND) {
        m_invalid_curve.reset();
    }
    m_style_kind = Style::KIND;
    const bool WINDOW_PLANES = hyprimate::workspace::needs_window_planes<Style>(resources);
    m_layered = Style::LAYERED || WINDOW_PLANES;
    m_window_planes = WINDOW_PLANES;
    m_uses_wallpaper = Style::USES_WALLPAPER || WINDOW_PLANES;
    m_native_direction = false;
    if constexpr (requires { Style::USES_NATIVE_DIRECTION; }) {
        m_native_direction = Style::USES_NATIVE_DIRECTION;
    }
    const auto RESOLVE =
        [&](std::string_view requested_curve) -> hyprimate::workspace::Transition::Curve {
        auto &manager = Animation::mgr();
        if (!manager) {
            return {};
        }
        const std::string NAME(requested_curve);
        if constexpr (requires {
                          resources.curves.resolve(NAME, resources.settings.prepared.duration);
                      }) {
            if (auto curve = resources.curves.resolve(NAME, resources.settings.prepared.duration)) {
                if (m_lifecycle != lifecycle) {
                    return {};
                }
                m_invalid_curve.reset();
                return curve;
            }
        }
        const bool KNOWN = manager->bezierExists(NAME);
        if (!KNOWN && (!m_invalid_curve || *m_invalid_curve != NAME)) {
            HyprlandAPI::addNotification(m_handle,
                "[hyprimate] Unknown workspace curve '" + NAME +
                    (m_turns.curve() ? "'; retaining the last valid curve."
                                     : "'; using the native default curve."),
                CHyprColor(1.F, 0.35F, 0.2F, 1.F),
                10000.F);
            if (m_lifecycle != lifecycle) {
                return {};
            }
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
    const double duration =
        m_effect_program ? m_effect_duration : resources.settings.prepared.duration;
    const double registry_duration = std::isfinite(duration) && duration > 0.0
                                         ? duration
                                         : (m_states.empty() ? 0.0 : m_turns.duration());
    m_turns.configure(enabled,
        static_cast<std::uint64_t>(Style::KIND),
        registry_duration,
        resources.settings.prepared.curve,
        m_settings_changed,
        RESOLVE);
    if (m_lifecycle != lifecycle) {
        return;
    }
    m_settings_changed = false;
}

const std::shared_ptr<CWorkspaceAnimation::Impl::MonitorState> *
CWorkspaceAnimation::Impl::state_slot(const PHLMONITOR &monitor) const {
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


bool CWorkspaceAnimation::Impl::ready(const PHLMONITOR &monitor) const {
    return monitor && monitor->m_enabled && monitor->m_output &&
           monitor->m_drmFormat != DRM_FORMAT_INVALID && monitor->m_transformedSize.x > 0 &&
           monitor->m_transformedSize.y > 0 &&
           std::ranges::any_of(
               m_ready_monitors, [&](const auto &weak) { return weak.lock() == monitor; });
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
            const hyprimate::gl::ScopedCompositorContext CONTEXT{};
            if (!CONTEXT) {
                return;
            }
            const hyprimate::workspace::ProfileCpuScope CPU{
                {hyprimate::perf::Domain::Workspace, Style::NAME, "transition"},
                hyprimate::perf::Phase::Setup};
            const hyprimate::perf::GpuScope GPU{
                {hyprimate::perf::Domain::Workspace, Style::NAME, "transition"}};
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
            if (bits.buffer || bits.damage || bits.viewport || bits.transform || bits.scale ||
                bits.offset || bits.alphaModifier) {
                ++tracked->generation;
            }
        });
        args.state->wallpaper_resources.push_back(std::move(resource));
        found = std::prev(args.state->wallpaper_resources.end());
    }
    auto &tracked = **found;
    const CBox GEOMETRY{args.position + offset, child->m_current.size};
    if (tracked.geometry &&
        (tracked.geometry->x != GEOMETRY.x || tracked.geometry->y != GEOMETRY.y ||
            tracked.geometry->width != GEOMETRY.width ||
            tracked.geometry->height != GEOMETRY.height || tracked.alpha != args.alpha ||
            tracked.plane != args.plane)) {
        ++tracked.generation;
    }
    tracked.geometry = GEOMETRY;
    tracked.alpha = args.alpha;
    tracked.plane = args.plane;
    args.identity->push_back(
        {.texture = child->m_current.texture, .generation = tracked.generation});
}

void CWorkspaceAnimation::Impl::capture_wallpaper_identity(MonitorState &entry,
    const PHLMONITOR &monitor,
    std::vector<WallpaperIdentity> &identity,
    bool shell) const {
    identity.clear();
    std::erase_if(
        entry.wallpaper_resources, [](const auto &item) { return item->surface.expired(); });
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


bool CWorkspaceAnimation::Impl::capture(MonitorState &entry,
    const PHLMONITOR &monitor,
    const PHLWORKSPACE &workspace,
    hyprimate::workspace::CaptureKind kind,
    const SP<Render::IFramebuffer> &target,
    bool inactive) {
    const auto KEY = profile_key();
    const hyprimate::workspace::ProfileCpuScope CPU{KEY, hyprimate::perf::Phase::Setup};
    const hyprimate::workspace::ProfileAttribution ATTRIBUTION{KEY};
    // Full-monitor special-workspace effects are baked into native pixels, not this
    // window-only plane. Fall back until both the special workspace and its fade settle.
    if (m_window_planes &&
        !burn_window_planes_allowed(monitor,
            workspace && workspace->type() == Workspace::eWorkspaceType::SPECIAL)) {
        return false;
    }
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
        const bool SELECTED =
            inactive ? Render::sceneSelectsFadeout(Render::eSceneMode::WORKSPACE_WITH_SHELL,
                           SOURCE.type,
                           SOURCE.workspace.lock() == workspace,
                           SOURCE.pinned)
                     : (!fadeout->workspace().lock() || fadeout->workspace().lock() == workspace);
        // Native window fadeouts already contain output-global dim/blur and may
        // span several per-window planes. Only this affected individual scene
        // abstains; composite and aggregate layered scenes keep native ordering.
        if (!entry.capture_policy.accepts_fadeout(SELECTED)) {
            entry.port.diagnostic("individual-fadeout");
            return false;
        }
    }
    // A shader must never see the monitor's potentially stale preblur cache.
    const auto capture_kind = kind == TurnKind::DestinationLive && entry.port.reuse_captures()
                                  ? TurnKind::DestinationIsolated
                                  : kind;
    const auto RESOURCES = hyprimate::workspace::blocks_surface_feedback(capture_kind)
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
                   owner->m_gesture_revision == gesture_revision && current &&
                   current->revision() == revision && safe_scene() &&
                   (!owner->m_native_hud || native_hud_safe(monitor)) &&
                   owner->composition_id(monitor) == composition;
        }
    } data{.owner = this,
        .monitor = monitor,
        .workspace = workspace,
        .inactive = inactive,
        .lifecycle = m_lifecycle,
        .gesture_revision = m_gesture_revision,
        .turn = turn(monitor),
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
    frame.shader_readable = false;
    auto &shell_identity =
        target == entry.source ? entry.source_shell_identity : entry.destination_shell_identity;
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
        m_capture_wallpaper =
            target == entry.source ? entry.source_wallpaper : entry.destination_wallpaper;
    }
    const ScopedCapture CAPTURE_SCOPE{m_capture_depth};
    const bool CAPTURED = hyprimate::workspace::capture_workspace(*g_pHyprRenderer,
        m_begin_render,
        monitor,
        target,
        RESOURCES,
        capture_kind,
        DRAW,
        &data,
        [](void *opaque) { return static_cast<DrawData *>(opaque)->allowed(); });
    // Capture markers are restored by scope guards, also on exceptions.
    release_floating_tail(entry, frame);
    const bool COMPLETE =
        entry.capture_policy.complete({.native_captured = CAPTURED && !entry.capture_failed,
            .wallpaper_captured = entry.wallpaper_captured});
    frame.shader_readable = COMPLETE && RESOURCES->isolated();
    if (!COMPLETE) {
        LOG(Log::DEBUG,
            "[hyprimate] workspace fallback reason=capture-incomplete monitor={} kind={} native={} "
            "pass_failed={} wallpaper={}",
            monitor->m_name,
            static_cast<int>(kind),
            CAPTURED,
            entry.capture_failed,
            entry.wallpaper_captured);
    }
    return COMPLETE;
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
    if (!owner->capture(*entry, MONITOR, composition.workspace.lock(), kind, TARGET)) {
        return false;
    }
    if (reuse_captures()) {
        LOG(Log::DEBUG, "[hyprimate] workspace shader endpoint capture width={} height={}",
            TARGET->m_size.x, TARGET->m_size.y);
    }
    return true;
}
double CWorkspaceAnimation::Impl::TurnPort::duration(
    const hyprimate::workspace::JourneyEndpoint &from,
    const hyprimate::workspace::JourneyEndpoint &destination,
    double base) const {
    if (entry->effect_program) {
        return entry->base_duration;
    }
    double duration = base;
    owner->m_styles.for_each([&]<class Style>(auto &resources) {
        if (Style::KIND == owner->m_style_kind) {
            duration = Style::duration(resources, from, destination);
        }
    });
    return duration;
}

std::optional<std::pair<double, hyprimate::workspace::Transition::Curve>>
CWorkspaceAnimation::Impl::TurnPort::next_pair(
    const hyprimate::workspace::JourneyEndpoint &from,
    const hyprimate::workspace::JourneyEndpoint &destination) const {
    if (!owner->m_admit_turns || !owner->m_turns.enabled()) {
        return std::pair<double, hyprimate::workspace::Transition::Curve>{};
    }
    entry->latch_effect();
    return std::pair{duration(from, destination, entry->base_duration), entry->curve};
}

bool CWorkspaceAnimation::Impl::TurnPort::reuse_captures() const {
    return entry->effect_program && !entry->effect_declined;
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

void CWorkspaceAnimation::Impl::TurnPort::damage() const {
    Impl::damage(entry->monitor.lock());
}

void CWorkspaceAnimation::Impl::TurnPort::landed() const {
    const auto MONITOR = entry->monitor.lock();
    LOG(Log::DEBUG,
        "[hyprimate] workspace landed monitor={}",
        MONITOR ? MONITOR->m_name : "<removed>");
}

void CWorkspaceAnimation::Impl::TurnPort::diagnostic(std::string_view reason) const {
    const auto MONITOR = entry->monitor.lock();
    LOG(Log::DEBUG,
        "[hyprimate] workspace fallback reason={} monitor={} style={}",
        reason,
        MONITOR ? MONITOR->m_name : "<removed>",
        static_cast<std::uint64_t>(owner->m_style_kind));
    if (reason.contains("scene") || reason == "stage-buffers") {
        const auto FACTS = scene_facts();
        LOG(Log::DEBUG,
            "[hyprimate] workspace safety session={} locked={} input={} exclusive={} ime={} "
            "non_window_focus={} protected={} invalid_fadeout={}",
            FACTS.session_available,
            FACTS.locked,
            FACTS.input_available,
            FACTS.exclusive_input,
            FACTS.input_method,
            FACTS.non_window_focus,
            FACTS.protected_content,
            FACTS.invalid_fadeout);
    }
}

std::vector<CWorkspaceAnimation::Impl::Snapshot> CWorkspaceAnimation::Impl::snapshot(
    std::initializer_list<PHLMONITOR> monitors) {
    const auto keep_alive = shared_from_this();
    const auto KEY = profile_key();
    const hyprimate::workspace::ProfileCpuScope CPU{KEY, hyprimate::perf::Phase::Setup};
    const hyprimate::workspace::ProfileAttribution ATTRIBUTION{KEY};
    const auto lifecycle = m_lifecycle;
    std::vector<Snapshot> result;
    result.reserve(monitors.size());
    for (const auto &monitor : monitors) {
        if (std::ranges::any_of(
                result, [&](const auto &item) { return item.monitor == monitor; })) {
            continue;
        }
        if (!ready(monitor) || !monitor->m_activeWorkspace) {
            if (monitor) {
                LOG(Log::DEBUG,
                    "[hyprimate] workspace fallback reason=output-not-ready monitor={}",
                    monitor->m_name);
            }
            continue;
        }
        const bool acquire = turn(monitor) == nullptr;
        if (acquire && !m_admit_turns) {
            continue;
        }
        const auto entry = ensure_state(monitor);
        if (acquire) {
            entry->latch_effect();
        }
        const auto staged = m_turns.get(monitor_key(monitor), entry->port);
        Snapshot item{.monitor = monitor,
            .composition = composition(monitor, true),
            .ready = false,
            .state = entry,
            .turn = staged,
            .revision = staged->revision() + 1,
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
            remember_wallpaper(monitor,
                monitor->m_activeSpecialWorkspace ? monitor->m_activeSpecialWorkspace
                                                  : monitor->m_activeWorkspace,
                entry->source_wallpaper);
        }
        result.push_back(std::move(item));
    }
    return result;
}

void CWorkspaceAnimation::Impl::finish(std::vector<Snapshot> &snapshots) {
    const auto keep_alive = shared_from_this();
    const auto KEY = profile_key();
    const hyprimate::workspace::ProfileCpuScope CPU{KEY, hyprimate::perf::Phase::Setup};
    const hyprimate::workspace::ProfileAttribution ATTRIBUTION{KEY};
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
        if (!m_admit_turns &&
            !transition_destination_allowed(item.monitor, AFTER.identity)) {
            cancel_transition(item.monitor);
            continue;
        }
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
                    animate(workspace,
                        Animation::Workspace::ANIMATION_TYPE_IN,
                        true,
                        true,
                        std::nullopt);
                };
                NORMALIZE(item.monitor->m_activeWorkspace);
                if (!current() || composition_id(item.monitor) != AFTER.identity) {
                    continue;
                }
                NORMALIZE(item.monitor->m_activeSpecialWorkspace);
                if (!current() || composition_id(item.monitor) != AFTER.identity) {
                    continue;
                }
            }
        }
        active_turn->begin(
            item.composition, AFTER, entry->base_duration, entry->curve, item.native_direction);
        if (turn(item.monitor) != active_turn.get() || item.lifecycle != m_lifecycle ||
            active_turn->revision() != item.revision + 1) {
            continue;
        }
        if (item.composition.identity != AFTER.identity) {
            LOG(Log::DEBUG,
                "[hyprimate] workspace request monitor={} before={} after={} accepted={} "
                "presented={}",
                item.monitor->m_name,
                item.composition.identity,
                AFTER.identity,
                active_turn->active(),
                active_turn->presented_once());
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
        if (std::ranges::any_of(
                before, [&](const auto &item) { return item.monitor == monitor; })) {
            continue;
        }
        auto one = snapshot({monitor});
        if (!one.empty()) {
            before.push_back(std::move(one.front()));
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
            owner->m_mutation_snapshots = {};
        }
    };
    const MutationDepth DEPTH{*this};
    m_mutation_snapshots = std::span<Snapshot>{before};
    original();
    m_mutation_snapshots = {};
    finish(before);
}

CWorkspaceAnimation::Impl::Snapshot *CWorkspaceAnimation::Impl::staged_mutation(
    const PHLMONITOR &monitor) {
    const auto FOUND = std::ranges::find_if(
        m_mutation_snapshots, [&](const Snapshot &item) { return item.monitor == monitor; });
    if (FOUND == m_mutation_snapshots.end()) {
        return nullptr;
    }
    const auto *current = turn(monitor);
    if (!hyprimate::workspace::staged_mutation_current({.ready = FOUND->ready,
            .same_lifecycle = FOUND->lifecycle == m_lifecycle,
            .same_turn = FOUND->turn && current == FOUND->turn.get(),
            .same_revision = current != nullptr && current->revision() == FOUND->revision})) {
        return nullptr;
    }
    return &*FOUND;
}

bool CWorkspaceAnimation::Impl::transition_destination_allowed(
    const PHLMONITOR &monitor, std::int64_t identity) {
    const auto *ACTIVE_TURN = turn(monitor);
    return hyprimate::workspace::transition_destination_allowed({.new_pair_admitted =
            m_admit_turns,
        .active_pair = ACTIVE_TURN && ACTIVE_TURN->active(),
        .requested = identity,
        .source = ACTIVE_TURN ? ACTIVE_TURN->source().identity : 0,
        .destination = ACTIVE_TURN ? ACTIVE_TURN->destination().identity : 0});
}

bool CWorkspaceAnimation::Impl::transition_destination_allowed(const PHLMONITOR &monitor,
    const PHLWORKSPACE &normal,
    const PHLWORKSPACE &special) {
    return transition_destination_allowed(monitor, m_compositions.get(normal, special));
}

void CWorkspaceAnimation::Impl::cancel_transition(const PHLMONITOR &monitor) {
    if (monitor && (turn(monitor) != nullptr || state(monitor) != nullptr)) {
        erase_state(monitor);
        damage(monitor);
    }
}

bool CWorkspaceAnimation::Impl::special_workspace_in_native_scene() const {
    return std::ranges::any_of(
        State::Workspace::state()->workspaces(), [](const auto &weak) {
            const auto workspace = weak.lock();
            return workspace && workspace->type() == Workspace::eWorkspaceType::SPECIAL &&
                   hyprimate::workspace::special_workspace_in_native_scene(
                       workspace->m_alpha->value(), workspace->m_alpha->isBeingAnimated());
        });
}

bool CWorkspaceAnimation::Impl::burn_window_planes_allowed(
    const PHLMONITOR &monitor, bool special_request) const {
    if (!m_window_planes) {
        return true;
    }
    const bool ACTIVE_SPECIAL = monitor && monitor->m_activeSpecialWorkspace;
    const float DIM_VALUE =
        monitor && monitor->m_specialDim ? monitor->m_specialDim->value() : 0.F;
    const float DIM_GOAL =
        monitor && monitor->m_specialDim ? monitor->m_specialDim->goal() : 0.F;
    const float BLUR_VALUE =
        monitor && monitor->m_specialBlur ? monitor->m_specialBlur->value() : 0.F;
    const float BLUR_GOAL =
        monitor && monitor->m_specialBlur ? monitor->m_specialBlur->goal() : 0.F;
    const bool SPECIAL_RENDERING = !special_request && !ACTIVE_SPECIAL && DIM_VALUE == 0.F &&
                                   DIM_GOAL == 0.F && BLUR_VALUE == 0.F && BLUR_GOAL == 0.F &&
                                   special_workspace_in_native_scene();
    return hyprimate::workspace::window_plane_capture_allowed({.required = true,
        .active_special = ACTIVE_SPECIAL,
        .special_rendering = SPECIAL_RENDERING,
        .dim_value = DIM_VALUE,
        .dim_goal = DIM_GOAL,
        .blur_value = BLUR_VALUE,
        .blur_goal = BLUR_GOAL,
        .special_request = special_request});
}
void CWorkspaceAnimation::Impl::decline_burn_transition(
    std::initializer_list<PHLMONITOR> monitors) {
    for (const auto &monitor : monitors) {
        cancel_transition(monitor);
    }
}

void CWorkspaceAnimation::Impl::decline_all_burn_transitions() {
    for (const auto &monitor : State::monitorState()->monitors()) {
        cancel_transition(monitor);
    }
}

void CWorkspaceAnimation::Impl::capture_gesture(const PHLMONITOR &monitor) {
    const auto keep_alive = shared_from_this();
    const auto gesture_revision = ++m_gesture_revision;
    if (m_capture_depth != 0 || !m_turns.enabled() || !monitor) {
        return;
    }
    if (m_window_planes && !burn_window_planes_allowed(monitor)) {
        decline_burn_transition({monitor});
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
    return hyprimate::workspace::WorkspaceTurn<TurnPort>::prime_native({.enabled = enabled,
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
    if (std::ranges::none_of(
            m_scanout_inhibited, [&](const auto &weak) { return weak.lock() == monitor; })) {
        m_scanout_inhibited.emplace_back(monitor);
    }
    monitor->m_solitaryClient.reset();
}

void CWorkspaceAnimation::Impl::restore_scanout(const PHLMONITOR &monitor) {
    auto inhibited = std::exchange(m_scanout_inhibited, {});
    for (const auto &weak : inhibited) {
        const auto candidate = weak.lock();
        if (!candidate) {
            continue;
        }
        if (monitor && candidate != monitor) {
            m_scanout_inhibited.push_back(weak);
        } else {
            candidate->recheckSolitary();
        }
    }
}

void CWorkspaceAnimation::Impl::pre_checks(const PHLMONITOR &monitor) {
    if (needs_native_prime(monitor, m_admit_turns && m_turns.enabled()) ||
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
    if (m_window_planes && !burn_window_planes_allowed(monitor)) {
        decline_burn_transition({monitor});
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
    const auto KEY = profile_key();
    const hyprimate::workspace::ProfileCpuScope CPU{KEY, hyprimate::perf::Phase::Update};
    const hyprimate::workspace::ProfileAttribution ATTRIBUTION{KEY};
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
        InventoryPort port{.owner = this,
            .entry = entry.get(),
            .monitor = monitor,
            .turn_identity = active_turn.get(),
            .turn_revision = active_turn->revision(),
            .inventory_revision = m_inventory_revision,
            .composition_identity = current.identity};
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
    if (m_window_planes && !burn_window_planes_allowed(monitor)) {
        decline_burn_transition({monitor});
        return false;
    }
    const auto *slot = state_slot(monitor);
    const auto entry = slot ? *slot : nullptr;
    const auto active_turn = m_turns.pin(monitor_key(monitor));
    if (entry == nullptr || active_turn == nullptr) {
        return false;
    }
    const auto KEY = profile_key();
    const hyprimate::workspace::ProfileCpuScope CPU{KEY, hyprimate::perf::Phase::Update};
    const hyprimate::workspace::ProfileAttribution ATTRIBUTION{KEY};
    entry->source_frame.endpoint = active_turn->source().endpoint();
    entry->destination_frame.endpoint = active_turn->destination().endpoint();
    hyprimate::workspace::WorkspaceFrame frame{.source = entry->source_frame,
        .destination = entry->destination_frame,
        .presentation = entry->presentation,
        .neighbors = entry->neighbors,
        .native_hud = m_native_hud};
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
    if (turn(monitor) != active_turn.get() || active_turn->revision() != revision) {
        return false;
    }
    if (!ticket) {
        if (!active_turn->active()) {
            erase_state(monitor);
        }
        return false;
    }
    sample_resources(KEY, *entry);
    Render::IHyprRenderer::addPassElement(ctx,
        makeUnique<CWorkspacePresentationPass>(ticket,
            monitor,
            std::move(frame),
            hyprimate::workspace::wallpaper_matches<SP<Render::ITexture>>(
                entry->source_wallpaper_identity, entry->destination_wallpaper_identity),
            KEY));
    active_turn->replay_hud([&](hyprimate::workspace::HudReplay step) {
        using hyprimate::workspace::HudReplay;
        const auto revision = active_turn->revision();
        const auto REPLAY = [&](const PHLLSREF &weak, bool popup) {
            if (turn(monitor) != active_turn.get() || active_turn->revision() != revision ||
                !safe_scene() || (m_native_hud && !native_hud_safe(monitor))) {
                return;
            }
            if (const auto SURFACE = weak.lock()) {
                ((*g_pHyprRenderer).*PreviewDrawAccess::LAYER)(
                    ctx, SURFACE, monitor, now, popup, false, false);
            }
        };
        if (step == HudReplay::Blur || step == HudReplay::RestoreBlur) {
            Render::IHyprRenderer::addPassElement(ctx,
                makeUnique<CWorkspaceHudBlurPass>(
                    active_turn, monitor, step == HudReplay::RestoreBlur));
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


void CWorkspaceAnimation::Impl::remove_monitor(const PHLMONITOR &monitor) {
    if (!monitor) {
        return;
    }
    ++m_inventory_revision;  // Other monitors can hold previews from this output.
    std::erase_if(m_ready_monitors,
        [&](const auto &weak) { return weak.expired() || weak.lock() == monitor; });
    m_observed.remove_monitor(PHLMONITORREF{monitor});
    erase_state(monitor);
}

void CWorkspaceAnimation::Impl::clear() {
    m_turns.reload();
    ++m_lifecycle;
    ++m_gesture_revision;
    const auto states = std::exchange(m_states, {});
    m_mutation_snapshots = {};
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
CWorkspaceAnimation::~CWorkspaceAnimation() {
    m_impl->shutdown();
}
bool CWorkspaceAnimation::install(std::string &error) {
    const auto impl = m_impl;
    return impl->install(error);
}
void CWorkspaceAnimation::shutdown() {
    const auto impl = m_impl;
    impl->shutdown();
}
void CWorkspaceAnimation::configure(
    const hyprimate::config::SWorkspaceSettings &settings, bool force) {
    const auto impl = m_impl;
    impl->configure(settings, force);
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
