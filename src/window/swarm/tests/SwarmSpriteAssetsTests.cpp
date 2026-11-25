#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cairo/cairo.h>

#include "Check.hpp"
#include "window/swarm/SwarmEmbeddedSprites.hpp"
#include "window/swarm/SwarmSprite.hpp"
#include "window/swarm/SwarmSpriteLoader.hpp"

namespace {
    using Surface = std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)>;

    bool same_image(cairo_surface_t *left, cairo_surface_t *right) {
        if (cairo_surface_status(left) != CAIRO_STATUS_SUCCESS ||
            cairo_surface_status(right) != CAIRO_STATUS_SUCCESS) return false;
        cairo_surface_flush(left);
        cairo_surface_flush(right);
        const int width = cairo_image_surface_get_width(left);
        const int height = cairo_image_surface_get_height(left);
        const int stride = cairo_image_surface_get_stride(left);
        if (width != cairo_image_surface_get_width(right) ||
            height != cairo_image_surface_get_height(right) ||
            stride != cairo_image_surface_get_stride(right)) return false;
        const auto *left_data = cairo_image_surface_get_data(left);
        const auto *right_data = cairo_image_surface_get_data(right);
        return std::equal(left_data, left_data + static_cast<std::size_t>(stride) * height, right_data);
    }

    void test_file_fallback() {
        namespace fs = std::filesystem;
        const auto name = (fs::temp_directory_path() / "hyprimate-swarm-XXXXXX").string();
        std::vector<char> buffer(name.begin(), name.end());
        buffer.push_back('\0');
        const char *created = ::mkdtemp(buffer.data());
        CHECK(created != nullptr);
        if (!created) return;
        struct Cleanup {
            fs::path path;
            ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
        };
        const Cleanup cleanup{created};
        const fs::path directory{created};
        const auto cat_path = fs::path(SWARM_TEST_SPRITE_DIR) / "cat.png";
        const auto bird_path = fs::path(SWARM_TEST_SPRITE_DIR) / "bird.png";
        const Surface cat{cairo_image_surface_create_from_png(cat_path.c_str()), cairo_surface_destroy};
        const Surface bird{cairo_image_surface_create_from_png(bird_path.c_str()), cairo_surface_destroy};

        const Surface fallback{hyprimate::load_swarm_sprite("cat", directory.string(), true), cairo_surface_destroy};
        CHECK(same_image(fallback.get(), cat.get()));
        const Surface fallback_extension{hyprimate::load_swarm_sprite(
            "cat.png", directory.string(), true), cairo_surface_destroy};
        CHECK(same_image(fallback_extension.get(), cat.get()));
        const Surface disabled{hyprimate::load_swarm_sprite("cat", directory.string(), false), cairo_surface_destroy};
        CHECK(cairo_surface_status(disabled.get()) == CAIRO_STATUS_FILE_NOT_FOUND);
        const Surface unknown{hyprimate::load_swarm_sprite("custom", directory.string(), true), cairo_surface_destroy};
        CHECK(cairo_surface_status(unknown.get()) == CAIRO_STATUS_FILE_NOT_FOUND);
        const Surface explicit_missing{hyprimate::load_swarm_sprite(
            (directory / "cat.png").string(), directory.string(), true), cairo_surface_destroy};
        CHECK(cairo_surface_status(explicit_missing.get()) == CAIRO_STATUS_FILE_NOT_FOUND);

        fs::copy_file(bird_path, directory / "cat.png");
        fs::copy_file(bird_path, directory / "custom.png");
        const Surface override_file{hyprimate::load_swarm_sprite("cat", directory.string(), true), cairo_surface_destroy};
        CHECK(same_image(override_file.get(), bird.get()));
        const Surface configured_file{hyprimate::load_swarm_sprite(
            "cat", directory.string(), false), cairo_surface_destroy};
        CHECK(same_image(configured_file.get(), bird.get()));
        const Surface custom{hyprimate::load_swarm_sprite("custom", directory.string(), false), cairo_surface_destroy};
        CHECK(same_image(custom.get(), bird.get()));
        const Surface explicit_file{hyprimate::load_swarm_sprite(
            (directory / "cat.png").string(), "/unused", true), cairo_surface_destroy};
        CHECK(same_image(explicit_file.get(), bird.get()));

        std::ofstream{directory / "cat.png", std::ios::binary | std::ios::trunc} << "not a PNG";
        const Surface invalid{hyprimate::load_swarm_sprite("cat", directory.string(), true), cairo_surface_destroy};
        CHECK(cairo_surface_status(invalid.get()) != CAIRO_STATUS_SUCCESS);
    }
}

int main() {
    for (const std::string_view name : {"bird", "cat", "spider"}) {
        const auto bytes = hyprimate::embedded_swarm_sprite(name, true);
        const auto with_extension = hyprimate::embedded_swarm_sprite(std::string(name) + ".png", true);
        CHECK(!bytes.empty());
        CHECK(bytes.data() == with_extension.data());
        CHECK(bytes.size() == with_extension.size());
        CHECK(hyprimate::embedded_swarm_sprite(name, false).empty());

        const auto path = hyprimate::sprite_path(hyprimate::SpriteName{name},
            hyprimate::SpriteDirectory{SWARM_TEST_SPRITE_DIR});
        std::ifstream input(path, std::ios::binary);
        CHECK(input.is_open());
        const std::vector<unsigned char> file_bytes{std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
        CHECK(std::ranges::equal(bytes, file_bytes));
    }
    CHECK(hyprimate::embedded_swarm_sprite("custom", true).empty());
    CHECK(hyprimate::embedded_swarm_sprite("./cat.png", true).empty());
    CHECK(hyprimate::embedded_swarm_sprite("/tmp/cat.png", true).empty());
    CHECK(hyprimate::sprite_path(hyprimate::SpriteName{"custom"},
              hyprimate::SpriteDirectory{SWARM_TEST_SPRITE_DIR}) ==
          std::string(SWARM_TEST_SPRITE_DIR) + "/custom.png");
    CHECK(hyprimate::sprite_path(hyprimate::SpriteName{"/tmp/cat.png"},
              hyprimate::SpriteDirectory{"/override"}) == "/tmp/cat.png");
    CHECK(hyprimate::sprite_path(hyprimate::SpriteName{"cat"},
              hyprimate::SpriteDirectory{"/override"}) == "/override/cat.png");
    test_file_fallback();
    return g_failures == 0 ? 0 : 1;
}
