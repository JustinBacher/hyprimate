#include "RuntimeProfiler.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <aquamarine/output/Output.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include "GpuTimer.hpp"
#include "LiveProfiler.hpp"
#include "MonitorIdentity.hpp"
#include "gl/CompositorContext.hpp"
#include "window/hyprland/HyprWindowPort.hpp"
#include "workspace/WorkspaceCaptureScope.hpp"

namespace hyprimate::perf {
    namespace {
        CHyprWindowPort *identity_port = nullptr;
        constexpr std::string_view COMMAND = "hyprimate:profile";
        using PresentationCallback = std::function<void(std::uint64_t)>;
        constexpr const char *PRESENTATION_PASS = "hyprimate-profile-presentation";

        // POST_WINDOW is emitted while building the pass. Consume transform receipts
        // only after the queued window passes have actually executed.
        class PresentationPass final : public IPassElement {
          public:
            PresentationPass(std::weak_ptr<PresentationCallback> callback, std::uint64_t monitor)
                : m_callback(std::move(callback)), m_monitor(monitor), m_session(generation()) {}
            std::vector<UP<IPassElement>> draw(Render::CRenderContext &ctx) override {
                if (!enabled() || generation() != m_session ||
                    ctx.m_mode != Render::RENDER_MODE_NORMAL || ctx.m_renderingSnapshot ||
                    ctx.m_renderingCapture || ctx.readOnlyEffects() ||
                    WorkspaceCaptureScope::for_context(ctx) ||
                    WorkspaceCaptureScope::is_destination_live(ctx) ||
                    monitor_identity(ctx.m_data.pMonitor.lock()) != m_monitor) return {};
                if (const auto callback = m_callback.lock()) {
                    (*callback)(m_monitor);
                    frame_end(m_monitor);
                    sample_memory(m_monitor);
                    poll_gpu();
                }
                return {};
            }
            bool needsLiveBlur(Render::CRenderContext &) override { return false; }
            bool needsPrecomputeBlur(Render::CRenderContext &) override { return false; }
            bool undiscardable(Render::CRenderContext &) override { return true; }
            bool disableSimplification(Render::CRenderContext &) override { return true; }
            const char *passName() override { return PRESENTATION_PASS; }
            ePassElementType type() override { return EK_CUSTOM; }
          private:
            std::weak_ptr<PresentationCallback> m_callback;
            std::uint64_t m_monitor, m_session;
        };
    }
    std::uint64_t monitor_identity(const PHLMONITOR &monitor) noexcept {
        if (!identity_port) return 0;
        try {
            return identity_port->monitor_identity(monitor);
        } catch (const std::bad_alloc &) {
            return 0;
        }
    }

