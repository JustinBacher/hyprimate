#include "shader/GlesRuntime.hpp"

#include <filesystem>

#include <algorithm>
#include <EGL/egl.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Check.hpp"
#include "GlHarness.hpp"

// These executable-local hooks forward real commands, including the bad enum
// used to trigger a deterministic render-phase error on the driver.
namespace driver_probe {
    bool fail_draw = false, fail_format_probe = false, observe_feedback = false;
    unsigned texture_calls = 0, large_textures = 0, buffer_calls = 0, binding_violations = 0;

    template <typename Proc>
    Proc resolve(const char *name) {
        const auto proc = reinterpret_cast<Proc>(eglGetProcAddress(name));
        if (proc == nullptr) {
            std::abort();
        }
        return proc;
    }

    GLint integer(GLenum property) {
        GLint value = 0;
        glGetIntegerv(property, &value);
        return value;
    }

    GLint binding(GLenum property, GLuint slot) {
        GLint value = 0;
        glGetIntegeri_v(property, slot, &value);
        return value;
    }

    bool compute_context_current(void *owner) noexcept {
        return owner && eglGetCurrentContext() == *static_cast<EGLContext *>(owner);
    }

    hyprimate::compute::GlResolver::Proc resolve_compute(const char *name, void *) noexcept {
        return eglGetProcAddress(name);
    }
}  // namespace driver_probe

extern "C" void GL_APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    static const auto draw = driver_probe::resolve<decltype(&glDrawArrays)>("glDrawArrays");
    draw(std::exchange(driver_probe::fail_draw, false) ? GLenum{GL_TEXTURE_2D} : mode, first, count);
}

extern "C" void GL_APIENTRY glTexStorage2D(GLenum target, GLsizei levels, GLenum format,
    GLsizei width, GLsizei height) {
    static const auto storage = driver_probe::resolve<decltype(&glTexStorage2D)>("glTexStorage2D");
    ++driver_probe::texture_calls;
    driver_probe::large_textures += width > 1 || height > 1;
    storage(target, levels, format, width, height);
}

extern "C" void GL_APIENTRY glBufferData(GLenum target, GLsizeiptr bytes, const void *data,
    GLenum usage) {
    static const auto storage = driver_probe::resolve<decltype(&glBufferData)>("glBufferData");
    ++driver_probe::buffer_calls;
    storage(target, bytes, data, usage);
}

extern "C" GLenum GL_APIENTRY glCheckFramebufferStatus(GLenum target) {
    static const auto check =
        driver_probe::resolve<decltype(&glCheckFramebufferStatus)>("glCheckFramebufferStatus");
    if (std::exchange(driver_probe::fail_format_probe, false)) {
        // Exercise the actual GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT path.
        glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    }
    return check(target);
}

extern "C" void GL_APIENTRY glBeginTransformFeedback(GLenum mode) {
    using namespace driver_probe;
    static const auto begin = resolve<decltype(&glBeginTransformFeedback)>("glBeginTransformFeedback");
    if (observe_feedback) {
        const auto dest = binding(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 0);
        binding_violations += dest == 0 || integer(GL_ARRAY_BUFFER_BINDING) != 0 ||
                              integer(GL_UNIFORM_BUFFER_BINDING) == dest;
        const auto slots = integer(GL_MAX_UNIFORM_BUFFER_BINDINGS);
        for (GLint i = 0; i < slots; ++i) {
            binding_violations += binding(GL_UNIFORM_BUFFER_BINDING, i) == dest;
        }
    }
    begin(mode);
}

extern "C" void GL_APIENTRY glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
    GLintptr offset, GLsizeiptr size) {
    using namespace driver_probe;
    static const auto bind = resolve<decltype(&glBindBufferRange)>("glBindBufferRange");
    if (observe_feedback && target == GL_UNIFORM_BUFFER && buffer != 0) {
        binding_violations += binding(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 0) ==
                              static_cast<GLint>(buffer);
    }
    bind(target, index, buffer, offset, size);
}

extern "C" void GL_APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type,
    GLboolean normalized, GLsizei stride, const void *pointer) {
    using namespace driver_probe;
    static const auto attrib = resolve<decltype(&glVertexAttribPointer)>("glVertexAttribPointer");
    if (observe_feedback) {
        const auto input = integer(GL_ARRAY_BUFFER_BINDING);
        binding_violations += input != 0 && input == binding(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 0);
    }
    attrib(index, size, type, normalized, stride, pointer);
}

namespace {
    using namespace hyprimate::shader;
    using Pixel = std::array<std::uint8_t, 4>;
    constexpr Ref OUTPUT{ResourceKind::Output, 0, Side::Single};
    constexpr Ref STATE_READ{ResourceKind::Texture, 0, Side::Read};
    constexpr Ref STATE_WRITE{ResourceKind::Texture, 0, Side::Write};
    constexpr Ref PARTICLE_READ{ResourceKind::Buffer, 0, Side::Read};
    constexpr Ref PARTICLE_WRITE{ResourceKind::Buffer, 0, Side::Write};

    GLint integer(GLenum property) {
        GLint value = 0;
        glGetIntegerv(property, &value);
        return value;
    }

