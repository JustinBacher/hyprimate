#include "shader/SceneShader.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/SceneResources.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include "gl/CompositorContext.hpp"
#include "gl/NativeFramebuffer.hpp"
#include "gl/NativeResource.hpp"
#include "shader/hyprland/HyprShaderProgram.hpp"
#include "shader/hyprland/HyprShaderTarget.hpp"

namespace hyprimate::shader {
    namespace {
        using hyprland::ClipList;
        using hyprland::Extent;
        using hyprland::Image;
        using hyprland::alpha_format;
        using hyprland::direct_target;
        using hyprland::extent;
        using hyprland::private_target;
        using hyprland::same_encoding;

        bool fail(std::string &error, const char *message) {
            error = message;
            return false;
        }
    }
    namespace {
        std::uint64_t scene_bytes(unsigned width, unsigned height, DRMFormat format) {
            return std::uint64_t{width} * height *
                (format == DRM_FORMAT_ABGR16161616F ? 8U : 4U);
        }

        bool fits_budget(std::uint64_t instance, std::uint64_t ancillary, std::uint64_t additional = 0) {
            return instance <= MAX_INSTANCE_BYTES && ancillary <= MAX_INSTANCE_BYTES - instance &&
                additional <= MAX_INSTANCE_BYTES - instance - ancillary;
        }
    }

    class SceneProgram {
      public:
        std::shared_ptr<WindowProgram> prepared;
        Event event = Event::Wallpaper;
    };

    const Environment &scene_environment(const SceneProgram &program) noexcept {
        return program.prepared->program.package().environment;
    }

    std::shared_ptr<SceneProgram> prepare_scene_program(Event event, const std::string &path,
        const std::string &parameters, std::string &error) {
        if (event != Event::Workspace && event != Event::Wallpaper) {
            fail(error, "not a scene shader event");
            return {};
        }
        auto program = prepare_window_program(event, path, parameters, error);
        if (!program) return {};
        if (std::ranges::any_of(program->program.package().bounds, [](float pad) { return pad != 0; })) {
            fail(error, "scene shader bounds must fit the output");
            return {};
        }
        static std::vector<std::weak_ptr<SceneProgram>> cache;
        std::erase_if(cache, [](const auto &entry) { return entry.expired(); });
        for (const auto &entry : cache) {
            if (auto old = entry.lock(); old && old->event == event && old->prepared == program) {
                return old;
            }
        }
        auto prepared = std::make_shared<SceneProgram>(SceneProgram{std::move(program), event});
        cache.emplace_back(prepared);
        return prepared;
    }

    struct SceneShader::Impl {
        explicit Impl(std::shared_ptr<SceneProgram> shader)
            : program(std::move(shader)), receipt_id(next_compute_instance_id()) {}
        std::shared_ptr<SceneProgram> program;
        Instance instance;
        const std::uint64_t receipt_id;
        Frame frame{};
        Image black;
        Image publication;
        std::optional<Extent> initialized_size;
        std::optional<bool> last_direct;
    };

    SceneShader::SceneShader(std::shared_ptr<SceneProgram> program)
        : m_impl(std::make_unique<Impl>(std::move(program))) {}

    SceneShader::~SceneShader() {
        if (scene_environment(*m_impl->program).used()) {
            const auto &stats = m_impl->instance.statistics();
            LOG(Log::DEBUG, "[hyprimate] environment retire update={} draw={} upload_bytes={} allocated={} allocations={}",
                stats.update_passes, stats.draws, stats.upload_bytes, stats.allocated_bytes, stats.allocations);
        }
        const auto &owner = *m_impl->program->prepared->owner;
        if (m_impl->program->prepared->has_compute && m_impl->initialized_size)
            m_impl->program->prepared->log_compute_instance_retirement(
                m_impl->receipt_id, m_impl->instance.compute_statistics(), "scene");
#ifdef HYPRIMATE_COMPUTE_BENCH_ADAPTER
        if (m_impl->program->prepared->has_compute && m_impl->initialized_size)
            if (const auto iterations = m_impl->instance.benchmark_cpu_dispatches(); iterations > 0)
                LOG(Log::DEBUG, "[hyprimate] compute benchmark cpu instance_id={} iterations={}",
                    m_impl->receipt_id, iterations);
#endif
        const gl::ScopedCompositorContext context{owner};
        if (context) m_impl->instance.release();
        else m_impl->instance.abandon();
        gl::release_native(m_impl->black, owner);
        gl::release_native(m_impl->publication, owner);
    }

