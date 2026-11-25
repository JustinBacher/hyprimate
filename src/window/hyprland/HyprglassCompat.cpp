#include <array>
#include <cmath>
#include <cstdint>
#include <dlfcn.h>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowEffectsController.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/transformer/TransformerList.hpp>

#include "AnimationTransformer.hpp"
#include "HyprWindowPort.hpp"
#include "HyprglassCompat.hpp"
#include "StyleTypes.hpp"
#include "plugin/PluginImports.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "window/WindowAnimations.hpp"
#include "window/shared/SheetRenderer.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"
#include "perf/GpuTimer.hpp"

// Opaque Hyprglass arguments: never inspect another plugin's private layout.
class CGlassDecoration;
struct SResolveContext;
namespace GlassRenderer {
    struct SMaskInfo;
    struct SFrostInput;
}

namespace {
    constexpr const char *RENDER_SYMBOL =
        "_ZN16CGlassDecoration10renderPassERN6Render14CRenderContextEN9Hyprutils6Memory14CSharedPointerIN7Monitor8CMonitorEEERKfb";
    constexpr const char *OWNER_SYMBOL = "_ZN16CGlassDecoration8getOwnerEv";
    constexpr const char *APPLY_SYMBOL =
        "_ZN13GlassRenderer16applyGlassEffectERN6Render14CRenderContextEN9Hyprutils6Memory14CSharedPointerINS0_12IFramebufferEEES7_RNS3_4Math4CBoxESA_fRKSt5arrayIfLm4EEfRKNS8_8Vector2DERK15SResolveContextPKNS_9SMaskInfoEfPKNS_11SFrostInputE";

    using Image = SP<Render::IFramebuffer>;
    using RenderFn = void (*)(CGlassDecoration *, Render::CRenderContext &, PHLMONITOR,
        const float &, bool);
    using OwnerFn = PHLWINDOW (*)(CGlassDecoration *);
    using ApplyFn = void (*)(Render::CRenderContext &, Image, Image, CBox &, CBox &, float,
        const std::array<float, 4> &, float, const Vector2D &, const SResolveContext &,
        const GlassRenderer::SMaskInfo *, float, const GlassRenderer::SFrostInput *);
}

class CHyprglassCompat::Impl {
  public:
    Impl(HANDLE owner, HyprWindowAnimations &animations, CHyprWindowPort &port)
        : m_owner(owner), m_animations(animations), m_port(port) {}
    ~Impl() { reset(); }
    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;
    Impl(Impl &&) = delete;
    Impl &operator=(Impl &&) = delete;
    void refresh();

  private:
    void reset();
    bool deform(Render::CRenderContext &ctx, const Image &sample, const Image &target,
        CBox &raw, CBox &transformed, float alpha, const std::array<float, 4> &radii,
        float rounding, const Vector2D &padding, const SResolveContext &resolve,
        float light, const GlassRenderer::SFrostInput *frost);
    static void render(CGlassDecoration *decoration, Render::CRenderContext &ctx,
        PHLMONITOR monitor, const float &alpha, bool xray);
    static void apply(Render::CRenderContext &ctx, Image sample, Image target,
        CBox &raw, CBox &transformed, float alpha, const std::array<float, 4> &radii,
        float rounding, const Vector2D &padding, const SResolveContext &resolve,
        const GlassRenderer::SMaskInfo *mask, float light, const GlassRenderer::SFrostInput *frost);

    inline static Impl *s_active = nullptr;
    inline static constexpr std::array IMPORTS{RENDER_SYMBOL, APPLY_SYMBOL};
    HANDLE m_owner;
    HyprWindowAnimations &m_animations;
    CHyprWindowPort &m_port;
    std::vector<CPlugin *> m_plugins;
    HANDLE m_plugin = nullptr;
    std::array<void **, 2> m_slots{};
    std::string m_loader_name;
    std::uintptr_t m_loader_map_address = 0;
    ElfW(Addr) m_loader_base = 0;
    hyprimate::PluginImportOwnership<2> m_import_ownership;
    RenderFn m_render = nullptr;
    ApplyFn m_apply = nullptr;
    OwnerFn m_get_owner = nullptr;
    PHLWINDOW m_window;
};

