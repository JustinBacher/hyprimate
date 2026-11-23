// Build this executable with src/perf/GpuTimer.cpp, EGL and GLESv2, not
// hyprimate-core: this recording sink isolates session attribution while all
// query execution, availability, nesting and context handling use real GLES.
#include "GlHarness.hpp"
#include "perf/GpuTimer.hpp"

#include <chrono>
#include <cstdio>
#include <thread>
#include <semaphore>

#include <GLES2/gl2ext.h>

namespace {
    bool collecting = false;
    std::uint64_t session = 1;
    unsigned samples = 0;
    unsigned drops = 0;
    unsigned stale = 0;
    unsigned disjoint_reads = 0;
    hyprimate::perf::GpuStatus status = hyprimate::perf::GpuStatus::NoContext;
    constexpr hyprimate::perf::Key KEY{hyprimate::perf::Domain::Window, "test", "draw"};
    int failures = 0;
#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); ++failures; \
} } while (false)
    void draw() {
        hyprimate::perf::GpuScope scope(KEY);
        glClearColor(0.1F, 0.2F, 0.3F, 1.F);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    void drain(unsigned expected) {
        // Only the test yields between nonblocking polls. Production never waits.
        glFlush();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (samples < expected && std::chrono::steady_clock::now() < deadline) {
            hyprimate::perf::poll_gpu();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(samples >= expected);
    }
}

namespace hyprimate::perf {
    bool enabled() noexcept { return collecting; }
    std::uint64_t generation() noexcept { return session; }
    void gpu_sample(Key key, std::uint64_t origin, std::uint64_t) noexcept {
        if (origin != session || !collecting) { ++stale; return; }
        CHECK(key.domain == KEY.domain && key.style == KEY.style && key.operation == KEY.operation);
        ++samples;
    }
    void gpu_dropped(std::uint64_t origin) noexcept { if (origin == session) ++drops; }
    void gpu_status(std::uint64_t origin, GpuStatus value) noexcept {
        if (origin == session) status = value;
    }
}

// Link this test with -Wl,--wrap=glGetIntegerv. Only observe the real GL
// invocation; never replace its state or synthesize query results.
extern "C" void __real_glGetIntegerv(GLenum pname, GLint *value);
extern "C" void __wrap_glGetIntegerv(GLenum pname, GLint *value) {
    if (pname == GL_GPU_DISJOINT_EXT) ++disjoint_reads;
    __real_glGetIntegerv(pname, value);
}

int main() {
    using namespace hyprimate::perf;
    // This must not require an EGL context or change support status.
    { GpuScope disabled(KEY); }
    CHECK(status == GpuStatus::NoContext);
    collecting = true;
    { GpuScope absent(KEY); }
    CHECK(status == GpuStatus::NoContext);
    gl_harness::CContext fixture(32, 32);
    if (!fixture.valid()) return 1;
    draw();
    if (status == GpuStatus::Unsupported) {
        std::fputs("SKIP: GL_EXT_disjoint_timer_query elapsed counters unavailable\n", stderr);
        release_gpu();
        return 77;
    }
    CHECK(status == GpuStatus::Available || status == GpuStatus::Disjoint);
    // Repeated polling exercises the pending path without reading unavailable results.
    poll_gpu();
    if (status == GpuStatus::Disjoint) draw();
    drain(1);

    const auto gen = reinterpret_cast<PFNGLGENQUERIESEXTPROC>(eglGetProcAddress("glGenQueriesEXT"));
    const auto begin = reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(eglGetProcAddress("glBeginQueryEXT"));
    const auto end = reinterpret_cast<PFNGLENDQUERYEXTPROC>(eglGetProcAddress("glEndQueryEXT"));
    const auto target = reinterpret_cast<PFNGLGETQUERYIVEXTPROC>(eglGetProcAddress("glGetQueryivEXT"));
    const auto erase = reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(eglGetProcAddress("glDeleteQueriesEXT"));
    draw();  // Leave one own sample pending before a foreign elapsed query starts.
    GLuint caller = 0;
    gen(1, &caller);
    begin(GL_TIME_ELAPSED_EXT, caller);
    const auto before_drops = drops;
    const auto before_disjoint_reads = disjoint_reads;
    poll_gpu();
    CHECK(drops == before_drops + 1);
    CHECK(disjoint_reads == before_disjoint_reads);
    draw();
    poll_gpu();
    GLint active = 0;
    target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &active);
    CHECK(static_cast<GLuint>(active) == caller);
    CHECK(drops == before_drops + 2);  // Skipped scope plus invalidated pending sample.
    CHECK(disjoint_reads == before_disjoint_reads);
    // Cleanup must leave the caller's active query untouched as well.
    release_gpu();
    target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &active);
    CHECK(static_cast<GLuint>(active) == caller);
    end(GL_TIME_ELAPSED_EXT);
    erase(1, &caller);

    const auto before_nested = drops;
    {
        GpuScope outer(KEY);
        GpuScope inner(KEY);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    CHECK(drops == before_nested + 1);
    drain(samples + 1);

    // Generation changes must reject an outstanding old-session result, even if
    // availability becomes true during the following poll.
    draw();
    const auto before_reset = samples;
    ++session;
    poll_gpu();
    CHECK(samples == before_reset);
    CHECK(stale == 0);
    draw();
    drain(before_reset + 1);
    // Also reset while an elapsed query is still active.
    {
        GpuScope old(KEY);
        ++session;
        glClear(GL_COLOR_BUFFER_BIT);
    }
    target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &active);
    CHECK(active == 0);
    // Abandoning a scope in another context must neither end that context's
    // query nor leave our original elapsed target permanently occupied.
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext original = eglGetCurrentContext();
    const EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    EGLint config_id = 0, count = 0;
    EGLConfig config{};
    CHECK(eglQueryContext(display, original, EGL_CONFIG_ID, &config_id));
    const EGLint filter[]{EGL_CONFIG_ID, config_id, EGL_NONE};
    CHECK(eglChooseConfig(display, filter, &config, 1, &count) && count == 1);
    const EGLint attributes[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLContext alternate = eglCreateContext(display, config, EGL_NO_CONTEXT, attributes);
    CHECK(alternate != EGL_NO_CONTEXT);
    if (alternate != EGL_NO_CONTEXT) {
        GLuint alternate_query = 0;
        {
            GpuScope abandoned(KEY);
            CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, alternate));
            gen(1, &alternate_query);
            begin(GL_TIME_ELAPSED_EXT, alternate_query);
        }
        target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &active);
        CHECK(static_cast<GLuint>(active) == alternate_query);
        end(GL_TIME_ELAPSED_EXT);
        erase(1, &alternate_query);
        // Establish independently owned timer resources in the alternate context.
        GLuint owned_query = 0;
        {
            GpuScope owned(KEY);
            target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &active);
            owned_query = static_cast<GLuint>(active);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        CHECK(eglMakeCurrent(display, surface, surface, original));
        poll_gpu();
        target(GL_TIME_ELAPSED_EXT, GL_CURRENT_QUERY_EXT, &active);
        CHECK(active == 0);
        draw();
        drain(samples + 1);
        std::binary_semaphore occupied(0), relinquish(0);
        bool worker_bound = false, retained = false;
        std::thread worker([&] {
            worker_bound = eglMakeCurrent(
                display, EGL_NO_SURFACE, EGL_NO_SURFACE, alternate) == EGL_TRUE;
            occupied.release();
            relinquish.acquire();
            if (worker_bound) {
                retained = glIsQuery(owned_query) == GL_TRUE;
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            }
            eglReleaseThread();
        });
        occupied.acquire();
        CHECK(worker_bound);
        release_gpu();  // Alternate is busy: its slots must survive for retry.
        relinquish.release();
        worker.join();
        CHECK(retained);
        release_gpu();  // Now alternate can be rebound and safely retired.
        CHECK(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, alternate));
        CHECK(glIsQuery(owned_query) == GL_FALSE);
        CHECK(eglMakeCurrent(display, surface, surface, original));
        CHECK(eglGetCurrentContext() == original);
        CHECK(eglGetCurrentSurface(EGL_DRAW) == surface);
        CHECK(eglDestroyContext(display, alternate));
    }
    collecting = false;
    poll_gpu();
    CHECK(stale == 0);
    release_gpu();
    CHECK(glGetError() == GL_NO_ERROR);  // Test-only: production never consumes GL errors.
    return failures ? 1 : 0;
}
