#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>

#include "perf/GpuTimer.hpp"
#include "perf/LiveProfiler.hpp"
#include "perf/MonitorIdentity.hpp"
#include "shader/GlesRuntime.hpp"
#include "shader/hyprland/HyprEnvironment.hpp"
#include "shader/hyprland/HyprEnvironmentInteractions.hpp"
#include "shader/hyprland/HyprWallpaperSource.hpp"
#include "shader/SceneShader.hpp"
#include "shader/hyprland/ShaderWallpaper.hpp"
#include "shader/hyprland/DisturbanceAtlas.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"

namespace hyprimate::shader {
    namespace {
        using Clock = std::chrono::steady_clock;
        constexpr const char *PASS_NAME = "hyprimate-shader-wallpaper";

        constexpr perf::Key PROFILE{perf::Domain::Wallpaper, "shader", "environment"};
        std::string_view source_namespace;
        bool usable_output(const PHLMONITOR &monitor) {
            return monitor && monitor->m_output && monitor->m_enabled && monitor->m_dpmsStatus &&
                   !monitor->isMirror() && monitor->m_transformedSize.x > 0 &&
                   monitor->m_transformedSize.y > 0 && monitor->m_scale > 0;
        }

        bool unlocked() {
            return g_pSessionLockManager && !g_pSessionLockManager->isSessionLocked();
        }

        bool output_context(Render::CRenderContext &ctx, const PHLMONITOR &monitor) {
            return usable_output(monitor) && unlocked() && ctx.active() &&
                   ctx.m_data.pMonitor.lock() == monitor && !ctx.m_renderingCapture &&
                   !ctx.m_renderingSnapshot &&
                   &Render::IHyprRenderer::currentPass(ctx) == &ctx.m_pass;
        }

        bool eligible(Render::CRenderContext &ctx, const PHLMONITOR &monitor) {
            return ctx.m_mode == Render::RENDER_MODE_NORMAL && !ctx.readOnlyEffects() &&
                   output_context(ctx, monitor) && monitor->m_renderingActive;
        }

        bool replay_context(Render::CRenderContext &ctx, const PHLMONITOR &monitor) {
            return ctx.m_mode == Render::RENDER_MODE_FULL_FAKE && output_context(ctx, monitor) &&
                   (WorkspaceCaptureScope::for_context(ctx) != nullptr ||
                       WorkspaceCaptureScope::is_destination_live(ctx)) &&
                   ctx.m_data.currentFB && ctx.m_data.currentFB == ctx.m_data.outFB &&
                   ctx.m_data.currentFB->m_size == monitor->m_transformedSize;
        }

        void redraw(const PHLMONITOR &monitor) {
            if (!g_pHyprRenderer || !usable_output(monitor)) {
                return;
            }
            monitor->m_blurFBDirty = true;
            g_pHyprRenderer->damageMonitor(monitor);
        }

        void log_fallback(std::string_view where, std::string_view error) noexcept {
            try {
                LOG(Log::WARN, "[hyprimate] wallpaper shader ({}): {}", where, error);
            } catch (const std::exception &) {
                // Diagnostic allocation must not interfere with fallback.
            }
        }

        struct Geometry {
            Vector2D logical, pixels, position;
            float scale;
            wl_output_transform transform;
            bool operator==(const Geometry &) const = default;
        };

        Geometry geometry_for(const PHLMONITOR &monitor) {
            return {.logical = monitor->m_size,
                .pixels = monitor->m_transformedSize,
                .position = monitor->m_position,
                .scale = monitor->m_scale,
                .transform = monitor->m_transform};
        }

        EnvironmentInputs environment_coordinates(const PHLMONITOR &monitor) {
            return {.output_size = {float(monitor->m_size.x), float(monitor->m_size.y)},
                .output_origin = {float(monitor->m_position.x), float(monitor->m_position.y)}};
        }

