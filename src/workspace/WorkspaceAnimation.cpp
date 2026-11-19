#include <algorithm>
#include <array>
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
#include "workspace/WorkspaceIdentity.hpp"
#include "workspace/WorkspaceStyleList.hpp"

namespace {
    using Clock = std::chrono::steady_clock;
    using TurnKind = hyprimate::workspace::CaptureKind;
    using Direction = hyprimate::workspace::Direction;

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
        const auto id = workspace->numberedID();
        return id;
    }

}  // namespace

class CWorkspaceAnimation::Impl {
  public:
    explicit Impl(HANDLE handle) : m_handle(handle) {
    }
    ~Impl() {
        shutdown();
    }

    using WallpaperIdentity = hyprimate::workspace::WallpaperIdentity<SP<Render::ITexture>>;
    struct WallpaperResource {
        WP<CWLSurfaceResource> surface;
        std::uint64_t generation = 0;
        CHyprSignalListener commit;
    };

    struct MonitorState;
    struct TurnPort {
        using Workspace = PHLWORKSPACEREF;
        Impl *owner;
        MonitorState *entry;
        bool buffers(bool active);
        bool capture(TurnKind kind, const hyprimate::workspace::TurnComposition<Workspace> &composition);
        void present_native();
        void swap_slots();
        void damage();
    };
    struct MonitorState {
        explicit MonitorState(Impl *owner) : port{owner, this} {}
        PHLMONITORREF monitor;
        SP<Render::IFramebuffer> source;
        SP<Render::IFramebuffer> destination;
        SP<Render::IFramebuffer> presentation;
        SP<Render::IFramebuffer> sourceWallpaper;
        SP<Render::IFramebuffer> destinationWallpaper;
        SP<Render::IFramebuffer> opaqueBackdrop;
        std::vector<WallpaperIdentity> sourceWallpaperIdentity;
        std::vector<WallpaperIdentity> destinationWallpaperIdentity;
        std::vector<std::unique_ptr<WallpaperResource>> wallpaperResources;
        bool wallpaperCaptured = false;
        SP<Render::CSceneResources> sourceResources;
        Clock::time_point last = Clock::now();
        TurnPort port;
    };

    using Composition = hyprimate::workspace::TurnComposition<PHLWORKSPACEREF>;
    struct Snapshot {
        PHLMONITOR monitor;
        Composition composition;
        bool ready = false;
    };
    Composition composition(const PHLMONITOR &monitor) {
        return {.identity = composition_id(monitor), .workspace = monitor->m_activeWorkspace,
            .numbered = numbered(monitor->m_activeWorkspace),
            .special = monitor->m_activeSpecialWorkspace != nullptr};
    }
    std::int64_t composition_id(const PHLMONITOR &monitor) {
        return monitor ? m_compositions.get(monitor->m_activeWorkspace, monitor->m_activeSpecialWorkspace) : 0;
    }


    bool install(std::string &error);
    void shutdown();
    void configure(bool enabled, std::string_view style, double duration,
        const hyprimate::workspace::GapFill &gap, std::string_view curveName);
    void configure(const hyprimate::config::SWorkspaceSettings &settings);
    void configure_style(bool enabled, hyprimate::config::WorkspaceStyle kind, bool wallpaper,
        double duration, std::string_view curveName);
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

    void *original(CFunctionHook *hook) const {
        return hook ? hook->m_original : nullptr;
    }
    RenderFn render_original() const {
        return reinterpret_cast<RenderFn>(original(m_renderHook));
    }
    DragFn drag_original() const {
        return reinterpret_cast<DragFn>(original(m_dragHook));
    }

    static Impl *s_active;

  public:
    static std::uintptr_t monitor_key(const PHLMONITOR &monitor) {
        return reinterpret_cast<std::uintptr_t>(monitor.get());
    }
    hyprimate::workspace::WorkspaceTurn<TurnPort> *turn(const PHLMONITOR &monitor) {
        return monitor ? m_turns.find(monitor_key(monitor)) : nullptr;
    }
    MonitorState *state(const PHLMONITOR &monitor);
    MonitorState &ensure_state(const PHLMONITOR &monitor);
    bool ensure_buffers(MonitorState &state, const PHLMONITOR &monitor);
    bool capture(MonitorState &state,
        const PHLMONITOR &monitor,
        const PHLWORKSPACE &workspace,
        hyprimate::workspace::CaptureKind kind,
        const SP<Render::IFramebuffer> &target);
    void erase_state(const PHLMONITOR &monitor);
    void damage(const PHLMONITOR &monitor) const;

