// Offscreen reference film driver. stdin: two packed top-first RGBA8 images,
// then whitespace-separated (0=forward / 1=backward, path progress) pairs.
// stdout: one packed, top-first RGB24 frame per sample; diagnostics go to stderr.
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

#include "GlHarness.hpp"
#include "production_gl.hpp"

namespace {
    using namespace hyprimate::workspace;
    using cube_offscreen::Frame;
    using cube_offscreen::Pixel;

    int dimension(std::string_view text) {
        int value = 0;
        const auto end = text.data() + text.size();
        const auto result = std::from_chars(text.data(), end, value);
        return result.ec == std::errc{} && result.ptr == end ? value : 0;
    }

    bool stream(Frame &frame, cube::Material &material, const cube::Settings &settings,
        std::vector<Pixel> &rgba) {
        std::vector<std::uint8_t> rgb(rgba.size() * 3);
        for (;;) {
            int backward = 0;
            double progress = 0.;
            const int fields = std::scanf("%d %lf", &backward, &progress);
            if (fields == EOF) return std::feof(stdin) && !std::ferror(stdin);
            if (fields != 2 || (backward != 0 && backward != 1) || !std::isfinite(progress) || progress < 0. ||
                progress > 1.) {
                std::fprintf(stderr, "invalid sample; expected direction (0 or 1) and path progress [0,1]\n");
                return false;
            }
            const auto direction = backward ? Direction::Backward : Direction::Forward;
            if (!frame.draw(material, progress, direction, settings)) {
                std::fprintf(stderr, "material rejected direction %d, progress %.17g\n", backward, progress);
                return false;
            }
            frame.read(rgba);
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
                std::fprintf(stderr, "GLES error 0x%x at direction %d, progress %.17g\n", error, backward, progress);
                return false;
            }
            // GLES reads RGBA8 portably. Strip alpha without flipping native workspace rows.
            for (std::size_t i = 0, dst = 0; i < rgba.size(); ++i) {
                rgb[dst++] = rgba[i][0];
                rgb[dst++] = rgba[i][1];
                rgb[dst++] = rgba[i][2];
            }
            if (std::fwrite(rgb.data(), 1, rgb.size(), stdout) != rgb.size() || std::fflush(stdout) != 0) {
                std::fprintf(stderr, "failed writing RGB24 frame\n");
                return false;
            }
        }
    }
}  // namespace

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s solid|inside-out WIDTH HEIGHT\n", argv[0]);
        return 1;
    }
    const std::string_view variant(argv[1]);
    const int width = dimension(argv[2]), height = dimension(argv[3]);
    if ((variant != "solid" && variant != "inside-out") || width <= 0 || height <= 0) {
        std::fprintf(stderr, "unknown cube variant or invalid frame dimensions\n");
        return 1;
    }
    const gl_harness::CContext context(1, 1);
    if (!context.valid()) {
        std::fprintf(stderr, "no offscreen GLES3 context\n");
        return 1;
    }
    GLint texture_limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture_limit);
    if (width > texture_limit || height > texture_limit) {
        std::fprintf(stderr, "frame dimensions exceed the GLES limit (%d)\n", texture_limit);
        return 1;
    }
    Frame frame(width, height);
    if (!frame.valid()) {
        std::fprintf(stderr, "cannot allocate the offscreen framebuffer\n");
        return 1;
    }
    std::vector<Pixel> rgba(static_cast<std::size_t>(width) * height);
    for (const bool second : {false, true}) {
        if (std::fread(rgba.data(), sizeof(Pixel), rgba.size(), stdin) != rgba.size()) {
            std::fprintf(stderr, "short RGBA image for capture %c\n", second ? 'B' : 'A');
            return 1;
        }
        frame.upload(second, rgba);
    }
    if (glGetError() != GL_NO_ERROR) {
        std::fprintf(stderr, "capture upload failed\n");
        return 1;
    }
    cube::Material material;
    if (!material.initialize()) {
        std::fprintf(stderr, "cube material initialization failed\n");
        return 1;
    }
    const bool solid = variant == "solid";
    const cube::Settings settings{solid ? cube::Variant::Solid : cube::Variant::InsideOut, 0.F, solid ? .25F : .45F};
    const bool rendered = stream(frame, material, settings, rgba);
    material.release();
    return rendered ? 0 : 1;
}
