// CMake target: compute-extended-state-tests. Link shader-gles and PkgConfig::TEST_EGL,
// include tests/, and run with EGL_PLATFORM=surfaceless. CLI: --backend=all|auto|native|slibs.
// `all` (the default) runs each supported strict route; absent extensions are reported separately.
#include "compute/Compute.hpp"
#include "shader/GlesRuntime.hpp"
#include "Check.hpp"
#include "GlHarness.hpp"

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <utility>

namespace {
bool fail_next_texture_storage = false;
bool texture_storage_fault_triggered = false;
GLuint failed_texture = 0;
}

extern "C" void GL_APIENTRY __real_glTexStorage2D(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
extern "C" void GL_APIENTRY __wrap_glTexStorage2D(
    GLenum target, GLsizei levels, GLenum format, GLsizei width, GLsizei height) {
    if (fail_next_texture_storage) {
        fail_next_texture_storage = false;
        texture_storage_fault_triggered = true;
        GLint bound = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
        failed_texture = static_cast<GLuint>(bound);
        __real_glTexStorage2D(target, levels, format, -1, height);
        return;
    }
    __real_glTexStorage2D(target, levels, format, width, height);
}

namespace {
namespace compute = hyprimate::compute;
namespace shader = hyprimate::shader;
using Pixel = std::array<std::uint8_t, 4>;
constexpr unsigned W = 12, H = 8, OX = 3, OY = 2, BW = W + 2 * OX, BH = H + 2 * OY;
constexpr Pixel SENTINEL{9, 13, 19, 255}, MSAA_CLEAR{17, 31, 47, 255}, DRAW_COLOR{220, 40, 90, 255};
constexpr shader::Ref OUTPUT{shader::ResourceKind::Output, 0, shader::Side::Single};
constexpr GLenum SAMPLE_MASK = 0x8E51, SAMPLE_MASK_VALUE = 0x8E52;
constexpr GLenum CLIP_DISTANCE0 = 0x3000, MAX_CLIP_DISTANCES = 0x0D32;
constexpr GLenum MAX_VIEWPORTS = 0x825B;
constexpr GLenum WINDOW_MODE = 0x8F13, MAX_WINDOWS = 0x8F14, WINDOW_COUNT = 0x8F15;
constexpr GLenum INCLUSIVE = 0x8F10, EXCLUSIVE = 0x8F11;

struct Owner { EGLContext context = EGL_NO_CONTEXT; };
bool current(void* p) noexcept {
    const auto* owner = static_cast<const Owner*>(p);
    return owner && eglGetCurrentContext() == owner->context;
}
compute::GlResolver::Proc resolve_compute(const char* name, void*) noexcept {
    return reinterpret_cast<compute::GlResolver::Proc>(eglGetProcAddress(name));
}
template<class T> T proc(const char* name) { return reinterpret_cast<T>(eglGetProcAddress(name)); }
bool extension(std::string_view wanted) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* value = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
        if (value && wanted == reinterpret_cast<const char*>(value)) return true;
    }
    return false;
}
const char* backend_name(compute::Backend backend) {
    return backend == compute::Backend::Auto ? "auto" :
           backend == compute::Backend::Native ? "native" : "slibs";
}
bool fail(const char* label, std::string_view detail = {}) {
    std::fprintf(stderr, "FAIL: %s%s%.*s\n", label, detail.empty() ? "" : ": ",
                 static_cast<int>(detail.size()), detail.data());
    report(false, label, __FILE__, __LINE__);
    return false;
}
#define REQUIRE(c, label) do { if (!(c)) return fail(label); } while (false)

using GetIndexedFloat = void (GL_APIENTRYP)(GLenum, GLuint, GLfloat*);
using SetIndexedFloat = void (GL_APIENTRYP)(GLuint, const GLfloat*);
using SetIndexedInt = void (GL_APIENTRYP)(GLuint, const GLint*);
using SetIndexedDepth = void (GL_APIENTRYP)(GLuint, GLfloat, GLfloat);
using ToggleIndexed = void (GL_APIENTRYP)(GLenum, GLuint);
using IsEnabledIndexed = GLboolean (GL_APIENTRYP)(GLenum, GLuint);
using SampleMaskProc = void (GL_APIENTRYP)(GLuint, GLbitfield);
using WindowRectsProc = void (GL_APIENTRYP)(GLenum, GLsizei, const GLint*);

