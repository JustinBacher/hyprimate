#include "compute/Compute.hpp"
#include "compute/tests/DrmEglContext.hpp"
#include "compute/tests/FixtureOracle.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef HYPRIMATE_SHADER_EXAMPLES
#error HYPRIMATE_SHADER_EXAMPLES must name the checked-in shader fixtures
#endif

namespace {
using namespace hyprimate::compute;
namespace fx = hyprimate::compute::fixture;
using Clock = std::chrono::steady_clock;
constexpr unsigned WARM = 8, SAMPLES = 120, STEPS = 8, TARGET = 64;
constexpr GLenum TIME_ELAPSED = 0x88bf, GPU_DISJOINT = 0x8fbb;

GlResolver::Proc resolve(const char* name, void*) noexcept {
    return reinterpret_cast<GlResolver::Proc>(eglGetProcAddress(name));
}
bool current(void* owner) noexcept {
    return eglGetCurrentContext() == *static_cast<EGLContext*>(owner);
}
double us(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }
std::string source(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open fixture " + path.string());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
template<class T> T require(Result<T> result) {
    if (!result) throw std::runtime_error(result.error());
    return std::move(*result);
}
void require(Result<void> result) {
    if (!result) throw std::runtime_error(result.error());
}
void json_string(std::ostream& out, std::string_view s) {
    out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << char(c);
        else if (c < 0x20) {
            constexpr char hex[] = "0123456789abcdef";
            out << "\\u00" << hex[c >> 4] << hex[c & 15];
        } else out << char(c);
    }
    out << '"';
}
const char* gl_string(GLenum name) {
    const auto* value = glGetString(name);
    return value ? reinterpret_cast<const char*>(value) : "";
}

GLuint shader(GLenum stage, const std::string& text) {
    const GLuint s = glCreateShader(stage);
    const char* p = text.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint n = 0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &n);
        std::string log(static_cast<std::size_t>(std::max(n, 1)), '\0');
        glGetShaderInfoLog(s, n, nullptr, log.data());
        glDeleteShader(s);
        throw std::runtime_error("presentation shader: " + log);
    }
    return s;
}

// Exactly the same typed fetch and target are used for all three modes.
class Presentation {
    GLuint program_ = 0, vao_ = 0, texture_ = 0, fbo_ = 0;
    std::array<GLint, 2> sampler_{-1, -1}, size_{-1, -1};
    bool particles_;
public:
    Presentation(Type type, const std::filesystem::path& root) : particles_(type.lanes != 1) {
        const auto prefix = std::string("#version 300 es\nprecision highp float;\nprecision highp int;\n");
        const std::string vertex = particles_
            ? prefix + "uniform vec2 hypr_target_size;\n" + require(sample_source("position", type)) +
                require(sample_source("velocity", type)) + source(root / "compute-particles/particles.vert")
            : prefix + R"(
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(2.0 * p - 1.0, 0.0, 1.0);
})";
        const std::string fragment = particles_
            ? prefix + "uniform int hypr_event;\nuniform float hypr_progress;\n" +
                source(root / "compute-particles/particles.frag")
            : prefix + require(sample_source("value", type)) + R"(