    HANDLE m_handle = nullptr;
    bool m_usesWallpaper = false;
    bool m_settingsChanged = true;
    std::optional<hyprimate::config::SWorkspaceSettings> m_lastSettings;
    std::optional<std::string> m_invalidCurve;
    CHyprSignalListener m_wallpaperListener;
    MonitorState *m_capturing = nullptr;
    SP<Render::IFramebuffer> m_captureWallpaper;
    int m_captureDepth = 0;
    int m_mutationDepth = 0;
    std::vector<PHLMONITORREF> m_suppressed;
    PHLMONITORREF m_gestureMonitor;
    std::vector<std::unique_ptr<MonitorState>> m_states;
    hyprimate::workspace::WorkspaceTurns<std::uintptr_t, TurnPort> m_turns;
    hyprimate::workspace::CompositionIdentities<PHLWORKSPACE, PHLWORKSPACEREF> m_compositions;
    hyprimate::config::WorkspaceStyle m_styleKind{};
    hyprimate::animation::StyleResources<hyprimate::workspace::WorkspaceStyles> m_styles;
    hyprimate::workspace::BeginRender m_beginRender = nullptr;

    CFunctionHook *m_changeHook = nullptr;
    CFunctionHook *m_specialHook = nullptr;
    CFunctionHook *m_moveHook = nullptr;
    CFunctionHook *m_swapHook = nullptr;
    CFunctionHook *m_animateHook = nullptr;
    CFunctionHook *m_renderHook = nullptr;
    CFunctionHook *m_dragHook = nullptr;
    CFunctionHook *m_blurHook = nullptr;
    CFunctionHook *m_gestureBeginHook = nullptr;
    CFunctionHook *m_gestureEndHook = nullptr;
    CFunctionHook *m_gestureCancelHook = nullptr;
};

namespace {
    CWorkspaceAnimation::Impl *active() {
        return CWorkspaceAnimation::Impl::s_active;
    }

    template <typename Fn>
    Fn original(CFunctionHook *hook) {
        return reinterpret_cast<Fn>(hook ? hook->m_original : nullptr);
    }
    // Coverage alpha belongs to the captured scene, not the native blur input.
    bool copy_opaque(const SP<Render::IFramebuffer> &source,
        const SP<Render::IFramebuffer> &target) {
        if (!source || !target || source == target || !source->isAllocated() ||
            !target->isAllocated() || source->m_size != target->m_size)
            return false;
        GLint read = 0, draw = 0;
        std::array<GLboolean, 4> mask{};
        std::array<GLfloat, 4> clear{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear.data());
        const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,
            static_cast<Render::GL::CGLFramebuffer *>(source.get())->getFBID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
            static_cast<Render::GL::CGLFramebuffer *>(target.get())->getFBID());
        const bool complete = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
                              glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (complete) {
            const auto width = static_cast<GLint>(source->m_size.x);
            const auto height = static_cast<GLint>(source->m_size.y);
            glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
            glClearColor(0.F, 0.F, 0.F, 1.F);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glColorMask(mask[0], mask[1], mask[2], mask[3]);
        glClearColor(clear[0], clear[1], clear[2], clear[3]);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
        if (scissor) glEnable(GL_SCISSOR_TEST);
        return complete;
    }

    SP<Render::IFramebuffer> hook_blur(Render::IHyprRenderer *renderer,
        Render::CRenderContext &ctx, SP<Render::IFramebuffer> source, float strength,
        const CRegion &damage, const Render::SBlurContext &context) {
        const auto *manager = active();
        auto fn = original<BlurFn>(manager ? manager->m_blurHook : nullptr);
        if (!fn) return nullptr;
        if (manager && manager->m_capturing && manager->m_capturing->wallpaperCaptured &&
            source && source->m_size == manager->m_capturing->source->m_size) {
            const auto backdrop = manager->m_capturing->opaqueBackdrop;
            if (!copy_opaque(source, backdrop)) {
                manager->m_capturing->wallpaperCaptured = false;
                return nullptr;
            }
            return fn(renderer, ctx, backdrop, strength, damage, context);
        }
        return fn(renderer, ctx, std::move(source), strength, damage, context);
    }

    void hook_change(Monitor::CMonitor *self,
        const PHLWORKSPACE &workspace,
        bool internal,
        bool noMouseMove,
        bool noFocus) {
        auto *manager = active();
        auto fn = original<ChangeFn>(manager ? manager->m_changeHook : nullptr);
        if (!fn) return;
        if (!manager || !manager->m_turns.enabled()) {
            fn(self, workspace, internal, noMouseMove, noFocus);
            return;
        }
        const auto monitor = self->m_self.lock();
        manager->mutate({monitor}, [&] { fn(self, workspace, internal, noMouseMove, noFocus); });
    }

    void hook_special(Monitor::CMonitor *self, const PHLWORKSPACE &workspace, bool noFocus) {
        auto *manager = active();
        auto fn = original<SpecialFn>(manager ? manager->m_specialHook : nullptr);
        if (!fn) return;
        if (!manager || !manager->m_turns.enabled()) {
            fn(self, workspace, noFocus);
            return;
        }
        const auto monitor = self->m_self.lock();
        manager->mutate({monitor}, [&] { fn(self, workspace, noFocus); });
    }