        struct Output {
            Output(const PHLMONITOR &target,
                const std::shared_ptr<SceneProgram> &prepared,
                double seconds, HyprEnvironmentInteractions *interactions)
                : monitor(target),
                  program(prepared),
                  duration(seconds),
                  seed(static_cast<std::uint64_t>(target->m_id) + 1),
                  collector(interactions) {
            }

            PHLMONITORREF monitor;
            std::shared_ptr<SceneProgram> program;
            std::optional<SceneShader> shader;
            CHyprSignalListener dpms;
            std::optional<Geometry> geometry;
            std::optional<Clock::time_point> last;
            double duration;
            std::uint64_t seed;
            double elapsed = 0;
            std::uint64_t tick = 0;
            bool failed = false;
            bool queued = false;
            bool drawn = false;
            bool redraw_next = false;
            HyprEnvironmentInteractions *collector = nullptr;
            PHLWORKSPACEREF workspace, special;
            WP<CWLSurfaceResource> original_owner;
            WP<Render::ITexture> original_texture;
            std::array<float, 4> original_uv{};
            bool has_original = false;
            double remaining = 0;
            std::uint64_t epoch = 0;
            std::optional<DisturbanceAtlas> atlas;

            void reset_environment() {
                shader.reset();
                if (atlas && atlas->gpu_bytes()) {
                    LOG(Log::DEBUG, "[hyprimate] environment mailbox storage={} uploaded={} copied={}",
                        atlas->gpu_bytes(), atlas->uploaded_bytes(), atlas->copied_bytes());
                }
                atlas.reset();
                last.reset();
                tick = 0;
                elapsed = remaining = 0;
                failed = false;
                drawn = false;
            }

            bool update_scope(const PHLMONITOR &target) {
                if (workspace.lock() == target->m_activeWorkspace &&
                    special.lock() == target->m_activeSpecialWorkspace) return false;
                reset_environment();
                workspace = target->m_activeWorkspace;
                special = target->m_activeSpecialWorkspace;
                return true;
            }

            [[nodiscard]] bool matches_original(const OriginalWallpaper &original) const {
                return has_original == bool(original) && original_owner == original.owner &&
                    original_texture.lock() == original.texture && original_uv == original.uv;
            }

            bool source_changed(const OriginalWallpaper &original) {
                if (matches_original(original)) return false;
                reset_environment();
                has_original = bool(original);
                original_owner = original.owner;
                original_texture = original.texture;
                original_uv = original.uv;
                return true;
            }

            [[nodiscard]] bool can_replay(const PHLMONITOR &target) const {
                // Environmental inputs are scoped to the authoritative output,
                // never to an offscreen workspace capture.
                return shader && !scene_environment(*program).used() && !failed && geometry == geometry_for(target);
            }

            void resize(const Geometry &next) {
                if (geometry == next) {
                    return;
                }
                geometry = next;
                reset_environment();
            }

            [[nodiscard]] bool running() const {
                if (collector && duration > 0) return remaining > 0;
                return duration == 0 || elapsed < duration;
            }

            void advance(bool disturbed) {
                const auto now = Clock::now();
                const double delta = last ? std::max(0., std::chrono::duration<double>(now - *last).count()) : 0.;
                const double settle = scene_environment(*program).settle_ms * .001;
                if (collector && duration > 0) {
                    remaining = disturbed ? settle : std::max(0., remaining - delta);
                    elapsed = settle - remaining;
                } else if (running() && last) {
                    elapsed = duration > 0 ? std::min(elapsed + delta, duration) : elapsed + delta;
                }
                const bool update = tick == 0 || disturbed || (last && delta > 0);
                last = running() ? std::optional{now} : std::nullopt;
                if (!update) return;
                const double phase_duration = collector && duration > 0 ? settle : duration;
                shader->advance(Frame{.tick = ++tick, .seed = seed, .event = Event::Wallpaper,
                    .progress = phase_duration > 0 ? float(elapsed / phase_duration) : 0.F,
                    .elapsed = float(elapsed), .delta = float(delta), .duration = float(phase_duration),
                    .width = float(geometry->logical.x), .height = float(geometry->logical.y),
                    .scale = geometry->scale});
            }
        };
        using Outputs = std::vector<std::shared_ptr<Output>>;
        Outputs *active_outputs = nullptr;

