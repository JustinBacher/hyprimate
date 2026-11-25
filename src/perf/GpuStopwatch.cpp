#include "perf/GpuStopwatch.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include "perf/LiveProfiler.hpp"

namespace hyprimate::perf {
    namespace {
        constexpr std::size_t SLOTS = 64;
        constexpr std::size_t MAX_SAMPLES = 1U << 16;

        struct Slot {
            GLuint begin = 0;
            GLuint end = 0;
            const char *label = nullptr;
            enum class Stage : std::uint8_t { Free, Open, Pending } stage = Stage::Free;
        };

        struct Series {
            std::vector<std::uint32_t> ns;
            std::uint64_t dropped = 0;
        };

        struct State {
            bool discovered = false;
            bool supported = false;
            PFNGLGENQUERIESEXTPROC gen = nullptr;
            PFNGLDELETEQUERIESEXTPROC erase = nullptr;
            PFNGLQUERYCOUNTEREXTPROC counter = nullptr;
            PFNGLGETQUERYOBJECTUIVEXTPROC available = nullptr;
            PFNGLGETQUERYOBJECTUI64VEXTPROC result = nullptr;
            EGLContext context = EGL_NO_CONTEXT;
            std::array<Slot, SLOTS> slots{};
            std::map<std::string, Series> series;
        };

        State g_state;

        bool discover(State &state) noexcept {
            const EGLContext CURRENT = eglGetCurrentContext();
            if (CURRENT == EGL_NO_CONTEXT) return false;
            if (state.context != CURRENT) {
                // Query names belong to one context; start over for a replacement context.
                state.slots = {};
                state.context = CURRENT;
                state.discovered = false;
            }
            if (!state.discovered) {
                state.discovered = true;
                const auto *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
                if (extensions != nullptr &&
                    std::strstr(extensions, "GL_EXT_disjoint_timer_query") != nullptr) {
#define LOAD(member, type, name) state.member = reinterpret_cast<type>(eglGetProcAddress(name))
                    LOAD(gen, PFNGLGENQUERIESEXTPROC, "glGenQueriesEXT");
                    LOAD(erase, PFNGLDELETEQUERIESEXTPROC, "glDeleteQueriesEXT");
                    LOAD(counter, PFNGLQUERYCOUNTEREXTPROC, "glQueryCounterEXT");
                    LOAD(available, PFNGLGETQUERYOBJECTUIVEXTPROC, "glGetQueryObjectuivEXT");
                    LOAD(result, PFNGLGETQUERYOBJECTUI64VEXTPROC, "glGetQueryObjectui64vEXT");
#undef LOAD
                    state.supported = state.gen != nullptr && state.erase != nullptr &&
                                      state.counter != nullptr && state.available != nullptr &&
                                      state.result != nullptr;
                }
            }
            return state.supported;
        }

        void poll(State &state) noexcept {
            GLint disjoint = 0;
            glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
            for (auto &slot : state.slots) {
                if (slot.stage != Slot::Stage::Pending) continue;
                GLuint ready = 0;
                state.available(slot.end, GL_QUERY_RESULT_AVAILABLE_EXT, &ready);
                if (ready == 0U) continue;
                auto &series = state.series[slot.label];
                if (disjoint != 0) {
                    ++series.dropped;
                } else {
                    GLuint64 begin = 0;
                    GLuint64 end = 0;
                    state.result(slot.begin, GL_QUERY_RESULT_EXT, &begin);
                    state.result(slot.end, GL_QUERY_RESULT_EXT, &end);
                    if (end >= begin && series.ns.size() < MAX_SAMPLES) {
                        series.ns.push_back(static_cast<std::uint32_t>(
                            std::min<GLuint64>(end - begin, UINT32_MAX)));
                    }
                }
                slot.stage = Slot::Stage::Free;
            }
        }
    }  // namespace

    GpuStopwatch::GpuStopwatch(const char *label) noexcept : m_label(label) {
        if (!enabled()) return;
        auto &state = g_state;
        if (!discover(state)) return;
        poll(state);
        for (std::size_t i = 0; i < state.slots.size(); ++i) {
            auto &slot = state.slots[i];
            if (slot.stage != Slot::Stage::Free) continue;
            if (slot.begin == 0U) {
                GLuint names[2] = {0, 0};
                state.gen(2, names);
                slot.begin = names[0];
                slot.end = names[1];
            }
            if (slot.begin == 0U || slot.end == 0U) break;
            slot.label = label;
            slot.stage = Slot::Stage::Open;
            state.counter(slot.begin, GL_TIMESTAMP_EXT);
            m_slot = static_cast<int>(i);
            return;
        }
        ++state.series[label].dropped;
    }

    GpuStopwatch::~GpuStopwatch() {
        if (m_slot < 0) return;
        auto &state = g_state;
        auto &slot = state.slots[static_cast<std::size_t>(m_slot)];
        if (slot.stage != Slot::Stage::Open || eglGetCurrentContext() != state.context) {
            slot.stage = Slot::Stage::Free;
            return;
        }
        state.counter(slot.end, GL_TIMESTAMP_EXT);
        slot.stage = Slot::Stage::Pending;
    }

    std::string gpu_stopwatch_report() {
        std::string out;
        auto &state = g_state;
        if (state.supported && eglGetCurrentContext() == state.context) poll(state);
        for (auto &[label, series] : state.series) {
            auto sorted = series.ns;
            std::ranges::sort(sorted);
            const auto AT = [&](double q) {
                return sorted.empty()
                    ? 0.0
                    : sorted[static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1))] /
                          1000.0;
            };
            double sum = 0;
            for (const auto value : sorted) sum += value;
            char line[256];
            std::snprintf(line,
                sizeof line,
                "gpu-stopwatch %s: n=%zu mean=%.1fus p50=%.1fus p95=%.1fus max=%.1fus dropped=%llu\n",
                label.c_str(),
                sorted.size(),
                sorted.empty() ? 0.0 : sum / static_cast<double>(sorted.size()) / 1000.0,
                AT(0.5),
                AT(0.95),
                sorted.empty() ? 0.0 : sorted.back() / 1000.0,
                static_cast<unsigned long long>(series.dropped));
            out += line;
        }
        return out;
    }

    void gpu_stopwatch_release() noexcept {
        auto &state = g_state;
        if (state.supported && eglGetCurrentContext() == state.context && state.erase != nullptr) {
            for (auto &slot : state.slots) {
                if (slot.begin != 0U) {
                    const GLuint names[2] = {slot.begin, slot.end};
                    state.erase(2, names);
                }
            }
        }
        state.slots = {};
        state.series.clear();
    }
}  // namespace hyprimate::perf