struct ViewportApi {
    GetIndexedFloat get_float{};
    SetIndexedFloat viewport{};
    SetIndexedInt scissor{};
    SetIndexedDepth depth{};
    ToggleIndexed enable{}, disable{};
    IsEnabledIndexed is_enabled{};
    GLint count{};
    const char* name{};
    bool load(const char* suffix, const char* extension_name) {
        const auto find = [suffix](const char* base) {
            return std::string(base) + suffix;
        };
        get_float = proc<GetIndexedFloat>(find("glGetFloati_v").c_str());
        viewport = proc<SetIndexedFloat>(find("glViewportIndexedfv").c_str());
        scissor = proc<SetIndexedInt>(find("glScissorIndexedv").c_str());
        depth = proc<SetIndexedDepth>(find("glDepthRangeIndexedf").c_str());
        enable = proc<ToggleIndexed>(find("glEnablei").c_str());
        disable = proc<ToggleIndexed>(find("glDisablei").c_str());
        is_enabled = proc<IsEnabledIndexed>(find("glIsEnabledi").c_str());
        if (!get_float || !viewport || !scissor || !depth || !enable || !disable || !is_enabled) return false;
        glGetIntegerv(MAX_VIEWPORTS, &count);
        name = extension_name;
        return count > 0;
    }
    bool available() const { return name != nullptr; }
};

struct HostState {
    bool indexed{}, pair{}, sample_supported{}, clip_supported{}, windows_supported{};
    std::array<std::array<GLfloat, 4>, 2> viewport{};
    std::array<std::array<GLint, 4>, 2> scissor{};
    std::array<std::array<GLfloat, 2>, 2> depth{};
    std::array<GLboolean, 2> scissor_enabled{};
    GLint draw_fbo{}, read_fbo{}, program{}, vao{}, active_texture{}, sample_value{}, window_mode{}, window_count{};
    GLboolean sample_enabled{}, clip_enabled{};
    bool operator==(const HostState&) const = default;
};

class StateFixture {
  public:
    StateFixture(GLint major, GLint minor) : major_(major), minor_(minor) {
        if (extension("GL_OES_viewport_array")) (void)viewport_.load("OES", "GL_OES_viewport_array");
        if (!viewport_.available() && extension("GL_NV_viewport_array"))
            (void)viewport_.load("NV", "GL_NV_viewport_array");
        sample_mask_ = (major_ > 3 || (major_ == 3 && minor_ >= 1)) && proc<SampleMaskProc>("glSampleMaski");
        sample_mask_proc_ = proc<SampleMaskProc>("glSampleMaski");
        const bool clip_ext = extension("GL_EXT_clip_cull_distance") || extension("GL_APPLE_clip_distance");
        clip_supported_ = (major_ > 3 || (major_ == 3 && minor_ >= 2) || clip_ext);
        if (clip_supported_) {
            GLint limit = 0;
            glGetIntegerv(MAX_CLIP_DISTANCES, &limit);
            clip_supported_ = limit > 0;
        }
        windows_supported_ = extension("GL_EXT_window_rectangles");
        original_ = capture();
        install();
        expected_ = capture();
        if (!viewport_.available())
            std::printf("UNVERIFIED: GL_OES_viewport_array and GL_NV_viewport_array indexed viewport/scissor/depth state; extensions or entrypoints unavailable\n");
        else if (viewport_.count < 2)
            std::printf("UNVERIFIED: %s index 1; GL_MAX_VIEWPORTS=%d\n", viewport_.name, viewport_.count);
        if (!sample_mask_)
            std::printf("UNVERIFIED: GLES 3.1 GL_SAMPLE_MASK/glSampleMaski state; context version or procedure unavailable\n");
        if (!clip_supported_)
            std::printf("UNVERIFIED: GL_EXT_clip_cull_distance/GL_APPLE_clip_distance clip state; no clip distance is exposed\n");
        if (!windows_supported_)
            std::printf("UNVERIFIED: GL_EXT_window_rectangles state; extension absent\n");
    }
    ~StateFixture() { restore(original_); }
    StateFixture(const StateFixture&) = delete;
    StateFixture& operator=(const StateFixture&) = delete;