    GLuint texture(unsigned width, unsigned height, const Pixel *pixels = nullptr,
        GLenum format = GL_RGBA8) {
        GLuint name = 0;
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (pixels) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        }
        return name;
    }

    struct Layers {
        Source source;
        Target target;

        Layers(unsigned width, unsigned height, unsigned source_width = 1,
            unsigned source_height = 1, const Pixel *pixels = nullptr) {
            const Pixel solid{255, 255, 255, 255};
            source = {texture(source_width, source_height, pixels ? pixels : &solid), source_width,
                source_height};
            target = {0, texture(width, height), width, height};
            glGenFramebuffers(1, &target.framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                target.texture, 0);
            CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        }

        ~Layers() {
            glDeleteFramebuffers(1, &target.framebuffer);
            glDeleteTextures(1, &target.texture);
            glDeleteTextures(1, &source.texture);
        }
    };

    Pixel pixel(const Target &target, int x, int y, GLenum slot = GL_COLOR_ATTACHMENT0) {
        const auto before = integer(GL_READ_FRAMEBUFFER_BINDING);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.framebuffer);
        const auto read_slot = integer(GL_READ_BUFFER);
        glReadBuffer(slot);
        Pixel value{};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, value.data());
        glReadBuffer(read_slot);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, before);
        return value;
    }

    bool near(Pixel actual, Pixel wanted) {
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const int difference = static_cast<int>(actual[i]) - static_cast<int>(wanted[i]);
            if (difference < -1 || difference > 1) {
                (void)std::fprintf(stderr, "pixel (%u,%u,%u,%u), wanted (%u,%u,%u,%u)\n",
                    unsigned{actual[0]}, unsigned{actual[1]}, unsigned{actual[2]}, unsigned{actual[3]},
                    unsigned{wanted[0]}, unsigned{wanted[1]}, unsigned{wanted[2]}, unsigned{wanted[3]});
                return false;
            }
        }
        return true;
    }

    bool checked(bool result, const std::string &error) {
        if (!result) {
            (void)std::fprintf(stderr, "%s\n", error.c_str());
        }
        CHECK(result);
        return result;
    }

    Pass raster(std::string name, std::string fragment, std::vector<Ref> outputs = {OUTPUT}) {
        Pass pass;
        pass.name = std::move(name);
        pass.vertex = default_vertex();
        pass.fragment = std::move(fragment);
        pass.outputs = std::move(outputs);
        return pass;
    }

    Package simple(std::string fragment = R"(
layout(location=0) out vec4 color;
void main() { color = hypr_sample_source(hypr_window_uv()); }
)") {
        Package package;
        package.passes.push_back(raster("render", std::move(fragment)));
        return package;
    }

    void environment_borrows_only_nonaliased_images_and_clears_old_batches() {
        auto package = simple(R"(
layout(location=0) out vec4 color;
void main() {
    // Deliberately read an unused element: a reused linked program may not
    // contain geometry from another instance or output.
    color = vec4(hypr_sample_wallpaper_original(vec2(.5)).r,
        hypr_interactions[4].x, float(hypr_interaction_count) * .25, 1.0);
}
)");
        package.events = {Event::Wallpaper};
        package.environment.original = true;
        package.environment.interactions = true;
        package.environment.settle_ms = 500;
        Program program;
        Instance instance, other;
        std::string error;
        if (!checked(program.initialize(std::move(package), {}, error, eglGetProcAddress), error) ||
            !checked(instance.initialize(program, 1, 1, error), error) ||
            !checked(other.initialize(program, 1, 1, error), error)) return;
        Layers images{1, 1};
        std::array<Interaction, 2> records{};
        records[1][0][0] = .5F;
        images.source.environment.original = images.source.texture;
        images.source.environment.interactions = records;
        CHECK(checked(instance.render(images.source, images.target, true, error), error));
        CHECK(near(pixel(images.target, 0, 0), {255, 128, 128, 255}));
        images.source.environment.interactions = {};
        images.source.environment.original = 0;
        CHECK(checked(other.render(images.source, images.target, true, error), error));
        CHECK(near(pixel(images.target, 0, 0), {0, 0, 0, 255}));
        images.source.environment.original = images.target.texture;
        CHECK(!other.render(images.source, images.target, true, error));
        CHECK(near(pixel(images.target, 0, 0), {0, 0, 0, 255}));
    }

    void source_subrect_orientation_and_aliases() {
        const Pixel border{255, 0, 255, 255};
        const Pixel a{40, 10, 0, 128}, b{0, 60, 10, 96}, c{0, 0, 120, 160}, d{20, 20, 20, 64};
        const std::array<Pixel, 16> pixels{border, border, border, border,
            border, a, b, border, border, c, d, border, border, border, border, border};
        Layers layers(4, 2, 4, 4, pixels.data());
        layers.source.uv = {0.25F, 0.25F, 0.5F, 0.5F};
        layers.target.domain = {-0.5F, 0, 2, 1};
        std::string error;
        Program program;
        Instance instance;
        if (!checked(program.initialize(simple(), {}, error, eglGetProcAddress), error) ||
            !checked(instance.initialize(program, 2, 2, error), error)) {
            return;
        }
        CHECK(!instance.render(layers.source, layers.target, false, error));
        CHECK(instance.statistics().draws == 0);

        // Borrow a target whose intended output route differs from the effect.
        // The second attachment must not be cleared or written, even across errors.
        const std::array<Pixel, 8> sentinel{b, b, b, b, b, b, b, b};
        const auto spare = texture(4, 2, sentinel.data());
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, spare, 0);
        const std::array<GLenum, 2> route{GL_NONE, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, route.data());
        if (checked(instance.render(layers.source, layers.target, true, error), error)) {
            CHECK(integer(GL_DRAW_BUFFER0) == GL_NONE);
            CHECK(integer(GL_DRAW_BUFFER1) == GL_COLOR_ATTACHMENT1);
            CHECK(near(pixel(layers.target, 0, 0), {}));
            CHECK(near(pixel(layers.target, 1, 0), a));
            CHECK(near(pixel(layers.target, 2, 0), b));
            CHECK(near(pixel(layers.target, 1, 1), c));
            CHECK(near(pixel(layers.target, 2, 1), d));
            CHECK(near(pixel(layers.target, 3, 1), {}));
            CHECK(near(pixel(layers.target, 1, 1, GL_COLOR_ATTACHMENT1), b));
        }
        CHECK(program.statistics().allocations == 0);
        CHECK(instance.statistics().allocations == 0);
        CHECK(instance.statistics().copy_bytes == 0);

        auto alias = layers.target;
        alias.texture = 0;  // No reliance on the supplied output texture name.
        const Source spare_source{spare, 4, 2};
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
            spare, 0);
        CHECK(!instance.render(spare_source, alias, true, error));
        CHECK(error.find("alias") != std::string::npos);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
            layers.target.texture, 0);
        const GLenum color_zero = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &color_zero);  // Inactive attachment can still feed back.
        CHECK(!instance.render(spare_source, layers.target, true, error));
        CHECK(error.find("alias") != std::string::npos);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
        CHECK(checked(instance.render(layers.source, layers.target, false, error), error));
        glDeleteTextures(1, &spare);
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void borrowed_target_origin_clips_draw_and_clear() {
        Layers layers(5, 5);
        constexpr std::array<GLfloat, 4> exterior{80.F / 255, 0, 0, 1};
        glClearBufferfv(GL_COLOR, 0, exterior.data());
        auto target = layers.target;
        target.width = 2;
        target.height = 2;
        target.origin_x = 2;
        target.origin_y = 1;
        auto region = simple(R"(
layout(location=0) out vec4 c;
void main() {
    if (hypr_uv.x > 0.5) discard;
    c = vec4(0, hypr_target_size.x/8.0, 0, 0.5);
}
)");
        region.passes[0].blend = true;
        Program program;
        Instance instance;
        std::string error;
        if (!checked(program.initialize(region, {}, error, eglGetProcAddress), error) ||
            !checked(instance.initialize(program, 2, 2, error), error) ||
            !checked(instance.render(layers.source, target, true, error), error)) {
            return;
        }
        CHECK(near(pixel(layers.target, 2, 1), {0, 64, 0, 128}));
        CHECK(near(pixel(layers.target, 3, 2), {}));  // Discarded, but inside clear.
        CHECK(near(pixel(layers.target, 1, 1), {80, 0, 0, 255}));
        CHECK(near(pixel(layers.target, 2, 3), {80, 0, 0, 255}));
        CHECK(near(pixel(layers.target, 0, 0), {80, 0, 0, 255}));

        // Preserve the host's underlying pixels and blend an existing premultiplied color.
        glClearBufferfv(GL_COLOR, 0, exterior.data());
        region.passes[0].clear = false;
        CHECK(checked(program.initialize(std::move(region), {}, error, eglGetProcAddress), error));
        CHECK(checked(instance.initialize(program, 2, 2, error), error));
        CHECK(checked(instance.render(layers.source, target, true, error), error));
        CHECK(near(pixel(layers.target, 2, 1), {40, 64, 0, 255}));
        CHECK(near(pixel(layers.target, 3, 2), {80, 0, 0, 255}));

        // An explicitly private pooled target must not expose its old pixels
        // when a package blends or discards instead of clearing the target.
        target.clear_before_render = true;
        CHECK(checked(instance.render(layers.source, target, false, error), error));
        CHECK(near(pixel(layers.target, 2, 1), {0, 64, 0, 128}));
        CHECK(near(pixel(layers.target, 3, 2), {}));
        CHECK(near(pixel(layers.target, 0, 0), {80, 0, 0, 255}));

        auto layered = simple(R"(
layout(location=0) out vec4 c;
void main() {
    if (hypr_uv.x > 0.5) discard;
    c = vec4(0, 0.25, 0, 0.5);
}
)");
        auto overlay = raster("overlay", R"(
layout(location=0) out vec4 c;
void main() {
    if (hypr_uv.x > 0.5) discard;
    c = vec4(0, 0, 0.25, 0.5);
}
)");
        overlay.clear = false;
        overlay.blend = true;
        layered.passes.push_back(std::move(overlay));
        CHECK(checked(program.initialize(std::move(layered), {}, error, eglGetProcAddress), error));
        CHECK(checked(instance.initialize(program, 2, 2, error), error));
        CHECK(checked(instance.render(layers.source, target, true, error), error));
        CHECK(near(pixel(layers.target, 2, 1), {0, 32, 64, 192}));
        CHECK(near(pixel(layers.target, 3, 2), {}));
        CHECK(near(pixel(layers.target, 0, 0), {80, 0, 0, 255}));
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void in_place_presentation_blends_only_damaged_pixels() {
        Layers layers(6, 4);
        constexpr std::array<GLfloat, 4> underlay{80.F / 255, 0, 0, 1};
        glClearBufferfv(GL_COLOR, 0, underlay.data());
        auto target = layers.target;
        target.origin_x = 1;
        target.origin_y = 1;
        target.width = 4;
        target.height = 2;
        const std::array clips{Clip{1, 1, 1, 2}, Clip{4, 2, 1, 1}};
        target.clips = clips;
        target.composite = true;
        Program program;
        Instance instance;
        std::string error;
        auto package = simple(R"(
layout(location=0) out vec4 color;
void main() {
    if (hypr_uv.x > .75) discard;
    color = hypr_sample_destination(hypr_uv) * .5;
}
)");
        auto overlay = raster("overlay", R"(
layout(location=0) out vec4 color;
void main() {
    if (hypr_uv.x > .75) discard;
    color = vec4(0.0, 0.0, .25, .5);
}
)");
        overlay.blend = true;
        overlay.clear = false;
        package.passes.push_back(std::move(overlay));
        const Pixel green{0, 128, 0, 255};
        layers.source.destination_texture = texture(1, 1, &green);
        if (checked(program.initialize(std::move(package), {}, error, eglGetProcAddress), error) &&
            checked(instance.initialize(program, 4, 2, error), error)) {
            auto alias = layers.source;
            alias.destination_texture = target.texture;
            CHECK(!instance.render(alias, target, true, error));
            CHECK(near(pixel(layers.target, 1, 1), {80, 0, 0, 255}));
            CHECK(checked(instance.render(layers.source, target, true, error), error));
            CHECK(near(pixel(layers.target, 1, 1), {20, 32, 64, 255}));
            CHECK(near(pixel(layers.target, 2, 1), {80, 0, 0, 255}));
            CHECK(near(pixel(layers.target, 4, 2), {80, 0, 0, 255}));
        }
        glDeleteTextures(1, &layers.source.destination_texture);
    }

    Package diffusion() {
        Package package;
        package.textures = {
            {.name = "state", .size = {.scale = 0.5F}, .ping_pong = true},
            {.name = "scratchA", .lifetime = Lifetime::Transient},
            {.name = "scratchB", .lifetime = Lifetime::Transient},
            {.name = "art", .image_width = 1, .image_height = 1, .image = {76, 0, 0, 255}},
        };
        auto init = raster("init", R"(
layout(location=0) out vec4 color;
void main() { color = vec4(16.0/255.0,0,0,1); }
)", {STATE_WRITE});
        init.phase = Phase::Initialize;
        auto step = raster("step", R"(
uniform highp sampler2D previous;
layout(location=0) out vec4 color;
void main() {
    float v = texelFetch(previous, ivec2(gl_FragCoord.xy), 0).r;
    v += hypr_delta/float(hypr_iterations) + (1.0+float(hypr_iteration))/255.0;
    color = vec4(v, 0, 0, 1);
}
)", {STATE_WRITE});
        step.phase = Phase::Update;
        step.inputs.push_back({"previous", STATE_READ});
        step.iterations = 2;
        constexpr Ref A{ResourceKind::Texture, 1}, B{ResourceKind::Texture, 2};
        auto mix = raster("mix", R"(
uniform highp sampler2D stateField;
uniform highp sampler2D artField;
layout(location=0) out vec4 a;
layout(location=1) out vec4 b;
void main() { a = texture(stateField, hypr_uv); b = texture(artField, hypr_uv); }
)", {A, B});
        mix.inputs = {{"stateField", STATE_READ}, {"artField", {ResourceKind::Texture, 3}}};
        auto draw = raster("draw", R"(
uniform highp sampler2D fieldA;
uniform highp sampler2D fieldB;
layout(location=0) out vec4 color;
void main() {
    color = vec4(texture(fieldA,hypr_uv).r, texture(fieldB,hypr_uv).r, hypr_delta, 1);
}
)");
        draw.inputs = {{"fieldA", A}, {"fieldB", B}};
        package.passes = {std::move(init), std::move(step), std::move(mix), std::move(draw)};
        return package;
    }

    void multipass_ticks_resize_and_independent_instances() {
        Layers layers(4, 4);
        auto package = diffusion();
        Program program;
        Instance a, b;
        std::string error;
        if (!checked(program.initialize(std::move(package), {}, error, eglGetProcAddress), error) ||
            !checked(a.initialize(program, 4, 4, error), error) ||
            !checked(b.initialize(program, 4, 4, error), error)) {
            return;
        }
        CHECK(program.statistics().allocated_bytes == 12);  // Two probe texels and the PNG.
        CHECK(program.statistics().upload_bytes == 4);
        CHECK(a.statistics().allocations == 4);
        CHECK(a.statistics().allocated_bytes == 160);
        const auto allocated = a.statistics().allocated_bytes;
        const auto output = [&](Instance &which, bool live, Pixel color) {
            if (checked(which.render(layers.source, layers.target, live, error), error)) {
                CHECK(near(pixel(layers.target, 0, 0), color));
                CHECK(near(pixel(layers.target, 3, 3), color));
            }
        };
        a.advance({.tick = 10, .delta = 40.F / 255});
        CHECK(!a.render(layers.source, layers.target, false, error));
        output(a, true, {59, 76, 40, 255});
        a.advance({.tick = 10, .delta = 1.F});
        output(a, true, {59, 76, 40, 255});
        a.advance({.tick = 11, .delta = 8.F / 255});
        a.advance({.tick = 12, .delta = 12.F / 255});
        a.advance({.tick = 11, .delta = 1.F});
        auto alias = layers.target;
        alias.texture = layers.source.texture;
        CHECK(!a.render(layers.source, alias, true, error));
        output(a, false, {59, 76, 40, 255});
        CHECK(a.statistics().update_passes == 2);
        output(a, true, {82, 76, 20, 255});
        CHECK(a.statistics().update_passes == 4);
        CHECK(a.statistics().initialize_passes == 1);
        CHECK(a.statistics().render_passes == 8);
        CHECK(a.statistics().draws == 13);
        CHECK(a.statistics().allocated_bytes == allocated);
        CHECK(a.statistics().upload_bytes == 0);
        CHECK(a.statistics().copy_bytes == 0);
        CHECK(!b.render(layers.source, layers.target, false, error));
        output(b, true, {16, 76, 0, 255});
        CHECK(checked(a.initialize(program, 4, 4, error), error));
        output(a, false, {82, 76, 20, 255});
        CHECK(!a.initialize(program, 8193, 4, error));
        output(a, false, {82, 76, 20, 255});
        CHECK(checked(a.initialize(program, 2, 2, error), error));
        CHECK(!a.render(layers.source, layers.target, false, error));
        output(a, true, {16, 76, 0, 255});
        CHECK(a.statistics().allocated_bytes == 40);
        a.release();
        CHECK(a.statistics().allocations == 0);
        CHECK(!a.render(layers.source, layers.target, true, error));
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void fixed_simulation_storage_survives_source_resize() {
        Layers layers(4, 4);
        auto package = diffusion();
        package.textures[0].size = {2, 2};
        package.textures[1].size = package.textures[2].size = {4, 4};
        Program program;
        Instance instance;
        std::string error;
        if (!checked(program.initialize(std::move(package), {}, error, eglGetProcAddress), error) ||
            !checked(instance.initialize(program, 4, 4, error), error)) return;
        instance.advance({.tick = 1, .delta = 40.F / 255});
        CHECK(checked(instance.render(layers.source, layers.target, true, error), error));
        CHECK(checked(instance.initialize(program, 6, 8, error), error));
        CHECK(checked(instance.render(layers.source, layers.target, false, error), error));
        CHECK(near(pixel(layers.target, 2, 2), {59, 76, 40, 255}));
    }

    void mrt_dimensions_are_checked_after_capture_size_resolution() {
        Layers layers(4, 4);
        auto package = diffusion();
        package.textures[1].size = {4, 4};  // The other attachment scales with capture.
        Program program;
        Instance instance;
        std::string error;
        if (!checked(program.initialize(std::move(package), {}, error, eglGetProcAddress), error) ||
            !checked(instance.initialize(program, 4, 4, error), error) ||
            !checked(instance.render(layers.source, layers.target, true, error), error)) {
            return;
        }
        const auto prior_textures = driver_probe::texture_calls;
        const auto prior_buffers = driver_probe::buffer_calls;
        CHECK(!instance.initialize(program, 2, 2, error));
        CHECK(driver_probe::texture_calls == prior_textures);
        CHECK(driver_probe::buffer_calls == prior_buffers);
        CHECK(error.find("incompatible GLES output dimensions") != std::string::npos);
        CHECK(checked(instance.render(layers.source, layers.target, false, error), error));
        CHECK(near(pixel(layers.target, 3, 3), {16, 76, 0, 255}));
    }

    void render_only_failure_preserves_committed_and_pending_simulation() {
        Layers layers(4, 4);
        Program program;
        Instance instance;
        std::string error;
        if (!checked(program.initialize(diffusion(), {}, error, eglGetProcAddress), error) ||
            !checked(instance.initialize(program, 4, 4, error), error)) {
            return;
        }
        instance.advance({.tick = 1, .delta = 40.F / 255});
        CHECK(checked(instance.render(layers.source, layers.target, true, error), error));
        instance.advance({.tick = 2, .delta = 20.F / 255});
        driver_probe::fail_draw = true;
        CHECK(!instance.render(layers.source, layers.target, false, error));
        CHECK(error.find("executing native shader pass") != std::string::npos);
        CHECK(!std::exchange(driver_probe::fail_draw, false));
        CHECK(instance.statistics().update_passes == 2);
        CHECK(checked(instance.render(layers.source, layers.target, false, error), error));
        CHECK(near(pixel(layers.target, 3, 3), {59, 76, 40, 255}));
        CHECK(checked(instance.render(layers.source, layers.target, true, error), error));
        CHECK(instance.statistics().update_passes == 4);
        CHECK(near(pixel(layers.target, 3, 3), {82, 76, 20, 255}));
        CHECK(glGetError() == GL_NO_ERROR);
    }

    std::vector<std::uint8_t> bytes(std::initializer_list<float> floats) {
        std::vector<std::uint8_t> bytes(floats.size() * sizeof(float));
        std::memcpy(bytes.data(), floats.begin(), bytes.size());
        return bytes;
    }

    Package particles() {
        Package package;
        package.textures = {{.name = "palette", .image_width = 1, .image_height = 2,
            .image = {255, 0, 0, 255, 0, 255, 0, 255}}};
        package.buffers = {
            {.name = "particles", .bytes = 32, .ping_pong = true,
                .initial = bytes({0.25, 0.25, 0, 0, 0.75, 0.75, 0, 0})},
            {.name = "physics", .bytes = 16, .initial = bytes({0.125F})},
            {.name = "corners", .bytes = 32,
                .initial = bytes({-0.125F,-0.125F, 0.125F,-0.125F, 0.125F,0.125F, -0.125F,0.125F})},
        };
        package.meshes = {
            {.name = "state", .primitive = Primitive::Points, .count = 2,
                .attributes = {{.location = 1, .components = 4, .stride = 16, .buffer = PARTICLE_READ}}},
            {.name = "quads", .count = 4, .instances = 2,
                .attributes = {{.location = 0, .components = 2, .stride = 8,
                                   .buffer = {ResourceKind::Buffer, 2}},
                    {.location = 1, .components = 4, .stride = 16, .divisor = 1,
                        .buffer = PARTICLE_READ}},
                .indices = {0, 1, 2, 0, 2, 3}},
        };
        Pass step;
        step.name = "integrate";
        step.phase = Phase::Update;
        step.mesh = 0;
        step.transform_feedback = true;
        step.feedback = PARTICLE_WRITE;
        step.varyings = {"nextPosition"};
        step.blocks = {{"Physics", {ResourceKind::Buffer, 1}}};
        step.vertex = R"(
layout(std140) uniform Physics { vec4 shift; };
layout(location=1) in vec4 previousPosition;
out vec4 nextPosition;
void main() {
    nextPosition = previousPosition + shift;
    gl_Position = vec4(0,0,0,1);
    gl_PointSize = 1.0;
}
)";
        auto draw = raster("quads", R"(
flat in vec4 marker;
layout(location=0) out vec4 color;
void main() { color = marker; }
)");
        draw.mesh = 1;
        draw.inputs = {{"art", {ResourceKind::Texture, 0}}};
        draw.vertex = R"(
uniform highp sampler2D art;
layout(location=0) in vec2 corner;
layout(location=1) in vec4 position;
out vec2 hypr_uv;
flat out vec4 marker;
void main() {
    marker = texelFetch(art, ivec2(0,gl_InstanceID),0);
    vec2 targetUV = position.xy + corner + vec2(0, marker.r * 0.125);
    hypr_uv = targetUV;
    gl_Position = vec4(targetUV * 2.0 - 1.0,0,1);
}
)";
        package.passes = {std::move(step), std::move(draw)};
        return package;
    }

    void transient_feedback_requires_exact_complete_capture() {
        auto package = particles();
        package.buffers.push_back({.name = "scratchBuffer", .bytes = 32, .lifetime = Lifetime::Transient});
        package.meshes.push_back({.name = "onePoint", .primitive = Primitive::Points, .count = 1});
        constexpr Ref scratch{ResourceKind::Buffer, 3};
        Pass init;
        init.name = "seedScratch";
        init.phase = Phase::Initialize;
        init.mesh = 0;
        init.transform_feedback = true;
        init.feedback = scratch;
        init.varyings = {"written"};
        init.vertex = R"(
out vec4 written;
void main() { written=vec4(1); gl_PointSize=1.0; gl_Position=vec4(0,0,0,1); }
)";
        auto update = init;
        update.name = "overwriteScratch";
        update.phase = Phase::Update;
        update.mesh = 2;  // Only the first vec4; the second would retain initialize-phase data.
        package.textures.push_back({.name = "observedTail", .lifetime = Lifetime::Transient});
        auto consume = raster("readTail", R"(
layout(std140) uniform Scratch { vec4 values[2]; };
layout(location=0) out vec4 color;
void main() { color = values[1]; }
)", {{ResourceKind::Texture, 1}});
        consume.phase = Phase::Update;
        consume.blocks = {{"Scratch", scratch}};
        package.passes.insert(package.passes.begin(), {init, update, consume});
        Program program;
        std::string error;
        CHECK(!program.initialize(package, {}, error, eglGetProcAddress));
        CHECK(error.find("every byte") != std::string::npos);

        // The nominal byte count fits exactly, but GLES would drop the third line vertex.
        package.meshes[0] = {.name = "incompleteLine", .primitive = Primitive::Lines, .count = 3};
        package.buffers[scratch.index].bytes = 48;
        CHECK(!program.initialize(std::move(package), {}, error, eglGetProcAddress));
        CHECK(error.find("complete primitives") != std::string::npos);
    }

    void feedback_storage_is_rebound_sequentially_for_uniform_and_vertex_inputs() {
        Layers layers(4, 4);
        constexpr Ref data{ResourceKind::Buffer, 0};
        constexpr Ref scratch{ResourceKind::Texture, 0};
        Package package;
        package.buffers = {{.name = "storage", .bytes = 3 * 4 * sizeof(float)}};
        package.textures = {{.name = "scratch", .lifetime = Lifetime::Transient}};
        package.meshes = {
            {.name = "write", .primitive = Primitive::Points, .count = 3},
            {.name = "read", .attributes = {{.location = 0, .components = 4, .stride = 16, .buffer = data}}},
        };
        auto before = raster("bindBeforeFeedback", R"(
layout(std140) uniform Data { vec4 value; };
layout(location=0) out vec4 color;
void main() { color = value; }
)", {scratch});
        before.phase = Phase::Update;
        before.blocks = {{"Data", data}};
        Pass feedback;
        feedback.name = "writeValues";
        feedback.phase = Phase::Update;
        feedback.mesh = 0;
        feedback.transform_feedback = true;
        feedback.feedback = data;
        feedback.varyings = {"written"};
        feedback.vertex = R"(
out vec4 written;
void main() { written=vec4(0.25,0.75,0,1); gl_PointSize=1.0; gl_Position=vec4(0,0,0,1); }
)";
        auto after = raster("readAfterFeedback", R"(
layout(std140) uniform Data { vec4 value; };
in float vertexRed;
layout(location=0) out vec4 color;
void main() { color = vec4(vertexRed, value.g, 0, 1); }
)");
        after.mesh = 1;
        after.blocks = {{"Data", data}};
        after.vertex = R"(
