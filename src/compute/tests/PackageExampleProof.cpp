// Build with CMake target compute-package-example-tests; CTest supplies the fixture root.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "shader/GlesRuntime.hpp"
#include "compute/Compute.hpp"
#include "GlHarness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace hyprimate;
using Pixel = std::array<std::uint8_t, 4>;
constexpr int W = 512, H = 512, OX = 7, OY = 11;
constexpr Pixel sentinel{9, 13, 19, 255};
constexpr std::array<Pixel, 4> colors{{{31, 67, 103, 255}, {61, 97, 139, 255},
                                        {89, 43, 157, 255}, {113, 149, 53, 255}}};

bool current(void* owner) noexcept {
    return eglGetCurrentContext() == *static_cast<EGLContext*>(owner);
}
compute::GlResolver::Proc resolve(const char* name, void*) noexcept {
    return reinterpret_cast<compute::GlResolver::Proc>(eglGetProcAddress(name));
}

bool require(bool condition, const char* label) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", label);
    return condition;
}

struct Images {
    shader::Source source{};
    shader::Target target{};
    bool valid = false;
    Images() {
        // Rows here are GL bottom-first. The source default UV must preserve them.
        const std::array<Pixel, 4> pixels = colors;
        glGenTextures(1, &source.texture);
        glBindTexture(GL_TEXTURE_2D, source.texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 2, 2);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        source.width = source.height = 2;
        glGenTextures(1, &target.texture);
        glBindTexture(GL_TEXTURE_2D, target.texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W + 2 * OX, H + 2 * OY);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &target.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
        valid = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        target.width = W; target.height = H;
        target.origin_x = OX; target.origin_y = OY;
        glDisable(GL_SCISSOR_TEST);
        const GLfloat clear[] = {9.F / 255, 13.F / 255, 19.F / 255, 1.F};
        glClearBufferfv(GL_COLOR, 0, clear);
    }
    ~Images() {
        glDeleteFramebuffers(1, &target.framebuffer);
        glDeleteTextures(1, &target.texture);
        glDeleteTextures(1, &source.texture);
    }
    std::vector<Pixel> read() const {
        std::vector<Pixel> pixels((W + 2 * OX) * (H + 2 * OY));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.framebuffer);
        glReadPixels(0, 0, W + 2 * OX, H + 2 * OY, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return pixels;
    }
};
const Pixel& at(const std::vector<Pixel>& image, int x, int y) {
    return image[(y + OY) * (W + 2 * OX) + x + OX];
}
bool near(Pixel a, Pixel b, int tolerance = 1) {
    for (int i = 0; i < 4; ++i)
        if (std::abs(int(a[i]) - int(b[i])) > tolerance) return false;
    return true;
}

// Independent scalar CPU oracle for the fixture's two impulses and Jacobi kernel.
std::vector<float> stencil(unsigned steps) {
    std::vector<float> a(256 * 256), b(a.size());
    a[64 * 256 + 85] = 1.F;
    a[192 * 256 + 170] = .6F;
    for (unsigned n = 0; n < steps; ++n) {
        for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
            const int i = y * 256 + x;
            const float c = a[i];
            const float left = x ? a[i - 1] : 0.F, right = x < 255 ? a[i + 1] : 0.F;
            const float down = y ? a[i - 256] : 0.F, up = y < 255 ? a[i + 256] : 0.F;
            b[i] = c + .2F * (left + right + down + up - 4.F * c);
        }
        a.swap(b);
    }
    return a;
}
Pixel sampled_source(int pixel_x, int pixel_y) {
    // The package renderer intentionally binds its linear sampler for host images.
    const float x = std::clamp(2.F * (pixel_x + .5F) / W - .5F, 0.F, 1.F);
    const float y = std::clamp(2.F * (pixel_y + .5F) / H - .5F, 0.F, 1.F);
    Pixel result{};
    for (unsigned c = 0; c < 4; ++c) {
        const float low = colors[0][c] * (1.F - x) + colors[1][c] * x;
        const float high = colors[2][c] * (1.F - x) + colors[3][c] * x;
        result[c] = static_cast<std::uint8_t>(std::lround(low * (1.F - y) + high * y));
    }
    return result;
}
Pixel stencil_pixel(float energy, Pixel source) {
    const float intensity = std::clamp(energy * 85.F, 0.F, 1.F);
    constexpr std::array<float, 3> tint{1.F, .26F, .06F};
    Pixel result = source;
    for (int i = 0; i < 3; ++i)
        result[i] = static_cast<std::uint8_t>(std::lround((source[i] * (1.F - intensity)) + 255.F * tint[i] * intensity));
    return result;
}