void CHyprglassCompat::Impl::reset() {
    std::array<void **, 2> slots{};
    void *temporary_handle = nullptr;
    bool resolved = false;
    if (m_plugin && g_pPluginSystem && g_pPluginSystem->getPluginByHandle(m_plugin)) {
        slots = hyprimate::writable_plugin_imports(m_plugin, IMPORTS);
        resolved = true;
    } else if (!m_loader_name.empty() && m_loader_map_address != 0) {
        // During teardown the registry may already have dropped its handle.
        // Reacquire only this exact still-loaded link_map, for this call only.
        temporary_handle = dlopen(m_loader_name.c_str(), RTLD_LAZY | RTLD_NOLOAD);
        link_map *map = nullptr;
        if (temporary_handle != nullptr &&
            dlinfo(temporary_handle, RTLD_DI_LINKMAP, &map) == 0 && map != nullptr &&
            reinterpret_cast<std::uintptr_t>(map) == m_loader_map_address &&
            map->l_addr == m_loader_base && map->l_name != nullptr && m_loader_name == map->l_name) {
            slots = hyprimate::writable_plugin_imports(temporary_handle, IMPORTS);
            resolved = true;
        }
    }
    const std::array REPLACEMENTS{
        reinterpret_cast<void *>(&render), reinterpret_cast<void *>(&apply)};
    if (resolved) {
        m_import_ownership.restore(slots, REPLACEMENTS);
    } else {
        // If identity capture failed, teardown cannot safely find a still-mapped
        // orphan; its slot may retain our callback (reported residual).
        m_import_ownership.clear();
    }
    if (temporary_handle != nullptr) dlclose(temporary_handle);
    m_slots = {};
    m_loader_name.clear();
    m_loader_map_address = 0;
    m_loader_base = 0;
    m_render = nullptr;
    m_apply = nullptr;
    m_get_owner = nullptr;
    m_window.reset();
    if (s_active == this) s_active = nullptr;
    m_plugin = nullptr;
}