    void hook_move(const State::Workspace::CPlacementController *self,
        PHLWORKSPACE workspace,
        PHLMONITOR monitor,
        bool noWarp,
        bool carryFocus,
        bool replace) {
        auto *manager = active();
        auto fn = original<MoveFn>(manager ? manager->m_moveHook : nullptr);
        if (!fn) return;
        if (!manager || !manager->m_turns.enabled()) {
            fn(self, workspace, monitor, noWarp, carryFocus, replace);
            return;
        }
        const auto oldMonitor = workspace ? workspace->m_monitor.lock() : PHLMONITOR{};
        manager->mutate({oldMonitor, monitor},
            [&] { fn(self, workspace, monitor, noWarp, carryFocus, replace); });
    }

    void hook_swap(
        const State::Workspace::CPlacementController *self, PHLMONITOR first, PHLMONITOR second) {
        auto *manager = active();
        auto fn = original<SwapFn>(manager ? manager->m_swapHook : nullptr);
        if (!fn) return;
        if (!manager || !manager->m_turns.enabled()) {
            fn(self, first, second);
            return;
        }
        manager->mutate({first, second}, [&] { fn(self, first, second); });
    }

    void hook_animate(PHLWORKSPACE workspace,
        Animation::Workspace::eAnimationType type,
        bool left,
        bool instant,
        std::optional<std::string> style) {
        const auto *manager = active();
        auto fn = original<AnimateFn>(manager ? manager->m_animateHook : nullptr);
        if (!fn) return;
        // Still let Hyprland establish its incoming/outgoing workspace state and callbacks.
        // Suppressing the call entirely leaves previously hidden workspaces transparent or
        // offset; instant completion avoids a competing native animation.
        const bool suppressed =
            manager && manager->suppresses(workspace ? workspace->m_monitor.lock() : PHLMONITOR{});
        fn(workspace, type, left, instant || suppressed, std::move(style));
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
        auto fn = manager ? manager->render_original() : nullptr;
        if (!fn) return;
        if (manager && mode == Render::eSceneMode::MONITOR && manager->queue(ctx, monitor, now)) {
            if (auto drag = manager->drag_original()) drag(renderer, ctx, monitor, now);
            return;
        }
        fn(renderer, ctx, monitor, workspace, now, mode, translate, scale);
    }

    void hook_drag(Render::IHyprRenderer *renderer,
        Render::CRenderContext &ctx,
        PHLMONITOR monitor,
        const Time::steady_tp &now) {
        const auto *manager = active();
        auto fn = manager ? manager->drag_original() : nullptr;
        if (!fn || (manager && manager->m_captureDepth > 0)) return;
        fn(renderer, ctx, monitor, now);
    }

    bool hook_gesture_begin(CUnifiedWorkspaceSwipeGesture *gesture, PHLMONITOR monitor) {
        auto *manager = active();
        auto fn = original<GestureBeginFn>(manager ? manager->m_gestureBeginHook : nullptr);
        if (!fn) return false;
        if (manager && manager->m_turns.enabled()) manager->capture_gesture(monitor);
        const bool begun = fn(gesture, monitor);
        if (!begun && manager) manager->clear_gesture();
        return begun;
    }

    void hook_gesture_end(CUnifiedWorkspaceSwipeGesture *gesture) {
        auto *manager = active();
        auto fn = original<GestureVoidFn>(manager ? manager->m_gestureEndHook : nullptr);
        if (fn) fn(gesture);
        if (manager) manager->clear_gesture();
    }

    void hook_gesture_cancel(CUnifiedWorkspaceSwipeGesture *gesture) {
        auto *manager = active();
        auto fn = original<GestureVoidFn>(manager ? manager->m_gestureCancelHook : nullptr);
        if (fn) fn(gesture);
        if (manager) manager->clear_gesture();
    }