layout(location=0) in vec4 state;
out float vertexRed;
out vec2 hypr_uv;
void main() {
    hypr_uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vertexRed = state.r;
    gl_Position = vec4(hypr_uv * 2.0 - 1.0, 0, 1);
}
)";
        package.passes = {std::move(before), std::move(feedback), std::move(after)};
        Program program;
        Instance instance;
        std::string error;
        if (!checked(program.initialize(std::move(package), {}, error, eglGetProcAddress), error) ||
            !checked(instance.initialize(program, 4, 4, error), error)) {
            return;
        }
        instance.advance({.tick = 1});
        const auto prior = driver_probe::binding_violations;
        driver_probe::observe_feedback = true;
        const bool rendered = instance.render(layers.source, layers.target, true, error);
        driver_probe::observe_feedback = false;
        CHECK(driver_probe::binding_violations == prior);
        if (checked(rendered, error)) {
            CHECK(near(pixel(layers.target, 3, 3), {64, 191, 0, 255}));
        }
    }

    struct HostState {
        static constexpr std::array<GLenum, 32> INTEGERS{GL_CURRENT_PROGRAM, GL_VERTEX_ARRAY_BINDING,
            GL_ARRAY_BUFFER_BINDING, GL_ELEMENT_ARRAY_BUFFER_BINDING, GL_ACTIVE_TEXTURE,
            GL_DRAW_FRAMEBUFFER_BINDING, GL_READ_FRAMEBUFFER_BINDING, GL_DRAW_BUFFER0,
            GL_DRAW_BUFFER1, GL_READ_BUFFER, GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA,
            GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA,
            GL_PIXEL_PACK_BUFFER_BINDING, GL_PIXEL_UNPACK_BUFFER_BINDING, GL_PACK_ALIGNMENT,
            GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS, GL_UNPACK_ALIGNMENT,
            GL_UNPACK_ROW_LENGTH, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS,
            GL_UNPACK_SKIP_IMAGES, GL_UNIFORM_BUFFER_BINDING, GL_TRANSFORM_FEEDBACK_BINDING,
            GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, GL_TRANSFORM_FEEDBACK_PAUSED};
        static constexpr std::array<GLenum, 11> CAPS{GL_BLEND, GL_SCISSOR_TEST, GL_STENCIL_TEST,
            GL_DEPTH_TEST, GL_CULL_FACE, GL_RASTERIZER_DISCARD, GL_SAMPLE_ALPHA_TO_COVERAGE,
            GL_SAMPLE_COVERAGE, GL_POLYGON_OFFSET_FILL, GL_PRIMITIVE_RESTART_FIXED_INDEX, GL_DITHER};
        std::array<GLint, INTEGERS.size()> ints{};
        std::array<GLboolean, CAPS.size()> caps{};
        std::array<GLint, 2> textures{}, samplers{}, indexed{};
        std::array<GLint64, 4> ranges{};
        std::array<GLint, 4> viewport{}, scissor{};
        std::array<GLfloat, 4> clear{};
        std::array<GLfloat, 2> depth_range{};
        std::array<GLboolean, 4> color_mask{};

        HostState() {
            for (std::size_t i = 0; i < INTEGERS.size(); ++i) ints[i] = integer(INTEGERS[i]);
            for (std::size_t i = 0; i < CAPS.size(); ++i) caps[i] = glIsEnabled(CAPS[i]);
            const auto active = integer(GL_ACTIVE_TEXTURE);
            for (unsigned i = 0; i < 2; ++i) {
                glActiveTexture(GL_TEXTURE0 + i);
                textures[i] = integer(GL_TEXTURE_BINDING_2D);
                samplers[i] = integer(GL_SAMPLER_BINDING);
            }
            glActiveTexture(active);
            glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &indexed[0]);
            glGetIntegeri_v(GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 0, &indexed[1]);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 0, &ranges[0]);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, 0, &ranges[1]);
            glGetInteger64i_v(GL_TRANSFORM_FEEDBACK_BUFFER_START, 0, &ranges[2]);
            glGetInteger64i_v(GL_TRANSFORM_FEEDBACK_BUFFER_SIZE, 0, &ranges[3]);
            glGetIntegerv(GL_VIEWPORT, viewport.data());
            glGetIntegerv(GL_SCISSOR_BOX, scissor.data());
            glGetFloatv(GL_COLOR_CLEAR_VALUE, clear.data());
            glGetFloatv(GL_DEPTH_RANGE, depth_range.data());
            glGetBooleanv(GL_COLOR_WRITEMASK, color_mask.data());
        }

        bool operator==(const HostState &) const = default;
    };

    GLuint host_feedback_program() {
        const auto vertex = gl_harness::detail::compile(GL_VERTEX_SHADER, R"(#version 300 es
out vec4 sent;
void main() { sent=vec4(0); gl_Position=vec4(0,0,0,1); gl_PointSize=1.0; }
)");
        const auto fragment = gl_harness::detail::compile(GL_FRAGMENT_SHADER,
            "#version 300 es\nvoid main() {}\n");
        const auto program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        const GLchar *varying = "sent";
        glTransformFeedbackVaryings(program, 1, &varying, GL_INTERLEAVED_ATTRIBS);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint status = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &status);
        CHECK(status == GL_TRUE);
        return program;
    }

    void feedback_instancing_uniform_blocks_and_host_state() {
        Layers layers(8, 8);
        Layers other(8, 8);
        std::string error;
        Program program;
        Instance instance;
        const auto host = host_feedback_program();
        glUseProgram(host);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, layers.target.framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, other.target.framebuffer);
        const auto ubo_alignment = integer(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT);
        std::array<GLuint, 5> buffers{};
        glGenBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        for (auto name : buffers) {
            glBindBuffer(GL_ARRAY_BUFFER, name);
            glBufferData(GL_ARRAY_BUFFER, ubo_alignment + 128, nullptr, GL_STATIC_DRAW);
        }
        GLuint vao = 0, tf = 0;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[4]);
        glGenTransformFeedbacks(1, &tf);
        glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, tf);
        glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffers[0], 4, 32);
        glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffers[1]);
        glBindBufferRange(GL_UNIFORM_BUFFER, 0, buffers[2], ubo_alignment, 32);
        glBindBuffer(GL_UNIFORM_BUFFER, buffers[3]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffers[4]);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, buffers[3]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 7);
        glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 9);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
        glPixelStorei(GL_UNPACK_SKIP_IMAGES, 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 8);
        glPixelStorei(GL_PACK_ROW_LENGTH, 6);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 1);
        glPixelStorei(GL_PACK_SKIP_ROWS, 2);
        GLuint sampler = 0;
        glGenSamplers(1, &sampler);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, other.source.texture);
        glBindSampler(0, sampler);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, other.target.texture);
        glBindSampler(1, sampler);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, layers.source.texture);
        glViewport(2, 3, 4, 5);
        glScissor(1, 2, 3, 4);
        glDepthRangef(0.25F, 0.75F);
        glClearColor(0.125F, 0.25F, 0.5F, 1);
        glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA);
        glBlendEquationSeparate(GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT);
        for (const auto cap : HostState::CAPS) glEnable(cap);
        CHECK(glGetError() == GL_NO_ERROR);
        const HostState before;
        const bool prepared = checked(program.initialize(particles(), {}, error, eglGetProcAddress), error) &&
                              checked(instance.initialize(program, 8, 8, error), error);
        CHECK(HostState{} == before);
        CHECK(glGetError() == GL_NO_ERROR);
        if (prepared) {
            CHECK(program.statistics().allocated_bytes == 32);  // PNG and shared EBO.
            CHECK(instance.statistics().allocated_bytes == 112);
            CHECK(instance.statistics().upload_bytes == 112);  // Includes UBO zero tail.
            instance.advance({.tick = 1});
            glBeginTransformFeedback(GL_POINTS);
            CHECK(integer(GL_TRANSFORM_FEEDBACK_ACTIVE) == GL_TRUE);
            CHECK(!instance.render(layers.source, layers.target, true, error));
            CHECK(error.find("unpaused") != std::string::npos);
            CHECK(instance.statistics().update_passes == 0);
            CHECK(integer(GL_TRANSFORM_FEEDBACK_ACTIVE) == GL_TRUE);
            glPauseTransformFeedback();
            const HostState paused;
            const auto prior = driver_probe::binding_violations;
            driver_probe::observe_feedback = true;
            const bool rendered = checked(instance.render(layers.source, layers.target, true, error), error);
            driver_probe::observe_feedback = false;
            CHECK(driver_probe::binding_violations == prior);
            CHECK(HostState{} == paused);
            CHECK(integer(GL_TRANSFORM_FEEDBACK_ACTIVE) == GL_TRUE);
            CHECK(integer(GL_TRANSFORM_FEEDBACK_PAUSED) == GL_TRUE);
            glResumeTransformFeedback();
            glEndTransformFeedback();
            CHECK(HostState{} == before);
            CHECK(glGetError() == GL_NO_ERROR);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_PACK_SKIP_ROWS, 0);
            if (rendered) {
                CHECK(near(pixel(layers.target, 3, 3), {255, 0, 0, 255}));
                CHECK(near(pixel(layers.target, 7, 6), {0, 255, 0, 255}));
                CHECK(near(pixel(layers.target, 3, 1), {}));  // Vertex sampling deformed the first quad.
                CHECK(near(pixel(layers.target, 1, 3), {}));  // UBO shifted the TF-produced positions.
                CHECK(instance.statistics().update_passes == 1);
                CHECK(instance.statistics().copy_bytes == 0);
            }
        }
        for (const auto cap : HostState::CAPS) glDisable(cap);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(0);
        glDeleteProgram(host);
        glDeleteTransformFeedbacks(1, &tf);
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        glDeleteSamplers(1, &sampler);
        for (const auto property : {GL_UNPACK_ROW_LENGTH, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_PIXELS,
                 GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_IMAGES, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS,
                 GL_PACK_SKIP_PIXELS}) {
            glPixelStorei(property, 0);
        }
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glActiveTexture(GL_TEXTURE0);
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void failed_preparation_preserves_generation_and_checks_gpu_limits() {
        Layers layers(2, 2);
        Program program;
        Instance old_instance, fresh;
        std::string error;
        if (!checked(program.initialize(simple(), {}, error, eglGetProcAddress), error) ||
            !checked(old_instance.initialize(program, 2, 2, error), error)) {
            return;
        }
        auto broken = simple("void main() { tokenThatWillNeverCompile; }");
        CHECK(!program.initialize(std::move(broken), {}, error, eglGetProcAddress));
        CHECK(error.find("fragment") != std::string::npos);
        auto short_feedback = particles();
        short_feedback.passes[0].feedback = {ResourceKind::Buffer, 1};
        short_feedback.passes[0].blocks.clear();
        short_feedback.passes[0].vertex = R"(
out vec4 nextPosition;
void main() { nextPosition=vec4(1); gl_Position=vec4(0,0,0,1); gl_PointSize=1.0; }
)";
        CHECK(!program.initialize(std::move(short_feedback), {}, error, eglGetProcAddress));
        CHECK(error.find("too small") != std::string::npos);
        auto short_block = particles();
        short_block.buffers[1].bytes = 8;
        CHECK(!program.initialize(std::move(short_block), {}, error, eglGetProcAddress));
        CHECK(error.find("uniform block exceeds") != std::string::npos);
        CHECK(checked(old_instance.render(layers.source, layers.target, true, error), error));
        CHECK(near(pixel(layers.target, 0, 0), {255, 255, 255, 255}));

        auto replacement = simple(R"(
layout(location=0) out vec4 color;
void main() { color = vec4(intensity,0,0,1); }
)");
        replacement.parameters.push_back({"intensity", {.type = ValueType::Float, .numbers = {1}}, 0, 1});
        Values outside{{.type = ValueType::Float, .numbers = {2}}};
        CHECK(!program.initialize(replacement, outside, error, eglGetProcAddress));
        outside.front().numbers[0] = std::numeric_limits<float>::quiet_NaN();
        CHECK(!program.initialize(replacement, outside, error, eglGetProcAddress));
        const auto values = parameter_values(replacement, R"({"intensity":0.25})");
        CHECK(values.has_value());
        if (!values || !checked(program.initialize(replacement, *values, error, eglGetProcAddress), error)) {
            return;
        }
        replacement.passes.clear();  // A mutable caller cannot change the frozen generation.
        if (!checked(fresh.initialize(program, 2, 2, error), error)) {
            return;
        }
        program.release();  // Both instances must retain their own compiled generation.
        CHECK(checked(fresh.render(layers.source, layers.target, true, error), error));
        CHECK(near(pixel(layers.target, 0, 0), {64, 0, 0, 255}));
        CHECK(checked(old_instance.render(layers.source, layers.target, false, error), error));
        CHECK(near(pixel(layers.target, 0, 0), {255, 255, 255, 255}));

        auto oversized = simple();
        oversized.textures.push_back({.name = "large", .format = Format::RGBA32F, .ping_pong = true});
        CHECK(checked(program.initialize(std::move(oversized), {}, error, eglGetProcAddress), error));
        const auto storage_calls = driver_probe::texture_calls;
        const auto buffer_calls = driver_probe::buffer_calls;
        CHECK(!fresh.initialize(program, 4096, 4096, error));
        CHECK(driver_probe::texture_calls == storage_calls);
        CHECK(driver_probe::buffer_calls == buffer_calls);
        CHECK(error.find("budget") != std::string::npos || error.find("extent") != std::string::npos);
        CHECK(checked(fresh.render(layers.source, layers.target, false, error), error));
        CHECK(near(pixel(layers.target, 0, 0), {64, 0, 0, 255}));

        const auto limit = std::min(integer(GL_MAX_DRAW_BUFFERS), integer(GL_MAX_COLOR_ATTACHMENTS));
        if (limit < static_cast<GLint>(MAX_TEXTURES)) {
            auto oversized_mrt = simple();
            auto mrt = raster("mrt", R"(layout(location=0) out vec4 c; void main(){ c=vec4(1); })", {});
            for (GLint i = 0; i <= limit; ++i) {
                oversized_mrt.textures.push_back({.name = "mrt" + std::to_string(i),
                    .lifetime = Lifetime::Transient});
                mrt.outputs.push_back({ResourceKind::Texture, static_cast<std::uint16_t>(i)});
            }
            oversized_mrt.passes.insert(oversized_mrt.passes.begin(), std::move(mrt));
            CHECK(!program.initialize(std::move(oversized_mrt), {}, error, eglGetProcAddress));
            CHECK(error.find("MRT exceeds") != std::string::npos);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void float_outputs_follow_actual_framebuffer_support() {
        constexpr std::array FORMATS{std::pair{Format::RGBA16F, GLenum{GL_RGBA16F}},
            std::pair{Format::RGBA32F, GLenum{GL_RGBA32F}}};
        Layers layers(2, 2);
        for (const auto &[format, gl_format] : FORMATS) {
            const auto test_texture = texture(2, 2, nullptr, gl_format);
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                test_texture, 0);
            const bool supported = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, layers.target.framebuffer);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &test_texture);
            auto package = simple(R"(
uniform highp sampler2D scratchField;
layout(location=0) out vec4 c;
void main(){ c=vec4(texture(scratchField,hypr_uv).rgb * 0.25,1); }
)");
            package.textures.push_back({.name = "scratch", .format = format,
                .lifetime = Lifetime::Transient});
            const Ref scratch{ResourceKind::Texture, 0};
            package.passes[0].inputs = {{"scratchField", scratch}};
            package.passes.insert(package.passes.begin(), raster("float_out", R"(
layout(location=0) out vec4 c;
void main(){ c=vec4(2,3,1,1); }
)", {scratch}));
            Program program;
            Instance instance;
            std::string error;
            const bool prepared = program.initialize(std::move(package), {}, error, eglGetProcAddress);
            CHECK(prepared == supported);
            if (!prepared) {
                CHECK(error.find("unsupported GLES framebuffer") != std::string::npos);
                continue;
            }
            const bool initialized = instance.initialize(program, 2, 2, error);
            CHECK(initialized == supported);
            if (initialized && checked(instance.render(layers.source, layers.target, true, error), error)) {
                CHECK(near(pixel(layers.target, 1, 1), {128, 191, 64, 255}));
            }
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void unsupported_combinations_stop_before_full_storage_and_asset_upload() {
        Program program;
        std::string error;
        const auto calls = driver_probe::texture_calls;
        const auto large = driver_probe::large_textures;
        const auto buffers = driver_probe::buffer_calls;
        driver_probe::fail_format_probe = true;
        CHECK(!program.initialize(diffusion(), {}, error, eglGetProcAddress));
        CHECK(!std::exchange(driver_probe::fail_format_probe, false));
        CHECK(error.find("unsupported GLES framebuffer") != std::string::npos);
        CHECK(driver_probe::texture_calls == calls + 1);  // One actual 1x1 probe, no asset upload.
        CHECK(driver_probe::large_textures == large);
        CHECK(driver_probe::buffer_calls == buffers);
        CHECK(glGetError() == GL_NO_ERROR);
    }

    void raster_outputs_must_match_the_manifest_locations_and_types() {
        constexpr std::array BAD{
            R"(
layout(location=0) out vec4 a;
void main() { a = vec4(1); }
)",
            R"(
layout(location=0) out vec4 a;
layout(location=1) out ivec4 b;
void main() { a = vec4(1); b = ivec4(2); }
)",
            R"(
layout(location=0) out vec4 a;
#if 1
layout(location=1) out vec4 b;
#endif
void main() { a = vec4(1); b = vec4(2); }
)",
            R"(