        Output *environment_output(const PHLMONITOR &monitor) {
            if (!active_outputs) return nullptr;
            const auto item = std::ranges::find_if(*active_outputs, [&](const auto &output) {
                return output->monitor.lock() == monitor;
            });
            return item != active_outputs->end() ? item->get() : nullptr;
        }

        class WallpaperPass final : public IPassElement {
          public:
            WallpaperPass(const std::shared_ptr<Output> &output, bool replay)
                : m_output(output), m_replay(replay) {
            }

            std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
                const auto OUTPUT = m_output.lock();
                if (!OUTPUT || OUTPUT->failed || (ctx.m_data.damage.empty() && !undiscardable(ctx)) ||
                    !ctx.m_data.renderModif.modifs.empty()) {
                    return {};
                }
                const auto MONITOR = OUTPUT->monitor.lock();
                if (m_replay) {
                    if (replay_context(ctx, MONITOR) && OUTPUT->can_replay(MONITOR)) {
                        // Only the committed procedural image is replayed. The
                        // capture's framebuffer and scene surfaces are not inputs.
                        try {
                            std::string error;
                            OUTPUT->shader->draw(ctx, {}, {}, error, false);
                        } catch (const std::exception &) {
                            // Keep the native background and live instance on replay failure.
                        }
                    }
                    return {};
                }
                if (OUTPUT->drawn || !eligible(ctx, MONITOR)) {
                    return {};
                }

                const perf::CpuScope cpu{PROFILE, perf::Phase::Draw};
                const perf::GpuScope gpu{PROFILE};
                std::string error;
                try {
                    const auto &request = scene_environment(*OUTPUT->program);
                    if (request.used()) OUTPUT->redraw_next |= OUTPUT->update_scope(MONITOR);
                    const auto original = request.original ? original_wallpaper(MONITOR, source_namespace)
                                                           : OriginalWallpaper{};
                    if (request.original) {
                        OUTPUT->redraw_next |= OUTPUT->source_changed(original);
                        if (!original) return {};
                    }
                    std::optional<HyprEnvironmentInteractions::Batch> batch;
                    auto inputs = request.used() ? environment_coordinates(MONITOR) : EnvironmentInputs{};
                    if (original) {
                        inputs.original = original.texture->m_texID;
                        inputs.original_uv = original.uv;
                    }
                    if (OUTPUT->collector) {
                        batch.emplace(OUTPUT->collector->collect(MONITOR));
                        if (batch->reset) {
                            OUTPUT->reset_environment();
                            OUTPUT->redraw_next = true;
                        }
                        inputs.interactions = std::span{batch->records}.first(batch->count);
                        if (OUTPUT->atlas) {
                            inputs.disturbances = OUTPUT->atlas->gather(OUTPUT->epoch,
                                std::span{batch->windows}.first(batch->count));
                        }
                    }
                    if (!OUTPUT->shader) {
                        OUTPUT->shader.emplace(OUTPUT->program);
                    }
                    OUTPUT->advance(batch && batch->count != 0);
                    if (OUTPUT->shader->draw(ctx, {}, {}, error, true, inputs)) {
                        if (perf::enabled()) perf::touch(PROFILE, perf::monitor_identity(MONITOR));
                        OUTPUT->drawn = true;
                        OUTPUT->redraw_next |= OUTPUT->running();
                        return {};
                    }
                    log_fallback(MONITOR->m_name, error);
                } catch (const std::exception &exception) {
                    log_fallback(MONITOR->m_name, exception.what());
                }
                OUTPUT->failed = true;
                OUTPUT->shader.reset();
                // A failure under partial damage may leave older wallpaper in
                // undamaged tiles. One native redraw freshens the entire output.
                OUTPUT->redraw_next = true;
                return {};
            }