    // Preserve native backdrop RGB for blur and glass effects, while recording foreground
    // coverage in alpha. The final shader can remove the wallpaper contribution exactly.
    class CWorkspaceWallpaperPass final : public IPassElement {
      public:
        CWorkspaceWallpaperPass(SP<Render::IFramebuffer> target, bool &captured)
            : m_target(std::move(target)), m_captured(captured) {}

        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            const auto source = ctx.m_data.currentFB;
            if (!source || !m_target || source == m_target ||
                source->m_size != m_target->m_size)
                return {};
            GLint read = 0, draw = 0;
            std::array<GLboolean, 4> mask{};
            std::array<GLfloat, 4> clear{};
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
            glGetBooleanv(GL_COLOR_WRITEMASK, mask.data());
            glGetFloatv(GL_COLOR_CLEAR_VALUE, clear.data());
            const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
            glDisable(GL_SCISSOR_TEST);
            const auto sourceID = static_cast<Render::GL::CGLFramebuffer *>(source.get())->getFBID();
            glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceID);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                static_cast<Render::GL::CGLFramebuffer *>(m_target.get())->getFBID());
            const auto width = static_cast<GLint>(source->m_size.x);
            const auto height = static_cast<GLint>(source->m_size.y);
            glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sourceID);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
            glClearColor(0.F, 0.F, 0.F, 0.F);
            glClear(GL_COLOR_BUFFER_BIT);
            glColorMask(mask[0], mask[1], mask[2], mask[3]);
            glClearColor(clear[0], clear[1], clear[2], clear[3]);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw));
            if (scissor) glEnable(GL_SCISSOR_TEST);
            m_captured = true;
            return {};
        }
        bool needsLiveBlur(Render::CRenderContext &) override { return false; }
        bool needsPrecomputeBlur(Render::CRenderContext &) override { return false; }
        bool undiscardable(Render::CRenderContext &) override { return true; }
        bool disableSimplification(Render::CRenderContext &) override { return true; }
        bool requiresFullDamage(Render::CRenderContext &) override { return true; }
        const char *passName() override { return "hyprimate-workspace-wallpaper"; }
        ePassElementType type() override { return EK_CUSTOM; }

      private:
        SP<Render::IFramebuffer> m_target;
        bool &m_captured;
    };

    class CWorkspacePresentationPass final : public IPassElement {
      public:
        CWorkspacePresentationPass(CWorkspaceAnimation::Impl *owner,
            PHLMONITOR monitor,
            SP<Render::IFramebuffer> source,
            SP<Render::IFramebuffer> destination,
            SP<Render::IFramebuffer> presentation,
            SP<Render::IFramebuffer> sourceWallpaper,
            SP<Render::IFramebuffer> destinationWallpaper,
            bool matchingWallpaper)
            : m_owner(owner),
              m_monitor(std::move(monitor)),
              m_source(std::move(source)),
              m_destination(std::move(destination)),
              m_presentation(std::move(presentation)),
              m_sourceWallpaper(std::move(sourceWallpaper)),
              m_destinationWallpaper(std::move(destinationWallpaper)),
              m_matchingWallpaper(matchingWallpaper) {
        }

        std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
            auto *turn = m_owner ? m_owner->turn(m_monitor) : nullptr;
            if (!turn) {
                copy_opaque(m_destination, m_presentation);
                return {};
            }
            turn->draw([&](double progress, Direction direction, bool matching) {
                bool drawn = false;
                m_owner->m_styles.for_each([&]<class Style>(auto &resources) {
                    if (Style::kind == m_owner->m_styleKind && resources.ready)
                        drawn = Style::draw(resources, ctx, m_source, m_destination, m_presentation,
                            m_sourceWallpaper, m_destinationWallpaper, progress, direction, matching);
                });
                return drawn;
            }, m_matchingWallpaper);
            if (!turn->active()) m_owner->erase_state(m_monitor);
            return {};
        }
        bool needsLiveBlur(Render::CRenderContext &) override {
            return false;
        }
        bool needsPrecomputeBlur(Render::CRenderContext &) override {
            return false;
        }
        bool undiscardable(Render::CRenderContext &) override {
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
        SP<Render::IFramebuffer> m_source;
        SP<Render::IFramebuffer> m_destination;
        SP<Render::IFramebuffer> m_presentation;
        SP<Render::IFramebuffer> m_sourceWallpaper;
        SP<Render::IFramebuffer> m_destinationWallpaper;
        bool m_matchingWallpaper;
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
    const std::array specs{
        Spec{CHANGE_NAME,
            CHANGE_SIGNATURE,
            reinterpret_cast<const void *>(&hook_change),
            &m_changeHook},
        Spec{SPECIAL_NAME,
            SPECIAL_SIGNATURE,
            reinterpret_cast<const void *>(&hook_special),
            &m_specialHook},
        Spec{MOVE_NAME, MOVE_SIGNATURE, reinterpret_cast<const void *>(&hook_move), &m_moveHook},
        Spec{SWAP_NAME, SWAP_SIGNATURE, reinterpret_cast<const void *>(&hook_swap), &m_swapHook},
        Spec{ANIMATE_NAME,
            ANIMATE_SIGNATURE,
            reinterpret_cast<const void *>(&hook_animate),
            &m_animateHook},
        Spec{RENDER_NAME,
            RENDER_SIGNATURE,
            reinterpret_cast<const void *>(&hook_render),
            &m_renderHook},
        Spec{DRAG_NAME, DRAG_SIGNATURE, reinterpret_cast<const void *>(&hook_drag), &m_dragHook},
        Spec{"blurFramebuffer", BLUR_SIGNATURE, reinterpret_cast<const void *>(&hook_blur), &m_blurHook},
        Spec{GESTURE_BEGIN_NAME,
            GESTURE_BEGIN_SIGNATURE,
            reinterpret_cast<const void *>(&hook_gesture_begin),
            &m_gestureBeginHook},
        Spec{GESTURE_END_NAME,
            GESTURE_END_SIGNATURE,
            reinterpret_cast<const void *>(&hook_gesture_end),
            &m_gestureEndHook},
        Spec{GESTURE_CANCEL_NAME,
            GESTURE_CANCEL_SIGNATURE,
            reinterpret_cast<const void *>(&hook_gesture_cancel),
            &m_gestureCancelHook},
    };
    m_beginRender = reinterpret_cast<hyprimate::workspace::BeginRender>(exact_symbol(
        HyprlandAPI::findFunctionsByName(m_handle, BEGIN_RENDER_NAME), BEGIN_RENDER_SIGNATURE));
    if (!m_beginRender) {
        error = "workspace beginRender symbol not found by exact signature";
        return false;
    }
    for (const auto &spec : specs) {
        const auto address =
            exact_symbol(HyprlandAPI::findFunctionsByName(m_handle, spec.name), spec.signature);
        if (!address) {
            error = std::string("workspace symbol not found: ") + spec.name;
            shutdown();
            return false;
        }
        *spec.slot = HyprlandAPI::createFunctionHook(m_handle, address, spec.replacement);
        if (!*spec.slot) {
            error = std::string("workspace hook creation failed: ") + spec.name;
            shutdown();
            return false;
        }
    }
    s_active = this;
    for (const auto &spec : specs) {
        if (!(*spec.slot)->hook()) {
            error = std::string("workspace hook install failed: ") + spec.name;
            shutdown();
            return false;
        }
    }
    m_wallpaperListener = Event::bus()->m_events.render.stage.listen(
        [this](const Event::SRenderStageEvent &event) {
            if (event.stage != RENDER_POST_WALLPAPER || !event.context ||
                !m_capturing || !m_captureWallpaper || m_captureDepth == 0)
                return;
            Render::IHyprRenderer::addPassElement(event.context->get(),
                makeUnique<CWorkspaceWallpaperPass>(m_captureWallpaper, m_capturing->wallpaperCaptured));
        });
    return true;
}

