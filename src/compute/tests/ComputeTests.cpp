#include "compute/Compute.hpp"
#include "Check.hpp"
#include "GlHarness.hpp"

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
    bool g_fail_next_native_shader_creation = false;
    bool g_fail_next_texture_storage = false;
    bool g_texture_storage_fault_triggered = false;
    GLuint g_texture_storage_fault_texture = 0;
}
extern "C" GLuint GL_APIENTRY __real_glCreateShader(GLenum shader_type);
extern "C" GLuint GL_APIENTRY __wrap_glCreateShader(GLenum shader_type) {
    if (g_fail_next_native_shader_creation && shader_type == 0x91B9u) {
        g_fail_next_native_shader_creation = false;
        return 0;
    }
    return __real_glCreateShader(shader_type);
}

extern "C" void GL_APIENTRY __real_glTexStorage2D(GLenum target, GLsizei levels, GLenum internal_format,
                                                  GLsizei width, GLsizei height);
extern "C" void GL_APIENTRY __wrap_glTexStorage2D(GLenum target, GLsizei levels, GLenum internal_format,
                                                  GLsizei width, GLsizei height) {
    if (g_fail_next_texture_storage) {
        g_fail_next_texture_storage = false;
        g_texture_storage_fault_triggered = true;
        GLint bound_texture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture);
        g_texture_storage_fault_texture = static_cast<GLuint>(bound_texture);
        __real_glTexStorage2D(target, levels, internal_format, -1, height);
        return;
    }
    __real_glTexStorage2D(target, levels, internal_format, width, height);
}

namespace {
    using namespace hyprimate::compute;

    struct ContextOwner {
        EGLContext context = EGL_NO_CONTEXT;
        bool live = true;
        bool deny_native = false;
        bool no_op_dispatch = false;
        unsigned native_lookups = 0;
    };

    void GL_APIENTRY no_op_dispatch(GLuint, GLuint, GLuint) noexcept {}

    bool is_current(void* owner) noexcept {
        const auto* context = static_cast<const ContextOwner*>(owner);
        return context && context->live && context->context != EGL_NO_CONTEXT &&
               eglGetCurrentContext() == context->context;
    }

    GlResolver::Proc resolve_proc(const char* name, void* owner) noexcept {
        auto* context = static_cast<ContextOwner*>(owner);
        if (std::strcmp(name, "glDispatchCompute") == 0 ||
            std::strcmp(name, "glBindImageTexture") == 0 ||
            std::strcmp(name, "glMemoryBarrier") == 0) {
            if (context) {
                ++context->native_lookups;
                if (context->deny_native) return nullptr;
                if (context->no_op_dispatch && std::strcmp(name, "glDispatchCompute") == 0)
                    return reinterpret_cast<GlResolver::Proc>(no_op_dispatch);
            }
        }
        return eglGetProcAddress(name);
    }
    struct Options {
        bool require_es30 = false;
        bool require_es31 = false;
        bool without_float_render = false;
        std::optional<Backend> backend;
    };

    template<class T>
    bool success(const Result<T>& result, const char* operation) {
        if (result) return true;
        std::fprintf(stderr, "compute test: %s failed: %s\n", operation, result.error().c_str());
        report(false, operation, __FILE__, __LINE__);
        return false;
    }

#define REQUIRE_RESULT(value, operation) do { if (!success((value), (operation))) return false; } while (false)
#define REQUIRE(condition, description) do { if (!(condition)) { std::fprintf(stderr, "compute test: %s\n", (description)); report(false, (description), __FILE__, __LINE__); return false; } } while (false)