layout(location=0) out vec4 a;
#define DECLARE(name) layout(location=1) out vec4 name
DECLARE(b);
void main() { a = vec4(1); b = vec4(2); }
)",
            R"(
layout(location=0) out vec4 a;
#define b otherName
layout(location=1) out vec4 b;
void main() { a = vec4(1); b = vec4(2); }
)",
        };
        Program program;
        std::string error;
        const auto calls = driver_probe::texture_calls;
        for (const auto *fragment : BAD) {
            auto fixture = diffusion();
            fixture.passes[2].fragment = fragment;
            CHECK(!program.initialize(std::move(fixture), {}, error, eglGetProcAddress));
            CHECK(error.find("fragment") != std::string::npos);
        }
        CHECK(driver_probe::texture_calls == calls);

        auto allowed = simple(R"(
#define TINT(x) (vec4((x), 0, 0, 1))
layout(location=0) out highp vec4 color;
void shade(out vec4 result) { result = TINT(0.125); }
void main() { shade(color); }
)");
        Layers layers(2, 2);
        Instance instance;
        if (checked(program.initialize(std::move(allowed), {}, error, eglGetProcAddress), error) &&
            checked(instance.initialize(program, 2, 2, error), error) &&
            checked(instance.render(layers.source, layers.target, true, error), error)) {
            CHECK(near(pixel(layers.target, 1, 1), {32, 0, 0, 255}));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }


    void compute_instances_keep_authorized_state_and_generation() {
        const auto source = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        const auto fixture = source / "examples/shaders/compute-stencil/effect.json";
        auto loaded = load_package(fixture.string());
        CHECK(loaded.has_value());
        if (!loaded) return;
        Layers layers(8, 8);
        EGLContext context = eglGetCurrentContext();
        const hyprimate::compute::GlResolver compute_resolver{
            driver_probe::resolve_compute, driver_probe::compute_context_current, &context};
        Program program;
        Instance first, second;
        std::string error;
        auto values = parameter_values(*loaded);
        CHECK(values.has_value());
        if (!values) return;
        auto fixture_package = std::move(*loaded);
        auto replacement = fixture_package;
        if (!checked(program.initialize(std::move(fixture_package), *values, error, eglGetProcAddress,
                compute_resolver), error) ||
            !checked(first.initialize(program, 8, 8, error), error) ||
            !checked(second.initialize(program, 8, 8, error), error)) return;
        constexpr std::size_t INSTANCE_BUDGET = 128ULL * 1024 * 1024;
        const auto prior_texture_calls = driver_probe::texture_calls;
        Instance ancillary_over_budget;
        CHECK(!ancillary_over_budget.initialize(program, 8, 8, error, INSTANCE_BUDGET));
        CHECK(error.find("128 MiB") != std::string::npos);
        CHECK(driver_probe::texture_calls == prior_texture_calls);
        CHECK(!first.initialize(program, 8, 8, error,
            INSTANCE_BUDGET - first.statistics().allocated_bytes + 1));


        CHECK(!first.render(layers.source, layers.target, false, error));
        CHECK(first.compute_statistics().native_dispatches == 0);
        CHECK(first.compute_statistics().slibs_dispatches == 0);
        CHECK(checked(first.render(layers.source, layers.target, true, error), error));
        const auto primed = first.compute_statistics();
        CHECK(primed.native_dispatches + primed.slibs_dispatches > 0);
        CHECK(checked(first.render(layers.source, layers.target, false, error), error));
        const auto replay = first.compute_statistics();
        CHECK(replay.native_dispatches == primed.native_dispatches);
        CHECK(replay.slibs_dispatches == primed.slibs_dispatches);
        CHECK(replay.samples > primed.samples);
        CHECK(checked(second.render(layers.source, layers.target, true, error), error));
        CHECK(second.compute_statistics().native_dispatches + second.compute_statistics().slibs_dispatches ==
            primed.native_dispatches + primed.slibs_dispatches);
        first.advance({.tick = 1, .seed = 17, .delta = 0.1F});
        second.advance({.tick = 1, .seed = 23, .delta = 0.2F});
        CHECK(checked(first.render(layers.source, layers.target, true, error), error));
        const auto first_committed_pixel = pixel(layers.target, 3, 3);
        CHECK(checked(second.render(layers.source, layers.target, true, error), error));
        const auto before_pending_replay = first.compute_statistics();
        const auto committed_pixel = first_committed_pixel;
        first.advance({.tick = 2, .seed = 17, .delta = 0.75F});
        CHECK(checked(first.render(layers.source, layers.target, false, error), error));
        CHECK(pixel(layers.target, 3, 3) == committed_pixel);
        CHECK(first.compute_statistics().native_dispatches == before_pending_replay.native_dispatches);
        CHECK(first.compute_statistics().slibs_dispatches == before_pending_replay.slibs_dispatches);
        CHECK(checked(first.render(layers.source, layers.target, true, error), error));
        const auto committed = first.compute_statistics();
        CHECK(committed.native_dispatches + committed.slibs_dispatches >
            before_pending_replay.native_dispatches + before_pending_replay.slibs_dispatches);
        const auto resized_pixel = pixel(layers.target, 3, 3);
        CHECK(checked(first.initialize(program, 10, 9, error), error));
        CHECK(checked(first.render(layers.source, layers.target, false, error), error));
        CHECK(pixel(layers.target, 3, 3) == resized_pixel);
        CHECK(first.compute_statistics().native_dispatches == committed.native_dispatches);
        CHECK(first.compute_statistics().slibs_dispatches == committed.slibs_dispatches);

        auto oversized = replacement;
        for (unsigned i = 0; i < 5; ++i) {
            oversized.fields.push_back({"extra" + std::to_string(i),
                {hyprimate::compute::Element::Float, 1},
                std::nullopt, 1.0F, Lifetime::Persistent, true});
        }
        Program oversized_program;
        Instance rejected;
        CHECK(checked(oversized_program.initialize(std::move(oversized), *values, error,
            eglGetProcAddress, compute_resolver), error));
        CHECK(!rejected.initialize(oversized_program, 2048, 2048, error));
        CHECK(error.find("128 MiB") != std::string::npos);
        CHECK(checked(first.render(layers.source, layers.target, false, error), error));
        replacement.passes.back().fragment += "\nthis is not valid GLSL;\n";
        CHECK(!program.initialize(std::move(replacement), *values, error, eglGetProcAddress,
            compute_resolver));
        CHECK(first.compute_statistics().native_dispatches == committed.native_dispatches);
        CHECK(first.compute_statistics().slibs_dispatches == committed.slibs_dispatches);
        first.abandon();
        CHECK(checked(second.render(layers.source, layers.target, false, error), error));
    }
    void indexed_color_state_is_preserved_during_prepare_and_mrt_draws() {
        const char *suffix = integer(GL_MINOR_VERSION) >= 2 ? "" : nullptr;
        if (!suffix) {
            const auto extensions = integer(GL_NUM_EXTENSIONS);
            for (GLint i = 0; i < extensions; ++i) {
                const auto *extension = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
                const std::string_view name(extension ? extension : "");
                if (name == "GL_EXT_draw_buffers_indexed") {
                    suffix = "EXT";
                    break;
                }
                if (name == "GL_OES_draw_buffers_indexed") suffix = "OES";
            }
        }
        if (!suffix) {
            return;  // No indexed host state on this driver.
        }
        using Enable = void (GL_APIENTRYP)(GLenum, GLuint);
        using Enabled = GLboolean (GL_APIENTRYP)(GLenum, GLuint);
        using Mask = void (GL_APIENTRYP)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);
        using Equation = void (GL_APIENTRYP)(GLuint, GLenum, GLenum);
        using Factors = void (GL_APIENTRYP)(GLuint, GLenum, GLenum, GLenum, GLenum);
        const auto name = [suffix](const char *stem) { return std::string(stem) + suffix; };
        const auto enable_slot = driver_probe::resolve<Enable>(name("glEnablei").c_str());
        const auto enabled_slot = driver_probe::resolve<Enabled>(name("glIsEnabledi").c_str());
        const auto mask_slot = driver_probe::resolve<Mask>(name("glColorMaski").c_str());
        const auto equation = driver_probe::resolve<Equation>(name("glBlendEquationSeparatei").c_str());
        const auto factors = driver_probe::resolve<Factors>(name("glBlendFuncSeparatei").c_str());

        Layers layers(4, 4);
        const auto snapshot = [&] {
            std::array<std::array<GLint, 11>, 3> slots{};
            constexpr std::array<GLenum, 6> tokens{GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB,
                GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA};
            for (GLuint i = 0; i < slots.size(); ++i) {
                auto &slot = slots[i];
                glGetIntegeri_v(GL_COLOR_WRITEMASK, i, slot.data());
                slot[4] = enabled_slot(GL_BLEND, i);
                for (std::size_t j = 0; j < tokens.size(); ++j) {
                    glGetIntegeri_v(tokens[j], i, &slot[5 + j]);
                }
            }
            return slots;
        };
        glDisable(GL_BLEND);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ZERO);
        enable_slot(GL_BLEND, 1);
        mask_slot(1, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
        mask_slot(2, GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        equation(1, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT);
        factors(1, GL_SRC_ALPHA, GL_ONE, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA);
        const auto before = snapshot();
        CHECK(before[0] != before[1]);
        CHECK(before[0] != before[2]);
        CHECK(glGetError() == GL_NO_ERROR);

        Program program;
        Instance instance;
        std::string error;
        CHECK(!program.initialize(diffusion(), {}, error));
        CHECK(error.find("procedure resolver") != std::string::npos);
        CHECK(snapshot() == before);
        const bool prepared = checked(program.initialize(diffusion(), {}, error, eglGetProcAddress), error) &&
                              checked(instance.initialize(program, 4, 4, error), error);
        CHECK(snapshot() == before);
        if (prepared) {
            CHECK(checked(instance.render(layers.source, layers.target, true, error), error));
            CHECK(snapshot() == before);
            CHECK(near(pixel(layers.target, 3, 3), {16, 76, 0, 255}));
        }
        glDisable(GL_BLEND);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ZERO);
        CHECK(glGetError() == GL_NO_ERROR);
    }
}  // namespace