void CWorkspaceAnimation::Impl::shutdown() {
    m_wallpaperListener.reset();
    const std::array hooks{m_gestureCancelHook,
        m_gestureEndHook,
        m_gestureBeginHook,
        m_dragHook,
        m_blurHook,
        m_renderHook,
        m_animateHook,
        m_swapHook,
        m_moveHook,
        m_specialHook,
        m_changeHook};
    for (auto *hook : hooks)
        if (hook) HyprlandAPI::removeFunctionHook(m_handle, hook);
    m_changeHook = m_specialHook = m_moveHook = m_swapHook = m_animateHook = nullptr;
    m_renderHook = m_dragHook = m_gestureBeginHook = m_gestureEndHook = m_gestureCancelHook =
        nullptr;
    m_blurHook = nullptr;
    s_active = nullptr;
    clear();
    if (Render::GL::g_pHyprOpenGL)
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    m_styles.for_each([]<class Style>(auto &resources) {
        if (resources.ready && Render::GL::g_pHyprOpenGL) resources.renderer.release();
        resources.ready = false;
    });
    m_beginRender = nullptr;
}

void CWorkspaceAnimation::Impl::configure(const hyprimate::config::SWorkspaceSettings &settings) {
    if (!m_lastSettings || *m_lastSettings != settings) {
        m_settingsChanged = true;
        m_lastSettings = settings;
    }
    m_styles.for_each([&]<class Style>(auto &resources) {
        if (settings.style != Style::kind) return;
        Style::prepare(resources, settings);
        const auto &prepared = resources.settings.prepared;
        configure_style(settings.enabled, Style::kind, Style::usesWallpaper, prepared.duration, prepared.curve);
    });
}

void CWorkspaceAnimation::Impl::configure(bool enabled, std::string_view style, double duration,
    const hyprimate::workspace::GapFill &gap, std::string_view curveName) {
    bool selected = false;
    m_styles.for_each([&]<class Style>(auto &resources) {
        if constexpr (requires { Style::legacyName; Style::setLegacyGap(resources, gap); }) {
            if (style != Style::legacyName) return;
            selected = true;
            m_settingsChanged = Style::setLegacyGap(resources, gap) || m_settingsChanged;
            configure_style(enabled, Style::kind, Style::usesWallpaper, duration, curveName);
        }
    });
    if (!selected) configure_style(false, m_styleKind, m_usesWallpaper, duration, curveName);
}

void CWorkspaceAnimation::Impl::configure_style(bool enabled,
    hyprimate::config::WorkspaceStyle kind, bool wallpaper, double duration, std::string_view curveName) {
    m_styleKind = kind;
    m_usesWallpaper = wallpaper;
    const auto resolve = [this](std::string_view curveName) -> hyprimate::workspace::Transition::Curve {
        auto &manager = Animation::mgr();
        if (!manager) return {};
        const std::string name(curveName);
        const bool known = manager->bezierExists(name);
        if (!known && (!m_invalidCurve || *m_invalidCurve != name)) {
            HyprlandAPI::addNotification(m_handle,
                "[hyprimate] Unknown workspace curve '" + name +
                    "'; retaining the last valid curve.",
                CHyprColor(1.F, 0.35F, 0.2F, 1.F), 10000.F);
            m_invalidCurve = name;
        }
        if (!known && m_turns.curve()) return m_turns.curve();
        const auto curve = manager->getBezier(
            known ? name : manager->bezierExists("default") ? "default" : "linear");
        if (!curve) return {};
        if (known) m_invalidCurve.reset();
        return [curve](double t) {
            return static_cast<double>(curve->getYForPoint(static_cast<float>(t)));
        };
    };
    m_turns.configure(enabled, static_cast<std::uint64_t>(kind), duration, curveName,
        m_settingsChanged, resolve);
    m_settingsChanged = false;
}