layout(location=0) out vec4 color;
void main() {
    ivec2 p = min(ivec2(gl_FragCoord.xy * vec2(hc_size_value.xy) / 64.0), hc_size_value.xy - 1);
    float v = clamp(hc_read_value(ivec3(p, 0)) * 1048576.0, 0.0, 1.0);
    color = vec4(v, v * 0.5, v * 0.25, 1.0);
})";
        const GLuint vs = shader(GL_VERTEX_SHADER, vertex), fs = shader(GL_FRAGMENT_SHADER, fragment);
        program_ = glCreateProgram();
        glAttachShader(program_, vs); glAttachShader(program_, fs); glLinkProgram(program_);
        glDeleteShader(vs); glDeleteShader(fs);
        GLint ok = 0; glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) {
            GLint n = 0; glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &n);
            std::string log(static_cast<std::size_t>(std::max(n, 1)), '\0');
            glGetProgramInfoLog(program_, n, nullptr, log.data());
            throw std::runtime_error("presentation link: " + log);
        }
        sampler_[0] = glGetUniformLocation(program_, particles_ ? "hc_tex_position" : "hc_tex_value");
        size_[0] = glGetUniformLocation(program_, particles_ ? "hc_size_position" : "hc_size_value");
        if (particles_) {
            sampler_[1] = glGetUniformLocation(program_, "hc_tex_velocity");
            size_[1] = glGetUniformLocation(program_, "hc_size_velocity");
        }
        glGenVertexArrays(1, &vao_);
        glGenTextures(1, &texture_); glBindTexture(GL_TEXTURE_2D, texture_);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, TARGET, TARGET);
        glGenFramebuffers(1, &fbo_); glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("presentation framebuffer incomplete");
    }
    ~Presentation() {
        GLint current_program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
        if (static_cast<GLuint>(current_program) == program_) glUseProgram(0);
        glDeleteFramebuffers(1, &fbo_); glDeleteTextures(1, &texture_);
        glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
    }
    void draw(Device& device, const Field& field, const Field* velocity = nullptr) {
        const auto view = require(device.sample(field));
        SampledField velocity_view{};
        if (particles_) velocity_view = require(device.sample(*velocity));
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, TARGET, TARGET);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        if (particles_) {
            glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
        glUseProgram(program_); glBindVertexArray(vao_);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, view.texture);
        glUniform1i(sampler_[0], 0);
        glUniform3i(size_[0], view.desc.domain.x, view.desc.domain.y, view.desc.domain.z);
        if (particles_) {
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, velocity_view.texture);
            glUniform1i(sampler_[1], 1);
            glUniform3i(size_[1], velocity_view.desc.domain.x, velocity_view.desc.domain.y, velocity_view.desc.domain.z);
            glUniform2f(glGetUniformLocation(program_, "hypr_target_size"), TARGET, TARGET);
            glUniform1i(glGetUniformLocation(program_, "hypr_event"), 0);
            glUniform1f(glGetUniformLocation(program_, "hypr_progress"), 1.0f);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 3, view.desc.domain.x);
        } else glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    std::vector<unsigned char> pixels() const {
        std::vector<unsigned char> result(TARGET * TARGET * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glReadPixels(0, 0, TARGET, TARGET, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
        return result;
    }
};

struct Timer {
    using Begin = void (*)(GLenum, GLuint);
    using End = void (*)(GLenum);
    using Result64 = void (*)(GLuint, GLenum, GLuint64*);
    Begin begin = nullptr;
    End end = nullptr;
    Result64 result = nullptr;
    std::array<GLuint, SAMPLES> queries{};
    bool available = false;
    Timer() {
        GLint count = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; ++i) {
            const auto* ext = glGetStringi(GL_EXTENSIONS, i);
            if (ext && std::string_view(reinterpret_cast<const char*>(ext)) == "GL_EXT_disjoint_timer_query")
                available = true;
        }
        begin = reinterpret_cast<Begin>(eglGetProcAddress("glBeginQueryEXT"));
        end = reinterpret_cast<End>(eglGetProcAddress("glEndQueryEXT"));
        result = reinterpret_cast<Result64>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
        available = available && begin && end && result;
        if (available) glGenQueries(SAMPLES, queries.data());
    }
    ~Timer() { if (available) glDeleteQueries(SAMPLES, queries.data()); }
};
void retire() {
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!fence) throw std::runtime_error("cannot create completion fence");
    GLenum status;
    do { status = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull); }
    while (status == GL_TIMEOUT_EXPIRED);
    glDeleteSync(fence);
    if (status == GL_WAIT_FAILED) throw std::runtime_error("completion fence failed");
}
struct Series {
    std::vector<double> submit, complete, cpu, gpu;
};
void write_series(std::ostream& out, const std::vector<double>& values) {
    if (values.empty()) { out << "null"; return; }
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    auto percentile = [&](double p) { return sorted[static_cast<std::size_t>(std::ceil(p * sorted.size())) - 1]; };
    out << "{\"median\":" << percentile(.5) << ",\"p95\":" << percentile(.95)
        << ",\"p99\":" << percentile(.99) << ",\"raw\":[";
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << ','; out << values[i]; }
    out << "]}";
}
void write_stats(std::ostream& out, const Statistics& a, const Statistics& b) {
    out << "\"compiles\":" << b.compiles-a.compiles << ",\"allocations\":" << b.allocations-a.allocations
        << ",\"pool_hits\":" << b.pool_hits-a.pool_hits << ",\"native_dispatches\":" << b.native_dispatches-a.native_dispatches
        << ",\"slibs_dispatches\":" << b.slibs_dispatches-a.slibs_dispatches << ",\"samples\":" << b.samples-a.samples
        << ",\"barriers\":" << b.barriers-a.barriers << ",\"fetch_barriers\":" << b.fetch_barriers-a.fetch_barriers
        << ",\"framebuffer_barriers\":" << b.framebuffer_barriers-a.framebuffer_barriers
        << ",\"update_barriers\":" << b.update_barriers-a.update_barriers << ",\"image_barriers\":" << b.image_barriers-a.image_barriers
        << ",\"uniform_updates\":" << b.uniform_updates-a.uniform_updates << ",\"upload_bytes\":" << b.upload_bytes-a.upload_bytes
        << ",\"copy_bytes\":" << b.copy_bytes-a.copy_bytes << ",\"readback_bytes\":" << b.readback_bytes-a.readback_bytes
        << ",\"live_bytes\":" << b.live_bytes << ",\"idle_bytes\":" << b.idle_bytes;
}