    bool has_extension(std::string_view wanted) {
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; ++i) {
            const auto* value = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
            if (value && wanted == reinterpret_cast<const char*>(value)) return true;
        }
        return false;
    }

    void print_capabilities(const Capabilities& caps, Backend preference) {
        std::printf("compute context: version=%s vendor=%s renderer=%s preference=%s native=%s slibs=%s\n",
                    caps.version.c_str(), caps.vendor.c_str(), caps.renderer.c_str(),
                    preference == Backend::Auto ? "auto" : preference == Backend::Native ? "native" : "slibs",
                    caps.native.available ? "available" : caps.native.reason.c_str(),
                    caps.slibs.available ? "available" : caps.slibs.reason.c_str());
        std::printf("compute limits: texture=%u viewport=%ux%u combined-samplers=%u image-units=%u "
                    "native-groups=%u,%u,%u local=%u,%u,%u invocations=%u formats=%u%u%u%u\n",
                    caps.texture_size, caps.viewport_width, caps.viewport_height, caps.combined_samplers,
                    caps.image_units, caps.group_count[0], caps.group_count[1], caps.group_count[2],
                    caps.local_size[0], caps.local_size[1], caps.local_size[2], caps.invocations,
                    caps.formats[0], caps.formats[1], caps.formats[2], caps.formats[3]);
    }

    bool check_backend(const Kernel& kernel, Backend preference, const Capabilities& caps) {
        if (preference == Backend::Auto) {
            const bool supported = kernel.backend() == Backend::Native ? caps.native.available : caps.slibs.available;
            REQUIRE(supported, "auto selected a backend not reported available");
        } else {
            REQUIRE(kernel.backend() == preference, "forced backend did not prepare on the requested route");
        }
        if (kernel.backend() == Backend::Native) REQUIRE(kernel.fallback_reason().empty(), "native kernel unexpectedly has a fallback diagnostic");
        return true;
    }

    template<class T>
    bool allocate(Device& device, FieldDesc desc, Field& field, const char* operation = "allocate field") {
        (void)sizeof(T);
        auto result = device.allocate(desc);
        if (!success(result, operation)) return false;
        field = std::move(*result);
        return true;
    }

    template<class T>
    bool upload(Device& device, Field& field, const std::vector<T>& values) {
        return success(device.upload(field, std::as_bytes(std::span(values))), "upload field");
    }

    template<class T>
    bool readback(Device& device, const Field& field, std::vector<T>& values) {
        return success(device.readback(field, std::as_writable_bytes(std::span(values))), "read back field");
    }

    template<std::size_t Inputs, std::size_t Outputs, std::size_t Parameters>
    bool dispatch(Device& device, const Kernel& kernel,
                  const std::array<const Field*, Inputs>& inputs,
                  const std::array<Field*, Outputs>& outputs,
                  const std::array<ParameterValue, Parameters>& parameters = {}) {
        const std::array<std::reference_wrapper<const Kernel>, 1> kernels{std::cref(kernel)};
        auto batch = device.begin(kernels);
        if (!success(batch, "begin compute batch")) return false;
        return success(batch->dispatch(kernel,
                                       FieldsIn{inputs.data(), inputs.size()},
                                       FieldsOut{outputs.data(), outputs.size()},
                                       std::span<const ParameterValue>{parameters}),
                       "dispatch compute kernel");
    }

    bool close_enough(const std::vector<float>& actual, const std::vector<float>& expected,
                      const char* label, float absolute = 1.0e-5f, float relative = 1.0e-5f) {
        if (actual.size() != expected.size()) {
            std::fprintf(stderr, "compute test: %s length differs (%zu != %zu)\n", label, actual.size(), expected.size());
            report(false, label, __FILE__, __LINE__);
            return false;
        }
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const auto difference = std::fabs(actual[i] - expected[i]);
            if (!(difference <= absolute + relative * std::fabs(expected[i]))) {
                std::fprintf(stderr, "compute test: %s[%zu] is %.9g, expected %.9g\n", label, i, actual[i], expected[i]);
                report(false, label, __FILE__, __LINE__);
                return false;
            }
        }
        return true;
    }

    KernelDesc copy_kernel(Type type, std::string name = "copy") {
        const std::string glsl_type(type_name(type));
        return {std::move(name),
                "void kernel(ivec3 id, inout " + glsl_type + " result) { result = hc_read_src(id); }",
                {{"src", type}}, {{"result", type}}, {}};
    }

    bool identity_case(Device& device, const Capabilities& caps, Backend preference,
                       Type type, Domain domain, const std::vector<float>& values,
                       const char* label) {
        Field input, output;
        REQUIRE(allocate<float>(device, {type, domain}, input), "could not allocate identity input");
        REQUIRE(allocate<float>(device, {type, domain}, output), "could not allocate identity output");
        REQUIRE(upload(device, input, values), "could not upload identity input");
        auto prepared = device.prepare(copy_kernel(type, label));
        REQUIRE_RESULT(prepared, "prepare identity kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "identity route check failed");
        const std::array<const Field*, 1> inputs{&input};
        const std::array<Field*, 1> outputs{&output};
        const std::array<ParameterValue, 0> parameters{};
        REQUIRE(dispatch(device, *prepared, inputs, outputs, parameters), "identity dispatch failed");
        std::vector<float> actual(values.size());
        REQUIRE(readback(device, output, actual), "identity readback failed");
        REQUIRE(close_enough(actual, values, label, 0.0f, 0.0f), "identity result differs");
        return true;
    }

    bool identity_case(Device& device, const Capabilities& caps, Backend preference,
                       Type type, Domain domain, const std::vector<std::uint32_t>& values,
                       const char* label) {
        Field input, output;
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, input), "could not allocate unsigned identity input");
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, output), "could not allocate unsigned identity output");
        REQUIRE(upload(device, input, values), "could not upload unsigned identity input");
        auto prepared = device.prepare(copy_kernel(type, label));
        REQUIRE_RESULT(prepared, "prepare unsigned identity kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "unsigned identity route check failed");
        const std::array<const Field*, 1> inputs{&input};
        const std::array<Field*, 1> outputs{&output};
        const std::array<ParameterValue, 0> parameters{};
        REQUIRE(dispatch(device, *prepared, inputs, outputs, parameters), "unsigned identity dispatch failed");
        std::vector<std::uint32_t> actual(values.size());
        REQUIRE(readback(device, output, actual), "unsigned identity readback failed");
        REQUIRE(actual == values, "unsigned identity result differs");
        return true;
    }

    bool identity_case(Device& device, const Capabilities& caps, Backend preference,
                       Type type, Domain domain, const std::vector<std::int32_t>& values,
                       const char* label) {
        Field input, output;
        REQUIRE(allocate<std::int32_t>(device, {type, domain}, input), "could not allocate signed identity input");
        REQUIRE(allocate<std::int32_t>(device, {type, domain}, output), "could not allocate signed identity output");
        REQUIRE(upload(device, input, values), "could not upload signed identity input");
        auto prepared = device.prepare(copy_kernel(type, label));
        REQUIRE_RESULT(prepared, "prepare signed identity kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "signed identity route check failed");
        const std::array<const Field*, 1> inputs{&input};
        const std::array<Field*, 1> outputs{&output};
        const std::array<ParameterValue, 0> parameters{};
        REQUIRE(dispatch(device, *prepared, inputs, outputs, parameters), "signed identity dispatch failed");
        std::vector<std::int32_t> actual(values.size());
        REQUIRE(readback(device, output, actual), "signed identity readback failed");
        REQUIRE(actual == values, "signed identity result differs");
        return true;
    }

    bool arithmetic_cases(Device& device, const Capabilities& caps, Backend preference) {
        for (unsigned lanes = 1; lanes <= 4; ++lanes) {
            const Type type{Element::Float, lanes};
            const Domain domain{13, 1, 1};
            const auto elements = std::size_t{domain.x} * lanes;
            std::vector<float> a(elements), b(elements), expected(elements), actual(elements);
            std::array<float, 4> gain_values{0.25f, 0.5f, 0.75f, 1.25f};
            for (std::size_t element = 0; element < domain.x; ++element) {
                for (unsigned lane = 0; lane < lanes; ++lane) {
                    const auto i = element * lanes + lane;
                    a[i] = 0.125f * static_cast<float>(element + 1) - 0.25f * static_cast<float>(lane);
                    b[i] = 0.0625f * static_cast<float>(2 * element + lane + 1);
                    expected[i] = a[i] + b[i] * gain_values[lane];
                }
            }
            const std::string glsl_type(type_name(type));
            KernelDesc desc{"float_arithmetic_" + std::to_string(lanes),
                            "void kernel(ivec3 id, inout " + glsl_type + " result) { result = hc_read_a(id) + hc_read_b(id) * gain; }",
                            {{"a", type}, {"b", type}}, {{"result", type}}, {{"gain", type}}};
            Field field_a, field_b, output;
            REQUIRE(allocate<float>(device, {type, domain}, field_a), "could not allocate arithmetic field a");
            REQUIRE(allocate<float>(device, {type, domain}, field_b), "could not allocate arithmetic field b");
            REQUIRE(allocate<float>(device, {type, domain}, output), "could not allocate arithmetic output");
            REQUIRE(upload(device, field_a, a), "could not upload arithmetic field a");
            REQUIRE(upload(device, field_b, b), "could not upload arithmetic field b");
            auto prepared = device.prepare(desc);
            REQUIRE_RESULT(prepared, "prepare float arithmetic kernel");
            REQUIRE(check_backend(*prepared, preference, caps), "float arithmetic route check failed");
            ParameterValue gain{gain_values, lanes};
            const std::array<const Field*, 2> inputs{&field_a, &field_b};
            const std::array<Field*, 1> outputs{&output};
            const std::array<ParameterValue, 1> parameters{gain};
            REQUIRE(dispatch(device, *prepared, inputs, outputs, parameters), "float arithmetic dispatch failed");
            REQUIRE(readback(device, output, actual), "float arithmetic readback failed");
            REQUIRE(close_enough(actual, expected, "float arithmetic"), "float arithmetic result differs");
        }

        const Type scalar{Element::Float, 1};
        const Domain output_domain{11, 1, 1};
        const Domain shorter_input{7, 1, 1};
        Field a, b, output;
        REQUIRE(allocate<float>(device, {scalar, output_domain}, a), "could not allocate differently sized input a");
        REQUIRE(allocate<float>(device, {scalar, shorter_input}, b), "could not allocate differently sized input b");
        REQUIRE(allocate<float>(device, {scalar, output_domain}, output), "could not allocate differently sized output");
        std::vector<float> av(11), bv(7), expected(11), actual(11);
        for (unsigned i = 0; i < av.size(); ++i) av[i] = 1.0f + static_cast<float>(i);
        for (unsigned i = 0; i < bv.size(); ++i) bv[i] = 0.5f + static_cast<float>(i);
        for (unsigned i = 0; i < expected.size(); ++i) expected[i] = av[i] + (i < bv.size() ? 2.0f * bv[i] : 0.0f);
        REQUIRE(upload(device, a, av), "could not upload differently sized input a");
        REQUIRE(upload(device, b, bv), "could not upload differently sized input b");
        KernelDesc desc{"different_input_domains",
                        "void kernel(ivec3 id, inout float result) { result = hc_read_a(id) + hc_read_b(id) * scale; }",
                        {{"a", scalar}, {"b", scalar}}, {{"result", scalar}}, {{"scale", scalar}}};
        auto prepared = device.prepare(desc);
        REQUIRE_RESULT(prepared, "prepare differently sized input kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "differently sized input route check failed");
        ParameterValue scale{std::array<float, 4>{2.0f, 0.0f, 0.0f, 0.0f}, 1};
        const std::array<const Field*, 2> inputs{&a, &b};
        const std::array<Field*, 1> outputs{&output};
        const std::array<ParameterValue, 1> parameters{scale};
        REQUIRE(dispatch(device, *prepared, inputs, outputs, parameters), "differently sized input dispatch failed");
        REQUIRE(readback(device, output, actual), "differently sized input readback failed");
        REQUIRE(close_enough(actual, expected, "different input domains"), "different-domain reads did not return typed zero out of range");
        return true;
    }

    bool indexed_read_case(Device& device, const Capabilities& caps, Backend preference) {
        constexpr unsigned count = 9;
        const Type type{Element::Int, 1};
        const Domain domain{count, 1, 1};
        Field input, output;
        REQUIRE(allocate<std::int32_t>(device, {type, domain}, input), "could not allocate indexed input");
        REQUIRE(allocate<std::int32_t>(device, {type, domain}, output), "could not allocate indexed output");
        std::vector<std::int32_t> values(count), expected(count), actual(count);
        for (unsigned i = 0; i < count; ++i) values[i] = static_cast<std::int32_t>(3 + i * 7);
        for (unsigned i = 0; i < count; ++i)
            expected[i] = (i ? values[i - 1] : 0) + (i + 1 < count ? values[i + 1] : 0);
        REQUIRE(upload(device, input, values), "could not upload indexed input");
        KernelDesc desc{"indexed_edges",
                        "void kernel(ivec3 id, inout int result) { int i = hc_index(id); result = hc_read_src(i - 1) + hc_read_src(i + 1); }",
                        {{"src", type}}, {{"result", type}}, {}};
        auto prepared = device.prepare(desc);
        REQUIRE_RESULT(prepared, "prepare indexed-read kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "indexed-read route check failed");
        const std::array<const Field*, 1> inputs{&input};
        const std::array<Field*, 1> outputs{&output};
        const std::array<ParameterValue, 0> parameters{};
        REQUIRE(dispatch(device, *prepared, inputs, outputs, parameters), "indexed-read dispatch failed");
        REQUIRE(readback(device, output, actual), "indexed-read readback failed");
        REQUIRE(actual == expected, "negative and upper-bound indexed reads did not return zero");
        return true;
    }

    bool mixed_integer_mrt_case(Device& device, const Capabilities& caps, Backend preference) {
        const Type signed_type{Element::Int, 4};
        const Type unsigned_type{Element::Uint, 4};
        const Domain domain{3, 1, 1};
        Field signed_in, unsigned_in, signed_out, unsigned_out;
        REQUIRE(allocate<std::int32_t>(device, {signed_type, domain}, signed_in), "could not allocate signed MRT input");
        REQUIRE(allocate<std::uint32_t>(device, {unsigned_type, domain}, unsigned_in), "could not allocate unsigned MRT input");
        REQUIRE(allocate<std::int32_t>(device, {signed_type, domain}, signed_out), "could not allocate signed MRT output");
        REQUIRE(allocate<std::uint32_t>(device, {unsigned_type, domain}, unsigned_out), "could not allocate unsigned MRT output");
        const std::vector<std::int32_t> signed_values{
            std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(), -1, 0,
            1, -2, 17, -31, 9, 10, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()};
        const std::vector<std::uint32_t> unsigned_values{
            0u, 1u, std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() - 1,
            2u, 3u, std::numeric_limits<std::uint32_t>::max() - 2, 5u,
            7u, 11u, 13u, 17u};
        REQUIRE(upload(device, signed_in, signed_values), "could not upload signed MRT input");
        REQUIRE(upload(device, unsigned_in, unsigned_values), "could not upload unsigned MRT input");
        KernelDesc desc{"mixed_integer_mrt",
                        "void kernel(ivec3 id, inout ivec4 signedResult, inout uvec4 unsignedResult) { signedResult = hc_read_s(id); unsignedResult = hc_read_u(id) + increment; }",
                        {{"s", signed_type}, {"u", unsigned_type}},
                        {{"signedResult", signed_type}, {"unsignedResult", unsigned_type}},
                        {{"increment", unsigned_type}}};
        auto prepared = device.prepare(desc);
        REQUIRE_RESULT(prepared, "prepare mixed signed/unsigned MRT kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "mixed MRT route check failed");
        ParameterValue increment{std::array<std::uint32_t, 4>{1u, 2u, 3u, 4u}, 4};
        const std::array<const Field*, 2> inputs{&signed_in, &unsigned_in};
        const std::array<Field*, 2> outputs{&signed_out, &unsigned_out};
        const std::array<ParameterValue, 1> parameters{increment};
        REQUIRE(dispatch(device, *prepared, inputs, outputs, parameters), "mixed MRT dispatch failed");
        std::vector<std::int32_t> signed_actual(signed_values.size());
        std::vector<std::uint32_t> unsigned_actual(unsigned_values.size());
        REQUIRE(readback(device, signed_out, signed_actual), "signed MRT readback failed");
        REQUIRE(readback(device, unsigned_out, unsigned_actual), "unsigned MRT readback failed");
        REQUIRE(signed_actual == signed_values, "signed extrema/vector MRT result differs");
        std::vector<std::uint32_t> unsigned_expected(unsigned_values.size());
        for (std::size_t i = 0; i < unsigned_expected.size(); ++i) unsigned_expected[i] = unsigned_values[i] + std::array<std::uint32_t, 4>{1u, 2u, 3u, 4u}[i % 4];
        REQUIRE(unsigned_actual == unsigned_expected, "unsigned wrap/vector MRT result differs");
        return true;
    }

    bool unsigned_wrap_case(Device& device, const Capabilities& caps, Backend preference) {
        const Type type{Element::Uint, 1};
        const Domain domain{5, 1, 1};
        Field input, output;
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, input), "could not allocate unsigned wrap input");
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, output), "could not allocate unsigned wrap output");
        const std::vector<std::uint32_t> values{0u, 1u, std::numeric_limits<std::uint32_t>::max(), std::numeric_limits<std::uint32_t>::max() - 1, 0x80000000u};
        constexpr std::uint32_t increment_value = 3;
        std::vector<std::uint32_t> expected(values.size()), actual(values.size());
        for (std::size_t i = 0; i < values.size(); ++i) expected[i] = values[i] + increment_value;
        REQUIRE(upload(device, input, values), "could not upload unsigned wrap input");
        KernelDesc desc{"unsigned_wrap",
                        "void kernel(ivec3 id, inout uint result) { result = hc_read_src(id) + increment; }",
                        {{"src", type}}, {{"result", type}}, {{"increment", type}}};
        auto prepared = device.prepare(desc);
        REQUIRE_RESULT(prepared, "prepare unsigned wrap kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "unsigned wrap route check failed");
        ParameterValue increment{std::array<std::uint32_t, 4>{increment_value, 0u, 0u, 0u}, 1};
        const std::array<const Field*, 1> inputs{&input};
        const std::array<Field*, 1> outputs{&output};
        const std::array<ParameterValue, 1> parameters{increment};
        REQUIRE(dispatch(device, *prepared, inputs, outputs, parameters), "unsigned wrap dispatch failed");
        REQUIRE(readback(device, output, actual), "unsigned wrap readback failed");
        REQUIRE(actual == expected, "unsigned addition did not preserve modulo-2^32 wrap");
        return true;
    }

    bool shape_cases(Device& device, const Capabilities& caps, Backend preference) {
        std::vector<float> grid(257u * 129u);
        for (unsigned y = 0; y < 129; ++y)
            for (unsigned x = 0; x < 257; ++x)
                grid[std::size_t{y} * 257 + x] = static_cast<float>(x + 257 * y) * 0.00390625f;
        REQUIRE(identity_case(device, caps, preference, {Element::Float, 1}, {257, 129, 1}, grid, "asymmetric_257x129"), "asymmetric grid case failed");

        std::vector<float> volume(7u * 5u * 3u);
        for (unsigned z = 0; z < 3; ++z)
            for (unsigned y = 0; y < 5; ++y)
                for (unsigned x = 0; x < 7; ++x)
                    volume[x + 7u * (y + 5u * z)] = static_cast<float>(x + 10 * y + 100 * z) * 0.125f;
        REQUIRE(identity_case(device, caps, preference, {Element::Float, 1}, {7, 5, 3}, volume, "volume_7x5x3"), "3D volume case failed");
        std::vector<std::int32_t> signed_volume(volume.size());
        std::vector<std::uint32_t> unsigned_volume(volume.size());
        for (std::size_t i = 0; i < volume.size(); ++i) {
            signed_volume[i] = static_cast<std::int32_t>(i) - 52;
            unsigned_volume[i] = static_cast<std::uint32_t>(i) * 0x01010101u;
        }
        signed_volume[0] = std::numeric_limits<std::int32_t>::min();
        signed_volume[1] = std::numeric_limits<std::int32_t>::max();
        unsigned_volume[0] = 0u;
        unsigned_volume[1] = std::numeric_limits<std::uint32_t>::max();
        REQUIRE(identity_case(device, caps, preference, {Element::Int, 1}, {7, 5, 3}, signed_volume, "signed_volume_7x5x3"),
                "signed integer volume case failed");
        REQUIRE(identity_case(device, caps, preference, {Element::Uint, 1}, {7, 5, 3}, unsigned_volume, "unsigned_volume_7x5x3"),
                "unsigned integer volume case failed");

        const unsigned long_count = caps.width + 1;
        REQUIRE(long_count <= 16'777'216u, "effective texture width is not representable for tiled one-dimensional test");
        std::vector<float> long_values(long_count);
        for (unsigned i = 0; i < long_count; ++i) long_values[i] = static_cast<float>((i * 17u) % 997u) * 0.001f;
        Field input, output;
        const Domain long_domain{long_count, 1, 1};
        REQUIRE(allocate<float>(device, {{Element::Float, 1}, long_domain}, input), "could not allocate tiled 1D input");
        REQUIRE(allocate<float>(device, {{Element::Float, 1}, long_domain}, output), "could not allocate tiled 1D output");
        REQUIRE(input.layout().width < long_count && input.layout().height > 1, "long 1D field was not tiled across physical rows");
        REQUIRE(upload(device, input, long_values), "could not upload tiled 1D input");
        auto prepared = device.prepare(copy_kernel({Element::Float, 1}, "tiled_1d"));
        REQUIRE_RESULT(prepared, "prepare tiled 1D copy kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "tiled 1D route check failed");
        const std::array<const Field*, 1> inputs{&input};
        const std::array<Field*, 1> outputs{&output};
        const std::array<ParameterValue, 0> parameters{};
        REQUIRE(dispatch(device, *prepared, inputs, outputs, parameters), "tiled 1D dispatch failed");
        std::vector<float> actual(long_count);
        REQUIRE(readback(device, output, actual), "tiled 1D readback failed");
        REQUIRE(close_enough(actual, long_values, "tiled 1D copy", 0.0f, 0.0f), "tiled 1D values differ");
        return true;
    }

    bool layout_validation_tests() {
        auto layout = field_layout({{Element::Float, 3}, {7, 5, 3}});
        REQUIRE_RESULT(layout, "compute field layout");
        REQUIRE(layout->logical_bytes == 7u * 5u * 3u * 3u * sizeof(float), "logical vec3 byte count is wrong");
        REQUIRE(layout->storage_bytes == std::size_t{7u * 5u * 3u} * 4u * sizeof(float), "physical vec3 storage did not include zero-padded channel");
        REQUIRE(!field_layout({{Element::Float, 1}, {0, 1, 1}}), "zero-axis field layout was accepted");
        REQUIRE(!field_layout({{Element::Uint, 4}, {8192, 8192, 1}}), "over-budget field layout was accepted");
        REQUIRE(!parse_backend("gpu-maybe"), "unknown backend spelling was accepted");
        REQUIRE(parse_backend("auto") && parse_backend("native") && parse_backend("slibs"), "supported backend spellings were rejected");
        REQUIRE(!parse_type("double"), "unsupported compute type was accepted");
        return true;
    }

    bool stencil_case(Device& device, const Capabilities& caps, Backend preference, Field& rendered_field,
                      float& rendered_value) {
        constexpr unsigned width = 19, height = 13, iterations = 7;
        const Type type{Element::Float, 1};
        const Domain domain{width, height, 1};
        std::vector<float> cpu(width * height, 0.0f);
        cpu[2 + width * 1] = 1.0f;
        cpu[width - 3 + width * (height - 2)] = 0.625f;
        cpu[width / 2 + width * (height / 2)] = 0.375f;
        Field first, second;
        REQUIRE(allocate<float>(device, {type, domain}, first), "could not allocate stencil first field");
        REQUIRE(allocate<float>(device, {type, domain}, second), "could not allocate stencil second field");
        REQUIRE(upload(device, first, cpu), "could not upload stencil impulses");
        Field* current_field = &first;
        Field* next_field = &second;
        KernelDesc desc{"jacobi_five_point",
                        "void kernel(ivec3 id, inout float result) { result = 0.5 * hc_read_src(id) + 0.125 * (hc_read_src(ivec3(id.x-1,id.y,id.z)) + hc_read_src(ivec3(id.x+1,id.y,id.z)) + hc_read_src(ivec3(id.x,id.y-1,id.z)) + hc_read_src(ivec3(id.x,id.y+1,id.z))); }",
                        {{"src", type}}, {{"result", type}}, {}};
        auto prepared = device.prepare(desc);
        REQUIRE_RESULT(prepared, "prepare Jacobi stencil");
        REQUIRE(check_backend(*prepared, preference, caps), "stencil route check failed");
        const auto before = device.statistics();
        const std::array<std::reference_wrapper<const Kernel>, 1> kernels{std::cref(*prepared)};
        {
            auto batch = device.begin(kernels);
            REQUIRE_RESULT(batch, "begin stencil batch");
            for (unsigned step = 0; step < iterations; ++step) {
                const std::array<const Field*, 1> inputs{current_field};
                const std::array<Field*, 1> outputs{next_field};
                const std::array<ParameterValue, 0> parameters{};
                REQUIRE_RESULT(batch->dispatch(*prepared, inputs, outputs, parameters), "dispatch Jacobi iteration");
                std::vector<float> next(cpu.size());
                for (unsigned y = 0; y < height; ++y) {
                    for (unsigned x = 0; x < width; ++x) {
                        const auto at = [&](int px, int py) -> float {
                            if (px < 0 || py < 0 || px >= static_cast<int>(width) || py >= static_cast<int>(height)) return 0.0f;
                            return cpu[static_cast<std::size_t>(py) * width + static_cast<unsigned>(px)];
                        };
                        next[std::size_t{y} * width + x] = 0.5f * at(static_cast<int>(x), static_cast<int>(y)) +
                            0.125f * (at(static_cast<int>(x) - 1, static_cast<int>(y)) + at(static_cast<int>(x) + 1, static_cast<int>(y)) +
                                      at(static_cast<int>(x), static_cast<int>(y) - 1) + at(static_cast<int>(x), static_cast<int>(y) + 1));
                    }
                }
                cpu.swap(next);
                std::swap(current_field, next_field);
            }
        }
        std::vector<float> actual(cpu.size());
        REQUIRE(readback(device, *current_field, actual), "stencil result readback failed");
        REQUIRE(close_enough(actual, cpu, "seven Jacobi steps"), "stencil result differs from CPU oracle");
        const auto after_readback = device.statistics();
        if (prepared->backend() == Backend::Native) {
            REQUIRE(after_readback.fetch_barriers > before.fetch_barriers, "native sampler dependency did not issue a fetch barrier");
            REQUIRE(after_readback.framebuffer_barriers > before.framebuffer_barriers, "native readback dependency did not issue a framebuffer barrier");
            REQUIRE(after_readback.image_barriers > before.image_barriers, "native prior-access to next image write did not issue an image barrier");
        }
        const unsigned sample_x = width / 2, sample_y = height / 2;
        rendered_value = cpu[sample_x + width * sample_y];
        rendered_field = std::move(*current_field);
        return true;
    }

    bool render_sampled_float(Device&, const Field&, std::string_view, std::array<int, 3>, float);

    bool particle_case(Device& device, const Capabilities& caps, Backend preference) {
        constexpr unsigned count = 73, steps = 32;
        const Type vector2{Element::Float, 2};
        const Type scalar{Element::Float, 1};
        const Domain domain{count, 1, 1};
        std::vector<float> position(count * 2), velocity(count * 2), acceleration{0.0005f, -0.00025f};
        for (unsigned i = 0; i < count; ++i) {
            position[2 * i] = i % 7 == 0 ? 0.91f : 0.15f + 0.009f * static_cast<float>(i);
            position[2 * i + 1] = 0.18f + 0.007f * static_cast<float>(i % 70);
            velocity[2 * i] = i % 5 == 0 ? 0.008f : -0.002f;
            velocity[2 * i + 1] = i % 3 == 0 ? 0.003f : -0.001f;
        }
        auto cpu_position = position;
        auto cpu_velocity = velocity;
        Field position_a, position_b, velocity_a, velocity_b;
        REQUIRE(allocate<float>(device, {vector2, domain}, position_a), "could not allocate particle position a");
        REQUIRE(allocate<float>(device, {vector2, domain}, position_b), "could not allocate particle position b");
        REQUIRE(allocate<float>(device, {vector2, domain}, velocity_a), "could not allocate particle velocity a");
        REQUIRE(allocate<float>(device, {vector2, domain}, velocity_b), "could not allocate particle velocity b");
        REQUIRE(upload(device, position_a, position), "could not upload particle positions");
        REQUIRE(upload(device, velocity_a, velocity), "could not upload particle velocities");
        FieldPair positions{std::move(position_a), std::move(position_b)};
        FieldPair velocities{std::move(velocity_a), std::move(velocity_b)};
        KernelDesc desc{"particle_step",
                        "void kernel(ivec3 id, inout vec2 nextPosition, inout vec2 nextVelocity) { vec2 p = hc_read_position(id) + hc_read_velocity(id) * dt; if (p.x >= 1.0) p.x -= 1.0; if (p.y >= 1.0) p.y -= 1.0; if (p.x < 0.0) p.x += 1.0; if (p.y < 0.0) p.y += 1.0; nextPosition = p; nextVelocity = hc_read_velocity(id) + acceleration * dt; }",
                        {{"position", vector2}, {"velocity", vector2}},
                        {{"nextPosition", vector2}, {"nextVelocity", vector2}},
                        {{"dt", scalar}, {"acceleration", vector2}}};
        auto prepared = device.prepare(desc);
        REQUIRE_RESULT(prepared, "prepare particle step");
        REQUIRE(check_backend(*prepared, preference, caps), "particle route check failed");
        ParameterValue dt{std::array<float, 4>{0.5f, 0.0f, 0.0f, 0.0f}, 1};
        ParameterValue accel{std::array<float, 4>{acceleration[0], acceleration[1], 0.0f, 0.0f}, 2};
        const std::array<ParameterValue, 2> parameters{dt, accel};
        const std::array<std::reference_wrapper<const Kernel>, 1> kernels{std::cref(*prepared)};
        {
            auto batch = device.begin(kernels);
            REQUIRE_RESULT(batch, "begin particle batch");
            for (unsigned step = 0; step < steps; ++step) {
                const std::array<const Field*, 2> inputs{&positions.read(), &velocities.read()};
                const std::array<Field*, 2> outputs{&positions.write(), &velocities.write()};
                REQUIRE_RESULT(batch->dispatch(*prepared, inputs, outputs, parameters), "dispatch particle iteration");
                auto next_position = cpu_position;
                auto next_velocity = cpu_velocity;
                for (unsigned i = 0; i < count; ++i) {
                    float x = cpu_position[2 * i] + cpu_velocity[2 * i] * 0.5f;
                    float y = cpu_position[2 * i + 1] + cpu_velocity[2 * i + 1] * 0.5f;
                    if (x >= 1.0f) x -= 1.0f;
                    if (y >= 1.0f) y -= 1.0f;
                    if (x < 0.0f) x += 1.0f;
                    if (y < 0.0f) y += 1.0f;
                    next_position[2 * i] = x;
                    next_position[2 * i + 1] = y;
                    next_velocity[2 * i] = cpu_velocity[2 * i] + acceleration[0] * 0.5f;
                    next_velocity[2 * i + 1] = cpu_velocity[2 * i + 1] + acceleration[1] * 0.5f;
                }
                cpu_position.swap(next_position);
                cpu_velocity.swap(next_velocity);
                positions.swap();
                velocities.swap();
            }
        }
        std::vector<float> position_actual(cpu_position.size()), velocity_actual(cpu_velocity.size());
        REQUIRE(readback(device, positions.read(), position_actual), "particle position readback failed");
        REQUIRE(readback(device, velocities.read(), velocity_actual), "particle velocity readback failed");
        REQUIRE(close_enough(position_actual, cpu_position, "32 particle position steps", 1.0e-5f, 1.0e-5f), "particle positions differ from CPU oracle");
        REQUIRE(close_enough(velocity_actual, cpu_velocity, "32 particle velocity steps", 1.0e-5f, 1.0e-5f), "particle velocities differ from CPU oracle");
        REQUIRE(render_sampled_float(device, positions.read(), "position", {0, 0, 0}, cpu_position[0]),
                "particle vertex-sampled presentation failed");
        return true;
    }

    struct DrawResources {
        GLuint texture{}, framebuffer{}, vao{}, program{};
        ~DrawResources() {
            if (program) glDeleteProgram(program);
            if (vao) glDeleteVertexArrays(1, &vao);
            if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
            if (texture) glDeleteTextures(1, &texture);
        }
    };

    bool render_sampled_float(Device& device, const Field& field, std::string_view alias,
                              std::array<int, 3> coordinate, float expected) {
        auto sampled_result = device.sample(field);
        REQUIRE_RESULT(sampled_result, "borrow sampled compute field");
        const auto& sampled = *sampled_result;
        REQUIRE(sampled.texture != 0 && sampled.desc == field.descriptor(), "sampled field view has wrong name or logical descriptor");
        REQUIRE(sampled.width == field.layout().width && sampled.height == field.layout().height, "sampled field physical extent differs from field layout");
        auto helper = sample_source(alias, field.descriptor().type);
        REQUIRE_RESULT(helper, "generate typed sampled-field helper");
        const std::string function = "hc_read_" + std::string(alias);
        const std::string sampled_value = function + "(ivec3(" + std::to_string(coordinate[0]) + "," +
            std::to_string(coordinate[1]) + "," + std::to_string(coordinate[2]) + "))" +
            (field.descriptor().type.lanes == 1 ? "" : ".x");
        const std::string vertex = "#version 300 es\nprecision highp float;\nprecision highp int;\n" + *helper +
            "out highp float sampledValue;\nvoid main(){highp vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
            "gl_Position=vec4(p*2.0-1.0,0.0,1.0);sampledValue=float(" + sampled_value + ");}\n";
        const std::string fragment = "#version 300 es\nprecision highp float;\nin highp float sampledValue;\n"
            "layout(location=0) out highp vec4 color;\nvoid main(){color=vec4(sampledValue,0.0,0.0,1.0);}\n";
        DrawResources resources;
        GLint old_draw = 0, old_read = 0, old_program = 0, old_vao = 0, old_active = 0, old_viewport[4]{};
        GLint old_texture0 = 0, old_sampler0 = 0;
        GLfloat old_clear[4]{};
        const std::array<GLenum, 4> caps{GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_CULL_FACE};
        std::array<GLboolean, caps.size()> old_caps{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read);
        glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
        glGetIntegerv(GL_VIEWPORT, old_viewport);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, old_clear);
        for (std::size_t i = 0; i < caps.size(); ++i) old_caps[i] = glIsEnabled(caps[i]);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture0);
        glGetIntegerv(GL_SAMPLER_BINDING, &old_sampler0);
        for (const auto cap : caps) glDisable(cap);
        glGenTextures(1, &resources.texture);
        glBindTexture(GL_TEXTURE_2D, resources.texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 7, 5);
        glGenFramebuffers(1, &resources.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, resources.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resources.texture, 0);
        REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "sample presentation target framebuffer is incomplete");
        resources.program = gl_harness::build_program(vertex, fragment);
        REQUIRE(resources.program != 0, "could not compile sampled-field presentation shader");
        glGenVertexArrays(1, &resources.vao);
        glBindVertexArray(resources.vao);
        glViewport(2, 1, 3, 2);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(resources.program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sampled.texture);
        glBindSampler(0, 0);
        const auto sampler_name = "hc_tex_" + std::string(alias);
        const auto size_name = "hc_size_" + std::string(alias);
        glUniform1i(glGetUniformLocation(resources.program, sampler_name.c_str()), 0);
        glUniform3i(glGetUniformLocation(resources.program, size_name.c_str()),
                    static_cast<GLint>(sampled.desc.domain.x), static_cast<GLint>(sampled.desc.domain.y),
                    static_cast<GLint>(sampled.desc.domain.z));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::array<std::uint8_t, 4> pixel{}, untouched{};
        glReadPixels(2, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, untouched.data());
        const auto expected_byte = static_cast<int>(std::lround(std::clamp(expected, 0.0f, 1.0f) * 255.0f));
        REQUIRE(std::abs(static_cast<int>(pixel[0]) - expected_byte) <= 1 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255,
                "off-origin vertex-sampled draw pixel differs from expected field value");
        REQUIRE(untouched[0] == 0 && untouched[1] == 0 && untouched[2] == 0 && untouched[3] == 0,
                "off-origin compute presentation modified an untouched target pixel");
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(old_draw));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(old_read));
        glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
        glUseProgram(static_cast<GLuint>(old_program));
        glBindVertexArray(static_cast<GLuint>(old_vao));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(old_texture0));
        glBindSampler(0, static_cast<GLuint>(old_sampler0));
        glActiveTexture(static_cast<GLenum>(old_active));
        glClearColor(old_clear[0], old_clear[1], old_clear[2], old_clear[3]);
        for (std::size_t i = 0; i < caps.size(); ++i) {
            if (old_caps[i]) glEnable(caps[i]); else glDisable(caps[i]);
        }
        return true;
    }

    bool raster_and_native_state_test(Device& device, const Capabilities& caps, Backend preference) {
        const Type type{Element::Uint, 1};
        const Domain domain{8, 1, 1};
        Field input, output;
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, input), "could not allocate state-test input");
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, output), "could not allocate state-test output");
        const std::vector<std::uint32_t> seed(domain.x, 5u);
        REQUIRE(upload(device, input, seed), "could not upload state-test input");
        KernelDesc desc{"state_scope", "void kernel(ivec3 id, inout uint result) { result = hc_read_src(id) + uint(id.x + 1); }",
                        {{"src", type}}, {{"result", type}}, {}};
        auto prepared = device.prepare(desc);
        REQUIRE_RESULT(prepared, "prepare state-scope kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "state-scope route check failed");
        const std::string vertex = "#version 300 es\nvoid main(){gl_Position=vec4(0.0,0.0,0.0,1.0);}\n";
        const std::string fragment = "#version 300 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.0);}\n";
        const GLuint sentinel_program = gl_harness::build_program(vertex, fragment);
        REQUIRE(sentinel_program != 0, "could not build caller sentinel program");
        GLuint sentinel_texture0 = 0, sentinel_texture = 0, sentinel_sampler0 = 0, sentinel_sampler = 0, caller_draw = 0, caller_read = 0;
        glGenTextures(1, &sentinel_texture0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sentinel_texture0);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 2, 2);
        glGenSamplers(1, &sentinel_sampler0);
        glSamplerParameteri(sentinel_sampler0, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glBindSampler(0, sentinel_sampler0);
        glGenTextures(1, &sentinel_texture);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, sentinel_texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 2, 2);
        glGenSamplers(1, &sentinel_sampler);
        glSamplerParameteri(sentinel_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glBindSampler(3, sentinel_sampler);
        glGenFramebuffers(1, &caller_draw);
        glGenFramebuffers(1, &caller_read);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, caller_draw);
        const GLenum no_draw_buffer = GL_NONE;
        glDrawBuffers(1, &no_draw_buffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, caller_read);
        glReadBuffer(GL_NONE);
        glViewport(3, 4, 11, 13);
        glEnable(GL_SCISSOR_TEST);
        glScissor(1, 2, 3, 4);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        glUseProgram(sentinel_program);

        GLuint image_texture = 0, image_array = 0;
        using BindImage = void (GL_APIENTRYP)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
        using GetIntegerIndexed = void (GL_APIENTRYP)(GLenum, GLuint, GLint*);
        BindImage bind_image = nullptr;
        if (prepared->backend() == Backend::Native) {
            // Image entry points stay dynamically resolved, just as in the production adapter.
            bind_image = reinterpret_cast<BindImage>(eglGetProcAddress("glBindImageTexture"));
            REQUIRE(bind_image != nullptr && caps.image_units >= 2, "native route lacks image state needed for restoration check");
            glGenTextures(1, &image_texture);
            glBindTexture(GL_TEXTURE_2D, image_texture);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, 1, 1);
            glGenTextures(1, &image_array);
            glBindTexture(GL_TEXTURE_2D_ARRAY, image_array);
            glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_R32UI, 1, 1, 4);
            bind_image(0, image_texture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
            bind_image(1, image_array, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
            glBindTexture(GL_TEXTURE_2D, sentinel_texture);
        }
        GLint expected_draw = 0, expected_read = 0, expected_program = 0, expected_viewport[4]{};
        GLint expected_active = 0, expected_texture0 = 0, expected_sampler0 = 0, expected_texture3 = 0, expected_sampler3 = 0, expected_draw_buffer = 0, expected_read_buffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &expected_draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &expected_read);
        glGetIntegerv(GL_CURRENT_PROGRAM, &expected_program);
        glGetIntegerv(GL_VIEWPORT, expected_viewport);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &expected_active);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &expected_texture3);
        glGetIntegerv(GL_SAMPLER_BINDING, &expected_sampler3);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &expected_texture0);
        glGetIntegerv(GL_SAMPLER_BINDING, &expected_sampler0);
        glActiveTexture(GL_TEXTURE3);
        glGetIntegerv(GL_DRAW_BUFFER0, &expected_draw_buffer);
        glGetIntegerv(GL_READ_BUFFER, &expected_read_buffer);
        std::array<GLint, 6> image0{}, image1{};
        if (prepared->backend() == Backend::Native) {
            constexpr std::array<GLenum, 6> properties{0x8F3A, 0x8F3B, 0x8F3C, 0x8F3D, 0x8F3E, 0x906E};
            const auto get_indexed = reinterpret_cast<GetIntegerIndexed>(eglGetProcAddress("glGetIntegeri_v"));
            REQUIRE(get_indexed != nullptr, "could not resolve indexed image-state query");
            for (std::size_t i = 0; i < properties.size(); ++i) {
                get_indexed(properties[i], 0, &image0[i]);
                get_indexed(properties[i], 1, &image1[i]);
            }
        }
        {
            const std::array<std::reference_wrapper<const Kernel>, 1> kernels{std::cref(*prepared)};
            auto batch = device.begin(kernels);
            REQUIRE_RESULT(batch, "begin hostile-state compute batch");
            const std::array<const Field*, 1> inputs{&input};
            const std::array<Field*, 1> outputs{&output};
            const std::array<ParameterValue, 0> parameters{};
            REQUIRE_RESULT(batch->dispatch(*prepared, inputs, outputs, parameters), "dispatch hostile-state kernel");
        }
        GLint actual = 0, viewport[4]{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &actual); REQUIRE(actual == expected_draw, "draw framebuffer binding was not restored");
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &actual); REQUIRE(actual == expected_read, "read framebuffer binding was not restored");
        glGetIntegerv(GL_CURRENT_PROGRAM, &actual); REQUIRE(actual == expected_program, "caller program was not restored");
        glGetIntegerv(GL_VIEWPORT, viewport); REQUIRE(std::equal(std::begin(viewport), std::end(viewport), std::begin(expected_viewport)), "viewport was not restored");
        glGetIntegerv(GL_ACTIVE_TEXTURE, &actual); REQUIRE(actual == expected_active, "active texture was not restored");
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual); REQUIRE(actual == expected_texture3, "untouched caller texture binding was not restored");
        glGetIntegerv(GL_SAMPLER_BINDING, &actual); REQUIRE(actual == expected_sampler3, "untouched caller sampler binding was not restored");
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual); REQUIRE(actual == expected_texture0, "touched caller texture binding was not restored");
        glGetIntegerv(GL_SAMPLER_BINDING, &actual); REQUIRE(actual == expected_sampler0, "touched caller sampler binding was not restored");
        glActiveTexture(GL_TEXTURE3);
        glGetIntegerv(GL_DRAW_BUFFER0, &actual); REQUIRE(actual == expected_draw_buffer, "caller draw routing was not restored");
        glGetIntegerv(GL_READ_BUFFER, &actual); REQUIRE(actual == expected_read_buffer, "caller read routing was not restored");
        REQUIRE(glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_BLEND), "caller raster enables were not restored");
        std::array<GLboolean, 4> color_mask{};
        glGetBooleanv(GL_COLOR_WRITEMASK, color_mask.data());
        REQUIRE((color_mask == std::array<GLboolean, 4>{GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE}), "caller color mask was not restored");
        if (prepared->backend() == Backend::Native) {
            constexpr std::array<GLenum, 6> properties{0x8F3A, 0x8F3B, 0x8F3C, 0x8F3D, 0x8F3E, 0x906E};
            const auto get_indexed = reinterpret_cast<GetIntegerIndexed>(eglGetProcAddress("glGetIntegeri_v"));
            for (std::size_t i = 0; i < properties.size(); ++i) {
                GLint after0 = 0, after1 = 0;
                get_indexed(properties[i], 0, &after0);
                get_indexed(properties[i], 1, &after1);
                REQUIRE(after0 == image0[i], "touched image binding property was not restored");
                REQUIRE(after1 == image1[i], "untouched higher image unit was modified");
            }
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glUseProgram(0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindSampler(3, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindSampler(0, 0);
        if (image_texture) glDeleteTextures(1, &image_texture);
        if (image_array) glDeleteTextures(1, &image_array);
        if (caller_draw) glDeleteFramebuffers(1, &caller_draw);
        if (caller_read) glDeleteFramebuffers(1, &caller_read);
        if (sentinel_sampler0) glDeleteSamplers(1, &sentinel_sampler0);
        if (sentinel_texture0) glDeleteTextures(1, &sentinel_texture0);
        glDeleteProgram(sentinel_program);
        return true;
    }

    GLuint build_feedback_program() {
        const std::string vertex =
            "#version 300 es\nin highp float inputValue;\nout highp float captured;\n"
            "void main(){ captured=inputValue; gl_Position=vec4(inputValue,0.0,0.0,1.0); }\n";
        const std::string fragment =
            "#version 300 es\nprecision highp float;\nout vec4 color;\nvoid main(){ color=vec4(0.0); }\n";
        const GLuint vertex_shader = gl_harness::detail::compile(GL_VERTEX_SHADER, vertex);
        const GLuint fragment_shader = gl_harness::detail::compile(GL_FRAGMENT_SHADER, fragment);
        if (!vertex_shader || !fragment_shader) {
            if (vertex_shader) glDeleteShader(vertex_shader);
            if (fragment_shader) glDeleteShader(fragment_shader);
            return 0;
        }
        const GLuint program = glCreateProgram();
        if (!program) {
            glDeleteShader(vertex_shader);
            glDeleteShader(fragment_shader);
            return 0;
        }
        glAttachShader(program, vertex_shader);
        glAttachShader(program, fragment_shader);
        glBindAttribLocation(program, 0, "inputValue");
        const GLchar* varyings[]{"captured"};
        glTransformFeedbackVaryings(program, 1, varyings, GL_INTERLEAVED_ATTRIBS);
        glLinkProgram(program);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        if (linked == GL_FALSE) {
            std::array<char, 1024> log{};
            glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
            std::fprintf(stderr, "transform-feedback fixture link failed: %s\n", log.data());
            glDeleteProgram(program);
            return 0;
        }
        return program;
    }

    bool feedback_state_matches(GLuint program, GLuint vao, GLuint feedback, GLuint buffer, bool paused) {
        GLint active = GL_FALSE, is_paused = GL_FALSE, current_program = 0, current_vao = 0, current_feedback = 0;
        GLint current_buffer = 0, current_generic_buffer = 0;
        glGetIntegerv(GL_TRANSFORM_FEEDBACK_ACTIVE, &active);
        glGetIntegerv(GL_TRANSFORM_FEEDBACK_PAUSED, &is_paused);
        glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current_vao);
        glGetIntegerv(GL_TRANSFORM_FEEDBACK_BINDING, &current_feedback);
        glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, &current_generic_buffer);
        glGetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 0, &current_buffer);
        REQUIRE(active == GL_TRUE && is_paused == (paused ? GL_TRUE : GL_FALSE),
                "compute changed active or paused transform-feedback state");
        REQUIRE(current_program == static_cast<GLint>(program) && current_vao == static_cast<GLint>(vao) &&
                current_feedback == static_cast<GLint>(feedback) && current_buffer == static_cast<GLint>(buffer) &&
                current_generic_buffer == static_cast<GLint>(buffer),
                "compute changed caller transform-feedback program, VAO, object, or buffer binding");
        return true;
    }

    bool feedback_buffer_matches(GLuint buffer) {
        constexpr float sentinel = -17.0f;
        glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer);
        const auto* mapped = static_cast<const float*>(
            glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(float) * 4), GL_MAP_READ_BIT));
        REQUIRE(mapped != nullptr, "could not map transform-feedback capture buffer");
        std::array<float, 4> captured{};
        std::copy_n(mapped, captured.size(), captured.begin());
        const GLboolean unmapped = glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
        REQUIRE(unmapped == GL_TRUE, "transform-feedback capture buffer became corrupt");
        REQUIRE(captured[0] == 0.25f, "caller point did not write its known transform-feedback value");
        REQUIRE(std::ranges::all_of(captured.begin() + 1, captured.end(),
                                    [](float value) { return value == sentinel; }),
                "compute contributed unexpected transform-feedback buffer writes");
        return true;
    }

    bool transform_feedback_isolation_test(Device& device, const Kernel& kernel) {
        GLint active = GL_FALSE, saved_program = 0, saved_vao = 0, saved_feedback = 0;
        GLint saved_array_buffer = 0, saved_feedback_buffer = 0, saved_indexed_feedback_buffer = 0;
        GLint active_query = 0;
        glGetIntegerv(GL_TRANSFORM_FEEDBACK_ACTIVE, &active);
        glGetIntegerv(GL_CURRENT_PROGRAM, &saved_program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved_vao);
        glGetIntegerv(GL_TRANSFORM_FEEDBACK_BINDING, &saved_feedback);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved_array_buffer);
        glGetIntegerv(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, &saved_feedback_buffer);
        glGetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 0, &saved_indexed_feedback_buffer);
        glGetQueryiv(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, GL_CURRENT_QUERY, &active_query);
        REQUIRE(active == GL_FALSE && active_query == 0, "transform-feedback fixture requires idle caller capture/query state");
        REQUIRE(glGetError() == GL_NO_ERROR, "GL error before transform-feedback fixture setup");

        const GLuint program = build_feedback_program();
        REQUIRE(program != 0, "could not build transform-feedback capture program");
        GLuint vao = 0, input_buffer = 0, capture_buffer = 0, feedback = 0, query = 0;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &input_buffer);
        glGenBuffers(1, &capture_buffer);
        glGenTransformFeedbacks(1, &feedback);
        glGenQueries(1, &query);
        REQUIRE(vao && input_buffer && capture_buffer && feedback && query,
                "could not allocate transform-feedback fixture objects");
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, input_buffer);
        constexpr float input_value = 0.25f;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(input_value)), &input_value, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(float)), nullptr);
        glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, feedback);
        const auto reset_capture = [&] {
            constexpr std::array<float, 4> initial{-17.0f, -17.0f, -17.0f, -17.0f};
            glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, capture_buffer);
            glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, static_cast<GLsizeiptr>(sizeof(initial)), initial.data(), GL_DYNAMIC_READ);
            glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, capture_buffer);
            return glGetError() == GL_NO_ERROR;
        };
        REQUIRE(reset_capture(), "could not initialize transform-feedback capture buffer");
        REQUIRE(glGetError() == GL_NO_ERROR, "GL error after transform-feedback fixture setup");

        const std::array<std::reference_wrapper<const Kernel>, 1> kernels{std::cref(kernel)};
        const std::array<const Field*, 0> no_inputs{};
        const std::array<ParameterValue, 0> no_parameters{};
        const FieldDesc output_desc{{Element::Uint, 1}, {4, 1, 1}};
        Field compute_output;
        REQUIRE(allocate<std::uint32_t>(device, output_desc, compute_output),
                "could not allocate paused-transform-feedback compute output");
        const std::array<Field*, 1> outputs{&compute_output};

        glUseProgram(program);
        glBeginTransformFeedback(GL_POINTS);
        REQUIRE(glGetError() == GL_NO_ERROR, "could not begin active-unpaused transform feedback");
        REQUIRE(feedback_state_matches(program, vao, feedback, capture_buffer, false),
                "active-unpaused transform-feedback setup mismatch");
        auto active_unpaused = device.begin(kernels);
        REQUIRE(!active_unpaused &&
                active_unpaused.error().find("active unpaused transform feedback") != std::string::npos,
                "compute did not explicitly reject active unpaused transform feedback");
        REQUIRE(glGetError() == GL_NO_ERROR, "active transform-feedback rejection introduced a GL error");
        REQUIRE(feedback_state_matches(program, vao, feedback, capture_buffer, false),
                "active unpaused transform feedback changed after rejection");
        glDrawArrays(GL_POINTS, 0, 1);
        glEndTransformFeedback();
        REQUIRE(glGetError() == GL_NO_ERROR, "caller draw after active-transform-feedback rejection failed");
        REQUIRE(feedback_buffer_matches(capture_buffer), "active-unpaused rejection changed caller capture");

        REQUIRE(reset_capture(), "could not reset paused-transform-feedback capture buffer");
        glBeginTransformFeedback(GL_POINTS);
        glPauseTransformFeedback();
        REQUIRE(glGetError() == GL_NO_ERROR, "could not pause transform feedback");
        REQUIRE(feedback_state_matches(program, vao, feedback, capture_buffer, true),
                "paused transform-feedback setup mismatch");
        auto batch = device.begin(kernels);
        REQUIRE_RESULT(batch, "compute begin with paused transform feedback and no query");
        REQUIRE_RESULT(batch->dispatch(kernel, no_inputs, outputs, no_parameters),
                       "compute dispatch with paused transform feedback and no query");
        batch = Batch{};
        REQUIRE(glGetError() == GL_NO_ERROR, "paused-transform-feedback compute left a GL error");
        REQUIRE(feedback_state_matches(program, vao, feedback, capture_buffer, true),
                "paused transform feedback was not preserved across successful compute");
        glResumeTransformFeedback();
        glDrawArrays(GL_POINTS, 0, 1);
        glEndTransformFeedback();
        REQUIRE(glGetError() == GL_NO_ERROR, "caller draw after paused-transform-feedback compute failed");
        REQUIRE(feedback_buffer_matches(capture_buffer), "paused-transform-feedback compute contaminated caller capture");
        std::vector<std::uint32_t> compute_values(4, 99u);
        REQUIRE(readback(device, compute_output, compute_values), "could not read paused-transform-feedback compute output");
        for (std::size_t i = 0; i < compute_values.size(); ++i)
            REQUIRE(compute_values[i] == i, "paused-transform-feedback compute produced an incorrect field value");

        REQUIRE(reset_capture(), "could not reset query-isolation capture buffer");
        glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query);
        glBeginTransformFeedback(GL_POINTS);
        glPauseTransformFeedback();
        REQUIRE(glGetError() == GL_NO_ERROR, "could not begin paused transform-feedback query fixture");
        GLint current_query = 0;
        glGetQueryiv(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, GL_CURRENT_QUERY, &current_query);
        REQUIRE(current_query == static_cast<GLint>(query), "transform-feedback primitives-written query did not become active");
        auto query_active = device.begin(kernels);
        REQUIRE(!query_active &&
                query_active.error().find("transform feedback query") != std::string::npos,
                "compute did not reject an active transform-feedback primitives-written query");
        REQUIRE(glGetError() == GL_NO_ERROR, "transform-feedback query rejection introduced a GL error");
        REQUIRE(feedback_state_matches(program, vao, feedback, capture_buffer, true),
                "paused transform-feedback query state changed after rejection");
        current_query = 0;
        glGetQueryiv(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, GL_CURRENT_QUERY, &current_query);
        REQUIRE(current_query == static_cast<GLint>(query),
                "compute changed the caller's active transform-feedback query");
        glResumeTransformFeedback();
        glDrawArrays(GL_POINTS, 0, 1);
        glEndTransformFeedback();
        glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
        REQUIRE(glGetError() == GL_NO_ERROR, "caller draw after transform-feedback query rejection failed");
        GLuint primitives_written = 0;
        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &primitives_written);
        REQUIRE(primitives_written == 1, "transform-feedback query did not count exactly the caller's point");
        REQUIRE(feedback_buffer_matches(capture_buffer), "rejected compute contributed to transform-feedback capture");

        glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, static_cast<GLuint>(saved_feedback));
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, static_cast<GLuint>(saved_indexed_feedback_buffer));
        glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, static_cast<GLuint>(saved_feedback_buffer));
        glBindVertexArray(static_cast<GLuint>(saved_vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(saved_array_buffer));
        glUseProgram(static_cast<GLuint>(saved_program));
        glDeleteQueries(1, &query);
        glDeleteTransformFeedbacks(1, &feedback);
        glDeleteBuffers(1, &capture_buffer);
        glDeleteBuffers(1, &input_buffer);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
        REQUIRE(glGetError() == GL_NO_ERROR, "transform-feedback isolation test cleanup left a GL error");
        return true;
    }

    bool caller_unsafe_state_rejection_test(Device& device, const Kernel& kernel) {
        const std::array<std::reference_wrapper<const Kernel>, 1> kernels{std::cref(kernel)};
        const std::string vertex = "#version 300 es\nvoid main(){gl_Position=vec4(0.0,0.0,0.0,1.0);}\n";
        const std::string fragment = "#version 300 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.0);}\n";
        const GLuint program = gl_harness::build_program(vertex, fragment);
        REQUIRE(program != 0, "could not build failed-relink fixture program");
        REQUIRE(glGetError() == GL_NO_ERROR, "GL error before failed-relink fixture setup");
        const GLuint invalid = glCreateShader(GL_FRAGMENT_SHADER);
        const GLchar* invalid_source = "#version 300 es\nvoid main(){ this is not valid GLSL; }\n";
        glShaderSource(invalid, 1, &invalid_source, nullptr);
        glCompileShader(invalid);
        GLint invalid_compiled = GL_TRUE;
        glGetShaderiv(invalid, GL_COMPILE_STATUS, &invalid_compiled);
        REQUIRE(invalid_compiled == GL_FALSE, "failed-relink shader unexpectedly compiled");
        glUseProgram(program);
        GLint attached_count = 0;
        glGetProgramiv(program, GL_ATTACHED_SHADERS, &attached_count);
        std::array<GLuint, 8> attached_shaders{};
        GLsizei returned_shaders = 0;
        glGetAttachedShaders(program, static_cast<GLsizei>(attached_shaders.size()), &returned_shaders, attached_shaders.data());
        bool detached_fragment = false;
        for (GLsizei i = 0; i < returned_shaders; ++i) {
            GLint shader_type = 0;
            glGetShaderiv(attached_shaders[static_cast<std::size_t>(i)], GL_SHADER_TYPE, &shader_type);
            if (shader_type == GL_FRAGMENT_SHADER) {
                glDetachShader(program, attached_shaders[static_cast<std::size_t>(i)]);
                detached_fragment = true;
            }
        }
        REQUIRE(attached_count == returned_shaders && detached_fragment,
                "failed-relink fixture did not find and detach its original fragment shader");
        REQUIRE(glGetError() == GL_NO_ERROR, "GL error detaching original fragment shader");
        glAttachShader(program, invalid);
        REQUIRE(glGetError() == GL_NO_ERROR, "GL error attaching invalid replacement fragment shader");
        glLinkProgram(program);
        REQUIRE(glGetError() == GL_NO_ERROR, "GL error linking invalid replacement shader");
        GLint linked = GL_TRUE, current = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        glGetIntegerv(GL_CURRENT_PROGRAM, &current);
        REQUIRE(linked == GL_FALSE && current == static_cast<GLint>(program),
                "failed relink fixture did not leave its prior executable current");
        auto relinked = device.begin(kernels);
        REQUIRE(!relinked && relinked.error().find("failed relink") != std::string::npos,
                "compute borrowed a current program whose relink had failed");
        REQUIRE(glGetError() == GL_NO_ERROR, "compute relink rejection introduced a GL error");
        glUseProgram(0);
        glDetachShader(program, invalid);
        glDeleteShader(invalid);
        glDeleteProgram(program);

        GLuint query = 0;
        glGenQueries(1, &query);
        constexpr GLenum conservative_query = 0x8D6A;
        glBeginQuery(conservative_query, query);
        auto occlusion = device.begin(kernels);
        glEndQuery(conservative_query);
        REQUIRE(!occlusion && occlusion.error().find("conservative visibility query") != std::string::npos,
                "compute did not reject an active conservative occlusion query");
        glDeleteQueries(1, &query);
        REQUIRE(transform_feedback_isolation_test(device, kernel), "transform-feedback isolation test failed");
        return true;
    }

    bool off_owner_retirement_test(Device& device) {
        Field retired;
        REQUIRE(allocate<std::uint32_t>(device, {{Element::Uint, 1}, {6, 2, 1}}, retired),
                "could not allocate off-owner retirement fixture");
        auto sampled = device.sample(retired);
        REQUIRE_RESULT(sampled, "sample off-owner retirement fixture");
        const auto name = static_cast<GLuint>(sampled->texture);
        const auto before = device.statistics();
        std::thread worker([field = std::move(retired)]() mutable { field = Field{}; });
        worker.join();
        const auto queued = device.statistics();
        REQUIRE(queued.live_bytes == before.live_bytes && queued.idle_bytes == before.idle_bytes,
                "off-owner field destruction changed GL pool accounting before owner-thread retirement");
        REQUIRE(glIsTexture(name) == GL_TRUE, "off-owner field destruction deleted a live GL texture");
        Field drain_trigger;
        REQUIRE(allocate<std::int32_t>(device, {{Element::Int, 1}, {7, 1, 1}}, drain_trigger),
                "could not trigger owner-thread retirement drain");
        drain_trigger = Field{};
        const auto drained = device.statistics();
        REQUIRE(drained.live_bytes < before.live_bytes && drained.idle_bytes > queued.idle_bytes,
                "owner-thread operation did not retire the deferred field into the idle pool");
        REQUIRE(glIsTexture(name) == GL_TRUE, "deferred field retirement deleted its pooled texture");
        return true;
    }
    bool dependency_and_copy_tests(Device& device, const Capabilities& caps, Backend preference) {
        const Type type{Element::Uint, 1};
        const Domain domain{17, 1, 1};
        Field source, first, second;
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, source), "could not allocate dependency source");
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, first), "could not allocate dependency first target");
        Field unequal_domain;
        REQUIRE(allocate<std::uint32_t>(device, {type, {18, 1, 1}}, unequal_domain),
                "could not allocate unequal-domain copy fixture");
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, second), "could not allocate dependency second target");
        std::vector<std::uint32_t> values(domain.x), replacement(domain.x), actual(domain.x);
        for (unsigned i = 0; i < domain.x; ++i) { values[i] = i * 3u + 1u; replacement[i] = 100u + i; }
        REQUIRE(upload(device, source, values), "could not upload dependency source");
        auto prepared = device.prepare(copy_kernel(type, "dependency_copy"));
        REQUIRE_RESULT(prepared, "prepare dependency copy kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "dependency route check failed");
        const std::array<const Field*, 1> inputs{&source};
        const std::array<Field*, 1> outputs{&first};
        const std::array<ParameterValue, 0> parameters{};
        REQUIRE(dispatch(device, *prepared, inputs, outputs, parameters), "first dependency dispatch failed");
        const auto after_write = device.statistics();
        REQUIRE(success(device.upload(first, std::as_bytes(std::span(replacement))), "overwrite image-written field with upload"), "post-dispatch upload failed");
        const auto after_upload = device.statistics();
        if (prepared->backend() == Backend::Native)
            REQUIRE(after_upload.update_barriers > after_write.update_barriers, "native image-to-upload dependency did not issue update barrier");
        const std::array<const Field*, 1> second_inputs{&first};
        const std::array<Field*, 1> second_outputs{&second};
        REQUIRE(dispatch(device, *prepared, second_inputs, second_outputs, parameters), "sampler dependency dispatch failed");
        REQUIRE(readback(device, second, actual), "dependency result readback failed");
        REQUIRE(actual == replacement, "upload-to-sampler dependency result differs");
        const auto before_copy = device.statistics();
        REQUIRE(success(device.copy(second, first), "copy equal fields"), "field copy failed");
        const auto after_copy = device.statistics();
        REQUIRE(after_copy.copy_bytes > before_copy.copy_bytes, "copy statistics did not count a successful field copy");
        REQUIRE(readback(device, first, actual), "copied field readback failed");
        REQUIRE(actual == replacement, "copied field contents differ");
        REQUIRE(!device.copy(first, first), "self-copy was accepted");
        REQUIRE(!device.copy(source, unequal_domain), "copy between fields with unequal logical domains was accepted");
        return true;
    }

    bool pooling_test(Device& device) {
        const FieldDesc desc{{Element::Int, 1}, {5, 3, 1}};
        std::vector<std::int32_t> nonzero(15, 123), cleared(15, -1);
        Field field;
        REQUIRE(allocate<std::int32_t>(device, desc, field), "could not allocate pool fixture");
        REQUIRE(upload(device, field, nonzero), "could not dirty pool fixture");
        const auto before_release = device.statistics();
        field = Field{};
        const auto pooled = device.statistics();
        REQUIRE(pooled.live_bytes < before_release.live_bytes, "field destruction did not release live allocation accounting");
        REQUIRE(pooled.idle_bytes <= 16u * 1024u * 1024u, "idle pool exceeded its byte bound");
        const auto hits_before = pooled.pool_hits;
        Field reused;
        REQUIRE(allocate<std::int32_t>(device, desc, reused), "could not reacquire pooled field");
        const auto after_reuse = device.statistics();
        REQUIRE(after_reuse.pool_hits > hits_before, "same-sized field did not reuse an idle allocation");
        REQUIRE(readback(device, reused, cleared), "could not read newly leased field");
        REQUIRE(std::ranges::all_of(cleared, [](std::int32_t value) { return value == 0; }), "pooled field exposed stale contents instead of typed zero");
        return true;
    }

    bool warm_counter_test(Device& device, const Capabilities& caps, Backend preference) {
        const Type type{Element::Uint, 1};
        const Domain domain{64, 1, 1};
        Field input, output;
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, input), "could not allocate warm input");
        REQUIRE(allocate<std::uint32_t>(device, {type, domain}, output), "could not allocate warm output");
        std::vector<std::uint32_t> values(domain.x);
        for (unsigned i = 0; i < domain.x; ++i) values[i] = 0xfffffff0u + i;
        REQUIRE(upload(device, input, values), "could not upload warm input");
        KernelDesc desc{"warm_uniform_dispatch",
                        "void kernel(ivec3 id, inout uint result) { result = hc_read_src(id) + amount; }",
                        {{"src", type}}, {{"result", type}}, {{"amount", type}}};
        auto prepared = device.prepare(desc);
        REQUIRE_RESULT(prepared, "prepare warm counter kernel");
        REQUIRE(check_backend(*prepared, preference, caps), "warm counter route check failed");
        ParameterValue amount{std::array<std::uint32_t, 4>{1u, 0u, 0u, 0u}, 1};
        const std::array<ParameterValue, 1> parameters{amount};
        const std::array<std::reference_wrapper<const Kernel>, 1> kernels{std::cref(*prepared)};
        auto batch = device.begin(kernels);
        REQUIRE_RESULT(batch, "begin warm counter batch");
        const std::array<const Field*, 1> inputs{&input};
        const std::array<Field*, 1> outputs{&output};
        REQUIRE_RESULT(batch->dispatch(*prepared, inputs, outputs, parameters), "warm-up dispatch");
        const auto warm = device.statistics();
        constexpr unsigned repetitions = 8;
        for (unsigned i = 0; i < repetitions; ++i)
            REQUIRE_RESULT(batch->dispatch(*prepared, inputs, outputs, parameters), "steady-state dispatch");
        const auto after = device.statistics();
        REQUIRE(after.compiles == warm.compiles, "steady-state dispatch compiled a kernel");
        REQUIRE(after.allocations == warm.allocations, "steady-state dispatch allocated a field");
        REQUIRE(after.upload_bytes == warm.upload_bytes && after.copy_bytes == warm.copy_bytes && after.readback_bytes == warm.readback_bytes,
                "steady-state dispatch transferred field data on the CPU");
        REQUIRE(after.uniform_updates >= warm.uniform_updates + repetitions, "uniform updates were not reported separately for warm dispatches");
        REQUIRE((prepared->backend() == Backend::Native ? after.native_dispatches - warm.native_dispatches : after.slibs_dispatches - warm.slibs_dispatches) == repetitions,
                "warm dispatch receipt count differs from requested dispatches");
        batch = Batch{};
        std::vector<std::uint32_t> actual(domain.x), expected(domain.x);
        for (unsigned i = 0; i < domain.x; ++i) expected[i] = values[i] + 1u;
        REQUIRE(readback(device, output, actual), "could not read warm output after iterations");
        REQUIRE(actual == expected, "warm repeated dispatches produced wrong results");
        return true;
    }

    bool expected_failure_tests(Device& device, const Capabilities& caps, Backend preference) {
        const Type type{Element::Float, 1};
        const Domain domain{4, 1, 1};
        auto bad_directive = copy_kernel(type, "reject_directive");
        bad_directive.source = "#define BAD 1\nvoid kernel(ivec3 id, inout float result) { result = hc_read_src(id); }";
        auto rejected_directive = device.prepare(bad_directive);
        REQUIRE(!rejected_directive, "unsupported preprocessor directive was accepted");
        REQUIRE(rejected_directive.error().find("reject_directive") != std::string::npos, "directive error did not name its kernel");
        auto cr_directive = copy_kernel(type, "reject_cr_directive");
        cr_directive.source = "/* comment before directive */\r#define HIDDEN 1\rvoid kernel(ivec3 id, inout float result) { result = hc_read_src(id); }";
        REQUIRE(!validate_kernel(cr_directive), "preprocessor directive after CR line ending/comment was accepted");
        auto comment_only = copy_kernel(type, "comment_directive_text");
        comment_only.source = "/* #define in a comment is inert */\r\nvoid kernel(ivec3 id, inout float result) { result = hc_read_src(id); }";
        REQUIRE(validate_kernel(comment_only), "lexer rejected a directive-like token inside a comment");
        auto line_comment_cr = copy_kernel(type, "reject_line_comment_cr_directive");
        line_comment_cr.source = "// comment\r#define HIDDEN 1\rvoid kernel(ivec3 id, inout float result) { result = hc_read_src(id); }";
        REQUIRE(!validate_kernel(line_comment_cr), "CR-terminated line comment bypassed directive rejection");
        auto bad_builtin = copy_kernel(type, "reject_builtin");
        bad_builtin.source = "void kernel(ivec3 id, inout float result) { result = gl_GlobalInvocationID.x; }";
        REQUIRE(!device.prepare(bad_builtin), "backend-specific invocation builtin was accepted in portable source");
        auto bad_compile = copy_kernel(type, "compile_failure");
        bad_compile.source = "void kernel(ivec3 id, inout float result) { result = undefined_symbol; }";
        auto compile_failure = device.prepare(bad_compile);
        REQUIRE(!compile_failure, "kernel with an undefined identifier compiled");
        REQUIRE(compile_failure.error().find("compile_failure") != std::string::npos, "compile error did not name its kernel");

        KernelDesc desc{"dispatch_failure", "void kernel(ivec3 id, inout float result) { result = hc_read_src(id) + amount; }",
                        {{"src", type}}, {{"result", type}}, {{"amount", type}}};
        auto prepared = device.prepare(desc);
        REQUIRE_RESULT(prepared, "prepare dispatch-failure fixture");
        REQUIRE(check_backend(*prepared, preference, caps), "dispatch-failure fixture route check failed");
        Field field, other;
        REQUIRE(allocate<float>(device, {type, domain}, field), "could not allocate dispatch failure field");
        REQUIRE(allocate<float>(device, {type, domain}, other), "could not allocate dispatch failure other field");
        Field wrong_domain;
        REQUIRE(allocate<float>(device, {type, {3, 1, 1}}, wrong_domain), "could not allocate mismatched MRT domain");
        REQUIRE(!device.allocate({{Element::Float, 1}, {0, 1, 1}}), "zero-size device field allocation was accepted");
        const std::array<std::reference_wrapper<const Kernel>, 1> kernels{std::cref(*prepared)};
        auto batch = device.begin(kernels);
        REQUIRE_RESULT(batch, "begin dispatch-failure batch");
        const std::array<const Field*, 1> alias_inputs{&field};
        const std::array<Field*, 1> alias_outputs{&field};
        const std::array<ParameterValue, 1> correct_parameters{ParameterValue{std::array<float, 4>{1, 0, 0, 0}, 1}};
        auto alias = batch->dispatch(*prepared, alias_inputs, alias_outputs, correct_parameters);
        REQUIRE(!alias && alias.error().find("alias") != std::string::npos, "read/write field alias was not rejected");
        const std::array<Field*, 1> outputs{&other};
        const std::array<ParameterValue, 1> wrong_parameters{ParameterValue{std::array<std::uint32_t, 4>{1, 0, 0, 0}, 1}};
        auto wrong_type = batch->dispatch(*prepared, alias_inputs, outputs, wrong_parameters);
        REQUIRE(!wrong_type && wrong_type.error().find("parameter type") != std::string::npos, "wrong typed parameter was not rejected");
        const std::array<ParameterValue, 0> missing_parameters{};
        auto missing = batch->dispatch(*prepared, alias_inputs, outputs, missing_parameters);
        REQUIRE(!missing && missing.error().find("arity") != std::string::npos, "wrong dispatch arity was not rejected");
        batch = Batch{};

        KernelDesc two_outputs{"duplicate_outputs",
                               "void kernel(ivec3 id, inout float left, inout float right) { left = 1.0; right = 2.0; }",
                               {}, {{"left", type}, {"right", type}}, {}};
        auto two = device.prepare(two_outputs);
        REQUIRE_RESULT(two, "prepare duplicate output fixture");
        const std::array<std::reference_wrapper<const Kernel>, 1> two_kernels{std::cref(*two)};
        auto two_batch = device.begin(two_kernels);
        REQUIRE_RESULT(two_batch, "begin duplicate output fixture");
        const std::array<const Field*, 0> no_inputs{};
        const std::array<Field*, 2> duplicate{&field, &field};
        const std::array<ParameterValue, 0> no_parameters{};
        auto duplicate_result = two_batch->dispatch(*two, no_inputs, duplicate, no_parameters);
        REQUIRE(!duplicate_result && duplicate_result.error().find("alias") != std::string::npos, "duplicate output field was accepted");

        const std::array<Field*, 2> mismatched{&field, &wrong_domain};

        auto mismatched_result = two_batch->dispatch(*two, no_inputs, mismatched, no_parameters);
        REQUIRE(!mismatched_result && mismatched_result.error().find("equal logical domain") != std::string::npos,
                "mismatched output domains were accepted");
        two_batch = Batch{};
        return true;
    }

    bool resolver_and_liveness_tests(ContextOwner& owner, GlResolver resolver, Backend preference,
                                     const Capabilities& capabilities) {
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (capabilities.native.available) {
            ContextOwner broken = owner;
            broken.deny_native = true;
            GlResolver broken_resolver{resolve_proc, is_current, &broken};
            Device missing_proc;
            auto initialized = missing_proc.initialize(broken_resolver, Backend::Native);
            REQUIRE(!initialized, "native initialization succeeded with missing required resolver functions");
            REQUIRE(initialized.error().find("glDispatchCompute") != std::string::npos,
                    "native resolver failure did not identify required procedures");
            REQUIRE(broken.native_lookups >= 1, "native initialization did not consult the supplied resolver");

            ContextOwner no_op = owner;
            no_op.no_op_dispatch = true;
            GlResolver no_op_resolver{resolve_proc, is_current, &no_op};
            Device false_native;
            auto false_native_result = false_native.initialize(no_op_resolver, Backend::Native);
            REQUIRE(!false_native_result &&
                    false_native_result.error().find("dispatch did not write the expected sentinel texel") != std::string::npos,
                    "native initialization accepted a no-op glDispatchCompute resolver");
            if (capabilities.slibs.available) {
                Device fallback;
                REQUIRE_RESULT(fallback.initialize(no_op_resolver, Backend::Auto), "initialize no-op native fallback");
                REQUIRE(!fallback.capabilities().native.available &&
                        fallback.capabilities().native.reason.find("dispatch did not write the expected sentinel texel") != std::string::npos,
                        "auto capabilities omitted the failed no-op native probe reason");
                REQUIRE(fallback.capabilities().slibs.available, "auto did not retain usable SLIBS after no-op native probe failure");
                REQUIRE_RESULT(fallback.release(), "release no-op native fallback device");
            }
        } else if (major > 3 || (major == 3 && minor >= 1)) {
            std::printf("native resolver fault injections unavailable: normal native probe did not pass (%s)\\n",
                        capabilities.native.reason.c_str());
        }

        Device stale_device;
        REQUIRE_RESULT(stale_device.initialize(resolver, preference), "initialize ownership fixture");
        Device peer;
        REQUIRE_RESULT(peer.initialize(resolver, preference), "initialize independent device fixture");
        const Type type{Element::Uint, 1};
        const Domain domain{4, 1, 1};
        KernelDesc cross_desc{"cross_device_field", "void kernel(ivec3 id, inout uint result) { result = hc_read_src(id); }",
                              {{"src", type}}, {{"result", type}}, {}};
        auto local_kernel = stale_device.prepare(cross_desc);
        auto peer_kernel = peer.prepare(cross_desc);
        REQUIRE_RESULT(local_kernel, "prepare local ownership kernel");
        REQUIRE_RESULT(peer_kernel, "prepare peer ownership kernel");
        Field local_input, local_output, peer_input;
        REQUIRE(allocate<std::uint32_t>(stale_device, {type, domain}, local_input), "allocate local ownership input");
        REQUIRE(allocate<std::uint32_t>(stale_device, {type, domain}, local_output), "allocate local ownership output");
        REQUIRE(allocate<std::uint32_t>(peer, {type, domain}, peer_input), "allocate peer ownership input");
        {
            const std::array<std::reference_wrapper<const Kernel>, 1> local_set{std::cref(*local_kernel)};
            auto batch = stale_device.begin(local_set);
            REQUIRE_RESULT(batch, "begin local ownership batch");
            const std::array<const Field*, 1> foreign_inputs{&peer_input};
            const std::array<Field*, 1> outputs{&local_output};
            const std::array<ParameterValue, 0> parameters{};
            auto foreign_field = batch->dispatch(*local_kernel, foreign_inputs, outputs, parameters);
            REQUIRE(!foreign_field && foreign_field.error().find("ownership") != std::string::npos,
                    "field from another live device was accepted");
        }
        const std::array<std::reference_wrapper<const Kernel>, 1> foreign_kernel_set{std::cref(*peer_kernel)};
        REQUIRE(!stale_device.begin(foreign_kernel_set), "kernel from another live device was accepted");

        KernelDesc desc{"stale_generation", "void kernel(ivec3 id, inout uint result) { result = hc_read_previous(id); }",
                        {{"previous", type}}, {{"result", type}}, {}};
        auto old_kernel = stale_device.prepare(desc);
        REQUIRE_RESULT(old_kernel, "prepare old-generation kernel");
        Field old_field;
        REQUIRE(allocate<std::uint32_t>(stale_device, {type, domain}, old_field), "allocate old-generation field");
        REQUIRE_RESULT(stale_device.release(), "release old-generation device");
        REQUIRE_RESULT(stale_device.initialize(resolver, preference), "initialize replacement generation");
        Field fresh_output;
        REQUIRE(allocate<std::uint32_t>(stale_device, {type, domain}, fresh_output), "allocate replacement output");
        const std::array<std::reference_wrapper<const Kernel>, 1> old_kernels{std::cref(*old_kernel)};
        REQUIRE(!stale_device.begin(old_kernels), "old kernel was accepted in a replacement device generation");
        auto fresh_kernel = stale_device.prepare(desc);
        REQUIRE_RESULT(fresh_kernel, "prepare replacement kernel");
        const std::array<std::reference_wrapper<const Kernel>, 1> fresh_kernels{std::cref(*fresh_kernel)};
        auto batch = stale_device.begin(fresh_kernels);
        REQUIRE_RESULT(batch, "begin replacement generation batch");
        const std::array<const Field*, 1> stale_inputs{&old_field};
        const std::array<Field*, 1> outputs{&fresh_output};
        const std::array<ParameterValue, 0> parameters{};
        auto stale_dispatch = batch->dispatch(*fresh_kernel, stale_inputs, outputs, parameters);
        REQUIRE(!stale_dispatch && stale_dispatch.error().find("ownership") != std::string::npos,
                "field from an expired device generation was accepted");
        REQUIRE_RESULT(stale_device.release(), "release replacement generation");

        Device live_check;
        REQUIRE_RESULT(live_check.initialize(resolver, preference), "initialize context liveness fixture");
        owner.live = false;
        auto lost = live_check.allocate({{Element::Uint, 1}, {1, 1, 1}});
        REQUIRE(!lost && lost.error().find("owner context") != std::string::npos,
                "operation succeeded after owner context predicate was withdrawn");
        owner.live = true;
        REQUIRE_RESULT(live_check.release(), "release restored context liveness fixture");
        return true;
    }

    bool ownership_thread_test(Device& device) {
        KernelDesc desc{"wrong_thread", "void kernel(ivec3 id, inout uint result) { result = uint(id.x); }", {}, {{"result", {Element::Uint, 1}}}, {}};
        bool rejected = false;
        std::thread worker([&] {
            const auto result = device.prepare(desc);
            rejected = !result && result.error().find("owner context and thread") != std::string::npos;
        });
        worker.join();
        REQUIRE(rejected, "device operation from a non-owner thread was not rejected");
        return true;
    }

    bool pool_and_failure_suite(Device& device, const Capabilities& caps, Backend preference) {
        REQUIRE(layout_validation_tests(), "field layout/type validation tests failed");
        REQUIRE(expected_failure_tests(device, caps, preference), "expected failure tests failed");
        REQUIRE(pooling_test(device), "pool scrub/reuse test failed");
        REQUIRE(warm_counter_test(device, caps, preference), "warm operation counters test failed");
        return true;
    }

    bool texture_storage_fault_test(GlResolver resolver) {
        REQUIRE(glGetError() == GL_NO_ERROR, "GL error before texture-storage fault fixture");
        Device device;
        REQUIRE_RESULT(device.initialize(resolver, Backend::Auto), "initialize texture-storage fault device");
        if (!device.capabilities().formats[2]) {
            std::printf("UNVERIFIED texture-storage failure recovery: R32UI allocation unsupported\n");
            return true;
        }
        const FieldDesc seed_desc{{Element::Uint, 1}, {4, 1, 1}};
        const FieldDesc recovery_desc{{Element::Uint, 1}, {11, 3, 1}};
        Field seed, recovered;
        REQUIRE(allocate<std::uint32_t>(device, seed_desc, seed), "allocate seeded texture-fault field");
        const std::vector<std::uint32_t> seed_values{0x12345678u, 0x80000000u, 17u, 0xffffffffu};
        REQUIRE(upload(device, seed, seed_values), "upload seeded texture-fault field");
        const auto before = device.statistics();
        g_texture_storage_fault_texture = 0;

        g_texture_storage_fault_triggered = false;
        g_fail_next_texture_storage = true;
        auto failed = device.allocate(recovery_desc);
        const bool fault_triggered = g_texture_storage_fault_triggered;
        g_fail_next_texture_storage = false;
        REQUIRE(fault_triggered, "texture-storage fault did not reach the real storage call");
        REQUIRE(!failed, "field allocation succeeded after the injected driver allocation error");
        REQUIRE(glGetError() == GL_INVALID_VALUE, "invalid storage extent did not produce GL_INVALID_VALUE");
        REQUIRE(glGetError() == GL_NO_ERROR, "texture-storage allocation error was not consumed cleanly");
        REQUIRE(g_texture_storage_fault_texture != 0, "texture-storage fault did not record a bound texture");
        REQUIRE(glIsTexture(g_texture_storage_fault_texture) == GL_FALSE,
                "failed texture storage left its newly created GL texture object alive");
        const auto after_failure = device.statistics();
        REQUIRE(after_failure.live_bytes == before.live_bytes,
                "failed texture allocation leaked live field-byte accounting");

        std::vector<std::uint32_t> actual_seed(seed_values.size());
        REQUIRE(readback(device, seed, actual_seed), "seeded field became unreadable after storage failure");
        REQUIRE(actual_seed == seed_values, "seeded field contents changed after storage failure");

        auto allocation = device.allocate(recovery_desc);
        REQUIRE_RESULT(allocation, "retry field allocation after injected storage failure");
        recovered = std::move(*allocation);
        std::vector<std::uint32_t> cleared(33, 0xdeadbeefu);
        REQUIRE(readback(device, recovered, cleared), "recovered field readback failed");
        REQUIRE(std::ranges::all_of(cleared, [](std::uint32_t value) { return value == 0u; }),
                "recovered allocation exposed partial or stale texture contents");
        REQUIRE(readback(device, seed, actual_seed), "seeded field became unreadable after allocation recovery");
        REQUIRE(actual_seed == seed_values, "seeded field contents changed during allocation recovery");
        REQUIRE(glGetError() == GL_NO_ERROR, "texture-storage recovery left a GL error");
        return true;
    }

    bool mixed_backend_order_test(GlResolver resolver, const Capabilities& available) {
        if (!available.native.available || !available.slibs.available || !available.formats[2]) {
            std::printf("UNVERIFIED mixed-backend ordering: native=%s slibs=%s R32UI=%s\n",
                        available.native.available ? "available" : available.native.reason.c_str(),
                        available.slibs.available ? "available" : available.slibs.reason.c_str(),
                        available.formats[2] ? "available" : "unavailable");
            return true;
        }
        Device device;
        REQUIRE_RESULT(device.initialize(resolver, Backend::Auto), "initialize mixed-backend device");
        const auto& caps = device.capabilities();
        if (!caps.native.available || !caps.slibs.available || !caps.formats[2]) {
            std::printf("UNVERIFIED mixed-backend ordering after Auto initialization: native=%s slibs=%s R32UI=%s\n",
                        caps.native.available ? "available" : caps.native.reason.c_str(),
                        caps.slibs.available ? "available" : caps.slibs.reason.c_str(),
                        caps.formats[2] ? "available" : "unavailable");
            return true;
        }

        const Type type{Element::Uint, 1};
        const Domain domain{7, 5, 1};
        const FieldDesc desc{type, domain};
        KernelDesc first_desc{"mixed_order_native_first",
                              "void kernel(ivec3 id, inout uint result) { result = uint(hc_index(id)) + 3u; }",
                              {}, {{"result", type}}, {}};
        KernelDesc middle_desc{"mixed_order_slips_fallback",
                              "void kernel(ivec3 id, inout uint result) { result = hc_read_src(id) + 2u; }",
                              {{"src", type}}, {{"result", type}}, {}};
        KernelDesc last_desc{"mixed_order_native_last",
                             "void kernel(ivec3 id, inout uint result) { result = hc_read_src(id) * 4u; }",
                             {{"src", type}}, {{"result", type}}, {}};
        auto first = device.prepare(first_desc);
        REQUIRE_RESULT(first, "prepare first Auto/native ordering kernel");
        REQUIRE(first->backend() == Backend::Native, "first Auto kernel did not select Native");

        g_fail_next_native_shader_creation = true;
        auto middle = device.prepare(middle_desc);
        const bool native_creation_was_intercepted = !g_fail_next_native_shader_creation;
        g_fail_next_native_shader_creation = false;
        REQUIRE(native_creation_was_intercepted, "test did not intercept the middle kernel's native shader creation");
        REQUIRE_RESULT(middle, "prepare middle Auto/SLIBS fallback kernel");
        REQUIRE(middle->backend() == Backend::Slibs && !middle->fallback_reason().empty(),
                "middle Auto kernel did not fall back from Native to SLIBS");

        auto last = device.prepare(last_desc);
        REQUIRE_RESULT(last, "prepare final Auto/native ordering kernel");
        REQUIRE(last->backend() == Backend::Native, "final Auto kernel did not return to Native");

        Field first_field, middle_field, final_field;
        REQUIRE(allocate<std::uint32_t>(device, desc, first_field), "allocate first mixed-route field");
        REQUIRE(allocate<std::uint32_t>(device, desc, middle_field), "allocate middle mixed-route field");
        REQUIRE(allocate<std::uint32_t>(device, desc, final_field), "allocate final mixed-route field");
        const std::array<std::reference_wrapper<const Kernel>, 3> kernels{
            std::cref(*first), std::cref(*middle), std::cref(*last)};
        auto batch = device.begin(kernels);
        REQUIRE_RESULT(batch, "begin mixed-backend ordering batch");
        const std::array<const Field*, 0> no_inputs{};
        const std::array<const Field*, 1> first_input{&first_field};
        const std::array<const Field*, 1> middle_input{&middle_field};
        const std::array<Field*, 1> first_output{&first_field};
        const std::array<Field*, 1> middle_output{&middle_field};
        const std::array<Field*, 1> final_output{&final_field};
        const std::array<ParameterValue, 0> no_parameters{};
        const auto before = device.statistics();
        REQUIRE_RESULT(batch->dispatch(*first, no_inputs, first_output, no_parameters), "dispatch Native first kernel");
        REQUIRE_RESULT(batch->dispatch(*middle, first_input, middle_output, no_parameters), "dispatch SLIBS middle kernel");
        REQUIRE_RESULT(batch->dispatch(*last, middle_input, final_output, no_parameters), "dispatch Native final kernel");
        batch = Batch{};
        const auto after = device.statistics();
        REQUIRE(after.native_dispatches == before.native_dispatches + 2 &&
                after.slibs_dispatches == before.slibs_dispatches + 1,
                "mixed-route batch did not submit Native→SLIBS→Native exactly once each");
        REQUIRE(after.fetch_barriers > before.fetch_barriers,
                "Native-to-SLIBS field sampling did not issue a texture-fetch barrier");
        REQUIRE(after.upload_bytes == before.upload_bytes && after.copy_bytes == before.copy_bytes &&
                after.readback_bytes == before.readback_bytes,
                "mixed-route dispatch chain transferred field data between kernels");
        std::vector<std::uint32_t> actual(domain.x * domain.y), expected(domain.x * domain.y);
        for (std::size_t i = 0; i < expected.size(); ++i)
            expected[i] = 4u * (static_cast<std::uint32_t>(i) + 5u);
        REQUIRE(readback(device, final_field, actual), "read back mixed-route final field");
        REQUIRE(actual == expected, "Native→SLIBS→Native shared-field result differs");
        return true;
    }

    bool run_backend_suite(Device& device, const Capabilities& caps, Backend preference,
                           bool run_ownership, GlResolver resolver) {
        print_capabilities(caps, preference);
        if (preference == Backend::Auto) {
            REQUIRE(mixed_backend_order_test(resolver, caps), "mixed Native→SLIBS→Native ordering test failed");
            REQUIRE(texture_storage_fault_test(resolver), "texture-storage fault recovery test failed");
        }
        REQUIRE(arithmetic_cases(device, caps, preference), "arithmetic test suite failed");
        REQUIRE(indexed_read_case(device, caps, preference), "indexed read test failed");
        REQUIRE(unsigned_wrap_case(device, caps, preference), "unsigned wrap test failed");
        REQUIRE(mixed_integer_mrt_case(device, caps, preference), "mixed integer MRT test failed");
        REQUIRE(shape_cases(device, caps, preference), "shape test suite failed");
        Field stencil_output;
        float stencil_pixel = 0.0f;
        REQUIRE(stencil_case(device, caps, preference, stencil_output, stencil_pixel), "stencil CPU-oracle test failed");
        REQUIRE(render_sampled_float(device, stencil_output, "src", {9, 6, 0}, stencil_pixel), "stencil sampled presentation failed");
        REQUIRE(particle_case(device, caps, preference), "32-step particle CPU-oracle test failed");
        if (run_ownership) {
            REQUIRE(ownership_thread_test(device), "owner-thread test failed");
            REQUIRE(off_owner_retirement_test(device), "off-owner retirement test failed");
        }
        REQUIRE(dependency_and_copy_tests(device, caps, preference), "dependency/copy test failed");
        REQUIRE(raster_and_native_state_test(device, caps, preference), "state restoration test failed");
        KernelDesc safe_state_kernel{"caller_state_gate", "void kernel(ivec3 id, inout uint result) { result = uint(id.x); }",
                                     {}, {{"result", {Element::Uint, 1}}}, {}};
        auto safe_state = device.prepare(safe_state_kernel);
        REQUIRE_RESULT(safe_state, "prepare caller state gate kernel");
        REQUIRE(check_backend(*safe_state, preference, caps), "caller state gate route check failed");
        REQUIRE(caller_unsafe_state_rejection_test(device, *safe_state), "unsafe caller state rejection test failed");
        REQUIRE(pool_and_failure_suite(device, caps, preference), "pool/failure/warm suite failed");
        return true;
    }

    bool run_one_backend(GlResolver resolver, Backend preference, bool run_ownership,
                         Capabilities* probed_caps = nullptr) {
        Device device;
        auto initialized = device.initialize(resolver, preference);
        if (!success(initialized, preference == Backend::Auto ? "initialize auto device" : preference == Backend::Native ? "initialize strict native device" : "initialize strict SLIBS device")) return false;
        const Capabilities caps = device.capabilities();
        if (probed_caps) *probed_caps = caps;
        REQUIRE(preference == Backend::Auto || (preference == Backend::Native ? caps.native.available : caps.slibs.available),
                "forced backend initialized without reporting its candidate available");
        bool passed = run_backend_suite(device, caps, preference, run_ownership, resolver);
        if (passed && run_ownership)
            passed = resolver_and_liveness_tests(*static_cast<ContextOwner*>(resolver.owner), resolver, preference, caps);
        const auto retired = device.statistics();
        std::printf("compute counters: compiles=%llu allocations=%llu pool_hits=%llu native=%llu slibs=%llu samples=%llu "
                    "barriers=%llu(fetch=%llu framebuffer=%llu update=%llu image=%llu) transfers(upload=%llu copy=%llu readback=%llu) "
                    "uniform_updates=%llu live=%zu idle=%zu\n",
                    static_cast<unsigned long long>(retired.compiles), static_cast<unsigned long long>(retired.allocations),
                    static_cast<unsigned long long>(retired.pool_hits), static_cast<unsigned long long>(retired.native_dispatches),
                    static_cast<unsigned long long>(retired.slibs_dispatches), static_cast<unsigned long long>(retired.samples),
                    static_cast<unsigned long long>(retired.barriers), static_cast<unsigned long long>(retired.fetch_barriers),
                    static_cast<unsigned long long>(retired.framebuffer_barriers), static_cast<unsigned long long>(retired.update_barriers),
                    static_cast<unsigned long long>(retired.image_barriers), static_cast<unsigned long long>(retired.upload_bytes),
                    static_cast<unsigned long long>(retired.copy_bytes), static_cast<unsigned long long>(retired.readback_bytes),
                    static_cast<unsigned long long>(retired.uniform_updates), retired.live_bytes, retired.idle_bytes);
        if (!passed) return false;
        return success(device.release(), "release compute device");
    }

    bool forced_native_rejection(GlResolver resolver, ContextOwner& owner) {
        const auto before = owner.native_lookups;
        Device native;
        auto initialized = native.initialize(resolver, Backend::Native);
        REQUIRE(!initialized, "strict native backend initialized on an ES 3.0 context");
        REQUIRE(initialized.error().find("OpenGL ES 3.1") != std::string::npos,
                "ES 3.0 forced-native error did not explain its version requirement");
        REQUIRE(owner.native_lookups == before, "ES 3.0 initialization attempted to resolve native-only procedures");
        return true;
    }

    bool parse_options(int argc, char** argv, Options& options) {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--require-es30") options.require_es30 = true;
            else if (arg == "--require-es31") options.require_es31 = true;
            else if (arg == "--without-float-render") options.without_float_render = true;
            else if (arg == "--backend" && i + 1 < argc) {
                const auto backend = parse_backend(argv[++i]);
                if (!backend) {
                    std::fprintf(stderr, "%s\n", backend.error().c_str());
                    return false;
                }
                options.backend = *backend;
            } else if (arg.starts_with("--backend=")) {
                const auto backend = parse_backend(arg.substr(std::string_view("--backend=").size()));
                if (!backend) {
                    std::fprintf(stderr, "%s\n", backend.error().c_str());
                    return false;
                }
                options.backend = *backend;
            } else {
                std::fprintf(stderr, "unknown compute-test argument: %.*s\n", static_cast<int>(arg.size()), arg.data());
                return false;
            }
        }
        if (options.require_es30 && options.require_es31) {
            std::fprintf(stderr, "--require-es30 and --require-es31 are mutually exclusive\n");
            return false;
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
    const auto* version_ptr = glGetString(GL_VERSION);
    const auto version = version_ptr ? reinterpret_cast<const char*>(version_ptr) : "unknown";
    std::printf("compute test GL_VERSION=%s\n", version);
    if (options.require_es30 && (major != 3 || minor != 0)) {
        std::fprintf(stderr, "SKIP: strict GLES 3.0 lane requested, actual context is %d.%d (%s)\n", major, minor, version);
        return 77;
    }
    if (options.require_es31 && (major != 3 || minor != 1)) {
        std::fprintf(stderr, "SKIP: strict GLES 3.1 lane requested, actual context is %d.%d (%s)\n", major, minor, version);
        return 77;
    }
    const bool float_render = has_extension("GL_EXT_color_buffer_float");
    std::printf("float-render-extension GL_EXT_color_buffer_float=%s\n", float_render ? "present" : "absent");
    if (options.without_float_render && float_render) {
        std::fprintf(stderr, "compute test requires GL_EXT_color_buffer_float to be absent; constrain Mesa extensions for this lane\n");
        return 1;
    }

    ContextOwner owner{eglGetCurrentContext()};
    GlResolver resolver{resolve_proc, is_current, &owner};
    bool ok = true;
    if (options.backend) {
        if (*options.backend == Backend::Native && major == 3 && minor == 0) {
            ok = forced_native_rejection(resolver, owner);
        } else {
            if (options.require_es31 && *options.backend != Backend::Native) {
                std::fprintf(stderr, "--require-es31 must run the strict native route; omit --backend or use --backend native\n");
                return 2;
            }
            ok = run_one_backend(resolver, *options.backend, true);
        }
    } else {
        Capabilities auto_caps;
        ok = run_one_backend(resolver, Backend::Auto, true, &auto_caps);
        if (ok && auto_caps.native.available) ok = run_one_backend(resolver, Backend::Native, false);
        if (ok && auto_caps.slibs.available) ok = run_one_backend(resolver, Backend::Slibs, false);
        if (ok && options.require_es30) ok = forced_native_rejection(resolver, owner);
        if (ok && options.require_es31 && !auto_caps.native.available) {
            std::fprintf(stderr, "SKIP: exact GLES 3.1 context has no usable native compute candidate: %s\n", auto_caps.native.reason.c_str());
            return 77;
        }
    }
    if (options.require_es31 && options.backend && *options.backend == Backend::Native && !ok) return g_failures ? 1 : 77;
    return ok && g_failures == 0 ? 0 : 1;
}