CWorkspaceAnimation::Impl::MonitorState *CWorkspaceAnimation::Impl::state(
    const PHLMONITOR &monitor) {
    const auto it = std::ranges::find_if(
        m_states, [&](const auto &entry) { return entry->monitor.lock() == monitor; });
    return it == m_states.end() ? nullptr : it->get();
}

CWorkspaceAnimation::Impl::MonitorState &CWorkspaceAnimation::Impl::ensure_state(
    const PHLMONITOR &monitor) {
    if (auto *existing = state(monitor)) return *existing;
    auto created = std::make_unique<MonitorState>(this);
    created->monitor = monitor;
    m_states.push_back(std::move(created));
    return *m_states.back();
}

bool CWorkspaceAnimation::Impl::ensure_buffers(MonitorState &entry, const PHLMONITOR &monitor) {
    if (!g_pHyprRenderer || !monitor || !monitor->resources() ||
        monitor->m_transformedSize.x <= 0 || monitor->m_transformedSize.y <= 0)
        return false;
    const auto size = monitor->m_transformedSize;
    const auto usable = [&](const SP<Render::IFramebuffer> &fb) {
        return fb && fb->isAllocated() && fb->m_size == size && fb->getTexture();
    };
    if (!usable(entry.source) || !usable(entry.destination) || !usable(entry.presentation) ||
        (m_usesWallpaper && (!usable(entry.sourceWallpaper) || !usable(entry.destinationWallpaper) ||
            !usable(entry.opaqueBackdrop)))) {
        // Never replace a frozen pair during a turn. Floating point alpha retains coverage
        // independently of output format (including XRGB and HDR) without RGB quantization loss.
        if (const auto *activeTurn = turn(monitor); activeTurn && activeTurn->active()) return false;
        const auto reference = monitor->resources()->getUnusedWorkBuffer(size);
        if (!usable(reference)) return false;
        const auto allocate = [&]() -> SP<Render::IFramebuffer> {
            // A null image description is the native unmanaged-colour convention.
            // The explicit-format pool overload rejects it, so own these retained targets.
            auto fb = g_pHyprRenderer->createFB("hyprimate workspace capture");
            if (!fb || !fb->alloc(static_cast<int>(size.x), static_cast<int>(size.y),
                           DRM_FORMAT_ABGR16161616F))
                return nullptr;
            fb->setImageDescription(reference->imageDescription());
            return fb;
        };
        entry.source = allocate();
        entry.destination = allocate();
        entry.presentation = allocate();
        if (m_usesWallpaper) {
            entry.sourceWallpaper = allocate();
            entry.destinationWallpaper = allocate();
            entry.opaqueBackdrop = allocate();
        }
        entry.sourceResources.reset();
    }
    if (!usable(entry.source) || !usable(entry.destination) || !usable(entry.presentation) ||
        (m_usesWallpaper && (!usable(entry.sourceWallpaper) || !usable(entry.destinationWallpaper) ||
            !usable(entry.opaqueBackdrop))))
        return false;
    if (!entry.sourceResources) {
        const auto blur = g_pHyprRenderer->createFB("hyprimate workspace source blur");
        entry.sourceResources = makeShared<Render::CSceneResources>(blur,
            Render::SSceneBufferDescription{.format = entry.source->m_drmFormat,
                .imageDescription = entry.source->imageDescription()});
    }
    bool ready = false;
    m_styles.for_each([&]<class Style>(auto &resources) {
        if (Style::kind != m_styleKind) return;
        if (!resources.ready) {
            const auto backend = g_pHyprRenderer->glBackend();
            if (!backend) return;
            backend->makeEGLCurrent();
            resources.ready = resources.renderer.initialize();
        }
        ready = resources.ready;
    });
    return ready;
}