    HostState capture() const {
        HostState s{};
        s.indexed = viewport_.available();
        s.pair = s.indexed && viewport_.count >= 2;
        s.sample_supported = sample_mask_;
        s.clip_supported = clip_supported_;
        s.windows_supported = windows_supported_;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s.draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s.read_fbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &s.program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &s.active_texture);
        const unsigned n = s.pair ? 2 : 1;
        if (s.indexed) {
            for (unsigned i = 0; i < n; ++i) {
                viewport_.get_float(GL_VIEWPORT, i, s.viewport[i].data());
                glGetIntegeri_v(GL_SCISSOR_BOX, i, s.scissor[i].data());
                viewport_.get_float(GL_DEPTH_RANGE, i, s.depth[i].data());
                s.scissor_enabled[i] = viewport_.is_enabled(GL_SCISSOR_TEST, i);
            }
        } else {
            std::array<GLint, 4> v{};
            glGetIntegerv(GL_VIEWPORT, v.data());
            for (std::size_t i = 0; i < v.size(); ++i) s.viewport[0][i] = static_cast<GLfloat>(v[i]);
            glGetIntegerv(GL_SCISSOR_BOX, s.scissor[0].data());
            glGetFloatv(GL_DEPTH_RANGE, s.depth[0].data());
            s.scissor_enabled[0] = glIsEnabled(GL_SCISSOR_TEST);
        }
        if (sample_mask_) {
            glGetIntegeri_v(SAMPLE_MASK_VALUE, 0, &s.sample_value);
            s.sample_enabled = glIsEnabled(SAMPLE_MASK);
        }
        if (clip_supported_) s.clip_enabled = glIsEnabled(CLIP_DISTANCE0);
        if (windows_supported_) {
            glGetIntegerv(WINDOW_MODE, &s.window_mode);
            glGetIntegerv(WINDOW_COUNT, &s.window_count);
        }
        return s;
    }
    bool unchanged(const char* operation) const {
        const bool same = capture() == expected_;
        if (!same) std::fprintf(stderr, "FAIL: host viewport/scissor/depth/sample/clip state changed after %s\n", operation);
        CHECK(same);
        return same;
    }
    const ViewportApi& viewport_api() const { return viewport_; }
    bool sample_supported() const { return sample_mask_; }
    bool clip_supported() const { return clip_supported_; }

    void restore(const HostState& s) const {
        if (viewport_.available()) {
            const unsigned n = s.pair ? 2 : 1;
            for (unsigned i = 0; i < n; ++i) {
                viewport_.viewport(i, s.viewport[i].data());
                viewport_.scissor(i, s.scissor[i].data());
                viewport_.depth(i, s.depth[i][0], s.depth[i][1]);
                (s.scissor_enabled[i] ? viewport_.enable : viewport_.disable)(GL_SCISSOR_TEST, i);
            }
        } else {
            const auto& v = s.viewport[0];
            glViewport(static_cast<GLint>(v[0]), static_cast<GLint>(v[1]),
                       static_cast<GLsizei>(v[2]), static_cast<GLsizei>(v[3]));
            glScissor(s.scissor[0][0], s.scissor[0][1], s.scissor[0][2], s.scissor[0][3]);
            glDepthRangef(s.depth[0][0], s.depth[0][1]);
            if (s.scissor_enabled[0]) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        }
        if (sample_mask_) {
            sample_mask_proc_(0, static_cast<GLbitfield>(s.sample_value));
            if (s.sample_enabled) glEnable(SAMPLE_MASK); else glDisable(SAMPLE_MASK);
        }
        if (clip_supported_) {
            if (s.clip_enabled) glEnable(CLIP_DISTANCE0); else glDisable(CLIP_DISTANCE0);
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(s.draw_fbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(s.read_fbo));
        glUseProgram(static_cast<GLuint>(s.program));
        glBindVertexArray(static_cast<GLuint>(s.vao));
        glActiveTexture(static_cast<GLenum>(s.active_texture));
    }

  private:
    GLint major_{}, minor_{};
    bool sample_mask_{}, clip_supported_{}, windows_supported_{};
    SampleMaskProc sample_mask_proc_{};
    ViewportApi viewport_{};
    HostState original_{}, expected_{};

    void install() {
        if (viewport_.available()) {
            const std::array<GLfloat, 4> v0{0.25F, 0.5F, 23.75F, 17.5F};
            const std::array<GLfloat, 4> v1{2.5F, 1.25F, 11.25F, 9.5F};
            const std::array<GLint, 4> s0{0, 0, 64, 64}, s1{7, 9, 3, 4};
            viewport_.viewport(0, v0.data());
            viewport_.scissor(0, s0.data());
            viewport_.depth(0, 0.125F, 0.875F);
            viewport_.enable(GL_SCISSOR_TEST, 0);
            if (viewport_.count >= 2) {
                viewport_.viewport(1, v1.data());
                viewport_.scissor(1, s1.data());
                viewport_.depth(1, 0.25F, 0.75F);
                viewport_.disable(GL_SCISSOR_TEST, 1);
            }
        } else {
            glViewport(1, 2, 24, 18);
            glScissor(0, 0, 64, 64);
            glDepthRangef(0.125F, 0.875F);
            glEnable(GL_SCISSOR_TEST);
        }
        if (sample_mask_) {
            sample_mask_proc_(0, 0);
            glEnable(SAMPLE_MASK);
        }
        if (clip_supported_) glEnable(CLIP_DISTANCE0);
    }
};