            bool needsLiveBlur(Render::CRenderContext & /*ctx*/) override {
                return false;
            }
            bool needsPrecomputeBlur(Render::CRenderContext & /*ctx*/) override {
                return false;
            }
            const char *passName() override {
                return PASS_NAME;
            }
            ePassElementType type() override {
                return EK_CUSTOM;
            }
            bool undiscardable(Render::CRenderContext & /*ctx*/) override {
                const auto output = m_output.lock();
                if (!output || m_replay) return false;
                const auto &request = scene_environment(*output->program);
                return request.interactions || request.publish || request.state.has_value();
            }
            // Do not mark any pixels opaque: the shader may have alpha, be culled,
            // or fail. Native background elements must remain available below it.
            std::optional<CBox> boundingBox(Render::CRenderContext &ctx) override {
                const auto MONITOR = ctx.m_data.pMonitor.lock();
                return MONITOR ? std::optional<CBox>{CBox{{}, MONITOR->m_size}} : std::nullopt;
            }

          private:
            std::weak_ptr<Output> m_output;
            bool m_replay;
        };

        void remove_queued() {
            if (!g_pHyprRenderer) {
                return;
            }
            // Eligibility restricts this pass to the context's primary queue.
            g_pHyprRenderer->context().m_pass.removeAllOfType(PASS_NAME);
        }