bool CWorkspaceAnimation::Impl::capture(MonitorState &entry,
    const PHLMONITOR &monitor,
    const PHLWORKSPACE &workspace,
    hyprimate::workspace::CaptureKind kind,
    const SP<Render::IFramebuffer> &target) {
    if (!ensure_buffers(entry, monitor)) return false;
    const auto resources = kind == hyprimate::workspace::CaptureKind::SourceIsolated
                               ? entry.sourceResources
                               : monitor->resources()->sceneResources();
    struct DrawData {
        Impl *owner;
        PHLMONITOR monitor;
        PHLWORKSPACE workspace;
    } data{this, monitor, workspace};
    const auto draw = [](void *opaque, Render::CRenderContext &ctx) {
        const auto &args = *static_cast<DrawData *>(opaque);
        const auto original = args.owner->render_original();
        if (!original || !g_pHyprRenderer) return;
        struct CaptureDepth {
            Impl &owner;
            explicit CaptureDepth(Impl &value) : owner(value) {
                ++owner.m_captureDepth;
            }
            ~CaptureDepth() {
                --owner.m_captureDepth;
            }
        };
        const CaptureDepth depth{*args.owner};
        original(g_pHyprRenderer.get(),
            ctx,
            args.monitor,
            args.workspace,
            Time::steadyNow(),
            Render::eSceneMode::MONITOR,
            {},
            1.F);
    };
    auto &identity = target == entry.source ? entry.sourceWallpaperIdentity
                                            : entry.destinationWallpaperIdentity;
    identity.clear();
    std::erase_if(entry.wallpaperResources, [](const auto &item) { return item->surface.expired(); });
    struct IdentityData {
        MonitorState &state;
        std::vector<WallpaperIdentity> &identity;
    } identityData{entry, identity};
    if (m_usesWallpaper) for (const auto &weak : monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]) {
        const auto layer = weak.lock();
        const auto surface = layer && layer->mapped() ? layer->resource() : nullptr;
        if (!surface) continue;
        surface->breadthfirst([](SP<CWLSurfaceResource> child, const Vector2D &, void *opaque) {
            auto &args = *static_cast<IdentityData *>(opaque);
            if (!child->m_current.texture) return;
            auto found = std::ranges::find_if(args.state.wallpaperResources,
                [&](const auto &item) { return item->surface.lock() == child; });
            if (found == args.state.wallpaperResources.end()) {
                auto resource = std::make_unique<WallpaperResource>();
                resource->surface = child;
                auto *tracked = resource.get();
                resource->commit = child->m_events.commit.listen([tracked] {
                    const auto surface = tracked->surface.lock();
                    if (!surface) return;
                    const auto &bits = surface->m_current.updated.bits;
                    // SHM surfaces reuse their texture object; identity must include content
                    // commits, but not frame callbacks or input-only state changes.
                    if (bits.buffer || bits.damage || bits.viewport || bits.transform ||
                        bits.scale || bits.offset || bits.alphaModifier)
                        ++tracked->generation;
                });
                args.state.wallpaperResources.push_back(std::move(resource));
                found = std::prev(args.state.wallpaperResources.end());
            }
            args.identity.push_back({child->m_current.texture, (*found)->generation});
        }, &identityData);
    }
    entry.wallpaperCaptured = false;
    m_capturing = m_usesWallpaper ? &entry : nullptr;
    m_captureWallpaper = m_usesWallpaper ?
        (target == entry.source ? entry.sourceWallpaper : entry.destinationWallpaper) : nullptr;
    const bool captured = hyprimate::workspace::captureWorkspace(
        *g_pHyprRenderer, m_beginRender, monitor, target, resources, kind, draw, &data);
    m_capturing = nullptr;
    m_captureWallpaper.reset();
    return captured && (!m_usesWallpaper || entry.wallpaperCaptured);
}

bool CWorkspaceAnimation::Impl::TurnPort::buffers(bool) {
    return owner->ensure_buffers(*entry, entry->monitor.lock());
}
bool CWorkspaceAnimation::Impl::TurnPort::capture(
    TurnKind kind, const hyprimate::workspace::TurnComposition<Workspace> &composition) {
    const auto monitor = entry->monitor.lock();
    const auto target = kind == TurnKind::SourceIsolated ? entry->source : entry->destination;
    return owner->capture(*entry, monitor, composition.workspace.lock(), kind, target);
}
void CWorkspaceAnimation::Impl::TurnPort::present_native() {
    copy_opaque(entry->destination, entry->presentation);
}
void CWorkspaceAnimation::Impl::TurnPort::swap_slots() {
    std::swap(entry->source, entry->destination);
    std::swap(entry->sourceWallpaper, entry->destinationWallpaper);
    std::swap(entry->sourceWallpaperIdentity, entry->destinationWallpaperIdentity);
}
void CWorkspaceAnimation::Impl::TurnPort::damage() {
    owner->damage(entry->monitor.lock());
}

std::vector<CWorkspaceAnimation::Impl::Snapshot> CWorkspaceAnimation::Impl::snapshot(
    std::initializer_list<PHLMONITOR> monitors) {
    std::vector<Snapshot> result;
    result.reserve(monitors.size());
    for (const auto &monitor : monitors) {
        if (!monitor ||
            std::ranges::any_of(result, [&](const auto &item) { return item.monitor == monitor; }))
            continue;
        auto &entry = ensure_state(monitor);
        Snapshot item{.monitor = monitor, .composition = composition(monitor)};
        item.ready = m_turns.get(monitor_key(monitor), entry.port).stage(item.composition);
        if (!item.ready) erase_state(monitor);
        result.push_back(std::move(item));
    }
    return result;
}

void CWorkspaceAnimation::Impl::finish(std::vector<Snapshot> &snapshots) {
    for (const auto &item : snapshots) {
        if (!item.ready || !item.monitor) continue;
        auto *entry = state(item.monitor);
        auto *activeTurn = turn(item.monitor);
        if (!entry || !activeTurn) continue;
        activeTurn->begin(item.composition, composition(item.monitor), m_turns.duration(), m_turns.curve());
        entry->last = Clock::now();
        if (!activeTurn->active()) erase_state(item.monitor);
    }
}