struct Instance {
    std::vector<Field> fields;
    std::vector<std::vector<float>> heat;
    std::vector<std::vector<fx::Vec2>> positions, velocities;
    unsigned active = 0;
};

void run(std::ostream& out, const std::filesystem::path& root, EGLContext& context,
         bool particles, unsigned n, unsigned instances, bool chain, Backend mode) {
    const bool cpu = mode == Backend::Auto;
    const unsigned slots = chain ? 2 : STEPS + 1;
    const unsigned fields_per_slot = particles ? 2 : 1;
    const unsigned steps = particles || n == 1 ? 1 : STEPS;
    const Domain domain{n, particles ? 1u : n, 1};
    const Type type{Element::Float, particles ? 2u : 1u};
    const auto parse_start = Clock::now();
    KernelDesc desc;
    desc.name = particles ? "particles-step" : "stencil-step";
    desc.source = source(root / (particles ? "compute-particles" : "compute-stencil") / "step.glsl");
    if (particles) {
        desc.inputs = {{"position", type}, {"velocity", type}};
        desc.outputs = {{"positions", type}, {"velocities", type}};
        desc.parameters = {{"acceleration", {Element::Float, 1}}, {"hypr_delta", {Element::Float, 1}}};
    } else {
        desc.inputs = {{"previous", type}}; desc.outputs = {{"energy", type}};
        desc.parameters = {{"diffusion", {Element::Float, 1}}};
    }
    const auto parse_end = Clock::now();
    Device device;
    const auto init_start = Clock::now();
    auto initialized = device.initialize({resolve, current, &context}, cpu ? Backend::Slibs : mode);
    const auto init_end = Clock::now();
    if (!initialized) {
        if (!cpu) {
            out << "{\"workload\":\"" << (particles ? "particles" : "stencil") << "\",\"count\":" << n
                << ",\"instances\":" << instances << ",\"dependency\":\"" << (chain ? "dependent" : "independent")
                << "\",\"mode\":\"" << (mode == Backend::Native ? "native" : "slibs") << "\",\"unavailable\":";
            json_string(out, initialized.error()); out << '}';
            return;
        }
        throw std::runtime_error(initialized.error());
    }
    const auto prepare_start = Clock::now();
    Kernel kernel;
    if (!cpu) {
        auto prepared = device.prepare(desc);
        if (!prepared) {
            out << "{\"workload\":\"" << (particles ? "particles" : "stencil") << "\",\"count\":" << n
                << ",\"instances\":" << instances << ",\"dependency\":\"" << (chain ? "dependent" : "independent")
                << "\",\"mode\":\"" << (mode == Backend::Native ? "native" : "slibs") << "\",\"unavailable\":";
            json_string(out, prepared.error()); out << '}';
            return;
        }
        kernel = std::move(*prepared);
    }
    const auto prepare_end = Clock::now();
    Presentation presentation(type, root);
    std::vector<Instance> state(instances);
    const auto allocation_start = Clock::now();
    for (auto& instance : state) {
        const unsigned gpu_slots = cpu ? 1 : slots;
        instance.fields.reserve(gpu_slots * fields_per_slot);
        for (unsigned i = 0; i < gpu_slots * fields_per_slot; ++i)
            instance.fields.push_back(require(device.allocate({type, domain})));
        if (cpu) {
            if (particles) {
                instance.positions.resize(slots, std::vector<fx::Vec2>(n));
                instance.velocities.resize(slots, std::vector<fx::Vec2>(n));
                fx::seed_particles(instance.positions[0], instance.velocities[0],
                    static_cast<float>(42u + static_cast<unsigned>(&instance - state.data()) * 7919u) / 16777216.0f);
            } else {
                instance.heat.resize(slots, std::vector<float>(static_cast<std::size_t>(n) * n));
                fx::seed_stencil(instance.heat[0], n, n);
            }
        } else if (particles) {
            std::vector<fx::Vec2> p(n), v(n);
            fx::seed_particles(p, v,
                static_cast<float>(42u + static_cast<unsigned>(&instance - state.data()) * 7919u) / 16777216.0f);
            require(device.upload(instance.fields[0], std::as_bytes(std::span{p})));
            require(device.upload(instance.fields[1], std::as_bytes(std::span{v})));
        } else {
            std::vector<float> seed(static_cast<std::size_t>(n) * n);
            fx::seed_stencil(seed, n, n);
            require(device.upload(instance.fields[0], std::as_bytes(std::span{seed})));
        }
    }
    const auto allocation_end = Clock::now();
    const std::array<ParameterValue, 2> params = particles
        ? std::array<ParameterValue, 2>{{{std::array<float,4>{.75f,0,0,0},1}, {std::array<float,4>{1.f/60.f,0,0,0},1}}}
        : std::array<ParameterValue, 2>{{{std::array<float,4>{.2f,0,0,0},1}, {std::array<float,4>{},1}}};
    const std::array<std::reference_wrapper<const Kernel>, 1> kernels{std::cref(kernel)};
    Series series;
    Timer timer;
    GLint disjoint_before = 0, disjoint_after = 0;
    double first_submit = 0, first_complete = 0;
    Statistics before{};
    for (unsigned tick = 0; tick < WARM + SAMPLES; ++tick) {
        if (tick == WARM) {
            before = device.statistics();
            if (timer.available) glGetIntegerv(GPU_DISJOINT, &disjoint_before);
        }
        const auto start = Clock::now();
        double cpu_time = 0;
        if (cpu) {
            const auto sim_start = Clock::now();
            for (auto& instance : state) {
                const unsigned base = instance.active;
                unsigned in = base, dest = 0;
                for (unsigned step = 0; step < steps; ++step) {
                    dest = chain ? 1 - in : (base + 1 + step) % slots;
                    if (particles) fx::step_particles(instance.positions[in], instance.velocities[in],
                        instance.positions[dest], instance.velocities[dest], .75f, 1.f / 60.f);
                    else fx::step_stencil(instance.heat[in], instance.heat[dest], n, n, .2f);
                    if (chain) in = dest;
                }
                instance.active = dest;
            }
            cpu_time = us(Clock::now() - sim_start);
            if (timer.available && tick >= WARM) timer.begin(TIME_ELAPSED, timer.queries[tick - WARM]);
            for (auto& instance : state) {
                if (particles) {
                    require(device.upload(instance.fields[0], std::as_bytes(std::span{instance.positions[instance.active]})));
                    require(device.upload(instance.fields[1], std::as_bytes(std::span{instance.velocities[instance.active]})));
                } else require(device.upload(instance.fields[0], std::as_bytes(std::span{instance.heat[instance.active]})));
            }
        } else {
            if (timer.available && tick >= WARM) timer.begin(TIME_ELAPSED, timer.queries[tick - WARM]);
            auto batch = require(device.begin(kernels));
            for (auto& instance : state) {
                const unsigned base = instance.active;
                unsigned in = base, dest = 0;
                for (unsigned step = 0; step < steps; ++step) {
                    dest = chain ? 1 - in : (base + 1 + step) % slots;
                    std::array<const Field*, 2> input{};
                    std::array<Field*, 2> output{};
                    for (unsigned f = 0; f < fields_per_slot; ++f) {
                        input[f] = &instance.fields[in * fields_per_slot + f];
                        output[f] = &instance.fields[dest * fields_per_slot + f];
                    }
                    require(batch.dispatch(kernel, std::span{input}.first(fields_per_slot),
                                           std::span{output}.first(fields_per_slot),
                                           std::span{params}.first(fields_per_slot)));
                    if (chain) in = dest;
                }
                instance.active = dest;
            }
        }
        for (auto& instance : state)
            presentation.draw(device, instance.fields[cpu ? 0 : instance.active * fields_per_slot],
                particles ? &instance.fields[cpu ? 1 : instance.active * fields_per_slot + 1] : nullptr);
        if (timer.available && tick >= WARM) timer.end(TIME_ELAPSED);
        const auto submitted = Clock::now();
        retire();
        const auto completed = Clock::now();
        if (!tick) { first_submit = us(submitted - start); first_complete = us(completed - start); }
        if (tick >= WARM) {
            series.submit.push_back(us(submitted - start));
            series.complete.push_back(us(completed - start));
            if (cpu) series.cpu.push_back(cpu_time);
        }
    }
    const auto after = device.statistics();
    // Untimed oracle proof: reproduce the complete submitted workload from the
    // same per-instance seeds, compare logical field values, then compare the
    // actual final render against an oracle-uploaded CPU reference target.
    for (unsigned instance_index = 0; instance_index < instances; ++instance_index) {
        std::vector<float> expected_heat;
        std::vector<fx::Vec2> expected_position, expected_velocity;
        if (particles) {
            expected_position.resize(n); expected_velocity.resize(n);
            fx::seed_particles(expected_position, expected_velocity,
                static_cast<float>(42u + instance_index * 7919u) / 16777216.0f);
            std::vector<fx::Vec2> next_position(n), next_velocity(n);
            for (unsigned tick = 0; tick < WARM + SAMPLES; ++tick) {
                fx::step_particles(expected_position, expected_velocity, next_position, next_velocity, .75f, 1.f/60.f);
                expected_position.swap(next_position); expected_velocity.swap(next_velocity);
            }
        } else {
            expected_heat.resize(static_cast<std::size_t>(n) * n);
            fx::seed_stencil(expected_heat, n, n);
            std::vector<float> next(expected_heat.size());
            for (unsigned tick = 0; tick < WARM + SAMPLES; ++tick)
                for (unsigned step = 0; step < (chain ? steps : 1u); ++step) {
                    fx::step_stencil(expected_heat, next, n, n, .2f);
                    expected_heat.swap(next);
                }
        }
        auto& instance = state[instance_index];
        const unsigned candidate_base = cpu ? 0 : instance.active * fields_per_slot;
        const Field& candidate = instance.fields[candidate_base];
        const Field& candidate_velocity = particles ? instance.fields[candidate_base + 1] : candidate;
        if (particles) {
            std::vector<fx::Vec2> actual_position(n), actual_velocity(n);
            require(device.readback(candidate, std::as_writable_bytes(std::span{actual_position})));
            require(device.readback(candidate_velocity, std::as_writable_bytes(std::span{actual_velocity})));
            for (std::size_t i = 0; i < n; ++i) {
                const auto close = [](float a, float b) { return std::abs(a-b) <= 1e-5f + 1e-5f*std::abs(b); };
                if (!close(actual_position[i].x, expected_position[i].x) ||
                    !close(actual_position[i].y, expected_position[i].y) ||
                    !close(actual_velocity[i].x, expected_velocity[i].x) ||
                    !close(actual_velocity[i].y, expected_velocity[i].y))
                    throw std::runtime_error("particle benchmark differs from CPU oracle");
            }
        } else {
            std::vector<float> actual(expected_heat.size());
            require(device.readback(candidate, std::as_writable_bytes(std::span{actual})));
            for (std::size_t i = 0; i < actual.size(); ++i)
                if (std::abs(actual[i]-expected_heat[i]) > 1e-5f + 1e-5f*std::abs(expected_heat[i]))
                    throw std::runtime_error("stencil benchmark differs from CPU oracle");
        }
        Field reference = require(device.allocate({type, domain}));
        require(device.upload(reference, particles ? std::as_bytes(std::span{expected_position})
                                                   : std::as_bytes(std::span{expected_heat})));
        Field reference_velocity;
        if (particles) {
            reference_velocity = require(device.allocate({type, domain}));
            require(device.upload(reference_velocity, std::as_bytes(std::span{expected_velocity})));
        }
        presentation.draw(device, reference, particles ? &reference_velocity : nullptr);
        const auto reference_pixels = presentation.pixels();
        presentation.draw(device, candidate, particles ? &candidate_velocity : nullptr);
        const auto candidate_pixels = presentation.pixels();
        bool asymmetric = false;
        for (std::size_t i = 0; i < reference_pixels.size(); i += 4) {
            asymmetric |= reference_pixels[i] != reference_pixels[i+1] ||
                          reference_pixels[i+1] != reference_pixels[i+2];
            for (unsigned c = 0; c < 4; ++c)
                if (std::abs(int(reference_pixels[i+c])-int(candidate_pixels[i+c])) > 3)
                    throw std::runtime_error("benchmark presentation differs from CPU-oracle render");
        }
        if (!asymmetric && (particles || n > 1))
            throw std::runtime_error("benchmark presentation lost asymmetric fixture signal");
    }
    if (timer.available) {
        glGetIntegerv(GPU_DISJOINT, &disjoint_after);
        if (!disjoint_before && !disjoint_after) {
            for (GLuint query : timer.queries) {
                GLuint64 ns = 0; timer.result(query, GL_QUERY_RESULT, &ns);
                series.gpu.push_back(static_cast<double>(ns) / 1000.0);
            }
        }
    }
    out << "{\"workload\":\"" << (particles ? "particles" : n == 1 ? "dispatch_baseline" : "stencil")
        << "\",\"count\":" << n << ",\"instances\":" << instances << ",\"steps\":" << steps
        << ",\"dependency\":\"" << (chain ? "dependent" : "independent") << "\",\"mode\":\""
        << (cpu ? "cpu" : mode == Backend::Native ? "native" : "slibs") << "\",\"cold_parse_us\":" << us(parse_end-parse_start)
        << ",\"oracle_validation\":\"cpu_oracle_and_render_match\",\"cold_initialize_us\":" << us(init_end-init_start)
        << ",\"cold_prepare_us\":" << us(prepare_end-prepare_start)
        << ",\"cold_allocate_seed_us\":" << us(allocation_end-allocation_start)
        << ",\"first_submit_us\":" << first_submit << ",\"first_complete_us\":" << first_complete
        << ",\"submitted_samples\":" << series.submit.size() << ",\"retired_samples\":" << series.complete.size()
        << ",\"timer_status\":\"" << (!timer.available ? "unavailable" : disjoint_before || disjoint_after ? "disjoint" : "valid") << "\",\"submit_us\":";
    write_series(out, series.submit); out << ",\"completion_us\":"; write_series(out, series.complete);
    out << ",\"cpu_simulation_us\":"; write_series(out, series.cpu);
    out << ",\"gpu_us\":"; write_series(out, series.gpu);
    out << ",\"steady_statistics\":{"; write_stats(out, before, after); out << "},\"total_statistics\":{";
    write_stats(out, {}, after); out << "}}";
}
}