        void redraw_outputs() {
            if (!State::monitorState()) {
                return;
            }
            for (const auto &monitor : State::monitorState()->monitors()) {
                redraw(monitor);
            }
        }
    }  // namespace

    WindowEnvironment window_environment(Render::CRenderContext &ctx, const Environment &request,
        const std::array<float, 4> &physical_box) {
        WindowEnvironment result;
        const auto monitor = ctx.m_data.pMonitor.lock();
        if (!eligible(ctx, monitor)) return result;
        result.inputs = environment_coordinates(monitor);
        const auto size = monitor->m_transformedSize;
        result.inputs.output_rect = {physical_box[0] / float(size.x), physical_box[1] / float(size.y),
            physical_box[2] / float(size.x), physical_box[3] / float(size.y)};
        const auto original = request.original ? original_wallpaper(monitor, source_namespace)
                                               : OriginalWallpaper{};
        if (original) {
            result.inputs.original = original.texture->m_texID;
            result.inputs.original_uv = original.uv;
            result.original = original.texture;
        }
        if (!request.processed && !request.sample_state) return result;
        const auto *output = environment_output(monitor);
        if (!output || !output->drawn || !output->shader || output->failed) return result;
        const auto &published = scene_environment(*output->program);
        if (output->workspace.lock() != monitor->m_activeWorkspace ||
            output->special.lock() != monitor->m_activeSpecialWorkspace) return result;
        if (published.original) {
            const auto lease = request.original ? original : original_wallpaper(monitor, source_namespace);
            if (!lease || !output->matches_original(lease)) return result;
        }
        if (request.processed) {
            const auto &image = output->shader->published_wallpaper();
            if (image && image->getTexture()) result.inputs.processed = image->getTexture()->m_texID;
        }
        if (request.sample_state && request.channel == published.channel) {
            result.inputs.state = output->shader->environment_state();
        }
        return result;
    }

    void emit_environment(Render::CRenderContext &ctx, const Environment &request,
        std::uint64_t window, GLuint texture) {
        const auto monitor = ctx.m_data.pMonitor.lock();
        if (!active_outputs || !eligible(ctx, monitor)) return;
        auto *output = environment_output(monitor);
        if (!output || !output->drawn || !output->collector || output->failed ||
            scene_environment(*output->program).channel != request.channel) return;
        if (!output->atlas) output->atlas.emplace();
        output->atlas->put(output->epoch, window, texture);
    }

    class ShaderWallpaper::Impl {
      public:
        explicit Impl(CHyprWindowPort &port) : m_port(port) {
            active_outputs = &m_outputs;
            // Register before the runtime's window styles. Their wallpaper
            // backdrop copies and marks must run above this pass after reloads too.
            m_stage = ::Event::bus()->m_events.render.stage.listen(
                [this](const ::Event::SRenderStageEvent &event) {
                    if (m_program) {
                        on_stage(event);
                    }
                });
        }
        ~Impl() {
            m_stage.reset();
            stop();
            active_outputs = nullptr;
            source_namespace = {};
        }
        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void configure(const std::string &path, const std::string &parameters, int duration_ms,
            const std::string &namespace_name) {
            const bool source_changed = m_namespace != namespace_name;
            if (source_changed) {
                m_namespace = namespace_name;
                source_namespace = m_namespace;
                for (const auto &out : m_outputs) out->reset_environment();
                redraw_outputs();
            }
            if (path.empty()) {
                stop();
                return;
            }
            if (duration_ms < 0 || duration_ms > 60000) {
                log_fallback(path, "invalid effect duration");
                return;
            }

            std::string error;
            std::shared_ptr<SceneProgram> program;
            try {
                program = prepare_scene_program(Event::Wallpaper, path, parameters, error);
            } catch (const std::exception &exception) {
                log_fallback(path, exception.what());
                return;
            }
            if (!program) {
                log_fallback(path, error);
                return;
            }

            const double SECONDS = duration_ms / 1000.0;
            if (m_program == program && m_duration == SECONDS) {
                return;
            }

            if (!m_program) {
                listen();
            }
            remove_queued();
            m_outputs.clear();
            m_interactions.reset();
            if (scene_environment(*program).interactions) {
                m_interactions.emplace(m_port);
            }
            m_program = std::move(program);
            m_duration = SECONDS;
            if (State::monitorState()) {
                for (const auto &monitor : State::monitorState()->monitors()) {
                    attach(monitor);
                }
            }
            redraw_outputs();
        }

      private:
        auto find(const PHLMONITOR &monitor) {
            return std::ranges::find_if(
                m_outputs, [&](const auto &output) { return output->monitor.lock() == monitor; });
        }

        void attach(const PHLMONITOR &monitor) {
            if (!monitor || !monitor->m_output || find(monitor) != m_outputs.end()) {
                return;
            }
            auto output = std::make_shared<Output>(monitor, m_program, m_duration,
                m_interactions ? &*m_interactions : nullptr);
            output->dpms =
                monitor->m_events.dpmsChanged.listen([weak = std::weak_ptr<Output>{output}] {
                    if (const auto OUTPUT = weak.lock()) {
                        if (OUTPUT->collector) OUTPUT->reset_environment();
                        else OUTPUT->last.reset();
                        redraw(OUTPUT->monitor.lock());
                    }
                });
            m_outputs.push_back(std::move(output));
        }

        void on_stage(const ::Event::SRenderStageEvent &event) {
            if (!event.monitor ||
                (event.stage != RENDER_BEGIN && event.stage != RENDER_POST_WALLPAPER &&
                    event.stage != RENDER_POST)) {
                return;
            }
            const auto ENTRY = find(event.monitor);
            if (ENTRY == m_outputs.end()) {
                return;
            }
            const auto &out = *ENTRY;

            if (event.stage == RENDER_POST) {
                if (!out->drawn) {
                    out->last.reset();
                }
                // Native pre-blur later than the wallpaper pass may clean the
                // blur cache. Invalidate after the pass, for the next animation frame.
                if (std::exchange(out->redraw_next, false) && unlocked()) {
                    redraw(event.monitor);
                }
                return;
            }
            if (!event.context) {
                return;
            }
            auto &ctx = event.context->get();
            if (event.stage == RENDER_POST_WALLPAPER && replay_context(ctx, event.monitor)) {
                if (out->can_replay(event.monitor)) {
                    Render::IHyprRenderer::addPassElement(
                        ctx, makeUnique<WallpaperPass>(out, true));
                }
                return;
            }
            if (!eligible(ctx, event.monitor)) {
                return;
            }
            if (event.stage == RENDER_BEGIN) {
                ++out->epoch;
                out->queued = out->drawn = out->redraw_next = false;
                return;
            }
            if (out->queued) {
                return;
            }
            out->resize(geometry_for(event.monitor));
            if (!out->failed) {
                // Hyprland has queued its stock and layer-shell backgrounds, but
                // not bottom layers, window blur or the windows above them.
                Render::IHyprRenderer::addPassElement(ctx, makeUnique<WallpaperPass>(out, false));
                out->queued = true;
            }
        }

        void listen() {
            auto &events = ::Event::bus()->m_events;
            m_pre_checks = events.render.preChecks.listen([this](const PHLMONITOR &monitor) {
                auto *out = environment_output(monitor);
                if (!out || !unlocked()) return;
                const auto &request = scene_environment(*out->program);
                if (!monitor) return;
                if (request.used() && usable_output(monitor)) {
                    bool dirty = out->update_scope(monitor);
                    if (request.original) dirty |= out->source_changed(original_wallpaper(monitor, source_namespace));
                    if (out->collector && out->collector->privacy_changed(monitor)) {
                        out->reset_environment();
                        dirty = true;
                    }
                    if (dirty) redraw(monitor);
                }
                // A solitary client can bypass the native background scene.
                if (!usable_output(monitor) || !monitor->m_solitaryClient.expired()) out->last.reset();
            });
            m_added = events.monitor.added.listen([this](const PHLMONITOR &monitor) {
                attach(monitor);
                redraw(monitor);
            });
            m_removed = events.monitor.preRemoved.listen([this](const PHLMONITOR &monitor) {
                if (!monitor) {
                    return;
                }
                if (g_pHyprRenderer &&
                    g_pHyprRenderer->context().m_data.pMonitor.lock() == monitor) {
                    remove_queued();
                }
                if (const auto ENTRY = find(monitor); ENTRY != m_outputs.end()) {
                    m_outputs.erase(ENTRY);
                }
            });
            if (g_pSessionLockManager) {
                const auto CHANGE = [this] {
                    for (const auto &out : m_outputs) {
                        if (scene_environment(*out->program).used()) out->reset_environment();
                        else out->last.reset();
                    }
                    if (m_interactions) m_interactions->clear();
                    redraw_outputs();
                };
                m_lock = g_pSessionLockManager->m_events.lock.listen(CHANGE);
                m_unlock = g_pSessionLockManager->m_events.unlock.listen(CHANGE);
            }
        }

        void stop() {
            m_pre_checks.reset();
            m_added.reset();
            m_removed.reset();
            m_lock.reset();
            m_unlock.reset();
            if (!m_program) {
                return;
            }
            remove_queued();
            m_outputs.clear();
            m_interactions.reset();
            m_program.reset();
            redraw_outputs();
        }

        std::shared_ptr<SceneProgram> m_program;
        double m_duration = 0;
        CHyprWindowPort &m_port;
        std::optional<HyprEnvironmentInteractions> m_interactions;
        std::string m_namespace;
        Outputs m_outputs;
        CHyprSignalListener m_stage, m_pre_checks, m_added, m_removed, m_lock, m_unlock;
    };

    ShaderWallpaper::ShaderWallpaper(CHyprWindowPort &port) : m_impl(std::make_unique<Impl>(port)) {
    }
    ShaderWallpaper::~ShaderWallpaper() = default;

    void ShaderWallpaper::configure(
        const std::string &path, const std::string &parameters, int duration_ms,
        const std::string &namespace_name) {
        if (m_impl) {
            m_impl->configure(path, parameters, duration_ms, namespace_name);
        }
    }

    void ShaderWallpaper::shutdown() {
        m_impl.reset();
    }
}  // namespace hyprimate::shader