void CHyprglassCompat::Impl::refresh() {
    if (!g_pPluginSystem) return;
    m_plugins.resize(g_pPluginSystem->pluginCount());
    g_pPluginSystem->sigGetPlugins(m_plugins.data(), m_plugins.size());
    CPlugin *glass = nullptr;
    for (auto *plugin : m_plugins) {
        if (plugin->m_name == "hyprglass") {
            glass = plugin;
            break;
        }
    }
    if (glass == nullptr) {
        // The plugin may be temporarily absent from the registry while another
        // loader reference keeps its import slots alive. Suspend without touching
        // saved slot pointers; refresh or reset will resolve current slots afresh.
        m_import_ownership.suspend();
        return;
    }
    const auto SLOTS = hyprimate::writable_plugin_imports(glass->m_handle, IMPORTS);
    const std::array REPLACEMENTS{
        reinterpret_cast<void *>(&render), reinterpret_cast<void *>(&apply)};
    if (glass->m_handle == m_plugin && SLOTS == m_slots) {
        if (m_import_ownership.observe(SLOTS, REPLACEMENTS, glass)) {
            HyprlandAPI::addNotification(m_owner,
                "[hyprimate] Hyprglass import ownership conflict; conflicting entry left untouched",
                CHyprColor(1.F, 0.6F, 0.2F, 1.F),
                5000.F);
        }
        // CPlugin's registry record identifies this load independently of a
        // recycled dlopen handle and import-slot addresses. If its exact address
        // is also reused, the API cannot distinguish reload from conflict; fail
        // safe by reporting a conflict and leaving the changed slot untouched.
        // A foreign wrapper can retain our callback after replacing its slot;
        // pinning would prevent clean reloads, so lifetime after unload remains unresolved (BD5).
        if (m_import_ownership.conflicted()) return;
        if ((s_active != this && m_render == nullptr) ||
            (SLOTS[0] != nullptr && SLOTS[1] != nullptr && *SLOTS[0] == REPLACEMENTS[0] &&
                *SLOTS[1] == REPLACEMENTS[1]))
            return;
    }
    reset();
    m_plugin = glass->m_handle;
    m_slots = SLOTS;
    m_render = reinterpret_cast<RenderFn>(dlsym(m_plugin, RENDER_SYMBOL));
    m_apply = reinterpret_cast<ApplyFn>(dlsym(m_plugin, APPLY_SYMBOL));
    m_get_owner = reinterpret_cast<OwnerFn>(dlsym(m_plugin, OWNER_SYMBOL));
    if (SLOTS[0] == nullptr || SLOTS[1] == nullptr ||
        m_render == nullptr || m_apply == nullptr || m_get_owner == nullptr) {
        m_render = nullptr;
        HyprlandAPI::addNotification(m_owner,
            "[hyprimate] Hyprglass alignment unavailable: unsupported window-glass imports",
            CHyprColor(1.F, 0.6F, 0.2F, 1.F), 5000.F);
        return;
    }
    link_map *map = nullptr;
    if (dlinfo(m_plugin, RTLD_DI_LINKMAP, &map) == 0 && map != nullptr &&
        map->l_name != nullptr && *map->l_name != '\0') {
        try {
            m_loader_name = map->l_name;
        } catch (const std::bad_alloc &) {
            m_loader_name.clear();
        }
        if (!m_loader_name.empty()) {
            m_loader_map_address = reinterpret_cast<std::uintptr_t>(map);
            m_loader_base = map->l_addr;
        } else {
            m_loader_map_address = 0;
            m_loader_base = 0;
        }
    } else {
        m_loader_name.clear();
        m_loader_map_address = 0;
        m_loader_base = 0;
    }
    if (m_loader_name.empty() || m_loader_map_address == 0 || m_loader_base == 0)
        return;
    s_active = this;
    m_import_ownership.install(SLOTS, REPLACEMENTS, glass);
}

void CHyprglassCompat::Impl::render(CGlassDecoration *decoration,
    Render::CRenderContext &ctx,
    PHLMONITOR monitor,
    const float &alpha,
    bool xray) {
    auto &self = *s_active;
    if (self.m_import_ownership.suspended()) {
        self.m_render(decoration, ctx, monitor, alpha, xray);
        return;
    }
    auto previous = std::exchange(self.m_window, self.m_get_owner(decoration));
    // Preserve by-value ABI arguments: the original borrows their caller-owned storage.
    self.m_render(decoration, ctx, monitor, alpha, xray);
    self.m_window = std::move(previous);
}

void CHyprglassCompat::Impl::apply(Render::CRenderContext &ctx, Image sample, Image target,
    CBox &raw, CBox &transformed, float alpha, const std::array<float, 4> &radii,
    float rounding, const Vector2D &padding, const SResolveContext &resolve,
    const GlassRenderer::SMaskInfo *mask, float light, const GlassRenderer::SFrostInput *frost) {
    auto &self = *s_active;
    if (self.m_import_ownership.suspended()) {
        self.m_apply(ctx, sample, target, raw, transformed, alpha, radii, rounding,
            padding, resolve, mask, light, frost);
        return;
    }
    // Layer/item glass has its own mask and does not belong to a window animation.
    if (!mask && self.deform(ctx, sample, target, raw, transformed, alpha, radii,
            rounding, padding, resolve, light, frost)) return;
    self.m_apply(ctx, sample, target, raw, transformed, alpha, radii, rounding,
        padding, resolve, mask, light, frost);
}

