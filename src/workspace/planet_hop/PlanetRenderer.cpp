#include <algorithm>
#include <cmath>
#include <new>

#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include "gl/NativeResource.hpp"
#include "gl/RawGlBracket.hpp"
#include "workspace/WorkspaceFrame.hpp"
#include "workspace/planet_hop/PlanetMaterial.hpp"
#include "workspace/planet_hop/PlanetRenderer.hpp"

namespace hyprimate::workspace {
    namespace {
        struct CaptureView {
            Vector2D size;
            [[nodiscard]] GLuint texture(const auto &buffer, bool cropped = false) const {
                return buffer && gl::native_current(buffer) &&
                               (cropped || buffer->m_size == size) && buffer->getTexture()
                           ? buffer->getTexture()->m_texID
                           : 0;
            }
            PlanetLayerTextures layer(const WorkspaceLayer &layer,
                const WorkspaceLayer *previous = nullptr,
                float live_mix = 1,
                bool cropped = false) const {
                return {.image = texture(layer.accumulated, cropped),
                    .backdrop = texture(layer.backdrop, cropped),
                    .previous_image =
                        (previous != nullptr) ? texture(previous->accumulated, cropped) : 0,
                    .previous_backdrop =
                        (previous != nullptr) ? texture(previous->backdrop, cropped) : 0,
                    .live_mix = live_mix};
            }
            PlanetCaptureTextures capture(const WorkspaceCapture &capture,
                std::vector<PlanetFloatingTextures> &floating) const {
                PlanetCaptureTextures result;
                const auto *previous = capture.previous.get();
                const float MIX = (previous != nullptr) && std::isfinite(capture.live_mix)
                                      ? static_cast<float>(std::clamp(capture.live_mix, 0.0, 1.0))
                                      : 1;
                result.composite = texture(capture.composite);
                result.wallpaper = texture(capture.wallpaper);
                result.previous_wallpaper =
                    (previous != nullptr) ? texture(previous->wallpaper) : 0;
                result.live_mix = MIX;
                result.bottom =
                    layer(capture.bottom, (previous != nullptr) ? &previous->bottom : nullptr, MIX);
                result.windows = layer(
                    capture.windows, (previous != nullptr) ? &previous->windows : nullptr, MIX);
                result.top =
                    layer(capture.top, (previous != nullptr) ? &previous->top : nullptr, MIX);
                floating.clear();
                if (capture.floating) {
                    floating.reserve(capture.floating->size());
                    for (const auto &item : *capture.floating) {
                        floating.push_back(
                            {.layer = layer(
                                 item.layer, &item.previous_layer, item.live_mix, item.cropped),
                                .rect = {static_cast<float>(item.rect.x),
                                    static_cast<float>(item.rect.y),
                                    static_cast<float>(item.rect.w),
                                    static_cast<float>(item.rect.h)},
                                .cropped = item.cropped,
                                .opacity = item.opacity});
                    }
                }
                result.floating = floating;
                return result;
            }
        };
    }  // namespace
    PlanetRenderer::PlanetRenderer() = default;
    PlanetRenderer::~PlanetRenderer() {
        release();
    }
    bool PlanetRenderer::initialize() {
        const gl::ScopedCompositorContext CONTEXT{};
        if (!CONTEXT || !m_context.ensure_current()) {
            return false;
        }
        if (m_shaders[0]) {
            return true;
        }
        if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
            g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL) {
            return false;
        }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<5> BRACKET{{}};
        const std::array VERTICES{planet_hop::shader::VERTEX,
            planet_hop::shader::QUAD_VERTEX,
            planet_hop::shader::SKY_VERTEX};
        const std::array FRAGMENTS{planet_hop::shader::FRAGMENT,
            planet_hop::shader::QUAD_FRAGMENT,
            planet_hop::shader::SKY_FRAGMENT};
        // A shader pointer is the readiness marker; publish the complete set only
        // after CPU construction, linking and raw GL initialization all succeed.
        std::array<std::unique_ptr<CShader>, 3> shaders;
        try {
            for (std::size_t index = 0; index < shaders.size(); ++index) {
                shaders[index] = std::make_unique<CShader>();
                if (!shaders[index]->createProgram(
                        std::string(VERTICES[index]), std::string(FRAGMENTS[index]), true, true)) {
                    return false;
                }
            }
        } catch (const std::bad_alloc &) {
            return false;
        }
        if (!m_core.initialize_raw({.planet = shaders[0]->program(),
                .quad = shaders[1]->program(),
                .sky = shaders[2]->program()})) {
            return false;
        }
        m_shaders = std::move(shaders);
        return true;
    }
    void PlanetRenderer::release() {
        m_position_caches = {};
        m_next_position_cache = 0;
        if (!m_shaders[0] && !m_shaders[1] && !m_shaders[2]) {
            m_core.abandon_raw();
            m_context.reset();
            return;
        }
        const gl::ScopedCompositorContext CONTEXT{m_context};
        if (!CONTEXT) {
            // CShader has no detach API; never run its destructor in another context.
            for (auto &shader : m_shaders) {
                [[maybe_unused]] const auto *const ABANDONED = shader.release();
            }
            m_core.abandon_raw();
            m_context.reset();
            return;
        }
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const gl::CRawGlBracket<5> BRACKET{{}};
        m_core.release_raw();
        for (auto &shader : m_shaders) {
            shader.reset();
        }
        m_context.reset();
    }
    const std::vector<planet_hop::Vec3> &PlanetRenderer::neighbor_positions(
        const WorkspaceFrame &frame,
        const planet_hop::Journey &journey,
        const planet_hop::Frame &scene) {
        const auto &options = journey.options;
        const std::array<double, 5> KEY{options.planet_size,
            options.planet_separation,
            options.elevation,
            options.solar_vertical_spread,
            scene.aspect};
        auto *cached = std::ranges::find_if(m_position_caches, [&](const auto &slot) {
            return slot.layout == options.planet_layout && slot.key == KEY &&
                   !slot.inventory.owner_before(frame.neighbors) &&
                   !frame.neighbors.owner_before(slot.inventory);
        });
        if (cached == m_position_caches.end()) {
            cached = std::ranges::find_if(
                m_position_caches, [](const auto &slot) { return slot.inventory.expired(); });
            if (cached == m_position_caches.end()) {
                cached =
                    m_position_caches.begin() + static_cast<std::ptrdiff_t>(m_next_position_cache);
                m_next_position_cache = (m_next_position_cache + 1) % m_position_caches.size();
            }
            std::vector<planet_hop::Vec3> positions;
            positions.reserve(frame.neighbors->size());
            for (const auto &preview : *frame.neighbors) {
                positions.push_back(
                    planet_hop::layout_position(preview.endpoint, options, scene.aspect));
            }
            cached->positions = std::move(positions);
            cached->inventory = frame.neighbors;
            cached->layout = options.planet_layout;
            cached->key = KEY;
        }
        return cached->positions;
    }
    void PlanetRenderer::prepare_neighbors(const WorkspaceFrame &frame,
        const planet_hop::Journey &journey,
        const planet_hop::Frame &scene,
        double radius) {
        m_neighbors.clear();
        if (journey.options.planet_layout == planet_hop::Layout::Focused || !frame.neighbors) {
            return;
        }
        const auto &positions = neighbor_positions(frame, journey, scene);
        const CaptureView VIEW{frame.presentation->m_size};
        m_neighbors.reserve(frame.neighbors->size());
        for (std::size_t index = 0; index < frame.neighbors->size(); ++index) {
            const auto &preview = (*frame.neighbors)[index];
            if (!planet_hop::scene_member(
                    preview.endpoint, journey.from, journey.to, journey.options.planet_layout) ||
                preview.endpoint.identity == journey.from.identity ||
                preview.endpoint.identity == journey.to.identity) {
                continue;
            }
            const auto &position = positions[index];
            const planet_hop::Vec3 CENTER{.x = position.x - scene.world_origin.x,
                .y = position.y - scene.world_origin.y,
                .z = position.z - scene.world_origin.z};
            const double OWNER_ASPECT =
                preview.endpoint.monitor_height > 0
                    ? preview.endpoint.monitor_width / preview.endpoint.monitor_height
                    : 1;
            const double BOUND = radius * std::max(1.0, OWNER_ASPECT) / std::max(1.0, scene.aspect);
            if (!planet_hop::neighbor_visible(scene, CENTER, BOUND)) {
                continue;
            }
            const GLuint IMAGE = VIEW.texture(preview.image, true);
            const GLuint PREVIOUS = VIEW.texture(preview.previous_image, true);
            m_neighbors.push_back({.endpoint = preview.endpoint,
                .center = CENTER,
                .image = IMAGE,
                .shared_wallpaper = IMAGE == 0,
                .previous_image = PREVIOUS,
                .live_mix = preview.live_mix,
                .image_mipped = IMAGE != 0,
                .previous_mipped = PREVIOUS != 0});
        }
    }
    bool PlanetRenderer::draw(Render::CRenderContext &ctx,
        const WorkspaceFrame &frame,
        const planet_hop::Journey &journey,
        double progress,
        Direction direction,
        bool matching,
        const PlanetRenderSettings &settings) {
        (void)ctx;
        (void)direction;
        (void)matching;
        if (!m_context.ensure_current()) {
            return false;
        }
        if (!g_pHyprRenderer || !Render::GL::g_pHyprOpenGL ||
            g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL || !frame.presentation ||
            !gl::native_current(frame.presentation) || !frame.presentation->getTexture() ||
            !frame.source.layered || !frame.destination.layered) {
            return false;
        }
        const auto SIZE = frame.presentation->m_size;
        const CaptureView VIEW{SIZE};
        const PlanetFrameTextures TEXTURES{.source = VIEW.capture(frame.source, m_source_floating),
            .destination = VIEW.capture(frame.destination, m_destination_floating),
            .output = frame.presentation->getTexture()->m_texID,
            .width = static_cast<int>(SIZE.x),
            .height = static_cast<int>(SIZE.y),
            .top_first = true,
            .native_hud = frame.native_hud,
            .source_identity = journey.from.identity,
            .destination_identity = journey.to.identity};
        if (TEXTURES.height <= 0) {
            return false;
        }
        const auto SCENE =
            planet_hop::evaluate(journey, progress, static_cast<double>(SIZE.x) / SIZE.y);
        prepare_neighbors(
            frame, journey, SCENE, SCENE.radius * std::max(0.F, settings.solar_background_scale));
        auto render_settings = settings;
        render_settings.rotation_enabled = journey.options.rotation_enabled;
        render_settings.rotation_limit = static_cast<float>(journey.options.rotation_limit);
        const Render::GL::CFramebufferBindingGuard BINDINGS{g_pHyprRenderer->glBackend()};
        const auto DRAW = [&]<std::size_t Units> {
            const gl::CRawGlBracket<Units> BRACKET{
                {.viewport_width = TEXTURES.width, .viewport_height = TEXTURES.height}};
            return m_core.draw_raw(TEXTURES, SCENE, progress, render_settings, m_neighbors);
        };
        // Extra units are touched only when both HUD endpoints can have capture history.
        if (!frame.native_hud && settings.overlay_mode == 1 &&
            ((frame.source.previous && frame.source.live_mix < 1) ||
                (frame.destination.previous && frame.destination.live_mix < 1))) {
            return DRAW.template operator()<9>();
        }
        return DRAW.template operator()<5>();
    }
}  // namespace hyprimate::workspace