void CWorkspaceAnimation::Impl::mutate(
    const std::vector<PHLMONITOR> &monitors, const std::function<void()> &original) {
    if (!m_turns.enabled() || m_captureDepth > 0 || m_mutationDepth > 0) {
        original();
        return;
    }
    std::vector<Snapshot> before;
    before.reserve(monitors.size());
    for (const auto &monitor : monitors) {
        auto one = snapshot({monitor});
        if (!one.empty()) before.push_back(std::move(one.front()));
    }
    m_suppressed.clear();
    for (const auto &item : before)
        if (item.ready) m_suppressed.emplace_back(item.monitor);
    struct MutationDepth {
        Impl &owner;
        explicit MutationDepth(Impl &value) : owner(value) {
            ++owner.m_mutationDepth;
        }
        ~MutationDepth() {
            --owner.m_mutationDepth;
        }
    };
    const MutationDepth depth{*this};
    original();
    m_suppressed.clear();
    finish(before);
}

bool CWorkspaceAnimation::Impl::suppresses(const PHLMONITOR &monitor) const {
    return monitor && std::ranges::any_of(m_suppressed,
                          [&](const auto &candidate) { return candidate.lock() == monitor; });
}

void CWorkspaceAnimation::Impl::capture_gesture(const PHLMONITOR &monitor) {
    if (!m_turns.enabled() || !monitor) return;
    auto captured = snapshot({monitor});
    if (!captured.empty() && captured.front().ready) m_gestureMonitor = monitor;
}

void CWorkspaceAnimation::Impl::clear_gesture() {
    const auto monitor = m_gestureMonitor.lock();
    m_gestureMonitor.reset();
    if (!monitor) return;
    if (auto *activeTurn = turn(monitor)) {
        activeTurn->release_gesture();
        if (!activeTurn->active()) erase_state(monitor);
    }
}

void CWorkspaceAnimation::Impl::pre_checks(const PHLMONITOR &monitor) {
    if (const auto *activeTurn = turn(monitor); activeTurn && activeTurn->active()) {
        monitor->m_solitaryClient.reset();
    }
}

void CWorkspaceAnimation::Impl::pre_render(const PHLMONITOR &monitor) {
    if (!monitor) return;
    auto *entry = state(monitor);
    auto *activeTurn = turn(monitor);
    if (!entry || !activeTurn || !activeTurn->active()) return;
    const auto now = Clock::now();
    const auto elapsed = std::chrono::duration<double>(now - entry->last).count();
    entry->last = now;
    activeTurn->frame(composition(monitor), elapsed);
    if (!activeTurn->active()) erase_state(monitor);
}

bool CWorkspaceAnimation::Impl::queue(
    Render::CRenderContext &ctx, const PHLMONITOR &monitor, const Time::steady_tp &) {
    if (m_captureDepth > 0 || !monitor) return false;
    auto *entry = state(monitor);
    const auto *activeTurn = turn(monitor);
    if (!entry || !activeTurn || !activeTurn->drawable() || !entry->source || !entry->destination ||
        !entry->presentation) return false;
    Render::IHyprRenderer::addPassElement(ctx,
        makeUnique<CWorkspacePresentationPass>(this,
            monitor,
            entry->source,
            entry->destination,
            entry->presentation,
            entry->sourceWallpaper,
            entry->destinationWallpaper,
            hyprimate::workspace::wallpaper_matches<SP<Render::ITexture>>(
                entry->sourceWallpaperIdentity, entry->destinationWallpaperIdentity)));
    CTexPassElement::SRenderData data;
    data.tex = entry->presentation->getTexture();
    data.box = {0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y};
    data.a = 1.F;
    data.damage = CRegion{data.box};
    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CTexPassElement>(std::move(data)));
    return true;
}


void CWorkspaceAnimation::Impl::damage(const PHLMONITOR &monitor) const {
    if (!monitor) return;
    monitor->addDamage(CBox{0, 0, monitor->m_transformedSize.x, monitor->m_transformedSize.y});
    monitor->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
}

void CWorkspaceAnimation::Impl::erase_state(const PHLMONITOR &monitor) {
    if (monitor) m_turns.remove(monitor_key(monitor));
    std::erase_if(m_states, [&](const auto &entry) { return entry->monitor.lock() == monitor; });
}

void CWorkspaceAnimation::Impl::remove_monitor(const PHLMONITOR &monitor) {
    if (!monitor) return;
    erase_state(monitor);
}

void CWorkspaceAnimation::Impl::clear() {
    m_turns.reload();
    m_turns.clear();
    for (const auto &entry : m_states)
        if (const auto monitor = entry->monitor.lock()) damage(monitor);
    m_states.clear();
    m_suppressed.clear();
    m_gestureMonitor.reset();
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
void CWorkspaceAnimation::configure(bool enabled, std::string_view style, double duration,
    const hyprimate::workspace::GapFill &gap, std::string_view curveName) {
    m_impl->configure(enabled, style, duration, gap, curveName);
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