bool run(const std::filesystem::path& root, compute::Backend backend,
         compute::GlResolver resolver, bool particle) {
    const char* name = particle ? "compute-particles" : "compute-stencil";
    const char* route = backend == compute::Backend::Native ? "native" : "slibs";
    std::printf("Checking %s on %s\n", name, route);
    auto loaded = shader::load_package((root / name / "effect.json").string());
    if (!loaded) { std::fprintf(stderr, "load %s: %s\n", name, loaded.error().c_str()); return false; }
    auto values = shader::parameter_values(*loaded);
    if (!values) { std::fprintf(stderr, "parameters: %s\n", values.error().c_str()); return false; }
    shader::Program program;
    shader::Instance moving, frozen;
    std::string error;
    if (!program.initialize(std::move(*loaded), *values, error, eglGetProcAddress, resolver, backend) ||
        !moving.initialize(program, W, H, error) ||
        (particle && !frozen.initialize(program, W, H, error))) {
        std::fprintf(stderr, "prepare %s/%s: %s\n", name, route, error.c_str()); return false;
    }
    Images images;
    if (!require(images.valid, "RGBA8 framebuffer complete")) return false;
    shader::Frame frame{};
    frame.tick = 1; frame.seed = 12345; frame.event = shader::Event::Open;
    frame.progress = 1.F; frame.width = W; frame.height = H;
    moving.advance(frame);
    if (!moving.render(images.source, images.target, true, error)) {
        std::fprintf(stderr, "initial render: %s\n", error.c_str()); return false;
    }
    const auto initial = images.read();
    const auto before = moving.compute_statistics();
    const auto passes_before = moving.statistics();
    // No explicit transfers, glReadPixels, or CPU simulation occur in this warm interval.
    for (unsigned tick = 2; tick <= 33; ++tick) {
        frame.tick = tick; frame.delta = .05F; frame.elapsed = float(tick - 1) * .05F;
        moving.advance(frame);
        if (!moving.render(images.source, images.target, true, error)) {
            std::fprintf(stderr, "warm render tick %u: %s\n", tick, error.c_str()); return false;
        }
    }
    const auto after = moving.compute_statistics();
    const auto passes_after = moving.statistics();
    const auto final = images.read();
    bool ok = true;
    const auto expected = std::uint64_t{32} * (particle ? 1 : 8);
    ok &= require(passes_after.update_passes - passes_before.update_passes == expected, "exact update iteration count");
    ok &= require(after.native_dispatches - before.native_dispatches == (backend == compute::Backend::Native ? expected : 0), "native strict dispatch count");
    ok &= require(after.slibs_dispatches - before.slibs_dispatches == (backend == compute::Backend::Slibs ? expected : 0), "SLIBS strict dispatch count");
    ok &= require(after.upload_bytes == before.upload_bytes && after.readback_bytes == before.readback_bytes &&
                  after.copy_bytes == before.copy_bytes && passes_after.upload_bytes == passes_before.upload_bytes &&
                  passes_after.copy_bytes == passes_before.copy_bytes, "no warm simulation transfers");
    ok &= require(at(final, -1, -1) == sentinel && at(final, W, H) == sentinel, "off-origin target preserves exterior");
    // The marker's exact UNORM encoding, with the two legal representations of the .30 tie.
    const Pixel marker = at(final, 64, 58);
    ok &= require(particle ? (marker == Pixel{242, 76, 191, 255} || marker == Pixel{242, 77, 191, 255})
                           : marker == Pixel{20, 235, 168, 255}, "marker exact color and orientation");
    if (!particle) {
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x)
            ok &= require(at(final, x ? 480 : 20, y ? 480 : 20) == colors[y * 2 + x], "default source orientation and opaque color");
        const auto oracle = stencil(8 * 33);
        // These pixels address exact texels around each impulse in the 256x256 field.
        for (const auto cell : {std::array<int, 2>{85, 64}, {88, 64}, {170, 192}, {173, 192}}) {
            const int x = cell[0], y = cell[1];
            const Pixel expected_pixel = stencil_pixel(oracle[y * 256 + x], sampled_source(2*x, 2*y));
            ok &= require(near(at(final, 2*x, 2*y), expected_pixel, 2), "stencil rendered pixel matches CPU diffusion oracle");
        }
        ok &= require(at(initial, 170, 128) != at(final, 170, 128), "stencil pixel changes after 32 updates");
    } else {
        // Same seed and tick count, but zero delta: isolates vertex-stage sampling of
        // updated positions from ordinary source/marker rendering and frame changes.
        frame.tick = 1; frame.delta = 0; frame.elapsed = 0;
        frozen.advance(frame);
        if (!frozen.render(images.source, images.target, true, error)) {
            std::fprintf(stderr, "frozen render: %s\n", error.c_str()); return false;
        }
        const auto control = images.read();
        unsigned changed = 0, colored = 0;
        for (int y = 20; y < H - 20; ++y) for (int x = 20; x < W - 20; ++x) {
            const Pixel& a = at(final, x, y); const Pixel& b = at(control, x, y);
            if (a != b) {
                ++changed;
                const Pixel base = colors[(y >= H/2) * 2 + (x >= W/2)];
                // Particle fragments are premultiplied blends of their blue/gold palette.
                if (a[3] == 255 && a[1] > base[1] && (a[0] != base[0] || a[2] != base[2])) ++colored;
            }
        }
        ok &= require(changed > 100 && colored > 100, "updated particle vertex samples alter colored pixels versus same-seed frozen control");
    }
    if (ok) std::printf("PASS %s %s: %llu warm dispatches\n", name, route, static_cast<unsigned long long>(expected));
    return ok;
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 2) { std::fprintf(stderr, "usage: %s [examples/shaders]\n", argv[0]); return 2; }
    gl_harness::CContext context{64, 64};
    if (!context.valid()) { std::fprintf(stderr, "SKIP: no GLES context\n"); return 77; }
    EGLContext owner = eglGetCurrentContext();
    compute::GlResolver resolver{resolve, current, &owner};
    compute::Device probe;
    auto initialized = probe.initialize(resolver);
    if (!initialized) { std::fprintf(stderr, "SKIP: compute unavailable: %s\n", initialized.error().c_str()); return 77; }
    const auto capabilities = probe.capabilities();
    std::printf("GLES %s; %s; %s\n", capabilities.version.c_str(), capabilities.vendor.c_str(), capabilities.renderer.c_str());
    if (auto released = probe.release(); !released) { std::fprintf(stderr, "probe release: %s\n", released.error().c_str()); return 1; }
    const std::filesystem::path root = argc == 2 ? argv[1] : "examples/shaders";
    bool ok = true; unsigned tested = 0;
    for (const auto backend : {compute::Backend::Native, compute::Backend::Slibs}) {
        const auto& limits = backend == compute::Backend::Native ? capabilities.native : capabilities.slibs;
        if (!limits.available) {
            std::fprintf(stderr, "SKIP %s: %s\n", backend == compute::Backend::Native ? "native" : "slibs", limits.reason.c_str());
            continue;
        }
        ++tested;
        ok &= run(root, backend, resolver, false);
        ok &= run(root, backend, resolver, true);
    }
    return !tested ? 77 : ok ? 0 : 1;
}
