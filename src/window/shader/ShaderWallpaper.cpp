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

#include "shader/GlesRuntime.hpp"
#include "shader/SceneShader.hpp"
#include "shader/ShaderWallpaper.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"

namespace hyprimate::shader {
    namespace {
        using Clock = std::chrono::steady_clock;
        constexpr const char *PASS_NAME = "hyprimate-shader-wallpaper";

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
            Vector2D logical, pixels;
            float scale;
            wl_output_transform transform;
            bool operator==(const Geometry &) const = default;
        };

        Geometry geometry_for(const PHLMONITOR &monitor) {
            return {.logical = monitor->m_size,
                .pixels = monitor->m_transformedSize,
                .scale = monitor->m_scale,
                .transform = monitor->m_transform};
        }

        struct Output {
            Output(const PHLMONITOR &target,
                const std::shared_ptr<SceneProgram> &prepared,
                double seconds)
                : monitor(target),
                  program(prepared),
                  duration(seconds),
                  seed(static_cast<std::uint64_t>(target->m_id) + 1) {
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

            [[nodiscard]] bool can_replay(const PHLMONITOR &target) const {
                return shader && !failed && geometry == geometry_for(target);
            }

            void resize(const Geometry &next) {
                if (geometry == next) {
                    return;
                }
                geometry = next;
                shader.reset();
                last.reset();
                elapsed = 0;
                tick = 0;
                failed = false;
            }

            [[nodiscard]] bool running() const {
                return duration == 0 || elapsed < duration;
            }

            void advance() {
                if (!running()) {
                    return;
                }
                const auto NOW = Clock::now();
                const double PREVIOUS = elapsed;
                if (last) {
                    elapsed += std::chrono::duration<double>(NOW - *last).count();
                    if (duration > 0) {
                        elapsed = std::min(elapsed, duration);
                    }
                }
                last = NOW;
                if (tick != 0 && elapsed == PREVIOUS) {
                    return;
                }
                shader->advance(Frame{.tick = ++tick,
                    .seed = seed,
                    .event = Event::Wallpaper,
                    .progress = duration > 0 ? static_cast<float>(elapsed / duration) : 0.0F,
                    .elapsed = static_cast<float>(elapsed),
                    .delta = static_cast<float>(elapsed - PREVIOUS),
                    .duration = static_cast<float>(duration),
                    .width = static_cast<float>(geometry->logical.x),
                    .height = static_cast<float>(geometry->logical.y),
                    .scale = geometry->scale});
            }
        };

        class WallpaperPass final : public IPassElement {
          public:
            WallpaperPass(const std::shared_ptr<Output> &output, bool replay)
                : m_output(output), m_replay(replay) {
            }

            std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
                const auto OUTPUT = m_output.lock();
                if (!OUTPUT || OUTPUT->failed || ctx.m_data.damage.empty() ||
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

                std::string error;
                try {
                    if (!OUTPUT->shader) {
                        OUTPUT->shader.emplace(OUTPUT->program);
                    }
                    OUTPUT->advance();
                    // The background below is only a compositing fallback. Do not
                    // pass it, any layers, or the current framebuffer as shader input.
                    if (OUTPUT->shader->draw(ctx, {}, {}, error)) {
                        OUTPUT->drawn = true;
                        OUTPUT->redraw_next = OUTPUT->running();
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

    class ShaderWallpaper::Impl {
      public:
        Impl() {
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
        }
        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void configure(const std::string &path, const std::string &parameters, int duration_ms) {
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
            auto output = std::make_shared<Output>(monitor, m_program, m_duration);
            output->dpms =
                monitor->m_events.dpmsChanged.listen([weak = std::weak_ptr<Output>{output}] {
                    if (const auto OUTPUT = weak.lock()) {
                        OUTPUT->last.reset();
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
                // A solitary client can take the direct-scanout path and skip render stages.
                if (usable_output(monitor) && monitor->m_solitaryClient.expired()) {
                    return;
                }
                if (const auto ENTRY = find(monitor); ENTRY != m_outputs.end()) {
                    (*ENTRY)->last.reset();
                }
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
                        out->last.reset();
                    }
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
            m_program.reset();
            redraw_outputs();
        }

        std::shared_ptr<SceneProgram> m_program;
        double m_duration = 0;
        std::vector<std::shared_ptr<Output>> m_outputs;
        CHyprSignalListener m_stage, m_pre_checks, m_added, m_removed, m_lock, m_unlock;
    };

    ShaderWallpaper::ShaderWallpaper() : m_impl(std::make_unique<Impl>()) {
    }
    ShaderWallpaper::~ShaderWallpaper() = default;

    void ShaderWallpaper::configure(
        const std::string &path, const std::string &parameters, int duration_ms) {
        if (m_impl) {
            m_impl->configure(path, parameters, duration_ms);
        }
    }

    void ShaderWallpaper::shutdown() {
        m_impl.reset();
    }
}  // namespace hyprimate::shader