    class RuntimeProfiler::Impl {
      public:
        Impl(HANDLE handle, CHyprWindowPort &port, PresentationCallback presented)
            : m_handle(handle), m_port(port),
              m_presented(std::make_shared<PresentationCallback>(std::move(presented))) {
            m_stage = Event::bus()->m_events.render.stage.listen([this](const Event::SRenderStageEvent &event) {
                if (!enabled() || !event.monitor) return;
                const auto id = m_port.monitor_identity(event.monitor);
                if (event.stage == RENDER_BEGIN && event.context &&
                    event.context->get().m_mode == Render::RENDER_MODE_NORMAL) {
                    frame_begin(id, event.monitor->m_refreshRate);
                    poll_gpu();
                } else if (event.stage == RENDER_LAST_MOMENT && event.context &&
                    event.context->get().m_mode == Render::RENDER_MODE_NORMAL) {
                    Render::IHyprRenderer::addPassElement(event.context->get(),
                        makeUnique<PresentationPass>(m_presented, id));
                }
            });
            m_added = Event::bus()->m_events.monitor.added.listen([this](const PHLMONITOR &monitor) {
                attach(monitor);
            });
            m_removed = Event::bus()->m_events.monitor.preRemoved.listen([this](const PHLMONITOR &monitor) {
                if (!monitor) return;
                remove_monitor(m_port.monitor_identity(monitor));
                std::erase_if(m_outputs, [&](const auto &entry) { return entry.monitor.lock() == monitor; });
            });
            for (const auto &monitor : State::monitorState()->monitors()) attach(monitor);
            m_command = HyprlandAPI::registerHyprCtlCommand(m_handle,
                {.name = std::string(COMMAND),
                    .match = IPC::Socket1::COMMAND_MATCH_PREFIX,
                    .handler = [this](const IPC::Socket1::SRequest &request) -> IPC::Socket1::SResponse {
                        return command(request.command);
                    }});
            if (!m_command) throw std::runtime_error("hyprimate: cannot register live profiling command");
            identity_port = &port;
        }
        ~Impl() {
            // Disconnect callbacks before unregistering or releasing context-owned queries.
            m_stage.reset();
            m_added.reset();
            m_removed.reset();
            m_outputs.clear();
            m_presented.reset();
            if (g_pHyprRenderer) {
                auto &ctx = g_pHyprRenderer->context();
                auto &pass = Render::IHyprRenderer::currentPass(ctx);
                pass.removeAllOfType(PRESENTATION_PASS);
                if (&pass != &ctx.m_pass) ctx.m_pass.removeAllOfType(PRESENTATION_PASS);
            }
            if (m_command) HyprlandAPI::unregisterHyprCtlCommand(m_handle, m_command);
            stop();
            release_gpu();
            if (identity_port == &m_port) identity_port = nullptr;
        }
      private:
        struct Output {
            PHLMONITORREF monitor;
            CHyprSignalListener present;
        };
        void attach(const PHLMONITOR &monitor) {
            if (!monitor || !monitor->m_output || std::ranges::any_of(m_outputs,
                [&](const auto &entry) { return entry.monitor.lock() == monitor; })) return;
            const auto id = m_port.monitor_identity(monitor);
            const PHLMONITORREF weak{monitor};
            auto listener = monitor->m_output->events.present.listen(
                [weak, id](const Aquamarine::IOutput::SPresentEvent &event) {
                    if (!enabled()) return;
                    const auto output = weak.lock();
                    if (!output) return;
                    std::uint64_t timestamp = 0;
                    if (event.when && event.when->tv_sec >= 0 && event.when->tv_nsec >= 0 &&
                        event.when->tv_nsec < 1'000'000'000) {
                        timestamp = static_cast<std::uint64_t>(event.when->tv_sec) * 1'000'000'000ULL +
                                    static_cast<std::uint64_t>(event.when->tv_nsec);
                    }
                    present(id, timestamp, event.presented,
                        event.refresh > 0 ? 1e9 / event.refresh : output->m_refreshRate);
                });
            m_outputs.push_back({.monitor = weak, .present = std::move(listener)});
        }
        std::string command(std::string_view request) {
            if (!request.starts_with(COMMAND)) return "error: invalid profiling command";
            request.remove_prefix(COMMAND.size());
            if (!request.empty() && request.front() != ' ') return "error: invalid profiling command";
            while (!request.empty() && request.front() == ' ') request.remove_prefix(1);
            while (!request.empty() && request.back() == ' ') request.remove_suffix(1);
            if (request == "start") {
                release_gpu();
                start();
                return "ok";
            }
            if (request == "reset") {
                release_gpu();
                reset();
                return "ok";
            }
            if (request == "stop") {
                if (enabled()) {
                    const gl::ScopedCompositorContext context{};
                    if (context) poll_gpu();
                    release_gpu();
                    stop();
                }
                return report();
            }
            if (request.empty() || request == "report") {
                if (enabled()) {
                    const gl::ScopedCompositorContext context{};
                    if (context) poll_gpu();
                }
                return report();
            }
            return "error: usage: hyprctl hyprimate:profile start|report|stop|reset";
        }
        HANDLE m_handle;
        CHyprWindowPort &m_port;
        SP<IPC::Socket1::SCommand> m_command;
        std::shared_ptr<PresentationCallback> m_presented;
        CHyprSignalListener m_stage, m_added, m_removed;
        std::vector<Output> m_outputs;
    };

    RuntimeProfiler::RuntimeProfiler(HANDLE handle, CHyprWindowPort &port,
        std::function<void(std::uint64_t)> presented)
        : m_impl(std::make_unique<Impl>(handle, port, std::move(presented))) {}
    RuntimeProfiler::~RuntimeProfiler() = default;
}