struct Images {
    shader::Source source{};
    shader::Target target{};
    GLuint source_name{}, target_name{};
    bool valid{};
    explicit Images(unsigned width = W, unsigned height = H) {
        glGenTextures(1, &source_name);
        glBindTexture(GL_TEXTURE_2D, source_name);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 1, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        constexpr Pixel white{255, 255, 255, 255};
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
        source = {source_name, 1, 1};
        glGenTextures(1, &target_name);
        glBindTexture(GL_TEXTURE_2D, target_name);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width + 2 * OX, height + 2 * OY);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &target.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_name, 0);
        valid = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        target.texture = target_name;
        target.width = width; target.height = height; target.origin_x = OX; target.origin_y = OY;
        glViewport(0, 0, width + 2 * OX, height + 2 * OY);
        glDisable(GL_SCISSOR_TEST);
        constexpr std::array<GLfloat, 4> clear{9.F / 255, 13.F / 255, 19.F / 255, 1.F};
        glClearBufferfv(GL_COLOR, 0, clear.data());
    }
    ~Images() {
        if (target.framebuffer) glDeleteFramebuffers(1, &target.framebuffer);
        if (target_name) glDeleteTextures(1, &target_name);
        if (source_name) glDeleteTextures(1, &source_name);
    }
    Pixel pixel(int x, int y) const {
        GLint previous = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.framebuffer);
        Pixel p{};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous));
        return p;
    }
};

class MsaaTarget {
  public:
    MsaaTarget() {
        GLint max_samples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
        if (max_samples < 2) return;
        glGenRenderbuffers(1, &color_);
        glBindRenderbuffer(GL_RENDERBUFFER, color_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, std::min(max_samples, 4), GL_RGBA8, 8, 8);
        glGenFramebuffers(1, &draw_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return;
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 8, 8);
        glGenFramebuffers(1, &resolve_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return;
        constexpr std::string_view vertex = R"glsl(#version 300 es
precision highp float;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";
        constexpr std::string_view fragment = R"glsl(#version 300 es
precision highp float;
layout(location=0) out vec4 color;
void main() { color = vec4(220.0/255.0, 40.0/255.0, 90.0/255.0, 1.0); }
)glsl";
        program_ = gl_harness::build_program(vertex, fragment);
        glGenVertexArrays(1, &vao_);
        valid_ = program_ && vao_;
    }
    ~MsaaTarget() {
        if (program_) glDeleteProgram(program_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (resolve_fbo_) glDeleteFramebuffers(1, &resolve_fbo_);
        if (draw_fbo_) glDeleteFramebuffers(1, &draw_fbo_);
        if (texture_) glDeleteTextures(1, &texture_);
        if (color_) glDeleteRenderbuffers(1, &color_);
    }
    bool valid() const { return valid_; }
    Pixel render_resolve(const StateFixture& state) const {
        const auto before = state.capture();
        const auto& api = state.viewport_api();
        if (api.available()) {
            const std::array<GLfloat, 4> viewport{0, 0, 8, 8};
            api.viewport(0, viewport.data());
            api.disable(GL_SCISSOR_TEST, 0);
        } else {
            glViewport(0, 0, 8, 8);
            glDisable(GL_SCISSOR_TEST);
        }
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_RASTERIZER_DISCARD);
        if (state.clip_supported()) glDisable(CLIP_DISTANCE0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo_);
        const GLenum attachment = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &attachment);
        constexpr std::array<GLfloat, 4> clear{17.F / 255, 31.F / 255, 47.F / 255, 1.F};
        glClearBufferfv(GL_COLOR, 0, clear.data());
        glBindVertexArray(vao_);
        glUseProgram(program_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, draw_fbo_);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_);
        glDrawBuffers(1, &attachment);
        glBlitFramebuffer(0, 0, 8, 8, 0, 0, 8, 8, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo_);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        Pixel result{};
        glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
        state.restore(before);
        return result;
    }
  private:
    GLuint color_{}, draw_fbo_{}, texture_{}, resolve_fbo_{}, program_{}, vao_{};
    bool valid_{};
};

