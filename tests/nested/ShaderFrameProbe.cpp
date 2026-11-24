#include <chrono>
#include <cstdlib>
#include <fstream>

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace {
    using Clock = std::chrono::steady_clock;
    CHyprSignalListener listener;
    std::ofstream trace;
    Clock::time_point begin;
    const void *output = nullptr;
}  // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE) {
    if (const char *path = std::getenv("HYPRIMATE_FRAME_TRACE"); path) {
        trace.open(path);
    }
    if (trace.is_open()) {
        listener =
            Event::bus()->m_events.render.stage.listen([](const Event::SRenderStageEvent &event) {
                if (!event.monitor) {
                    return;
                }
                if (event.stage == RENDER_PRE) {
                    begin = Clock::now();
                    output = event.monitor.get();
                } else if (event.stage == RENDER_POST && output == event.monitor.get()) {
                    const auto END = Clock::now();
                    trace
                        << std::chrono::duration_cast<std::chrono::nanoseconds>(
                               END.time_since_epoch())
                               .count()
                        << ' '
                        << std::chrono::duration_cast<std::chrono::nanoseconds>(END - begin).count()
                        << '\n';
                    output = nullptr;
                }
            });
    }
    return {"shader-frame-trace", "nested renderer CPU wall duration", "Hyprimate test", "1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    listener.reset();
    trace.close();
}