bool CHyprglassCompat::Impl::deform(Render::CRenderContext &ctx, const Image &sample,
    const Image &target, CBox &raw, CBox &transformed, float alpha,
    const std::array<float, 4> &radii, float rounding, const Vector2D &padding,
    const SResolveContext &resolve, float light, const GlassRenderer::SFrostInput *frost) {
    using namespace hyprimate::animation;
    const auto monitor = ctx.m_data.pMonitor.lock();
    if (!m_window || !monitor || !sample || !target || !CHyprWindowPort::session_unlocked() ||
        ctx.readOnlyEffects() || ctx.m_renderingCapture || ctx.m_renderingSnapshot ||
        WorkspaceCaptureScope::for_context(ctx) || WorkspaceCaptureScope::is_destination_live(ctx))
        return false;
    auto *transformer = m_window->effects().transformers()->get<CAnimationTransformer>();
    if (!transformer || !transformer->active() || transformer->handle().style !=
            hyprimate::config::Style::Liquid) return false;
    const double scale = monitor->m_scale;
    if (!std::isfinite(scale) || scale <= 0.0) return false;

    // Exactly the native transformer's source domain, including decorations.
    // Using raw (the main surface) would stretch the liquid field differently.
    auto source = m_window->getFullWindowBoundingBox();
    const auto workspace = m_window->m_workspace;
    const auto offset = workspace && !(m_window->m_state & Desktop::View::WINDOW_STATE_PINNED)
        ? workspace->m_renderOffset->value() : Vector2D{};
    source.translate(offset + m_window->presentation().floatingOffset() - monitor->m_position);
    const auto output = transformer->transformedExtents(source).scale(scale);
    const auto canvas_box = Render::pixelBoxForLogical(
        transformer->transformedExtents(source).expand(1.0 / scale), scale);
    source.scale(scale);
    const auto raster_box = Render::pixelBoxForLogical(source.copy().scale(1.0 / scale), scale);
    if (source.empty() || canvas_box.empty() || raster_box.empty()) return false;
    const auto glass = hyprimate::gl::get_work_buffer(ctx, raster_box.size());
    if (!glass) return false;

    const hyprimate::perf::GpuScope GPU_PROFILE{profile_key(transformer->handle().style)};
    const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
    {
        const auto state = ctx.saveDrawState();
        g_pHyprRenderer->bindFB(ctx, glass);
        ctx.m_data.fbSize = raster_box.size();
        ctx.m_data.damage = CRegion{0, 0, raster_box.w, raster_box.h};
        g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);
        g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
        auto local = raw.copy().translate(-raster_box.pos());
        // Sampling has already happened against the real backdrop. Redirect only
        // the final shaded glass quad, never the background cache or its inputs.
        m_apply(ctx, sample, glass, local, transformed,
            alpha, radii, rounding, padding, resolve, nullptr, light, frost);
    }

    CHyprAnimationCanvas canvas{ctx, m_port.canvas_resources()};
    const Draw<CHyprAnimationCanvas> draw{.canvas = &canvas, .image = &glass,
        .source = to_box(source), .output = to_box(output), .canvas_box = to_box(canvas_box),
        .image_origin = {.x = raster_box.x, .y = raster_box.y},
        .image_size = {.x = raster_box.w, .y = raster_box.h}, .scale = scale,
        // Glass must never replace retained client pixels or advance publication.
        .real_pass = false, .monitor = m_port.monitor_identity(monitor)};
    if (!m_animations.draw(transformer->handle(), draw) || !canvas.layer()) return false;
    const hyprimate::gl::NativeMeshSamplers samplers;
    Render::GL::CHyprOpenGLImpl::STextureRenderData texture_data{};
    texture_data.damage = &ctx.m_data.damage;
    texture_data.allowCustomUV = true;
    g_pHyprRenderer->bindFB(ctx, target);
    Render::GL::g_pHyprOpenGL->renderTextureMesh(ctx, canvas.layer()->getTexture(), canvas_box,
        sheet_canvas_quad(), texture_data);
    return true;
}

CHyprglassCompat::CHyprglassCompat(HANDLE owner, HyprWindowAnimations &animations,
    CHyprWindowPort &port) : m_impl(std::make_unique<Impl>(owner, animations, port)) {}
CHyprglassCompat::~CHyprglassCompat() = default;
void CHyprglassCompat::refresh() { m_impl->refresh(); }