compute::Type field_type(const compute::Capabilities& caps) {
    if (caps.formats[2]) return {compute::Element::Uint, 1};
    if (caps.formats[3]) return {compute::Element::Uint, 4};
    if (caps.formats[0]) return {compute::Element::Int, 1};
    return {compute::Element::Int, 4};
}
std::vector<std::byte> integer_data(compute::Type type, unsigned count, const std::array<int, 4>& v) {
    std::vector<std::byte> bytes(std::size_t{count} * type.lanes * 4);
    for (unsigned i = 0; i < count; ++i) for (unsigned lane = 0; lane < type.lanes; ++lane) {
        const std::uint32_t word = static_cast<std::uint32_t>(v[lane]);
        std::memcpy(bytes.data() + (std::size_t{i} * type.lanes + lane) * 4, &word, 4);
    }
    return bytes;
}
std::string expression(compute::Type type, const std::array<int, 4>& v) {
    const std::string suffix = type.scalar == compute::Element::Uint ? "u" : "";
    if (type.lanes == 1) return std::to_string(v[0]) + suffix;
    std::string result(compute::type_name(type));
    result += "(";
    for (unsigned i = 0; i < type.lanes; ++i) {
        if (i) result += ",";
        result += std::to_string(v[i]) + suffix;
    }
    return result + ")";
}
compute::KernelDesc constant_kernel(compute::Type type, const char* name, const std::array<int, 4>& v) {
    return {name, "void kernel(ivec3 id, inout " + std::string(compute::type_name(type)) +
            " result){result=" + expression(type, v) + ";}", {}, {{"result", type}}, {}};
}
shader::Package inline_package(compute::Type type) {
    shader::Package package;
    shader::Field state{"state", type, std::nullopt, 1.F,
                        shader::Lifetime::Persistent, false};
    package.fields.push_back(std::move(state));
    shader::Pass seed;
    seed.name = "seed"; seed.phase = shader::Phase::Initialize; seed.compute = true; seed.clear = false;
    seed.kernel = "void kernel(ivec3 id, inout " + std::string(compute::type_name(type)) +
                  " value){value=" + expression(type, {64, 64, 64, 64}) + ";}";
    seed.outputs = {{shader::ResourceKind::Field, 0, shader::Side::Single}};
    shader::Pass draw;
    draw.name = "present"; draw.phase = shader::Phase::Render;
    draw.vertex = std::string(shader::default_vertex());
    const std::string value = std::string("hc_read_state(ivec3(0,0,0))") + (type.lanes > 1 ? ".x" : "");
    draw.fragment = "layout(location=0) out vec4 color;void main(){color=vec4(float(" + value +
                    ")/255.0,float(hc_size_state.x)/64.0,float(hc_size_state.y)/64.0,1.0);}";
    draw.inputs = {{"state", {shader::ResourceKind::Field, 0, shader::Side::Single}}};
    draw.outputs = {OUTPUT};
    package.passes = {std::move(seed), std::move(draw)};
    return package;
}