int main(int argc, char **argv) {
    const bool require_es30 = argc == 2 && std::string_view(argv[1]) == "--require-es30";
    const gl_harness::CContext context(32, 32);
    if (!context.valid()) {
        return 77;
    }
    const auto *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const auto *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    (void)std::fprintf(stderr, "%s | %s\n", version, renderer);
    if (require_es30 && (integer(GL_MAJOR_VERSION) != 3 || integer(GL_MINOR_VERSION) != 0)) {
        (void)std::fprintf(stderr, "test requires an actual GLES 3.0 context\n");
        return 77;
    }
    source_subrect_orientation_and_aliases();
    environment_borrows_only_nonaliased_images_and_clears_old_batches();
    borrowed_target_origin_clips_draw_and_clear();
    in_place_presentation_blends_only_damaged_pixels();
    multipass_ticks_resize_and_independent_instances();
    fixed_simulation_storage_survives_source_resize();
    mrt_dimensions_are_checked_after_capture_size_resolution();
    render_only_failure_preserves_committed_and_pending_simulation();
    compute_instances_keep_authorized_state_and_generation();
    feedback_instancing_uniform_blocks_and_host_state();
    transient_feedback_requires_exact_complete_capture();
    feedback_storage_is_rebound_sequentially_for_uniform_and_vertex_inputs();
    failed_preparation_preserves_generation_and_checks_gpu_limits();
    float_outputs_follow_actual_framebuffer_support();
    unsupported_combinations_stop_before_full_storage_and_asset_upload();
    raster_outputs_must_match_the_manifest_locations_and_types();
    indexed_color_state_is_preserved_during_prepare_and_mrt_draws();
    return g_failures == 0 ? 0 : 1;
}