    void SceneShader::advance(const Frame &frame) noexcept {
        if (frame.tick > m_impl->frame.tick) m_impl->frame = frame;
    }

    GLuint SceneShader::environment_state() const noexcept {
        const auto &environment = scene_environment(*m_impl->program);
        return environment.state ? m_impl->instance.exported_texture(*environment.state) : 0;
    }

    const Image &SceneShader::published_wallpaper() const noexcept {
        return m_impl->publication;
    }

    bool SceneShader::draw(Render::CRenderContext &ctx, const Image &source, const Image &destination,
        std::string &error, bool authoritative, const EnvironmentInputs &environment) {
        auto &state = *m_impl;
        auto &prepared = *state.program->prepared;
        const auto monitor = ctx.m_data.pMonitor.lock();
        const auto encoding = ctx.m_data.currentFB;
        const bool workspace = state.program->event == Event::Workspace;
        const bool live = authoritative && !ctx.readOnlyEffects();
        if (!monitor || !encoding || !ctx.active() || !ctx.shouldRenderContent(false) ||
            !prepared.owner->ensure_current() || !g_pHyprRenderer ||
            (workspace && (!live || !source || !destination))) {
            return fail(error, "unavailable compositor scene shader context");
        }
        const auto size = extent(monitor->m_transformedSize.x, monitor->m_transformedSize.y);
        if (!size || (workspace && !same_encoding(source, destination))) {
            return fail(error, "incompatible scene shader endpoint dimensions or color encoding");
        }
        const auto lease = [&](const Image &image) -> GLuint {
            const auto tex = image ? image->getTexture() : nullptr;
            return tex && tex->m_texID && tex->m_type == Render::TEXTURE_RGBA &&
                tex->m_transform == HYPRUTILS_TRANSFORM_NORMAL && !image->getMirrorTexture() &&
                tex->m_size == monitor->m_transformedSize ? tex->m_texID : 0;
        };
        const Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
        if (!workspace && !state.black && live) {
            const auto publication_bytes = state.publication
                ? scene_bytes(static_cast<unsigned>(state.publication->m_size.x),
                    static_cast<unsigned>(state.publication->m_size.y), state.publication->m_drmFormat)
                : 0U;
            if (!fits_budget(state.instance.statistics().allocated_bytes, publication_bytes, 4U))
                return fail(error, "wallpaper source and simulation exceed the instance budget");
            auto black = g_pHyprRenderer->createFB("hyprimate empty wallpaper input");
            if (!black || !gl::allocate_framebuffer(*black, 1, 1, DRM_FORMAT_ABGR8888)) {
                return fail(error, "cannot allocate isolated wallpaper input");
            }
            const Render::GL::CFramebufferBindingGuard clear_bindings{g_pHyprRenderer->glBackend()};
            const auto saved = ctx.saveDrawState();
            g_pHyprRenderer->bindFB(ctx, black);
            ctx.m_data.fbSize = {1, 1};
            ctx.m_data.damage = CRegion{0, 0, 1, 1};
            ctx.m_data.clipBox = {};
            ctx.m_data.transformDamage = false;
            g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
            state.black = std::move(black);
        }
        const auto black = state.black ? state.black->getTexture()->m_texID : 0;
        const auto src = workspace ? lease(source) : black;
        const auto dst = workspace ? lease(destination) : black;
        if (!src || !dst) return fail(error, "invalid scene shader source lease");
        const Source input{.texture = src, .width = workspace ? size->width : 1U,
            .height = workspace ? size->height : 1U, .destination_texture = dst, .environment = environment};
        if (size != state.initialized_size) {
            const auto publication_bytes = state.publication
                ? scene_bytes(static_cast<unsigned>(state.publication->m_size.x),
                    static_cast<unsigned>(state.publication->m_size.y), state.publication->m_drmFormat)
                : 0U;
            const auto ancillary_bytes = (state.black ? 4U : 0U) + publication_bytes;
            if (!live || !state.instance.initialize(state.program->prepared->program,
                    size->width, size->height, error, ancillary_bytes)) return false;
            state.initialized_size = size;
        }
        if (live) state.instance.advance(state.frame);
        const Box box{0, 0, double(size->width), double(size->height)};
        constexpr std::array<float, 4> FULL{0, 0, 1, 1};
        const auto &input_encoding = workspace ? source : encoding;
        const bool publish = scene_environment(*state.program).publish;
        const auto report = [&](bool direct) {
            if (!live || state.last_direct == direct) return;
            state.last_direct = direct;
            LOG(Log::DEBUG, "[hyprimate] shader {} final={} width={} height={}",
                workspace ? "workspace" : "wallpaper", direct ? "direct" : "pooled",
                size->width, size->height);
        };
        if (!publish && prepared.program.can_composite_direct()) {
            ClipList clips;
            bool submitted = false;
            if (const auto target = direct_target(ctx, input_encoding, box, *size, FULL, clips, submitted)) {
                const bool drawn = state.instance.render(input, *target, live, error);
                if (!drawn && submitted) LOG(Log::WARN, "[hyprimate] direct scene submission failed: {}", error);
                if (drawn && submitted) report(true);
                return drawn || submitted;
            }
        }
        Image layer;
        if (publish) {
            const auto want = input_encoding->imageDescription();
            const auto format = alpha_format(input_encoding->m_drmFormat);
            if (state.publication && (!want || !state.publication->imageDescription() ||
                    state.publication->imageDescription()->value() != want->value() ||
                    state.publication->m_drmFormat != format || state.publication->m_size != monitor->m_transformedSize)) {
                gl::release_native(state.publication, *prepared.owner);
            }
            if (!state.publication && live) {
                const auto bytes = std::uint64_t(size->width) * size->height *
                    (format == DRM_FORMAT_ABGR16161616F ? 8 : 4);
                const auto ancillary = state.black ? 4U : 0U;
                if (!fits_budget(state.instance.statistics().allocated_bytes, ancillary, bytes))
                    return fail(error, "published wallpaper and simulation exceed the instance budget");
                const gl::FramebufferAllocationState allocation;
                auto publication = g_pHyprRenderer->createFB("hyprimate published wallpaper");
                if (!publication || !want ||
                    !gl::allocate_framebuffer(*publication, size->width, size->height, format)) {
                    return fail(error, "cannot allocate published wallpaper");
                }
                publication->setImageDescription(want);
                state.publication = std::move(publication);
            }
            layer = state.publication;
        } else {
            if (!monitor->resources() || !input_encoding->imageDescription()) {
                return fail(error, "no compositor scene work buffer owner");
            }
            const auto format = alpha_format(input_encoding->m_drmFormat);
            const auto bytes = scene_bytes(size->width, size->height, format);
            const auto ancillary = state.black ? 4U : 0U;
            if (!fits_budget(state.instance.statistics().allocated_bytes, ancillary, bytes))
                return fail(error, "scene work buffer and simulation exceed the instance budget");
            const gl::FramebufferAllocationState allocation;
            layer = monitor->resources()->getUnusedWorkBuffer(format,
                input_encoding->imageDescription(), monitor->m_transformedSize);
            if (ctx.sceneResources()) layer = ctx.sceneResources()->prepareWorkBuffer(std::move(layer));
        }
        const auto target = private_target(layer, *size, FULL);
        if (!target || layer == encoding || layer == source || layer == destination ||
            ((!publish || live) && !state.instance.render(input, *target, live, error))) return false;
        CTexPassElement::SRenderData data;
        data.tex = layer->getTexture();
        data.box = {box.x, box.y, box.w, box.h};
        data.damage = ctx.m_data.damage;
        data.useProvidedDamage = true;
        g_pHyprRenderer->draw(ctx, data, data.damage);
        report(false);
        return true;
    }
}  // namespace hyprimate::shader