int main(int argc, char** argv) {
    std::string node = "/dev/dri/renderD128", json_path;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--render-node" && i + 1 < argc) node = argv[++i];
        else if (arg == "--json" && i + 1 < argc) json_path = argv[++i];
        else { std::fprintf(stderr, "usage: %s [--render-node PATH] [--json PATH]\n", argv[0]); return EXIT_FAILURE; }
    }
    try {
        fx::DrmEglContext egl(node);
        if (!egl.valid()) return 77;
        EGLContext context = eglGetCurrentContext();
        std::ofstream file;
        if (!json_path.empty()) {
            file.open(json_path);
            if (!file) throw std::runtime_error("cannot open JSON output " + json_path);
        }
        std::ostream& out = json_path.empty() ? std::cout : file;
        out << std::setprecision(10) << "{\"render_node\":"; json_string(out, node);
        out << ",\"vendor\":"; json_string(out, gl_string(GL_VENDOR));
        out << ",\"renderer\":"; json_string(out, gl_string(GL_RENDERER));
        out << ",\"version\":"; json_string(out, gl_string(GL_VERSION));
        out << ",\"warmup_ticks\":" << WARM << ",\"measured_ticks\":" << SAMPLES
            << ",\"target_width\":" << TARGET << ",\"target_height\":" << TARGET
            << ",\"completion_boundary\":\"one tick fence\",\"barrier_cost\":\"incremental comparison only\",\"rows\":[";
        bool first = true;
        const std::filesystem::path root{HYPRIMATE_SHADER_EXAMPLES};
        for (bool particles : {false, true}) {
            const std::array<unsigned, 4> sizes = particles ? std::array<unsigned, 4>{1024, 16384, 65536, 0}
                                                            : std::array<unsigned, 4>{1, 64, 256, 512};
            for (unsigned n : sizes) if (n)
                for (unsigned count : {1u, 4u, 8u})
                    for (bool chain : {true, false})
                        for (Backend mode : {Backend::Auto, Backend::Native, Backend::Slibs}) {
                            if (!first) out << ',';
                            first = false;
                            run(out, root, context, particles, n, count, chain, mode);
                        }
        }
        out << "]}\n";
        if (!out) throw std::runtime_error("failed writing benchmark JSON");
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    }
}