bool near(Pixel a, Pixel b) {
    for (std::size_t i = 0; i < 4; ++i)
        if (std::abs(int(a[i]) - int(b[i])) > 1) return false;
    return true;
}
bool run_route(compute::GlResolver resolver, const std::shared_ptr<Owner>& owner,
               const compute::Capabilities& caps, compute::Backend preference) {
    compute::Device device;
    if (auto init = device.initialize(resolver, preference); !init) return fail("initialize Device", init.error());
    const auto type = field_type(device.capabilities());
    auto kernel = device.prepare(constant_kernel(type, "extended-state", {61, 62, 63, 64}));
    if (!kernel) return fail("prepare direct kernel", kernel.error());
    if (preference != compute::Backend::Auto && kernel->backend() != preference)
        return fail("strict Device backend selection");
    shader::Program program;
    shader::Instance instance;
    std::string error;
    if (!program.initialize(inline_package(type), {}, error, eglGetProcAddress, resolver, preference, owner))
        return fail("prepare inline shader::Program", error);
    Images images;
    Images resized_images(24, 16);
    REQUIRE(images.valid && resized_images.valid, "create package targets");
    MsaaTarget msaa;
    StateFixture state(caps.major, caps.minor);
    if (!instance.initialize(program, W, H, error)) return fail("initialize shader::Instance", error);
    REQUIRE(state.unchanged("Instance::initialize field allocation"), "Instance initialization preserves GLES state");

    const compute::FieldDesc desc{type, {3, 2, 1}};
    auto field_result = device.allocate(desc);
    if (!field_result) return fail("Device::allocate", field_result.error());
    compute::Field field = std::move(*field_result);
    REQUIRE(state.unchanged("Device::allocate"), "field allocation preserves GLES state");
    const auto expected = integer_data(type, 6, {61, 62, 63, 64});
    {
        const std::array<std::reference_wrapper<const compute::Kernel>, 1> kernels{std::cref(*kernel)};
        auto batch_result = device.begin(kernels);
        if (!batch_result) return fail("Device::begin", batch_result.error());
        auto batch = std::move(*batch_result);
        const std::array<compute::Field*, 1> outputs{&field};
        const std::array<compute::ParameterValue, 0> parameters{};
        if (auto dispatch = batch.dispatch(*kernel, {}, outputs, parameters); !dispatch)
            return fail("Device::Batch::dispatch", dispatch.error());
    }
    REQUIRE(state.unchanged("Device batch"), "batch restores indexed viewports, scissor, depth, sample mask and clip state");
    std::vector<std::byte> actual(expected.size());
    if (auto read = device.readback(field, actual); !read) return fail("read direct result", read.error());
    REQUIRE(actual == expected, "direct Device dispatch produced expected field data");
    REQUIRE(state.unchanged("Device::readback"), "readback preserves host GLES state");

    shader::Frame frame{};
    frame.tick = 1; frame.event = shader::Event::Open; frame.progress = 1.F;
    frame.width = W; frame.height = H;
    instance.advance(frame);
    if (!instance.render(images.source, images.target, true, error)) return fail("live package render", error);
    REQUIRE(state.unchanged("live package render"), "compute-field consumer render preserves host GLES state");
    const auto live = instance.compute_statistics();
    if (!instance.render(images.source, images.target, false, error)) return fail("package replay", error);
    REQUIRE(state.unchanged("package replay"), "read-only replay preserves host GLES state");
    const auto replay = instance.compute_statistics();
    REQUIRE(live.native_dispatches == replay.native_dispatches &&
            live.slibs_dispatches == replay.slibs_dispatches, "replay does not dispatch or advance the field");
    REQUIRE(near(images.pixel(OX + W / 2, OY + H / 2), Pixel{64, 48, 32, 255}),
            "real fragment shader samples the initialized capture-relative compute field");
    REQUIRE(images.pixel(0, 0) == SENTINEL, "off-origin render retains the known host sentinel pixel");
    REQUIRE(state.unchanged("package pixel check"), "pixel checks preserve host GLES state");

    const auto old_compute_bytes = instance.compute_statistics().live_bytes;
    const auto old_instance_bytes = instance.statistics().allocated_bytes;
    fail_next_texture_storage = true;
    texture_storage_fault_triggered = false;
    failed_texture = 0;
    const bool failed_resize = instance.initialize(program, 24, 16, error);
    fail_next_texture_storage = false;
    REQUIRE(!failed_resize && texture_storage_fault_triggered && failed_texture != 0,
            "resize allocation fault reaches immutable texture storage");
    REQUIRE(glIsTexture(failed_texture) == GL_FALSE, "failed immutable texture name is deleted");
    REQUIRE(glGetError() == GL_NO_ERROR, "resize failure consumes the injected GL error");
    REQUIRE(instance.compute_statistics().live_bytes == old_compute_bytes &&
            instance.statistics().allocated_bytes == old_instance_bytes,
            "failed resize preserves old live storage accounting");
    REQUIRE(state.unchanged("failed capture-relative resize"),
            "failed resize restores host GLES state");
    if (!instance.render(images.source, images.target, false, error))
        return fail("replay old instance after failed resize", error);
    REQUIRE(near(images.pixel(OX + W / 2, OY + H / 2), Pixel{64, 48, 32, 255}),
            "failed resize leaves old compute result drawable");
    REQUIRE(state.unchanged("old instance replay after failed resize"),
            "old instance replay preserves host GLES state");

    if (!instance.initialize(program, 24, 16, error))
        return fail("retry capture-relative resize", error);
    REQUIRE(state.unchanged("successful capture-relative resize"),
            "successful resize restores host GLES state");
    frame.tick = 2;
    frame.width = 24; frame.height = 16;
    instance.advance(frame);
    if (!instance.render(resized_images.source, resized_images.target, true, error))
        return fail("render resized compute field", error);
    REQUIRE(near(resized_images.pixel(OX + 12, OY + 8), Pixel{64, 96, 64, 255}),
            "successful resize exposes the new field domain and computed data");
    REQUIRE(state.unchanged("resized capture-relative field render"),
            "resized field render preserves host GLES state");

    if (msaa.valid()) {
        const auto resolved = msaa.render_resolve(state);
        const Pixel expected_pixel = state.sample_supported() ? MSAA_CLEAR : DRAW_COLOR;
        REQUIRE(near(resolved, expected_pixel), "MSAA color target resolves to the expected sample-mask pixel");
        REQUIRE(state.unchanged("MSAA draw and resolve"), "MSAA check restores the host state");
    } else {
        std::printf("UNVERIFIED: GLES 3.1 GL_SAMPLE_MASK resolved-pixel case; no complete multisampled RGBA8 target\n");
    }
    std::printf("PASS: Device + shader::Program/Instance backend=%s selected=%s\n",
                backend_name(preference), backend_name(kernel->backend()));
    return true;
}

bool active_window_case(compute::GlResolver resolver) {
    if (!extension("GL_EXT_window_rectangles")) {
        std::printf("UNVERIFIED: GL_EXT_window_rectangles active-state decline and pool scrub; extension absent\n");
        return true;
    }
    auto set_rectangles = proc<WindowRectsProc>("glWindowRectanglesEXT");
    GLint max_rectangles = 0;
    glGetIntegerv(MAX_WINDOWS, &max_rectangles);
    if (!set_rectangles || max_rectangles < 1) {
        std::printf("UNVERIFIED: GL_EXT_window_rectangles active-state decline; entrypoint/rectangle slot unavailable\n");
        return true;
    }
    compute::Device device;
    if (auto init = device.initialize(resolver, compute::Backend::Slibs); !init)
        return fail("initialize SLIBS active-window case", init.error());
    const auto type = field_type(device.capabilities());
    const compute::FieldDesc desc{type, {4, 1, 1}};
    const auto seed = integer_data(type, 4, {101, 103, 107, 109});
    auto kernel = device.prepare(constant_kernel(type, "window-rect-decline", {211, 223, 227, 229}));
    if (!kernel) return fail("prepare active-window kernel", kernel.error());
    auto field_result = device.allocate(desc);
    if (!field_result) return fail("allocate active-window field", field_result.error());
    compute::Field field = std::move(*field_result);
    if (auto upload = device.upload(field, seed); !upload) return fail("seed active-window field", upload.error());
    const auto before = device.statistics();
    const std::array<GLint, 4> rectangle{0, 0, 1, 1};
    set_rectangles(INCLUSIVE, 1, rectangle.data());
    GLint active_mode = 0, active_count = 0;
    glGetIntegerv(WINDOW_MODE, &active_mode);
    glGetIntegerv(WINDOW_COUNT, &active_count);
    CHECK(active_mode == static_cast<GLint>(INCLUSIVE) && active_count == 1);
    const std::array<std::reference_wrapper<const compute::Kernel>, 1> kernels{std::cref(*kernel)};
    bool rejected = false;
    {
        auto denied = device.begin(kernels);
        rejected = !denied && denied.error().find("active EXT_window_rectangles") != std::string::npos;
        if (denied) {
            auto unexpected_batch = std::move(*denied);
        }
    }
    CHECK(rejected);
    const auto after = device.statistics();
    CHECK(after.pool_hits == before.pool_hits && after.slibs_dispatches == before.slibs_dispatches);
    set_rectangles(EXCLUSIVE, 0, nullptr);
    GLint inactive_mode = 0, inactive_count = -1;
    glGetIntegerv(WINDOW_MODE, &inactive_mode);
    glGetIntegerv(WINDOW_COUNT, &inactive_count);
    CHECK(inactive_mode == static_cast<GLint>(EXCLUSIVE) && inactive_count == 0);
    std::vector<std::byte> observed(seed.size());
    if (auto read = device.readback(field, observed); !read) return fail("verify rejected field is unchanged", read.error());
    REQUIRE(observed == seed, "active window rectangles reject before field mutation");

    const auto layout = field.layout();
    field = compute::Field{};
    const auto pooled = device.statistics();
    CHECK(pooled.live_bytes + layout.storage_bytes == before.live_bytes);
    CHECK(pooled.idle_bytes >= layout.storage_bytes);
    auto reused = device.allocate(desc);
    if (!reused) return fail("reuse field after declined batch", reused.error());
    CHECK(device.statistics().pool_hits == pooled.pool_hits + 1);
    CHECK(device.statistics().live_bytes == before.live_bytes);
    std::vector<std::byte> zeros(layout.logical_bytes), cleared(layout.logical_bytes);
    if (auto read = device.readback(*reused, cleared); !read) return fail("read zeroed reused field", read.error());
    REQUIRE(cleared == zeros, "pool reuse scrubs the declined field payload");
    std::printf("PASS: active GL_EXT_window_rectangles decline preserves data; pool reuse is zeroed\n");
    return true;
}

struct Options { bool all = true; compute::Backend backend = compute::Backend::Auto; };
bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        std::string_view value(argv[i]);
        if (value == "--backend" && i + 1 < argc) value = argv[++i];
        else if (value.starts_with("--backend=")) value.remove_prefix(10);
        else { std::fprintf(stderr, "unknown argument: %.*s\n", int(value.size()), value.data()); return false; }
        if (value == "all") options.all = true;
        else {
            const auto selected = compute::parse_backend(value);
            if (!selected) { std::fprintf(stderr, "%s\n", selected.error().c_str()); return false; }
            options.all = false; options.backend = *selected;
        }
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) return 2;
    gl_harness::CContext context;
    if (!context.valid()) {
        std::fprintf(stderr, "SKIP: EGL GLES context unavailable; use EGL_PLATFORM=surfaceless\n");
        return 77;
    }
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    const auto* version = glGetString(GL_VERSION);
    std::printf("extended-state GL_VERSION=%s\n", version ? reinterpret_cast<const char*>(version) : "unknown");
    auto owner = std::make_shared<Owner>(Owner{eglGetCurrentContext()});
    const compute::GlResolver resolver{resolve_compute, current, owner.get()};
    compute::Capabilities caps;
    {
        compute::Device probe;
        if (auto init = probe.initialize(resolver, compute::Backend::Auto); !init) {
            std::fprintf(stderr, "SKIP: no portable compute backend: %s\n", init.error().c_str());
            return 77;
        }
        caps = probe.capabilities();
        if (auto release = probe.release(); !release) return fail("release capability probe", release.error()) ? 0 : 1;
    }
    std::printf("routes: native=%s; slibs=%s\n",
        caps.native.available ? "available" : caps.native.reason.c_str(),
        caps.slibs.available ? "available" : caps.slibs.reason.c_str());
    std::vector<compute::Backend> routes;
    if (options.all) {
        if (caps.native.available) routes.push_back(compute::Backend::Native);
        if (caps.slibs.available) routes.push_back(compute::Backend::Slibs);
        if (!caps.native.available) std::printf("UNVERIFIED: native backend; %s\n", caps.native.reason.c_str());
        if (!caps.slibs.available) std::printf("UNVERIFIED: SLIBS backend; %s\n", caps.slibs.reason.c_str());
    } else if (options.backend == compute::Backend::Auto ||
               (options.backend == compute::Backend::Native && caps.native.available) ||
               (options.backend == compute::Backend::Slibs && caps.slibs.available)) {
        routes.push_back(options.backend);
    } else {
        const auto& reason = options.backend == compute::Backend::Native ? caps.native.reason : caps.slibs.reason;
        std::printf("UNVERIFIED: requested %s backend; %s\n", backend_name(options.backend), reason.c_str());
    }
    bool ok = true;
    for (const auto route : routes) ok = run_route(resolver, owner, caps, route) && ok;
    if (caps.slibs.available) ok = active_window_case(resolver) && ok;
    else if (extension("GL_EXT_window_rectangles"))
        std::printf("UNVERIFIED: GL_EXT_window_rectangles decline case; SLIBS backend unavailable\n");
    return ok && g_failures == 0 ? 0 : 1;
}
